#ifndef OPEN_SECURE_ROM_EVENT_LOG_H
#define OPEN_SECURE_ROM_EVENT_LOG_H

#include "open_secure_rom/types.h"

enum {
    OPEN_SECURE_ROM_EVENT_BOOT_READY = 0x00010001,
    OPEN_SECURE_ROM_EVENT_MAILBOX_READY = 0x00010002,
    OPEN_SECURE_ROM_EVENT_AKE_READY = 0x00010004,
    OPEN_SECURE_ROM_EVENT_REGIONS_READY = 0x00010006,
};

sep_u32 sep_protocol_event_log_append(sep_u32 event_code);
void sep_protocol_diagnostic_record_write(
    sep_u32 event_code,
    const void *payload,
    sep_size_t payload_length
);
void sep_protocol_log_diagnostic_payload(
    sep_u32 event_code,
    const void *payload,
    sep_size_t payload_length
);
void sep_protocol_event_log_snapshot(
    sep_u32 output[16],
    sep_size_t output_length
);
void sep_protocol_diagnostic_record_snapshot(
    sep_u32 output[8],
    sep_size_t output_length
);

#endif
