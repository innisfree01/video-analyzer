#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>

#include "uvc_stream.h"
#include "uvc_protocol.h"

#define LOG_TAG "STREAM"
#define LOG_INFO(fmt, ...)  fprintf(stdout, "[%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   fprintf(stderr, "[%s][ERROR] " fmt "\n", LOG_TAG, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  fprintf(stderr, "[%s][WARN] " fmt "\n", LOG_TAG, ##__VA_ARGS__)

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static void fourcc_to_str(uint32_t fourcc, char *buf)
{
    buf[0] = (char)(fourcc & 0xFF);
    buf[1] = (char)((fourcc >> 8) & 0xFF);
    buf[2] = (char)((fourcc >> 16) & 0xFF);
    buf[3] = (char)((fourcc >> 24) & 0xFF);
    buf[4] = '\0';
}

int uvc_stream_open(UvcStream *stream, const char *dev_path)
{
    memset(stream, 0, sizeof(*stream));
    strncpy(stream->dev_path, dev_path, sizeof(stream->dev_path) - 1);

    stream->fd = open(dev_path, O_RDWR | O_NONBLOCK);
    if (stream->fd < 0) {
        LOG_ERR("Cannot open %s: %s", dev_path, strerror(errno));
        return -1;
    }

    struct v4l2_capability cap;
    if (xioctl(stream->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        LOG_ERR("VIDIOC_QUERYCAP failed on %s", dev_path);
        close(stream->fd);
        return -1;
    }

    LOG_INFO("Opened %s:", dev_path);
    LOG_INFO("  driver:       %s", cap.driver);
    LOG_INFO("  card:         %s", cap.card);
    LOG_INFO("  bus_info:     %s", cap.bus_info);
    LOG_INFO("  version:      %u.%u.%u",
             (cap.version >> 16) & 0xFF,
             (cap.version >> 8) & 0xFF,
             cap.version & 0xFF);
    LOG_INFO("  capabilities: 0x%08X", cap.capabilities);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOG_ERR("%s does not support V4L2_CAP_VIDEO_CAPTURE", dev_path);
        close(stream->fd);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        LOG_ERR("%s does not support V4L2_CAP_STREAMING", dev_path);
        close(stream->fd);
        return -1;
    }

    stream->reassembly_cap = UVC_STREAM_MAX_SIZE;
    stream->reassembly_buf = (uint8_t *)malloc(stream->reassembly_cap);
    if (!stream->reassembly_buf) {
        LOG_ERR("Failed to allocate reassembly buffer");
        close(stream->fd);
        return -1;
    }
    stream->reassembly_len = 0;

    return 0;
}

void uvc_stream_enumerate_formats(UvcStream *stream)
{
    LOG_INFO("=== Enumerating formats for %s ===", stream->dev_path);

    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    while (xioctl(stream->fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        char fourcc_str[5];
        fourcc_to_str(fmtdesc.pixelformat, fourcc_str);
        LOG_INFO("  Format[%u]: '%s' (0x%08X) - %s %s",
                 fmtdesc.index, fourcc_str, fmtdesc.pixelformat,
                 fmtdesc.description,
                 (fmtdesc.flags & V4L2_FMT_FLAG_COMPRESSED) ? "[compressed]" : "");

        /* Enumerate frame sizes for this format */
        struct v4l2_frmsizeenum frmsize;
        memset(&frmsize, 0, sizeof(frmsize));
        frmsize.pixel_format = fmtdesc.pixelformat;
        frmsize.index = 0;

        while (xioctl(stream->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                LOG_INFO("    Size[%u]: %ux%u", frmsize.index,
                         frmsize.discrete.width, frmsize.discrete.height);

                /* Enumerate frame intervals for this size */
                struct v4l2_frmivalenum frmival;
                memset(&frmival, 0, sizeof(frmival));
                frmival.pixel_format = fmtdesc.pixelformat;
                frmival.width = frmsize.discrete.width;
                frmival.height = frmsize.discrete.height;
                frmival.index = 0;

                while (xioctl(stream->fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0) {
                    if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                        LOG_INFO("      FPS: %u/%u",
                                 frmival.discrete.denominator,
                                 frmival.discrete.numerator);
                    } else if (frmival.type == V4L2_FRMIVAL_TYPE_STEPWISE ||
                               frmival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
                        LOG_INFO("      FPS range: %u/%u - %u/%u",
                                 frmival.stepwise.min.denominator,
                                 frmival.stepwise.min.numerator,
                                 frmival.stepwise.max.denominator,
                                 frmival.stepwise.max.numerator);
                    }
                    frmival.index++;
                }
            } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
                       frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
                LOG_INFO("    Size range: %u-%u x %u-%u (step %u x %u)",
                         frmsize.stepwise.min_width, frmsize.stepwise.max_width,
                         frmsize.stepwise.min_height, frmsize.stepwise.max_height,
                         frmsize.stepwise.step_width, frmsize.stepwise.step_height);
            }
            frmsize.index++;
        }

        fmtdesc.index++;
    }

    /* Print current format */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(stream->fd, VIDIOC_G_FMT, &fmt) == 0) {
        char fourcc_str[5];
        fourcc_to_str(fmt.fmt.pix.pixelformat, fourcc_str);
        LOG_INFO("  Current active format:");
        LOG_INFO("    pixfmt:    '%s' (0x%08X)", fourcc_str, fmt.fmt.pix.pixelformat);
        LOG_INFO("    size:      %ux%u", fmt.fmt.pix.width, fmt.fmt.pix.height);
        LOG_INFO("    sizeimage: %u", fmt.fmt.pix.sizeimage);
        LOG_INFO("    bytesperline: %u", fmt.fmt.pix.bytesperline);
        LOG_INFO("    field:     %u", fmt.fmt.pix.field);
        LOG_INFO("    colorspace: %u", fmt.fmt.pix.colorspace);
    }

    LOG_INFO("=== End format enumeration ===");
}

