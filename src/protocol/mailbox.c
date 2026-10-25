#include "open_secure_rom/event_log.h"
#include "open_secure_rom/mailbox.h"
#include "open_secure_rom/protocol.h"
#include "open_secure_rom/runtime.h"
#include "open_secure_rom/runtime_storage.h"
#include "open_secure_rom/system_closure.h"

enum {
    SEP_MAILBOX_CONTROL = 0x10,
    SEP_MAILBOX_RAW = 0x14,
    SEP_MAILBOX_ENABLE = 0x18,
    SEP_MAILBOX_RX_STATUS = 0x1c,
    SEP_MAILBOX_TX_STATUS = 0x20,
    SEP_MAILBOX_RX_SLOT = 0x100,
    SEP_MAILBOX_TX_SLOT = 0x200,
    SEP_MAILBOX_EMPTY = 1U << 17,
    SEP_MAILBOX_RX_EVENT = 1U << 4,
    SEP_MAILBOX_ENABLE_RX_NOT_EMPTY = 1U << 4,
    SEP_MAILBOX_ENABLE_TX_EMPTY = 1U << 5,
    SEP_MAILBOX_ENABLE_EVENTS = SEP_MAILBOX_ENABLE_RX_NOT_EMPTY | SEP_MAILBOX_ENABLE_TX_EMPTY,
    SEP_MAILBOX_ACTIVE = 1U << 31,
};

static volatile sep_u8 *const mailbox = (volatile sep_u8 *)0x05000000;
static sep_bool pending_sequence_valid;
static sep_u8 pending_sequence;
static sep_u64 shared_region_base;
static sep_u8 shared_region_mode;

extern sep_mailbox_slot_t sep_mailbox_load_pair(const volatile void *address);
extern void sep_mailbox_store_pair(
    volatile void *address,
    sep_u64 low,
    sep_u64 high
);
static volatile sep_u32 *mailbox_register(sep_size_t offset) {
    return (volatile sep_u32 *)(mailbox + offset);
}

void sep_mailbox_activate_and_wait_ready(void) {
    while ((*mailbox_register(SEP_MAILBOX_RAW) & SEP_MAILBOX_ACTIVE) == 0) {
        *mailbox_register(SEP_MAILBOX_CONTROL) |= 1;
        *mailbox_register(SEP_MAILBOX_RAW) |= SEP_MAILBOX_ACTIVE;
    }
    *mailbox_register(SEP_MAILBOX_ENABLE) &= ~(sep_u32)SEP_MAILBOX_ENABLE_EVENTS;
}

void sep_mailbox_send_startup_event(sep_u32 phase) {
    const sep_mailbox_slot_t slot = {
        .low = 0x00d200ffULL | ((sep_u64)phase << 32),
        .high = 0,
    };
    sep_mailbox_send_slot(&slot);
}

void sep_mailbox_build_and_send_response(
    sep_u8 tag,
    sep_u8 argument,
    sep_u32 payload
) {
    const sep_mailbox_slot_t slot = sep_protocol_build_response(
        sep_mailbox_take_pending_sequence(),
        tag,
        argument,
        payload
    );
    sep_mailbox_send_slot(&slot);
}

sep_mailbox_slot_t sep_mailbox_receive_slot(void) {
    while ((*mailbox_register(SEP_MAILBOX_RX_STATUS) & SEP_MAILBOX_EMPTY) != 0) {
        *mailbox_register(SEP_MAILBOX_ENABLE) =
            (*mailbox_register(SEP_MAILBOX_ENABLE) & ~(sep_u32)SEP_MAILBOX_ENABLE_EVENTS) |
            (sep_u32)SEP_MAILBOX_ENABLE_RX_NOT_EMPTY;
        sep_wait_for_event_once_synchronized();
        *mailbox_register(SEP_MAILBOX_ENABLE) &= ~(sep_u32)SEP_MAILBOX_ENABLE_EVENTS;
    }
    const sep_mailbox_slot_t slot =
        sep_mailbox_load_pair(mailbox + SEP_MAILBOX_RX_SLOT);

    /* RX is a level interrupt.  Reading the final slot word removes the
     * FIFO entry; W1C then lets the controller recompute and lower its line.
     * The same register holds the active bit.  On Virtualization.framework's
     * mailbox, a write without it loses the message queued to the AP (it
     * arrives as zeros) and the handshake stalls. */
    *mailbox_register(SEP_MAILBOX_RAW) = SEP_MAILBOX_ACTIVE | SEP_MAILBOX_RX_EVENT;
    return slot;
}

