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

        struct v4l2_frmsizeenum frmsize;
        memset(&frmsize, 0, sizeof(frmsize));
        frmsize.pixel_format = fmtdesc.pixelformat;
        frmsize.index = 0;

        while (xioctl(stream->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                LOG_INFO("    Size[%u]: %ux%u", frmsize.index,
                         frmsize.discrete.width, frmsize.discrete.height);
            } else {
                LOG_INFO("    Size range: %u-%u x %u-%u",
                         frmsize.stepwise.min_width, frmsize.stepwise.max_width,
                         frmsize.stepwise.min_height, frmsize.stepwise.max_height);
            }
            frmsize.index++;
        }

        fmtdesc.index++;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(stream->fd, VIDIOC_G_FMT, &fmt) == 0) {
        char fourcc_str[5];
        fourcc_to_str(fmt.fmt.pix.pixelformat, fourcc_str);
        LOG_INFO("  Current format: '%s' %ux%u sizeimage=%u",
                 fourcc_str, fmt.fmt.pix.width, fmt.fmt.pix.height,
                 fmt.fmt.pix.sizeimage);
    }

    LOG_INFO("=== End format enumeration ===");
}

/*
 * Check if a format+size combination is supported by the device.
 */
static int format_supports_size(int fd, uint32_t pixfmt,
                                uint32_t width, uint32_t height)
{
    struct v4l2_frmsizeenum frmsize;
    memset(&frmsize, 0, sizeof(frmsize));
    frmsize.pixel_format = pixfmt;
    frmsize.index = 0;

    while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
        if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            if (frmsize.discrete.width == width &&
                frmsize.discrete.height == height)
                return 1;
        } else {
            if (width >= frmsize.stepwise.min_width &&
                width <= frmsize.stepwise.max_width &&
                height >= frmsize.stepwise.min_height &&
                height <= frmsize.stepwise.max_height)
                return 1;
        }
        frmsize.index++;
    }
    return 0;
}

/*
 * Check if a pixel format is supported by the device.
 */
static int device_has_format(int fd, uint32_t pixfmt)
{
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    while (xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        if (fmtdesc.pixelformat == pixfmt)
            return 1;
        fmtdesc.index++;
    }
    return 0;
}

/*
 * Find the largest supported resolution for a given pixel format.
 */
static int find_max_resolution(int fd, uint32_t pixfmt,
                               uint32_t *out_w, uint32_t *out_h)
{
    struct v4l2_frmsizeenum frmsize;
    memset(&frmsize, 0, sizeof(frmsize));
    frmsize.pixel_format = pixfmt;
    frmsize.index = 0;

    uint32_t best_w = 0, best_h = 0;

    while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
        uint32_t w, h;
        if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            w = frmsize.discrete.width;
            h = frmsize.discrete.height;
        } else {
            w = frmsize.stepwise.max_width;
            h = frmsize.stepwise.max_height;
        }

        if ((uint64_t)w * h > (uint64_t)best_w * best_h) {
            best_w = w;
            best_h = h;
        }
        frmsize.index++;
    }

    if (best_w > 0) {
        *out_w = best_w;
        *out_h = best_h;
        return 0;
    }
    return -1;
}

#ifndef V4L2_PIX_FMT_H264
#define V4L2_PIX_FMT_H264  v4l2_fourcc('H', '2', '6', '4')
#endif
#ifndef V4L2_PIX_FMT_H265
#define V4L2_PIX_FMT_H265  v4l2_fourcc('H', '2', '6', '5')
#endif

#ifndef V4L2_PIX_FMT_MJPG
#define V4L2_PIX_FMT_MJPG  v4l2_fourcc('M', 'J', 'P', 'G')
#endif
/* Use local name to avoid conflict with V4L2_PIX_FMT_YUYV from videodev2.h */
#define LOCAL_PIX_FMT_YUYV  v4l2_fourcc('Y', 'U', 'Y', 'V')