int uvc_stream_set_format(UvcStream *stream, uint32_t pixfmt,
                          uint32_t width, uint32_t height)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* First read current format */
    if (xioctl(stream->fd, VIDIOC_G_FMT, &fmt) < 0) {
        LOG_ERR("VIDIOC_G_FMT failed: %s", strerror(errno));
        return -1;
    }

    char fourcc_str[5];
    fourcc_to_str(pixfmt, fourcc_str);
    LOG_INFO("Requesting format: '%s' %ux%u on %s",
             fourcc_str, width, height, stream->dev_path);

    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(stream->fd, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_WARN("VIDIOC_S_FMT for '%s' %ux%u failed: %s",
                 fourcc_str, width, height, strerror(errno));

        /* Fall back: re-read whatever the device accepted */
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(stream->fd, VIDIOC_G_FMT, &fmt) < 0) {
            LOG_ERR("VIDIOC_G_FMT failed after S_FMT: %s", strerror(errno));
            return -1;
        }
    }

    fourcc_to_str(fmt.fmt.pix.pixelformat, fourcc_str);
    stream->pix_fmt      = fmt.fmt.pix.pixelformat;
    stream->frame_width  = fmt.fmt.pix.width;
    stream->frame_height = fmt.fmt.pix.height;
    stream->sizeimage    = fmt.fmt.pix.sizeimage;

    LOG_INFO("Negotiated format on %s:", stream->dev_path);
    LOG_INFO("  pixfmt:    '%s' (0x%08X)", fourcc_str, fmt.fmt.pix.pixelformat);
    LOG_INFO("  size:      %ux%u", fmt.fmt.pix.width, fmt.fmt.pix.height);
    LOG_INFO("  sizeimage: %u bytes", fmt.fmt.pix.sizeimage);
    LOG_INFO("  bytesperline: %u", fmt.fmt.pix.bytesperline);

    return 0;
}

