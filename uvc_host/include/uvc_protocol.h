#ifndef UVC_PROTOCOL_H
#define UVC_PROTOCOL_H

#include <stdint.h>

#pragma pack(push, 1)

/* ========================================================================
 * UVC Extension Unit definitions
 * ======================================================================== */

#define EXRNSION_UNIT_ID        0x0B

#define UVC_SET_CUR             0x01
#define UVC_GET_CUR             0x81
#define UVC_GET_MIN             0x82
#define UVC_GET_MAX             0x83
#define UVC_GET_RES             0x84
#define UVC_GET_LEN             0x85
#define UVC_GET_INFO            0x86
#define UVC_GET_DEF             0x87

/* Command IDs (selectors) */
#define XC_UVC_CMD_VIDEO_ATTR           0x01
#define XC_UVC_CMD_NTP                  0x02
#define XC_UVC_CMD_AUDIO_ATTR           0x03
#define XC_UVC_CMD_ALARM_DETECT         0x04
#define XC_UVC_CMD_SENSOR_ATTR          0x05
#define XC_UVC_CMD_COMMON_CTRL          0x06
#define XC_UVC_CMD_DEVICE_INFO          0x07
#define XC_UVC_CMD_CPE_CTRL             0x08
#define XC_UVC_CMD_WIFI_PASSWORD        0x09
#define XC_UVC_CMD_GET_CPE_INFO         0x0A
#define XC_UVC_CMD_HOST_STATUS_NOTIFY   0x0B
#define XC_UVC_CMD_OTA                  0x0C
#define XC_UVC_CMD_GET_SDCARD_INFO      0x0D
#define XC_UVC_CMD_GET_ETH_INFO         0x0E
#define XC_UVC_CMD_SET_HOST_INFO        0x0F
#define XC_UVC_CMD_GET_CPE_VERSION      0x10
#define XC_UVC_CMD_SET_FACTORY_INFO     0x11
#define XC_UVC_CMD_GET_DID_SN           0x12
#define XC_UVC_CMD_GET_MAC_INFO         0x13

/* VID/PID for IPC camera */
#define UVC_IPC_VID             0x3337
#define UVC_IPC_PID             0x4321

/* VID/PID for extension board */
#define UVC_EXT_VID             0x3338
#define UVC_EXT_PID             0x4322

/* ========================================================================
 * 1.2.1 Video parameters (Command ID: 0x01)
 * ======================================================================== */

typedef enum {
    VIDEO_ATTR_SWITCH_ON        = 1 << 0,
    VIDEO_ATTR_RESOLUTION       = 1 << 1,
    VIDEO_ATTR_VENC             = 1 << 2,
    VIDEO_ATTR_OSD              = 1 << 3,
    VIDEO_ATTR_TIME_LOCATION    = 1 << 4,
    VIDEO_ATTR_LOGO_LOCATION    = 1 << 5,
    VIDEO_ATTR_RATE_CTRL_MODE   = 1 << 6,
    VIDEO_ATTR_OSD_REVERSE      = 1 << 7,
    VIDEO_ATTR_GOP              = 1 << 8,
    VIDEO_ATTR_FRAME_RATE       = 1 << 9,
    VIDEO_ATTR_RATE             = 1 << 10,
    VIDEO_ATTR_KEY_FRAME        = 1 << 11,
    VIDEO_ATTR_HUMAN_BOX        = 1 << 12,
    VIDEO_ATTR_QP               = 1 << 13,
} VideoAttrCtrlBit;

typedef enum {
    UVC_VENC_TYPE_H264 = 1,
    UVC_VENC_TYPE_H265,
    UVC_VENC_TYPE_YUV,
    UVC_VENC_TYPE_MJPEG,
} UvcVencType;

typedef struct {
    uint32_t mask;
    uint8_t  channel;
    uint8_t  switch_on;
    uint16_t width;
    uint16_t height;
    uint8_t  venc;
    uint8_t  osd;
    uint32_t time_location;
    uint32_t logo_location;
    uint8_t  rate_ctrl_mode;
    uint8_t  osd_reverse;
    uint8_t  GOP;
    uint8_t  frame_rate;
    uint16_t rate;
    uint8_t  key_frame;
    uint8_t  human_box;
    uint8_t  qp_type;
    uint8_t  max_qp;
    uint8_t  min_qp;
    uint8_t  max_i_qp;
    uint8_t  min_i_qp;
    uint8_t  change_pos;
    uint8_t  res[26];
} UvcVideoAttr_t;

/*
 * Channel calculation:
 *   Chan = i * N + j
 *   i = sensor index (0~3)
 *   N = max channels per sensor (currently 3)
 *   j = channel index within sensor (0~2)
 *
 *   sensor0: 0,1,2    sensor1: 3,4,5
 *   sensor2: 6,7,8    sensor3: 9,10,11
 */
