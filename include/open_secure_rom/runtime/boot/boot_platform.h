#ifndef OPEN_SECURE_ROM_BOOT_PLATFORM_H
#define OPEN_SECURE_ROM_BOOT_PLATFORM_H

#include "open_secure_rom/types.h"

/*
 * How the SEP firmware expects its ROM to enter it.
 *
 * This belongs to the ROM the firmware was built against, not to the
 * firmware's release: the vphone600 SEPOS and the iPhone14,4 SEPOS of the
 * same release begin with the same instructions and part ways at the fork
 * right after them, so the code cannot tell them apart.  The platform is
 * chosen at build time; see docs/platforms.md for the table and how to add
 * a row.
 */
typedef enum {
    /* AVPSEPBooter.vresearch1, the SEP ROM of Virtualization.framework. */
    SEP_BOOT_PLATFORM_VRESEARCH1 = 0,
    /* The t8110 SEPROM, as the iphone-cli QEMU machine models it for d16. */
    SEP_BOOT_PLATFORM_T8110 = 1,
    SEP_BOOT_PLATFORM_COUNT,
} sep_boot_platform_id_t;

typedef struct {
    /*
     * Where the runtime is entered.  Zero derives it the way AVPSEPBooter
     * does: region one's base plus the runtime mode's offset.
     */
    sep_u64 runtime_entry;
    /* What the handoff's runtime_destination says, when not the entry. */
    sep_u64 reported_destination;
    /*
     * The handoff's runtime_region_length.  Zero is AVPSEPBooter's rule,
     * region one's length capped at SEP_BOOT_RUNTIME_REGION_LENGTH_CAP;
     * anything else is written as it is.
     */
    sep_u32 runtime_region_length;
    /*
     * The runtime takes its handoff pointer from s3_7_c15_c2_0, with bit
     * zero set as the cold-boot flag, instead of from x0.  x0 carries the
     * same value.
     */
    sep_bool handoff_in_power_down_register;
    /* x1 at entry is the entry plus this; zero enters with x1 clear. */
    sep_u64 cold_input_offset;
    /*
     * The ROM builds the translation tables the runtime turns its MMU on
     * with; otherwise the runtime builds its own.
     */
    sep_bool rom_builds_translation_tables;
    /*
     * Addresses that arrive over the mailbox are translated through
     * dart-sep's stream zero, read at SEP_DART_BASE (shared_region.c);
     * otherwise they are used as they are.
     */
    sep_bool walks_dart;
} sep_boot_platform_t;

enum {
    SEP_BOOT_RUNTIME_REGION_LENGTH_CAP = 0x18000,
};

const sep_boot_platform_t *sep_boot_platform(void);

#ifdef OPEN_SECURE_ROM_HOST_TEST
void open_secure_rom_boot_platform_test_select(sep_boot_platform_id_t id);
#endif

#endif