int uvc_stream_start(UvcStream *stream)
{
    /* Only request and mmap buffers on the first call.
     * On restart (after STREAMOFF), just re-queue and STREAMON. */
    if (!stream->buffers_mapped) {
        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count  = UVC_STREAM_BUF_COUNT;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (xioctl(stream->fd, VIDIOC_REQBUFS, &req) < 0) {
            LOG_ERR("VIDIOC_REQBUFS failed: %s", strerror(errno));
            return -1;
        }

        if (req.count < 2) {
            LOG_ERR("Insufficient buffer memory on %s (got %u)", stream->dev_path, req.count);
            return -1;
        }

        stream->n_buffers = req.count;
        LOG_INFO("Allocated %u V4L2 buffers on %s", stream->n_buffers, stream->dev_path);

        for (uint32_t i = 0; i < stream->n_buffers; i++) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;

            if (xioctl(stream->fd, VIDIOC_QUERYBUF, &buf) < 0) {
                LOG_ERR("VIDIOC_QUERYBUF failed for buffer %u", i);
                return -1;
            }

            LOG_INFO("  Buffer[%u]: length=%u offset=0x%X", i, buf.length, buf.m.offset);

            stream->buffers[i].length = buf.length;
            stream->buffers[i].start = mmap(NULL, buf.length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            stream->fd, buf.m.offset);

            if (stream->buffers[i].start == MAP_FAILED) {
                LOG_ERR("mmap failed for buffer %u: %s", i, strerror(errno));
                return -1;
            }
        }

        stream->buffers_mapped = true;
    } else {
        LOG_INFO("Re-starting stream on %s (buffers already mapped)", stream->dev_path);
    }

    /* (Re-)queue all buffers */
    for (uint32_t i = 0; i < stream->n_buffers; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (xioctl(stream->fd, VIDIOC_QBUF, &buf) < 0) {
            LOG_ERR("VIDIOC_QBUF failed for buffer %u: %s", i, strerror(errno));
            return -1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(stream->fd, VIDIOC_STREAMON, &type) < 0) {
        LOG_ERR("VIDIOC_STREAMON failed: %s", strerror(errno));
        return -1;
    }

    stream->streaming = true;
    stream->reassembly_len = 0;
    LOG_INFO("V4L2 STREAMON success on %s", stream->dev_path);
    return 0;
}

/*
 * Parse DY66-framed data from a raw buffer.
 * The buffer may contain multiple concatenated frames.
 */
static void parse_frames(UvcStream *stream, const uint8_t *data,
                         uint32_t length, frame_callback_t cb, void *user_data)
{
    uint32_t pos = 0;

    while (pos < length) {
        /* Skip UVC payload header bytes (2-byte prefix) */
        if (pos + 2 <= length) {
            uint8_t b0 = data[pos];
            uint8_t b1 = data[pos + 1];
            if (b0 == 0x02 && (b1 == 0x80 || b1 == 0x81 || b1 == 0x82 || b1 == 0x83)) {
                pos += 2;
                continue;
            }
        }

        if (pos + sizeof(UvcFrameHeader_t) > length)
            break;

        /* Look for DY66 magic: 0x44 0x59 0x36 0x36 */
        if (data[pos] != 0x44 || data[pos+1] != 0x59 ||
            data[pos+2] != 0x36 || data[pos+3] != 0x36) {
            pos++;
            continue;
        }

        UvcFrameHeader_t header;
        memcpy(&header, data + pos, sizeof(header));

        if (header.DataLen < sizeof(UvcFrameHeader_t) ||
            header.DataLen > UVC_STREAM_MAX_SIZE) {
            LOG_ERR("Invalid DataLen=%u at pos=%u, skipping", header.DataLen, pos);
            pos++;
            continue;
        }

        if (pos + header.DataLen > length) {
            /* Incomplete frame, save remainder for reassembly */
            uint32_t remain = length - pos;
            if (remain > stream->reassembly_cap) {
                stream->reassembly_cap = remain * 2;
                stream->reassembly_buf = realloc(stream->reassembly_buf,
                                                 stream->reassembly_cap);
            }
            memcpy(stream->reassembly_buf, data + pos, remain);
            stream->reassembly_len = remain;
            return;
        }

        uint32_t payload_offset = sizeof(UvcFrameHeader_t);
        uint32_t payload_len = header.DataLen - payload_offset;
        const uint8_t *payload = data + pos + payload_offset;

        stream->total_frames++;
        stream->total_bytes += header.DataLen;

        if (header.FrameType == UVC_FRAME_TYPE_VIDEO)
            stream->video_frames++;
        else if (header.FrameType == UVC_FRAME_TYPE_DETECT)
            stream->detect_events++;

        if (cb)
            cb(&header, payload, payload_len, user_data);

        pos += header.DataLen;
    }
}

static void hexdump(const uint8_t *data, uint32_t len, uint32_t max_bytes)
{
    uint32_t n = (len < max_bytes) ? len : max_bytes;
    fprintf(stdout, "  HEX[%u bytes]: ", len);
    for (uint32_t i = 0; i < n; i++)
        fprintf(stdout, "%02X ", data[i]);
    if (n < len)
        fprintf(stdout, "...");
    fprintf(stdout, "\n");
}