#define UVC_CHANNELS_PER_SENSOR  3
#define UVC_SENSORS_PER_DEVICE   2
#define UVC_MAKE_CHANNEL(sensor, ch)  ((sensor) * UVC_CHANNELS_PER_SENSOR + (ch))

/* ========================================================================
 * 1.2.2 NTP time (Command ID: 0x02)
 * ======================================================================== */

typedef enum {
    NTP_TIMEZONE        = 1 << 0,
    NTP_TIME            = 1 << 1,
    OSD_TIME_FORMAT     = 1 << 2,
    OSD_DATE_FORMAT     = 1 << 3,
} NtpCtrlBit;

typedef struct {
    uint32_t mask;
    int32_t  time_zone;
    uint32_t time_sec;
    uint32_t time_usec;
    uint8_t  osd_time_format;
    uint8_t  osd_date_format;
    uint8_t  res[42];
} UvcNtp_t;

/* ========================================================================
 * 1.2.3 Audio parameters (Command ID: 0x03)
 * ======================================================================== */

typedef enum {
    AUDIO_ATTR_SWITCH_ON        = 1 << 0,
    AUDIO_ATTR_AENC             = 1 << 1,
    AUDIO_ATTR_BITS_PER_SAMPLE  = 1 << 2,
    AUDIO_ATTR_SAMPLE_RATE      = 1 << 3,
    AUDIO_ATTR_VOLUME           = 1 << 4,
    AUDIO_ATTR_MUTE             = 1 << 5,
} AudioAttrCtrlBit;

typedef struct {
    uint32_t mask;
    uint8_t  channel;
    uint8_t  switch_on;
    uint8_t  aenc;
    uint8_t  bits_per_sample;
    uint16_t sample_rate;
    uint8_t  volume;
    uint8_t  mute;
    uint8_t  res[48];
} UvcAudioAttr_t;

/* ========================================================================
 * 1.2.4 Alarm detection (Command ID: 0x04)
 * ======================================================================== */

#define REGION_POINT_NUM  6
#define PACK_POINT(x, y)  (((uint32_t)(y) << 16) | ((uint32_t)(x) & 0xFFFF))

typedef enum {
    ALARM_DETECT_REGION_POINTS          = 1 << 0,
    ALARM_DETECT_MOTION_ON              = 1 << 1,
    ALARM_DETECT_MOTION_SENSITIVITY     = 1 << 2,
    ALARM_DETECT_HUMAN_ON               = 1 << 3,
    ALARM_DETECT_HUMAN_SENSITIVITY      = 1 << 4,
    ALARM_DETECT_FACE_ON                = 1 << 5,
    ALARM_DETECT_FACE_SENSITIVITY       = 1 << 6,
    ALARM_DETECT_MOTION_AUDIO_ALARM     = 1 << 7,
    ALARM_DETECT_HUMAN_AUDIO_ALARM      = 1 << 8,
    ALARM_DETECT_FACE_AUDIO_ALARM       = 1 << 9,
    ALARM_DETECT_SOUND_ON               = 1 << 10,
    ALARM_DETECT_SOUND_SENSITIVITY      = 1 << 11,
} AlarmDetectCtrlBit;

typedef struct {
    uint32_t mask;
    uint32_t region_point[REGION_POINT_NUM];
    uint8_t  motion_on;
    uint8_t  motion_sensitivity;
    uint8_t  human_on;
    uint8_t  human_sensitivity;
    uint8_t  face_on;
    uint8_t  face_sensitivity;
    uint8_t  motion_audio_alarm;
    uint8_t  human_audio_alarm;
    uint8_t  face_audio_alarm;
    uint8_t  sound_det_on;
    uint8_t  sound_sensitivity;
    uint8_t  res[23];
} UvcAlarmDetect_t;

/* ========================================================================
 * 1.2.5 Sensor channel attributes (Command ID: 0x05)
 * ======================================================================== */

typedef enum {
    SENSOR_ATTR_MIRROR  = 1 << 0,
    SENSOR_ATTR_FLIP    = 1 << 1,
} SensorAttrCtrlBit;

typedef struct {
    uint32_t mask;
    uint8_t  dev_id;
    uint8_t  mirror;
    uint8_t  flip;
    uint8_t  res[53];
} UvcSensorAttr_t;

/* ========================================================================
 * 1.2.6 Common control (Command ID: 0x06)
 * ======================================================================== */