int uvc_stream_find_best_format(UvcStream *stream, uint32_t want_width,
                                uint32_t want_height, uint8_t want_venc,
                                FormatSelection *out)
{
    int fd = stream->fd;
    memset(out, 0, sizeof(*out));

    /*
     * Strategy: match the V4L2 pixel format to the desired XU encoding.
     *
     * H.264/H.265 via XU → V4L2 H264 format (H.265 is tunneled through H.264 pipe)
     * MJPEG via XU        → V4L2 MJPG format
     * YUV via XU          → V4L2 YUYV format
     *
     * If the preferred format isn't available, fall back to the next best option.
     */

    /* Priority list based on user's requested encoding */
    struct {
        uint32_t v4l2_fmt;
        uint8_t  xu_venc;
        const char *name;
    } candidates[4];
    int n_candidates = 0;

    if (want_venc == UVC_VENC_TYPE_H265) {
        candidates[n_candidates++] = (typeof(candidates[0])){V4L2_PIX_FMT_H265, UVC_VENC_TYPE_H265, "H265"};
        candidates[n_candidates++] = (typeof(candidates[0])){V4L2_PIX_FMT_H264, UVC_VENC_TYPE_H265, "H264"};
        candidates[n_candidates++] = (typeof(candidates[0])){V4L2_PIX_FMT_MJPG, UVC_VENC_TYPE_MJPEG, "MJPG"};
        candidates[n_candidates++] = (typeof(candidates[0])){LOCAL_PIX_FMT_YUYV, UVC_VENC_TYPE_YUV, "YUYV"};
    } else if (want_venc == UVC_VENC_TYPE_H264) {
        candidates[n_candidates++] = (typeof(candidates[0])){V4L2_PIX_FMT_H264, UVC_VENC_TYPE_H264, "H264"};
        candidates[n_candidates++] = (typeof(candidates[0])){V4L2_PIX_FMT_MJPG, UVC_VENC_TYPE_MJPEG, "MJPG"};
        candidates[n_candidates++] = (typeof(candidates[0])){LOCAL_PIX_FMT_YUYV, UVC_VENC_TYPE_YUV, "YUYV"};
    } else if (want_venc == UVC_VENC_TYPE_MJPEG) {
        candidates[n_candidates++] = (typeof(candidates[0])){V4L2_PIX_FMT_MJPG, UVC_VENC_TYPE_MJPEG, "MJPG"};
        candidates[n_candidates++] = (typeof(candidates[0])){V4L2_PIX_FMT_H264, UVC_VENC_TYPE_H264, "H264"};
        candidates[n_candidates++] = (typeof(candidates[0])){LOCAL_PIX_FMT_YUYV, UVC_VENC_TYPE_YUV, "YUYV"};
    } else {
        candidates[n_candidates++] = (typeof(candidates[0])){LOCAL_PIX_FMT_YUYV, UVC_VENC_TYPE_YUV, "YUYV"};
        candidates[n_candidates++] = (typeof(candidates[0])){V4L2_PIX_FMT_H264, UVC_VENC_TYPE_H264, "H264"};
        candidates[n_candidates++] = (typeof(candidates[0])){V4L2_PIX_FMT_MJPG, UVC_VENC_TYPE_MJPEG, "MJPG"};
    }

    for (int i = 0; i < n_candidates; i++) {
        if (!device_has_format(fd, candidates[i].v4l2_fmt))
            continue;

        /* Check if desired resolution is supported */
        if (format_supports_size(fd, candidates[i].v4l2_fmt, want_width, want_height)) {
            out->pixfmt = candidates[i].v4l2_fmt;
            out->width = want_width;
            out->height = want_height;
            out->matched_venc = candidates[i].xu_venc;
            LOG_INFO("[%s] Best format: %s %ux%u (xu_venc=%d)",
                     stream->dev_path, candidates[i].name,
                     want_width, want_height, candidates[i].xu_venc);
            return 0;
        }

        /* Desired resolution not available, use the largest available */
        uint32_t max_w, max_h;
        if (find_max_resolution(fd, candidates[i].v4l2_fmt, &max_w, &max_h) == 0) {
            out->pixfmt = candidates[i].v4l2_fmt;
            out->width = max_w;
            out->height = max_h;
            out->matched_venc = candidates[i].xu_venc;
            LOG_WARN("[%s] Requested %ux%u not available for %s, using max %ux%u",
                     stream->dev_path, want_width, want_height,
                     candidates[i].name, max_w, max_h);
            return 0;
        }
    }

    LOG_ERR("[%s] No suitable format found!", stream->dev_path);
    return -1;
}