int uvc_stream_read_frame(UvcStream *stream, frame_callback_t cb, void *user_data)
{
    if (!stream->streaming)
        return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(stream->fd, &fds);

    struct timeval tv;
    tv.tv_sec  = 5;
    tv.tv_usec = 0;

    int r = select(stream->fd + 1, &fds, NULL, NULL, &tv);
    if (r < 0) {
        if (errno == EINTR)
            return 0;
        LOG_ERR("select failed on %s: %s", stream->dev_path, strerror(errno));
        return -1;
    }
    if (r == 0) {
        LOG_WARN("select timeout (5s) on %s (total bufs received so far: %lu)",
                 stream->dev_path, (unsigned long)stream->v4l2_buffers_received);
        return -1;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(stream->fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN)
            return 0;
        LOG_ERR("VIDIOC_DQBUF failed on %s: %s", stream->dev_path, strerror(errno));
        return -1;
    }

    if (buf.index >= stream->n_buffers) {
        LOG_ERR("Invalid buffer index %u", buf.index);
        return -1;
    }

    stream->v4l2_buffers_received++;

    uint8_t *data = (uint8_t *)stream->buffers[buf.index].start;
    uint32_t length = buf.bytesused;

    /* Log first few buffers for diagnosis */
    if (stream->v4l2_buffers_received <= 5) {
        LOG_INFO("[%s] V4L2 buf #%lu: index=%u bytesused=%u flags=0x%X",
                 stream->dev_path,
                 (unsigned long)stream->v4l2_buffers_received,
                 buf.index, length, buf.flags);
        if (length > 0)
            hexdump(data, length, 64);
    }

    if (length > 0) {
        if (stream->reassembly_len > 0) {
            uint32_t total = stream->reassembly_len + length;
            if (total > stream->reassembly_cap) {
                stream->reassembly_cap = total * 2;
                stream->reassembly_buf = realloc(stream->reassembly_buf,
                                                 stream->reassembly_cap);
            }
            memcpy(stream->reassembly_buf + stream->reassembly_len, data, length);
            stream->reassembly_len = total;

            parse_frames(stream, stream->reassembly_buf,
                         stream->reassembly_len, cb, user_data);
            /* parse_frames updates reassembly_len if there's leftover */
        } else {
            stream->reassembly_len = 0;
            parse_frames(stream, data, length, cb, user_data);
        }
    }

    if (xioctl(stream->fd, VIDIOC_QBUF, &buf) < 0) {
        LOG_ERR("VIDIOC_QBUF failed on %s: %s", stream->dev_path, strerror(errno));
        return -1;
    }

    return 1;
}

void uvc_stream_stop(UvcStream *stream)
{
    if (!stream->streaming)
        return;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(stream->fd, VIDIOC_STREAMOFF, &type) < 0) {
        LOG_ERR("VIDIOC_STREAMOFF failed: %s", strerror(errno));
    }

    stream->streaming = false;
    LOG_INFO("Streaming stopped on %s", stream->dev_path);
}

void uvc_stream_close(UvcStream *stream)
{
    uvc_stream_stop(stream);

    for (uint32_t i = 0; i < stream->n_buffers; i++) {
        if (stream->buffers[i].start && stream->buffers[i].start != MAP_FAILED) {
            munmap(stream->buffers[i].start, stream->buffers[i].length);
        }
    }

    if (stream->reassembly_buf) {
        free(stream->reassembly_buf);
        stream->reassembly_buf = NULL;
    }

    if (stream->fd >= 0) {
        close(stream->fd);
        stream->fd = -1;
    }

    LOG_INFO("Closed %s", stream->dev_path);
}

void uvc_stream_print_stats(const UvcStream *stream)
{
    LOG_INFO("=== Stream stats for %s ===", stream->dev_path);
    LOG_INFO("  V4L2 buffers: %lu", (unsigned long)stream->v4l2_buffers_received);
    LOG_INFO("  DY66 frames:  %lu", (unsigned long)stream->total_frames);
    LOG_INFO("  Total bytes:  %lu", (unsigned long)stream->total_bytes);
    LOG_INFO("  Video frames: %lu", (unsigned long)stream->video_frames);
    LOG_INFO("  Detect events: %lu", (unsigned long)stream->detect_events);
}
