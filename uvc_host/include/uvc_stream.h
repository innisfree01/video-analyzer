#ifndef UVC_STREAM_H
#define UVC_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "uvc_protocol.h"

#define UVC_STREAM_BUF_COUNT    4
#define UVC_STREAM_MAX_SIZE     (4 * 1024 * 1024)

typedef void (*frame_callback_t)(const UvcFrameHeader_t *header,
                                 const uint8_t *payload,
                                 uint32_t payload_len,
                                 void *user_data);

typedef struct {
    void   *start;
    size_t  length;
} StreamBuffer;

/* Result of format auto-selection */
typedef struct {
    uint32_t pixfmt;         /* V4L2 pixel format (fourcc) */
    uint32_t width;
    uint32_t height;
    uint8_t  matched_venc;   /* Recommended XU venc type for this V4L2 format */
} FormatSelection;

typedef struct {
    int             fd;
    char            dev_path[64];
    bool            streaming;
    bool            buffers_mapped;

    StreamBuffer    buffers[UVC_STREAM_BUF_COUNT];
    uint32_t        n_buffers;

    /* Accumulated reassembly buffer for multi-packet frames */
    uint8_t        *reassembly_buf;
    uint32_t        reassembly_len;
    uint32_t        reassembly_cap;

    /* Negotiated V4L2 format */
    uint32_t        pix_fmt;
    uint32_t        frame_width;
    uint32_t        frame_height;
    uint32_t        sizeimage;

    /* Stats */
    uint64_t        total_frames;
    uint64_t        total_bytes;
    uint64_t        video_frames;
    uint64_t        detect_events;
    uint64_t        v4l2_buffers_received;
    uint64_t        v4l2_error_buffers;
} UvcStream;

int  uvc_stream_open(UvcStream *stream, const char *dev_path);
void uvc_stream_enumerate_formats(UvcStream *stream);
int  uvc_stream_find_best_format(UvcStream *stream, uint32_t want_width,
                                 uint32_t want_height, uint8_t want_venc,
                                 FormatSelection *out);
int  uvc_stream_set_format(UvcStream *stream, uint32_t pixfmt,
                           uint32_t width, uint32_t height);
int  uvc_stream_start(UvcStream *stream);
int  uvc_stream_read_frame(UvcStream *stream, frame_callback_t cb, void *user_data);
void uvc_stream_stop(UvcStream *stream);
void uvc_stream_close(UvcStream *stream);
void uvc_stream_print_stats(const UvcStream *stream);

#endif /* UVC_STREAM_H */
