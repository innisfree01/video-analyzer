#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>

#include "uvc_xu_ctrl.h"
#include "uvc_protocol.h"

#define LOG_TAG "XU_CTRL"
#define LOG_INFO(fmt, ...)  fprintf(stdout, "[%s] " fmt "\n", LOG_TAG, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   fprintf(stderr, "[%s][ERROR] " fmt "\n", LOG_TAG, ##__VA_ARGS__)

int uvc_xu_ctrl(int fd, unsigned int unit, unsigned int selector,
                unsigned int query, unsigned int data_size,
                unsigned char *data_addr)
{
    struct uvc_xu_control_query x_ctrl;
    memset(&x_ctrl, 0, sizeof(x_ctrl));

    x_ctrl.unit     = unit;
    x_ctrl.selector = selector;
    x_ctrl.query    = query;
    x_ctrl.size     = data_size;
    x_ctrl.data     = data_addr;

    int ret = ioctl(fd, UVCIOC_CTRL_QUERY, &x_ctrl);
    if (ret < 0) {
        LOG_ERR("UVCIOC_CTRL_QUERY failed: unit=0x%02X sel=0x%02X query=0x%02X errno=%d (%s)",
                unit, selector, query, errno, strerror(errno));
        return -1;
    }
    return 0;
}

int uvc_xu_set(int fd, unsigned int unit, unsigned int selector,
               unsigned int data_size, unsigned char *data_addr)
{
    return uvc_xu_ctrl(fd, unit, selector, UVC_SET_CUR, data_size, data_addr);
}

int uvc_xu_get_cur(int fd, unsigned int unit, unsigned int selector,
                   unsigned int data_size, unsigned char *data_addr)
{
    return uvc_xu_ctrl(fd, unit, selector, UVC_GET_CUR, data_size, data_addr);
}

/* ========================================================================
 * High-level XU commands
 * ======================================================================== */

int uvc_open_video_channel(int fd, uint8_t channel, uint16_t width,
                           uint16_t height, uint8_t venc, uint8_t fps,
                           uint16_t bitrate)
{
    UvcVideoAttr_t attr;
    memset(&attr, 0, sizeof(attr));

    attr.mask = VIDEO_ATTR_SWITCH_ON | VIDEO_ATTR_RESOLUTION |
                VIDEO_ATTR_VENC | VIDEO_ATTR_FRAME_RATE |
                VIDEO_ATTR_RATE_CTRL_MODE | VIDEO_ATTR_RATE |
                VIDEO_ATTR_GOP;

    attr.channel        = channel;
    attr.switch_on      = 1;
    attr.width          = width;
    attr.height         = height;
    attr.venc           = venc;
    attr.frame_rate     = fps;
    attr.rate_ctrl_mode = 1; /* CBR */
    attr.rate           = bitrate;
    attr.GOP            = fps; /* one I-frame per second */

    LOG_INFO("Opening video channel %d: %dx%d venc=%d fps=%d rate=%dK",
             channel, width, height, venc, fps, bitrate);

    return uvc_xu_set(fd, EXRNSION_UNIT_ID, XC_UVC_CMD_VIDEO_ATTR,
                      sizeof(attr), (unsigned char *)&attr);
}

int uvc_close_video_channel(int fd, uint8_t channel)
{
    UvcVideoAttr_t attr;
    memset(&attr, 0, sizeof(attr));

    attr.mask       = VIDEO_ATTR_SWITCH_ON;
    attr.channel    = channel;
    attr.switch_on  = 0;

    LOG_INFO("Closing video channel %d", channel);

    return uvc_xu_set(fd, EXRNSION_UNIT_ID, XC_UVC_CMD_VIDEO_ATTR,
                      sizeof(attr), (unsigned char *)&attr);
}

int uvc_set_ntp_time(int fd)
{
    UvcNtp_t ntp;
    memset(&ntp, 0, sizeof(ntp));

    struct timeval tv;
    gettimeofday(&tv, NULL);

    ntp.mask            = NTP_TIMEZONE | NTP_TIME | OSD_TIME_FORMAT | OSD_DATE_FORMAT;
    ntp.time_zone       = 800; /* East +8:00 */
    ntp.time_sec        = (uint32_t)tv.tv_sec;
    ntp.time_usec       = (uint32_t)tv.tv_usec;
    ntp.osd_time_format = 0; /* 24h */
    ntp.osd_date_format = 1; /* yyyy/MM/dd */

    LOG_INFO("Setting NTP time: %u sec, timezone +8", ntp.time_sec);

    return uvc_xu_set(fd, EXRNSION_UNIT_ID, XC_UVC_CMD_NTP,
                      sizeof(ntp), (unsigned char *)&ntp);
}

int uvc_open_audio_channel(int fd, uint8_t channel, uint8_t volume)
{
    UvcAudioAttr_t attr;
    memset(&attr, 0, sizeof(attr));

    attr.mask       = AUDIO_ATTR_SWITCH_ON | AUDIO_ATTR_VOLUME | AUDIO_ATTR_MUTE;
    attr.channel    = channel;
    attr.switch_on  = 1;
    attr.volume     = volume;
    attr.mute       = 0;

    LOG_INFO("Opening audio channel %d, volume=%d", channel, volume);

    return uvc_xu_set(fd, EXRNSION_UNIT_ID, XC_UVC_CMD_AUDIO_ATTR,
                      sizeof(attr), (unsigned char *)&attr);
}

int uvc_close_audio_channel(int fd, uint8_t channel)
{
    UvcAudioAttr_t attr;
    memset(&attr, 0, sizeof(attr));

    attr.mask       = AUDIO_ATTR_SWITCH_ON;
    attr.channel    = channel;
    attr.switch_on  = 0;

    LOG_INFO("Closing audio channel %d", channel);

    return uvc_xu_set(fd, EXRNSION_UNIT_ID, XC_UVC_CMD_AUDIO_ATTR,
                      sizeof(attr), (unsigned char *)&attr);
}

int uvc_set_host_status(int fd, uint32_t status)
{
    UvcHostStatus_t hs;
    memset(&hs, 0, sizeof(hs));

    hs.mask         = STATUS_CTRL_HOST_STATUS;
    hs.host_status  = status;

    LOG_INFO("Setting host status: %u", status);

    return uvc_xu_set(fd, EXRNSION_UNIT_ID, XC_UVC_CMD_HOST_STATUS_NOTIFY,
                      sizeof(hs), (unsigned char *)&hs);
}

int uvc_get_device_info(int fd, char *version_buf, size_t buf_len)
{
    UvcDeviceInfo_t info;
    memset(&info, 0, sizeof(info));

    int ret = uvc_xu_get_cur(fd, EXRNSION_UNIT_ID, XC_UVC_CMD_DEVICE_INFO,
                             sizeof(info), (unsigned char *)&info);
    if (ret < 0)
        return ret;

    snprintf(version_buf, buf_len, "%s", info.version);
    LOG_INFO("Device version: %s", version_buf);
    return 0;
}

int uvc_request_keyframe(int fd, uint8_t channel)
{
    UvcVideoAttr_t attr;
    memset(&attr, 0, sizeof(attr));

    attr.mask       = VIDEO_ATTR_KEY_FRAME;
    attr.channel    = channel;
    attr.key_frame  = 1;

    return uvc_xu_set(fd, EXRNSION_UNIT_ID, XC_UVC_CMD_VIDEO_ATTR,
                      sizeof(attr), (unsigned char *)&attr);
}
