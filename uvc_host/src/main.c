#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>

#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ  1031
#endif

#include "uvc_protocol.h"
#include "uvc_xu_ctrl.h"
#include "uvc_stream.h"

#define MAX_CAMERAS         4
#define DEFAULT_WIDTH       1920
#define DEFAULT_HEIGHT      1080
#define DEFAULT_FPS         30
#define DEFAULT_BITRATE     2000   /* Kbps */
#define DEFAULT_VENC        UVC_VENC_TYPE_H265
#define DEFAULT_OUTPUT_DIR  "./output"
#define DEFAULT_PIPE_DIR    "/tmp/uvc_stream"

static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
    fprintf(stderr, "\nReceived signal %d, shutting down...\n", sig);
}

#define PIPE_BUF_SIZE  (1 << 20)  /* 1 MiB — max allowed by Linux */

/*
 * Write to a named-pipe fd with SIGPIPE protection.
 * Returns bytes written, or -1 on error (pipe broken / no reader).
 */
static ssize_t pipe_write(int fd, const void *buf, size_t len)
{
    sigset_t block, prev;
    sigemptyset(&block);
    sigaddset(&block, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &block, &prev);

    ssize_t n = write(fd, buf, len);
    int saved_errno = errno;

    pthread_sigmask(SIG_SETMASK, &prev, NULL);
    errno = saved_errno;
    return n;
}

/* ========================================================================
 * Per-camera context
 * ======================================================================== */

typedef struct {
    uint8_t     channel;
    FILE       *video_fp;
    bool        got_keyframe;
    int         pipe_fd;            /* named-pipe fd, -1 if unused */
    bool        pipe_need_keyframe; /* true = waiting for I-frame before pipe write */
    char        pipe_path[256];     /* e.g. /tmp/uvc_stream/sensor0_ch0.h265 */
} SensorSlot;

typedef struct {
    char        dev_path[64];
    uint8_t     base_sensor;     /* first sensor index on this UVC device */
    uint16_t    width;
    uint16_t    height;
    uint8_t     fps;
    uint16_t    bitrate;
    uint8_t     venc;
    char        output_dir[256];
    int         save_video;
    int         save_detect;
    int         pipe_enabled;
    char        pipe_dir[256];

    UvcStream   stream;
    SensorSlot  sensors[UVC_SENSORS_PER_DEVICE];
    uint64_t    last_stats_time;

    pthread_t   thread;
    int         thread_running;
} CameraContext;

static const char *venc_to_str(uint8_t venc)
{
    switch (venc) {
    case UVC_VENC_TYPE_H264:  return "H.264";
    case UVC_VENC_TYPE_H265:  return "H.265";
    case UVC_VENC_TYPE_YUV:   return "YUV";
    case UVC_VENC_TYPE_MJPEG: return "MJPEG";
    default:                  return "Unknown";
    }
}

static const char *venc_to_ext(uint8_t venc)
{
    switch (venc) {
    case UVC_VENC_TYPE_H264:  return "h264";
    case UVC_VENC_TYPE_H265:  return "h265";
    case UVC_VENC_TYPE_YUV:   return "yuv";
    case UVC_VENC_TYPE_MJPEG: return "mjpeg";
    default:                  return "raw";
    }
}

/* ========================================================================
 * Frame callback — processes each decoded frame from the stream
 * ======================================================================== */

static SensorSlot *find_sensor_slot(CameraContext *cam, uint8_t channel)
{
    for (int s = 0; s < UVC_SENSORS_PER_DEVICE; s++) {
        if (cam->sensors[s].channel == channel)
            return &cam->sensors[s];
    }
    return NULL;
}