void sep_mailbox_send_slot(const sep_mailbox_slot_t *slot) {
    while ((*mailbox_register(SEP_MAILBOX_TX_STATUS) & SEP_MAILBOX_EMPTY) == 0) {
        *mailbox_register(SEP_MAILBOX_ENABLE) =
            (*mailbox_register(SEP_MAILBOX_ENABLE) & ~(sep_u32)SEP_MAILBOX_ENABLE_EVENTS) |
            (sep_u32)SEP_MAILBOX_ENABLE_TX_EMPTY;
        sep_wait_for_event_once_synchronized();
        *mailbox_register(SEP_MAILBOX_ENABLE) &= ~(sep_u32)SEP_MAILBOX_ENABLE_EVENTS;
    }
    sep_mailbox_store_pair(mailbox + SEP_MAILBOX_TX_SLOT, slot->low, slot->high);
}

sep_u8 sep_mailbox_take_pending_sequence(void) {
    if (!pending_sequence_valid) {
        sep_fatal_wait_loop();
    }
    const sep_u8 sequence = pending_sequence;
    pending_sequence_valid = 0;
    return sequence;
}

void sep_mailbox_send_c7_payload_response(sep_u32 payload) {
    sep_mailbox_build_and_send_response(0xc7, 0, payload);
}

void sep_mailbox_send_empty_response(sep_u8 tag) {
    sep_mailbox_build_and_send_response(tag, 0, 0);
}

void sep_mailbox_send_tag_6a_empty_response(void) {
    sep_mailbox_send_empty_response(0x6a);
}

sep_u64 sep_mailbox_get_shared_region_base(void) {
    return shared_region_base;
}

sep_u8 sep_mailbox_get_shared_region_mode(void) {
    return shared_region_mode;
}

void sep_platform_mailbox_command9(sep_bool has_upper_word) {
    (void)has_upper_word;
}

void sep_platform_protocol_shutdown(sep_bool shutting_down) {
    (void)shutting_down;
}

void sep_platform_mailbox_command19(void) {}

void sep_protocol_transition_noop_hook(void) {}

void open_secure_rom_mailbox_protocol_reset(void) {
    pending_sequence_valid = 0;
    pending_sequence = 0;
    shared_region_base = 0;
    shared_region_mode = 0;
    open_secure_rom_protocol_phase_reset();
}

sep_u32 sep_mailbox_receive_and_dispatch_requests(void) {
    for (;;) {
        const sep_mailbox_slot_t request = sep_mailbox_receive_slot();
        if (pending_sequence_valid) {
            sep_fatal_wait_loop();
        }
        pending_sequence = (sep_u8)(request.low >> 8);
        pending_sequence_valid = 1;
        const sep_protocol_dispatch_result_t result =
            sep_protocol_dispatch_request(request.low);
        if (result.has_response) {
            pending_sequence_valid = 0;
            sep_mailbox_send_slot(&result.response);
        }
        if (result.phase == 2) {
            shared_region_base = result.shared_region_base;
            shared_region_mode = result.shared_region_mode;
        }
        if (result.phase != 0) {
            return result.phase;
        }
    }
}

void __attribute__((noreturn)) sep_mailbox_protocol_coordinator(void) {
    sep_clear_research_value64();
    sep_clear_research_seed();
    open_secure_rom_mailbox_protocol_reset();
    sep_mailbox_activate_and_wait_ready();
    sep_protocol_event_log_append(OPEN_SECURE_ROM_EVENT_MAILBOX_READY);
    sep_mailbox_send_startup_event(1);
    if (sep_mailbox_receive_and_dispatch_requests() != 1) {
        sep_fatal_wait_loop();
    }
    sep_protocol_event_log_append(OPEN_SECURE_ROM_EVENT_AKE_READY);
    sep_runtime_prepare_zeroed_regions(sep_mailbox_get_command11_word());
    sep_protocol_event_log_append(OPEN_SECURE_ROM_EVENT_REGIONS_READY);
    sep_mailbox_send_startup_event(2);
    if (sep_mailbox_receive_and_dispatch_requests() != 2) {
        sep_fatal_wait_loop();
    }
    if ((sep_mailbox_get_shared_region_base() >> 32) != 0) {
        sep_fatal_wait_loop();
    }
    sep_shared_region_import_and_commit(
        sep_mailbox_get_shared_region_base(),
        sep_mailbox_get_shared_region_mode()
    );
}
