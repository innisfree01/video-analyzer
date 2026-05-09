#ifndef UVC_EVENT_RECORD_H
#define UVC_EVENT_RECORD_H

#include <stddef.h>
#include <stdint.h>

#include "uvc_protocol.h"

int uvc_event_record_detect(const UvcFrameHeader_t *header,
                            const void *payload,
                            size_t payload_len);

#endif /* UVC_EVENT_RECORD_H */