static void on_frame(const UvcFrameHeader_t *header,
                     const uint8_t *payload, uint32_t payload_len,
                     void *user_data)
{
    CameraContext *cam = (CameraContext *)user_data;

    switch (header->FrameType) {
    case UVC_FRAME_TYPE_VIDEO: {
        SensorSlot *slot = find_sensor_slot(cam, header->Channel);
        if (!slot)
            break;

        if (!slot->got_keyframe) {
            if (header->KeyFrame)
                slot->got_keyframe = true;
            else
                break;
        }

        /* Write to file */
        if (cam->save_video && slot->video_fp)
            fwrite(payload, 1, payload_len, slot->video_fp);

        /* Write to named pipe (non-blocking, tolerates no reader) */
        if (slot->pipe_path[0]) {
            /* Try to open pipe on keyframes if not yet connected */
            if (slot->pipe_fd < 0 && header->KeyFrame) {
                slot->pipe_fd = open(slot->pipe_path,
                                     O_WRONLY | O_NONBLOCK);
                if (slot->pipe_fd >= 0) {
                    fcntl(slot->pipe_fd, F_SETPIPE_SZ, PIPE_BUF_SIZE);
                    slot->pipe_need_keyframe = true;
                }
            }

            if (slot->pipe_fd >= 0) {
                /* After pipe open or data loss, wait for a keyframe */
                if (slot->pipe_need_keyframe) {
                    if (!header->KeyFrame)
                        goto pipe_done;
                    slot->pipe_need_keyframe = false;
                }

                /*
                 * Pre-check: ensure pipe has room for the ENTIRE frame.
                 * A partial write would corrupt the H.265 bitstream.
                 */
                int queued = 0;
                if (ioctl(slot->pipe_fd, FIONREAD, &queued) == 0) {
                    if (PIPE_BUF_SIZE - queued < (int)payload_len) {
                        slot->pipe_need_keyframe = true;
                        goto pipe_done;
                    }
                }

                ssize_t n = pipe_write(slot->pipe_fd, payload, payload_len);
                if (n < 0) {
                    if (errno == EPIPE) {
                        close(slot->pipe_fd);
                        slot->pipe_fd = -1;
                    } else if (errno == EAGAIN) {
                        slot->pipe_need_keyframe = true;
                    }
                } else if ((size_t)n < payload_len) {
                    /* Partial write — bitstream corrupted, resync */
                    slot->pipe_need_keyframe = true;
                }
            }
        }
        pipe_done:

        if (header->KeyFrame) {
            fprintf(stdout, "[CAM%d] %s I-Frame #%u ch=%d %dx%d %s ts=%lu ms len=%u\n",
                    cam->base_sensor, cam->dev_path,
                    header->FrameIndex, header->Channel,
                    header->FrameWidth, header->FrameHeight,
                    venc_to_str(header->VencType),
                    (unsigned long)header->Timestamp, payload_len);
        }
        break;
    }

    case UVC_FRAME_TYPE_DETECT: {
        const char *event_name = "unknown";
        if (header->EventType == UVC_EVENT_TYPE_MOTION)
            event_name = "motion";
        else if (header->EventType == UVC_EVENT_TYPE_HUMAN)
            event_name = "human";
        else if (header->EventType == UVC_EVENT_TYPE_SOUND)
            event_name = "sound";

        fprintf(stdout, "[CAM%d] DETECT event=%s ch=%d payload=%u bytes\n",
                cam->base_sensor, event_name, header->Channel, payload_len);

        if (cam->save_detect && payload_len > 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/cam%d_detect_%s_%u.jpg",
                     cam->output_dir, cam->base_sensor, event_name,
                     header->FrameIndex);
            FILE *fp = fopen(path, "wb");
            if (fp) {
                fwrite(payload, 1, payload_len, fp);
                fclose(fp);
                fprintf(stdout, "[CAM%d] Saved detection snapshot: %s\n",
                        cam->base_sensor, path);
            }
        }
        break;
    }

    case UVC_FRAME_TYPE_NETWORK: {
        if (payload_len >= sizeof(UvcBindInfo_t)) {
            UvcBindInfo_t bind_info;
            memcpy(&bind_info, payload, sizeof(bind_info));
            fprintf(stdout, "[CAM%d] NETWORK bind: ssid=%s token=%s\n",
                    cam->base_sensor, bind_info.ssid, bind_info.token);
        }
        break;
    }

    case UVC_FRAME_TYPE_GPS_RTK:
        fprintf(stdout, "[CAM%d] GPS-RTK: %.*s\n",
                cam->base_sensor, (int)payload_len, payload);
        break;

    case UVC_FRAME_TYPE_IPC_RESET:
        fprintf(stdout, "[CAM%d] IPC RESET event received!\n", cam->base_sensor);
        break;

    case UVC_FRAME_TYPE_GSENSOR_DATA:
        break;

    case UVC_FRAME_TYPE_AUDIO:
        break;

    default:
        fprintf(stdout, "[CAM%d] Unknown frame type=%d len=%u\n",
                cam->base_sensor, header->FrameType, payload_len);
        break;
    }
}

