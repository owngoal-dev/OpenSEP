#include "open_secure_rom/event_log.h"
#include "open_secure_rom/mailbox.h"
#include "open_secure_rom/platform.h"
#include "open_secure_rom/protocol.h"
#include "open_secure_rom/runtime.h"

void __attribute__((noreturn)) sep_main_initialize(void) {
    sep_configure_timer_event_stream();
    sep_platform_counter_initialize();
    sep_platform_runtime_barrier_noop();
    sep_platform_identity_initialize();
    sep_platform_main_post_initialize();
    if (sep_platform_main_initialize_status() != 0) {
        sep_fatal_wait_loop();
    }

    if (sep_platform_primary_runtime_path_enabled()) {
        sep_platform_configuration_initialize();
        sep_main_platform_configuration_ready_hook();
    } else {
        sep_platform_configuration_alternate_init_noop();
        sep_main_platform_configuration_fallback_hook();
    }

    sep_u8 *const boot_seed = (sep_u8 *)(uintptr_t)0x04020000;
    sep_fill_research_seed_pattern(boot_seed, 8);
    boot_seed[7] = 0;
    sep_platform_transition_stage_noop(4);
    sep_platform_configuration_seed_research_block();

    sep_u64 api_key[2] = {0, 0};
    sep_fill_research_seed_pattern(api_key, sizeof(api_key));
    if ((api_key[0] | api_key[1]) == 0) {
        sep_fatal_wait_loop();
    }
    sep_write_apibkey_el1(api_key[0], api_key[1]);
    sep_memzero(api_key, sizeof(api_key));
    sep_set_sctlr_el1_enib(1);
    sep_protocol_event_log_append(OPEN_SECURE_ROM_EVENT_BOOT_READY);
    sep_mailbox_protocol_coordinator();
}