typedef enum {
    COMMON_CTRL_START_TALK      = 1 << 0,
    COMMON_CTRL_START_BIND      = 1 << 1,
    COMMON_CTRL_WHITE_LED       = 1 << 2,
    COMMON_CTRL_ELE_ATTACK_1    = 1 << 3,
    COMMON_CTRL_ELE_ATTACK_2    = 1 << 4,
    COMMON_CTRL_ELE_LIGHT_1     = 1 << 5,
    COMMON_CTRL_ELE_LIGHT_2     = 1 << 6,
    COMMON_CTRL_NIGHT_VISION    = 1 << 7,
    COMMON_CTRL_START_GYRO      = 1 << 8,
} CommonCtrlBit;

typedef struct {
    uint32_t mask;
    uint8_t  start_talk;
    uint8_t  start_bind;
    uint8_t  white_led_on;
    uint8_t  ele_attack1;
    uint8_t  ele_attack2;
    uint8_t  ele_light1;
    uint8_t  ele_light2;
    uint8_t  night_vision;
    uint8_t  start_gyro_data;
    uint8_t  gyro_dis;
    uint8_t  res[46];
} UvcCommonCtrl_t;

/* ========================================================================
 * 1.2.7 Device info (Command ID: 0x07)
 * ======================================================================== */

typedef struct {
    uint8_t version[32];
    uint8_t res[28];
} UvcDeviceInfo_t;

/* ========================================================================
 * 1.2.11 Host status notify (Command ID: 0x0B)
 * ======================================================================== */

typedef enum {
    STATUS_CTRL_HOST_STATUS = 1 << 0,
} HostStatusCtrlBit;

typedef enum {
    HOST_STATUS_PREPARE_BINDING = 0,
    HOST_STATUS_CONNECT_INTERNET,
    HOST_STATUS_CONNECT_CPE,
    HOST_STATUS_CONNECT_CLOUD,
    HOST_STATUS_OTHER_NET_ERROR,
    HOST_STATUS_BIND_ACCOUNT,
    HOST_STATUS_DISCONNECT_CLOUD,
    HOST_STATUS_RECONNECT_CLOUD,
    HOST_STATUS_NUM,
} HOST_STATUS_E;

typedef struct {
    uint32_t mask;
    uint32_t host_status;
    uint8_t  res[52];
} UvcHostStatus_t;

/* ========================================================================
 * 2.1 Frame header (device -> host)
 * ======================================================================== */

#define MAKEFLAG(ch0, ch1, ch2, ch3)  \
    ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
     ((uint32_t)(uint8_t)(ch2) << 16) | ((uint32_t)(uint8_t)(ch3) << 24))

#define UVC_FRAME_HEADER_FLAG   MAKEFLAG('D','Y','6','6')

typedef enum {
    UVC_FRAME_TYPE_VIDEO = 1,
    UVC_FRAME_TYPE_DETECT,
    UVC_FRAME_TYPE_NETWORK,
    UVC_FRAME_TYPE_GPS_RTK,
    UVC_FRAME_TYPE_IPC_RESET,
    UVC_FRAME_TYPE_GSENSOR_DATA,
    UVC_FRAME_TYPE_AUDIO,
} UvcFrameType;

typedef enum {
    UVC_EVENT_TYPE_MOTION = 1,
    UVC_EVENT_TYPE_HUMAN,
    UVC_EVENT_TYPE_SOUND,
    UVC_EVENT_TYPE_MAX,
} UvcEventType;

typedef struct {
    uint32_t HeaderFlag;
    uint32_t DataLen;
    uint8_t  FrameType;
    uint8_t  KeyFrame;
    uint8_t  Channel;
    uint8_t  EventType;
    uint32_t FrameIndex;
    uint64_t ClockTime;
    uint64_t Timestamp;
    uint16_t FrameWidth;
    uint16_t FrameHeight;
    uint8_t  VencType;
    uint8_t  res[3];
} UvcFrameHeader_t;

/* ========================================================================
 * 2.2.1 Network binding info
 * ======================================================================== */

#define UVC_WIFI_SSID_SIZE   32
#define UVC_PASSWORD_SIZE    64
#define UVC_URL_SIZE         256

typedef struct {
    uint8_t ssid[UVC_WIFI_SSID_SIZE];
    uint8_t pwd[UVC_PASSWORD_SIZE];
    uint8_t token[UVC_URL_SIZE];
} UvcBindInfo_t;

/* ========================================================================
 * 2.2.2 GSensor data
 * ======================================================================== */

#define UVC_GSENSOR_DATA_NUM  50

typedef struct {
    uint64_t frame_pts;
    uint64_t pts;
    int32_t  x;
    int32_t  y;
    int32_t  z;
    uint8_t  res[4];
} UvcGyroData_t;

typedef struct {
    int32_t       num;
    int32_t       gsensor_id;
    UvcGyroData_t data[UVC_GSENSOR_DATA_NUM];
} UvcGyroDataGroup_t;

#pragma pack(pop)

#endif /* UVC_PROTOCOL_H */