/* ========================================================================
 * Camera worker thread
 * ======================================================================== */

static void *camera_worker(void *arg)
{
    CameraContext *cam = (CameraContext *)arg;
    int ret;

    fprintf(stdout, "[CAM%d] Worker thread started for %s\n",
            cam->base_sensor, cam->dev_path);

    /* 1. Open the V4L2 device */
    ret = uvc_stream_open(&cam->stream, cam->dev_path);
    if (ret < 0) {
        fprintf(stderr, "[CAM%d] Failed to open stream %s\n",
                cam->base_sensor, cam->dev_path);
        cam->thread_running = 0;
        return NULL;
    }

    int fd = cam->stream.fd;

    /* 2. Enumerate and auto-select the best V4L2 format */
    uvc_stream_enumerate_formats(&cam->stream);

    FormatSelection sel;
    ret = uvc_stream_find_best_format(&cam->stream,
                                      cam->width, cam->height,
                                      cam->venc, &sel);
    if (ret < 0) {
        fprintf(stderr, "[CAM%d] No compatible format on %s\n",
                cam->base_sensor, cam->dev_path);
        uvc_stream_close(&cam->stream);
        cam->thread_running = 0;
        return NULL;
    }

    /* Override the XU venc and resolution to match what V4L2 actually supports */
    uint8_t  actual_venc   = sel.matched_venc;
    uint16_t actual_width  = (uint16_t)sel.width;
    uint16_t actual_height = (uint16_t)sel.height;

    if (actual_venc != cam->venc) {
        fprintf(stdout, "[CAM%d] V4L2 format requires venc=%s (requested %s)\n",
                cam->base_sensor, venc_to_str(actual_venc), venc_to_str(cam->venc));
    }

    ret = uvc_stream_set_format(&cam->stream, sel.pixfmt, sel.width, sel.height);
    if (ret < 0) {
        fprintf(stderr, "[CAM%d] Failed to set V4L2 format\n", cam->base_sensor);
        uvc_stream_close(&cam->stream);
        cam->thread_running = 0;
        return NULL;
    }

    /* 3. Sync NTP time to device */
    uvc_set_ntp_time(fd);

    /* 4. Notify device of host status */
    uvc_set_host_status(fd, HOST_STATUS_CONNECT_INTERNET);

    /* 5. Query device info */
    char version[64] = {0};
    if (uvc_get_device_info(fd, version, sizeof(version)) == 0) {
        fprintf(stdout, "[CAM%d] Device firmware: %s\n", cam->base_sensor, version);
    }

    /* 6. Start V4L2 streaming FIRST, then open XU channel.
     *    USB streaming endpoint must be active before device pushes data. */
    ret = uvc_stream_start(&cam->stream);
    if (ret < 0) {
        fprintf(stderr, "[CAM%d] Failed to start V4L2 streaming\n", cam->base_sensor);
        uvc_stream_close(&cam->stream);
        cam->thread_running = 0;
        return NULL;
    }

    usleep(200000);

    /* 7. Open video channels for all sensors on this UVC device */
    for (int s = 0; s < UVC_SENSORS_PER_DEVICE; s++) {
        uint8_t ch = cam->sensors[s].channel;
        ret = uvc_open_video_channel(fd, ch,
                                     actual_width, actual_height,
                                     actual_venc, cam->fps, cam->bitrate);
        if (ret < 0) {
            fprintf(stderr, "[CAM%d] Failed to open video channel %d via XU\n",
                    cam->base_sensor, ch);
        } else {
            fprintf(stdout, "[CAM%d] XU switch_on: ch=%d %dx%d %s\n",
                    cam->base_sensor, ch, actual_width, actual_height,
                    venc_to_str(actual_venc));
        }

        if (cam->save_video) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/sensor%d_ch%d.%s",
                     cam->output_dir, cam->base_sensor + s, ch,
                     venc_to_ext(actual_venc));
            cam->sensors[s].video_fp = fopen(filepath, "wb");
            if (!cam->sensors[s].video_fp) {
                fprintf(stderr, "[CAM%d] Cannot open output file %s: %s\n",
                        cam->base_sensor, filepath, strerror(errno));
            } else {
                fprintf(stdout, "[CAM%d] Saving video to %s\n",
                        cam->base_sensor, filepath);
            }
        }

        /* Create named pipe for live streaming */
        if (cam->pipe_enabled) {
            snprintf(cam->sensors[s].pipe_path,
                     sizeof(cam->sensors[s].pipe_path),
                     "%s/sensor%d_ch%d.%s",
                     cam->pipe_dir, cam->base_sensor + s, ch,
                     venc_to_ext(actual_venc));

            unlink(cam->sensors[s].pipe_path);
            if (mkfifo(cam->sensors[s].pipe_path, 0666) < 0 && errno != EEXIST) {
                fprintf(stderr, "[CAM%d] mkfifo(%s) failed: %s\n",
                        cam->base_sensor, cam->sensors[s].pipe_path,
                        strerror(errno));
            } else {
                cam->sensors[s].pipe_fd =
                    open(cam->sensors[s].pipe_path, O_WRONLY | O_NONBLOCK);
                if (cam->sensors[s].pipe_fd < 0 && errno == ENXIO) {
                    fprintf(stdout, "[CAM%d] Pipe %s created (no reader yet)\n",
                            cam->base_sensor, cam->sensors[s].pipe_path);
                } else if (cam->sensors[s].pipe_fd >= 0) {
                    fcntl(cam->sensors[s].pipe_fd, F_SETPIPE_SZ,
                          PIPE_BUF_SIZE);
                    cam->sensors[s].pipe_need_keyframe = true;
                    fprintf(stdout, "[CAM%d] Pipe %s opened (buf=%d)\n",
                            cam->base_sensor, cam->sensors[s].pipe_path,
                            PIPE_BUF_SIZE);
                }
            }
        }
    }

    /* 11. Main capture loop */
    struct timeval tv_start;
    gettimeofday(&tv_start, NULL);
    cam->last_stats_time = (uint64_t)tv_start.tv_sec;
    int consecutive_timeouts = 0;

    while (g_running && cam->thread_running) {
        ret = uvc_stream_read_frame(&cam->stream, on_frame, cam);
        if (ret < 0) {
            consecutive_timeouts++;

            if (consecutive_timeouts == 1) {
                fprintf(stderr, "[CAM%d] Timeout #1 — re-sending XU switch_on...\n",
                        cam->base_sensor);
                for (int s = 0; s < UVC_SENSORS_PER_DEVICE; s++)
                    uvc_open_video_channel(fd, cam->sensors[s].channel,
                                           actual_width, actual_height,
                                           actual_venc, cam->fps, cam->bitrate);
            } else if (consecutive_timeouts == 3) {
                fprintf(stderr, "[CAM%d] Timeout #3 — requesting keyframe...\n",
                        cam->base_sensor);
                for (int s = 0; s < UVC_SENSORS_PER_DEVICE; s++)
                    uvc_request_keyframe(fd, cam->sensors[s].channel);
            } else if (consecutive_timeouts >= 6) {
                fprintf(stderr, "[CAM%d] Too many timeouts (%d). "
                        "Restarting V4L2 stream...\n",
                        cam->base_sensor, consecutive_timeouts);

                uvc_stream_stop(&cam->stream);
                usleep(500000);

                ret = uvc_stream_start(&cam->stream);
                if (ret < 0) {
                    fprintf(stderr, "[CAM%d] Failed to restart streaming\n",
                            cam->base_sensor);
                    break;
                }

                usleep(200000);
                for (int s = 0; s < UVC_SENSORS_PER_DEVICE; s++)
                    uvc_open_video_channel(fd, cam->sensors[s].channel,
                                           actual_width, actual_height,
                                           actual_venc, cam->fps, cam->bitrate);
                consecutive_timeouts = 0;
            }

            usleep(200000);
            continue;
        }

        if (ret > 0)
            consecutive_timeouts = 0;

        /* Print periodic stats every 10 seconds */
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        uint64_t now_sec = (uint64_t)tv_now.tv_sec;
        if (now_sec - cam->last_stats_time >= 10) {
            uvc_stream_print_stats(&cam->stream);
            cam->last_stats_time = now_sec;
        }
    }

    /* 12. Cleanup */
    fprintf(stdout, "[CAM%d] Shutting down...\n", cam->base_sensor);

    for (int s = 0; s < UVC_SENSORS_PER_DEVICE; s++) {
        uvc_close_video_channel(fd, cam->sensors[s].channel);
        if (cam->sensors[s].video_fp) {
            fclose(cam->sensors[s].video_fp);
            cam->sensors[s].video_fp = NULL;
        }
        if (cam->sensors[s].pipe_fd >= 0) {
            close(cam->sensors[s].pipe_fd);
            cam->sensors[s].pipe_fd = -1;
        }
        if (cam->sensors[s].pipe_path[0])
            unlink(cam->sensors[s].pipe_path);
    }
    uvc_stream_stop(&cam->stream);
    uvc_stream_close(&cam->stream);

    uvc_stream_print_stats(&cam->stream);
    cam->thread_running = 0;
    return NULL;
}

