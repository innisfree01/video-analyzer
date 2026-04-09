#ifndef UVC_XU_CTRL_H
#define UVC_XU_CTRL_H

#include "uvc_protocol.h"

int uvc_xu_ctrl(int fd, unsigned int unit, unsigned int selector,
                unsigned int query, unsigned int data_size,
                unsigned char *data_addr);

int uvc_xu_set(int fd, unsigned int unit, unsigned int selector,
               unsigned int data_size, unsigned char *data_addr);

int uvc_xu_get_cur(int fd, unsigned int unit, unsigned int selector,
                   unsigned int data_size, unsigned char *data_addr);

/* High-level commands */

int uvc_open_video_channel(int fd, uint8_t channel, uint16_t width,
                           uint16_t height, uint8_t venc, uint8_t fps,
                           uint16_t bitrate);

int uvc_close_video_channel(int fd, uint8_t channel);

int uvc_set_ntp_time(int fd);

int uvc_open_audio_channel(int fd, uint8_t channel, uint8_t volume);

int uvc_close_audio_channel(int fd, uint8_t channel);

int uvc_set_host_status(int fd, uint32_t status);

int uvc_get_device_info(int fd, char *version_buf, size_t buf_len);

int uvc_request_keyframe(int fd, uint8_t channel);

#endif /* UVC_XU_CTRL_H */
