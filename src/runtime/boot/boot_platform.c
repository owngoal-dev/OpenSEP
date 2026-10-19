#include "open_secure_rom/boot_platform.h"

#include "open_secure_rom/runtime/boot/bootstrap.h"

/*
 * The platform this ROM is built for: `make PLATFORM=...`, which the Makefile
 * turns into one of the sep_boot_platform_id_t names.
 */
#ifndef OPEN_SECURE_ROM_PLATFORM
#error "OPEN_SECURE_ROM_PLATFORM is set by the Makefile from PLATFORM"
#endif

static const sep_boot_platform_t boot_platforms[] = {
    /*
     * What AVPSEPBooter.vresearch1 does, read out of it (addresses are the
     * ROM's own, linked at 0x0200_0000).  It enters at region one's base
     * plus the mode's offset, 0x0600_0000 for mode four (0x2001FD8 over the
     * table at 0x2024300); hands over the handoff pointer in x0 with x1
     * clear (0x2001C44 into 0x2001AC0); caps runtime_region_length at
     * 0x18000 (0x2001D00); and builds no translation tables.  The
     * vphone600 SEPOS keeps x0, branches on it being zero, and reads the
     * magic through it (0x4008, 0x404C, 0x6328 in 23E5207q).
     *
     * It walks no DART.  Virtualization.framework backs nothing at the
     * t8110 DART's address unless the guest has about 7.5 GiB of memory or
     * more; below that the read aborts and the copy panics mid-restore, and
     * above it the read is guest RAM.
     */
    [SEP_BOOT_PLATFORM_VRESEARCH1] = {
        .runtime_entry = 0,
        .reported_destination = 0,
        .runtime_region_length = 0,
        .handoff_in_power_down_register = 0,
        .cold_input_offset = 0,
        .rom_builds_translation_tables = 0,
        .walks_dart = 0,
    },
    /*
     * What the d16 SEPOS needs from the t8110 SEPROM, measured on the
     * iphone-cli QEMU machine against 21A329 and checked against 23A341.
     *
     * Entry: after the fork it reads `mrs x0, s3_7_c15_c2_0`.  Zero takes
     * the resume stub (0x4694 in 21A329), bit zero clear its sleep routine,
     * and bit zero set `eor x0, x0, #1` -- d2400000, a logical-immediate
     * EOR easily misread as a movz -- then the cold stub at 0x4660.  With
     * the register reading zero it resumes a suspend that never happened,
     * whatever is in x0.
     *
     * Address: it recomputes its base from its own PC and runs only at
     * 0x3_4000_0000, where the emulator aliases the region.  Told the
     * AP-side address instead, it built page tables whose descriptors all
     * pointed back into 0x3_4000_0000 (forty-two of them, none at the
     * image) and parked on a vector fetch when its MMU came on.
     *
     * runtime_destination: read back through image+0x1D068 as a number.
     * `sep64.c:209` halts unless `value >> 35` is non-zero, and the handoff
     * copy wants `value - (0x2_5D24_0318 << 10) + [+0x1070]` between 2 and
     * 0x4000_0002, so the field reports 0x9_0000_0000 rather than the entry.
     *
     * runtime_region_length: halved to size region one, out of which the
     * root task carves every app's data; `eispAppl_d6x` alone asks for
     * 0x344_8000, more than the region's own 0x200_0000.
     *
     * It turns its MMU on with tables the ROM builds (write_runtime_tables),
     * and its cold path copies sixteen kilobytes from x1 before it builds
     * anything.
     */
    [SEP_BOOT_PLATFORM_T8110] = {
        .runtime_entry = SEP_BOOT_RUNTIME_ENTRY,
        .reported_destination = 0x900000000ULL,
        .runtime_region_length = 0x8000000,
        .handoff_in_power_down_register = 1,
        .cold_input_offset = 0x680000ULL,
        .rom_builds_translation_tables = 1,
        .walks_dart = 1,
    },
};

_Static_assert(
    sizeof(boot_platforms) / sizeof(boot_platforms[0]) ==
        SEP_BOOT_PLATFORM_COUNT,
    "every sep_boot_platform_id_t needs a row"
);

#ifdef OPEN_SECURE_ROM_HOST_TEST
static sep_boot_platform_id_t selected_platform = OPEN_SECURE_ROM_PLATFORM;

void open_secure_rom_boot_platform_test_select(sep_boot_platform_id_t id) {
    selected_platform = id;
}
#else
static const sep_boot_platform_id_t selected_platform =
    OPEN_SECURE_ROM_PLATFORM;
#endif

const sep_boot_platform_t *sep_boot_platform(void) {
    return &boot_platforms[selected_platform];
}