/* ========================================================================
 * Usage & argument parsing
 * ======================================================================== */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "UVC Camera Host Controller for NVIDIA Orin AGX\n"
        "Communicates with UVC cameras via XU extension protocol to start video streaming.\n"
        "\n"
        "Options:\n"
        "  -d, --device DEV       Video device path (can be specified multiple times)\n"
        "                         Default: /dev/video0 and /dev/video2\n"
        "  -W, --width N          Video width  (default: %d)\n"
        "  -H, --height N         Video height (default: %d)\n"
        "  -f, --fps N            Frame rate   (default: %d)\n"
        "  -b, --bitrate N        Bitrate in Kbps (default: %d)\n"
        "  -e, --encoding TYPE    Encoding: h264, h265, yuv, mjpeg (default: h265)\n"
        "  -o, --output DIR       Output directory (default: %s)\n"
        "  -s, --save             Save video stream to file\n"
        "  -S, --save-detect      Save detection event snapshots\n"
        "  -p, --pipe             Output to named pipes for live streaming\n"
        "  -P, --pipe-dir DIR     Pipe directory (default: %s)\n"
        "  -h, --help             Show this help\n"
        "\n"
        "Examples:\n"
        "  %s -d /dev/video0 -d /dev/video2 -s\n"
        "  %s -d /dev/video0 -W 1280 -H 720 -f 25 -e h264 -b 1000 -s\n"
        "  %s                                   # uses default /dev/video0 + /dev/video2\n"
        "  %s -p                                # output to named pipes in %s\n"
        "  %s -p -P /tmp/my_pipes               # pipes in custom directory\n"
        "\n",
        prog,
        DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_FPS, DEFAULT_BITRATE,
        DEFAULT_OUTPUT_DIR, DEFAULT_PIPE_DIR,
        prog, prog, prog, prog, DEFAULT_PIPE_DIR, prog);
}