int uvc_stream_set_format(UvcStream *stream, uint32_t pixfmt,
                          uint32_t width, uint32_t height)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    char fourcc_str[5];
    fourcc_to_str(pixfmt, fourcc_str);
    LOG_INFO("Setting format: '%s' %ux%u on %s",
             fourcc_str, width, height, stream->dev_path);

    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(stream->fd, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_ERR("VIDIOC_S_FMT failed on %s: %s", stream->dev_path, strerror(errno));

        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(stream->fd, VIDIOC_G_FMT, &fmt) < 0) {
            LOG_ERR("VIDIOC_G_FMT failed: %s", strerror(errno));
            return -1;
        }
    }

    fourcc_to_str(fmt.fmt.pix.pixelformat, fourcc_str);
    stream->pix_fmt      = fmt.fmt.pix.pixelformat;
    stream->frame_width  = fmt.fmt.pix.width;
    stream->frame_height = fmt.fmt.pix.height;
    stream->sizeimage    = fmt.fmt.pix.sizeimage;

    LOG_INFO("Active format on %s: '%s' %ux%u sizeimage=%u",
             stream->dev_path, fourcc_str,
             fmt.fmt.pix.width, fmt.fmt.pix.height,
             fmt.fmt.pix.sizeimage);

    return 0;
}

int uvc_stream_start(UvcStream *stream)
{
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
            LOG_ERR("Insufficient buffer memory on %s (got %u)",
                    stream->dev_path, req.count);
            return -1;
        }

        stream->n_buffers = req.count;

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

            LOG_INFO("  Buffer[%u]: length=%u", i, buf.length);

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
        LOG_INFO("Mapped %u V4L2 buffers on %s", stream->n_buffers, stream->dev_path);
    }

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
    LOG_INFO("STREAMON on %s", stream->dev_path);
    return 0;
}

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
    fprintf(stdout, "    ");
    for (uint32_t i = 0; i < n; i++) {
        fprintf(stdout, "%02X ", data[i]);
        if ((i + 1) % 16 == 0 && i + 1 < n)
            fprintf(stdout, "\n    ");
    }
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
        LOG_WARN("select timeout (5s) on %s (bufs=%lu, errors=%lu)",
                 stream->dev_path,
                 (unsigned long)stream->v4l2_buffers_received,
                 (unsigned long)stream->v4l2_error_buffers);
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

    /* Check for error flag */
    if (buf.flags & V4L2_BUF_FLAG_ERROR) {
        stream->v4l2_error_buffers++;
        if (stream->v4l2_error_buffers <= 3) {
            LOG_WARN("[%s] V4L2 buf #%lu: ERROR flag set, bytesused=%u flags=0x%X",
                     stream->dev_path,
                     (unsigned long)stream->v4l2_buffers_received,
                     length, buf.flags);
        }
        /* Re-queue and continue */
        xioctl(stream->fd, VIDIOC_QBUF, &buf);
        return 0;
    }

    /* Log first few successful buffers */
    if (stream->v4l2_buffers_received - stream->v4l2_error_buffers <= 5 && length > 0) {
        LOG_INFO("[%s] V4L2 buf: bytesused=%u flags=0x%X",
                 stream->dev_path, length, buf.flags);
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
    LOG_INFO("STREAMOFF on %s", stream->dev_path);
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
    LOG_INFO("=== Stats %s ===", stream->dev_path);
    LOG_INFO("  V4L2 bufs:    %lu (errors: %lu)",
             (unsigned long)stream->v4l2_buffers_received,
             (unsigned long)stream->v4l2_error_buffers);
    LOG_INFO("  DY66 frames:  %lu (%lu video, %lu detect)",
             (unsigned long)stream->total_frames,
             (unsigned long)stream->video_frames,
             (unsigned long)stream->detect_events);
    LOG_INFO("  Total bytes:  %lu", (unsigned long)stream->total_bytes);
}