static uint8_t parse_venc(const char *str)
{
    if (strcasecmp(str, "h264") == 0) return UVC_VENC_TYPE_H264;
    if (strcasecmp(str, "h265") == 0) return UVC_VENC_TYPE_H265;
    if (strcasecmp(str, "yuv") == 0)  return UVC_VENC_TYPE_YUV;
    if (strcasecmp(str, "mjpeg") == 0) return UVC_VENC_TYPE_MJPEG;
    fprintf(stderr, "Unknown encoding: %s, using H.265\n", str);
    return UVC_VENC_TYPE_H265;
}

int main(int argc, char *argv[])
{
    CameraContext cameras[MAX_CAMERAS];
    int n_cameras = 0;
    uint16_t width   = DEFAULT_WIDTH;
    uint16_t height  = DEFAULT_HEIGHT;
    uint8_t  fps     = DEFAULT_FPS;
    uint16_t bitrate = DEFAULT_BITRATE;
    uint8_t  venc    = DEFAULT_VENC;
    char     output_dir[256] = DEFAULT_OUTPUT_DIR;
    int      save_video  = 0;
    int      save_detect = 0;
    int      pipe_enabled = 0;
    char     pipe_dir[256] = DEFAULT_PIPE_DIR;

    memset(cameras, 0, sizeof(cameras));

    static struct option long_opts[] = {
        {"device",      required_argument, 0, 'd'},
        {"width",       required_argument, 0, 'W'},
        {"height",      required_argument, 0, 'H'},
        {"fps",         required_argument, 0, 'f'},
        {"bitrate",     required_argument, 0, 'b'},
        {"encoding",    required_argument, 0, 'e'},
        {"output",      required_argument, 0, 'o'},
        {"save",        no_argument,       0, 's'},
        {"save-detect", no_argument,       0, 'S'},
        {"pipe",        no_argument,       0, 'p'},
        {"pipe-dir",    required_argument, 0, 'P'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:W:H:f:b:e:o:sSpP:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'd':
            if (n_cameras < MAX_CAMERAS) {
                strncpy(cameras[n_cameras].dev_path, optarg,
                        sizeof(cameras[n_cameras].dev_path) - 1);
                n_cameras++;
            }
            break;
        case 'W': width   = (uint16_t)atoi(optarg); break;
        case 'H': height  = (uint16_t)atoi(optarg); break;
        case 'f': fps     = (uint8_t)atoi(optarg);  break;
        case 'b': bitrate = (uint16_t)atoi(optarg); break;
        case 'e': venc    = parse_venc(optarg);      break;
        case 'o': strncpy(output_dir, optarg, sizeof(output_dir) - 1); break;
        case 's': save_video  = 1; break;
        case 'S': save_detect = 1; break;
        case 'p': pipe_enabled = 1; break;
        case 'P':
            pipe_enabled = 1;
            strncpy(pipe_dir, optarg, sizeof(pipe_dir) - 1);
            break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    /* Default: /dev/video0 and /dev/video2 */
    if (n_cameras == 0) {
        strncpy(cameras[0].dev_path, "/dev/video0", sizeof(cameras[0].dev_path) - 1);
        strncpy(cameras[1].dev_path, "/dev/video2", sizeof(cameras[1].dev_path) - 1);
        n_cameras = 2;
    }

    /* Create output directory */
    mkdir(output_dir, 0755);

    /* Create pipe directory */
    if (pipe_enabled)
        mkdir(pipe_dir, 0755);

    /* Install signal handler */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    fprintf(stdout, "========================================\n");
    fprintf(stdout, " UVC Host Controller for Orin AGX\n");
    fprintf(stdout, "========================================\n");
    fprintf(stdout, " Cameras:    %d\n", n_cameras);
    fprintf(stdout, " Resolution: %dx%d\n", width, height);
    fprintf(stdout, " FPS:        %d\n", fps);
    fprintf(stdout, " Encoding:   %s\n", venc_to_str(venc));
    fprintf(stdout, " Bitrate:    %d Kbps\n", bitrate);
    fprintf(stdout, " Output:     %s\n", output_dir);
    fprintf(stdout, " Save video: %s\n", save_video ? "YES" : "NO");
    fprintf(stdout, " Pipe out:   %s\n", pipe_enabled ? pipe_dir : "NO");
    fprintf(stdout, "========================================\n\n");

    /* Initialize and launch camera threads */
    for (int i = 0; i < n_cameras; i++) {
        cameras[i].base_sensor = (uint8_t)(i * UVC_SENSORS_PER_DEVICE);
        for (int s = 0; s < UVC_SENSORS_PER_DEVICE; s++) {
            cameras[i].sensors[s].channel =
                UVC_MAKE_CHANNEL(cameras[i].base_sensor + s, 0);
            cameras[i].sensors[s].pipe_fd = -1;
        }
        cameras[i].width        = width;
        cameras[i].height       = height;
        cameras[i].fps          = fps;
        cameras[i].bitrate      = bitrate;
        cameras[i].venc         = venc;
        cameras[i].save_video   = save_video;
        cameras[i].save_detect  = save_detect;
        cameras[i].pipe_enabled = pipe_enabled;
        strncpy(cameras[i].output_dir, output_dir, sizeof(cameras[i].output_dir) - 1);
        strncpy(cameras[i].pipe_dir, pipe_dir, sizeof(cameras[i].pipe_dir) - 1);
        cameras[i].thread_running = 1;

        fprintf(stdout, "Starting camera %d: %s -> channels [%d, %d]\n",
                i, cameras[i].dev_path,
                cameras[i].sensors[0].channel,
                cameras[i].sensors[1].channel);

        int ret = pthread_create(&cameras[i].thread, NULL,
                                 camera_worker, &cameras[i]);
        if (ret != 0) {
            fprintf(stderr, "Failed to create thread for camera %d: %s\n",
                    i, strerror(ret));
            cameras[i].thread_running = 0;
        }
    }

    /* Wait for all threads to finish */
    for (int i = 0; i < n_cameras; i++) {
        if (cameras[i].thread_running || cameras[i].thread) {
            pthread_join(cameras[i].thread, NULL);
        }
    }

    fprintf(stdout, "\nAll cameras stopped. Goodbye.\n");
    return 0;
}
