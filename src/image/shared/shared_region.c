#include "open_secure_rom/system_closure.h"

#include "open_secure_rom/boot_platform.h"

#include "open_secure_rom/der.h"
#include "open_secure_rom/event_log.h"
#include "open_secure_rom/panic.h"
#include "open_secure_rom/runtime.h"
#include "open_secure_rom/runtime_storage.h"

static void __attribute__((noreturn)) panic_copy_status(
    open_secure_rom_shared_region_status_t status
) {
    sep_u32 reason;
    if (status == OPEN_SECURE_ROM_SHARED_REGION_PAGE_LENGTH) {
        reason = 243;
    } else if (status == OPEN_SECURE_ROM_SHARED_REGION_SOURCE_ALIGNMENT) {
        sep_protocol_event_log_append(0x80020008);
        reason = 248;
    } else {
        reason = 256;
    }
    sep_panic_with_reason_and_pc(
        reason,
        (sep_u64)(uintptr_t)__builtin_return_address(0)
    );
}

/*
 * Where an address the main processor hands over actually is.
 *
 * /arm-io/sep names dart-sep as its `iommu-parent`, so the addresses that
 * arrive over the mailbox -- the firmware's, as `address >> 12` in the boot
 * frame -- are that DART's, not machine addresses.  Measured on the boot this
 * was written for: the kernel programs stream zero with a table at
 * 0x8_38FA_4000, and 0x4000 comes out at 0x8_0DCB_0000, whose first bytes are
 * `30 83 66 a1` -- the SEPOS image's DER SEQUENCE.  Read raw, 0x4000 is low
 * main memory, there is no SEQUENCE there, and this file panics with 350.
 *
 * On silicon the DART sits on the bus and the SEP never sees it.  Here the SEP
 * core shares the main processor's physical space -- one stage-2 map for the
 * whole machine -- so there is nowhere to put a translator between them and
 * the walk has to happen on this side.  It is a property of the machine, not
 * of the part being modelled, which is why it lives in one function and says
 * so.
 *
 * The walk is two levels of eleven index bits over sixteen kilobyte pages,
 * which is also this file's page size, so one translation covers one copy.
 */
enum {
    SEP_DART_BASE = 0x25d2c0000ULL,
    SEP_DART_STREAM = 0,
    SEP_DART_TCR = 0x1000,
    SEP_DART_TTBR = 0x1400,
    SEP_DART_TCR_BYPASS = 1u << 1,
    SEP_DART_TTBR_VALID = 1u << 0,
    SEP_DART_PAGE_BITS = 14,
    SEP_DART_INDEX_BITS = 11,
    /*
     * The page beside the one the copy itself uses, so a walk and a copy never
     * want the same slot.
     */
    SEP_DART_WINDOW = 0x02104000ULL,
    /* MAIR attribute 0 is Device-nGnRnE, attribute 1 is normal write-back. */
    SEP_ATTR_DEVICE = 0,
    SEP_ATTR_NORMAL = 1,
};

static sep_u32 sep_peek32(sep_u64 physical_address, sep_u32 attribute) {
    const sep_u64 page = physical_address & ~0x3fffULL;
    const sep_size_t offset = (sep_size_t)(physical_address & 0x3fff);
    sep_u32 value;

    sep_runtime_map_page(SEP_DART_WINDOW, page, 2, 1, attribute, 0);
    if (attribute != SEP_ATTR_DEVICE) {
        sep_dcache_clean_invalidate_range(
            (const void *)(uintptr_t)SEP_DART_WINDOW,
            OPEN_SECURE_ROM_SHARED_PAGE_SIZE
        );
    }
    value = *(const volatile sep_u32 *)(uintptr_t)(SEP_DART_WINDOW + offset);
    sep_runtime_unmap_page(SEP_DART_WINDOW);
    return value;
}

static sep_u64 sep_peek64(sep_u64 physical_address, sep_u32 attribute) {
    const sep_u64 low = sep_peek32(physical_address, attribute);
    const sep_u64 high = sep_peek32(physical_address + 4, attribute);

    return low | (high << 32);
}

sep_u64 sep_shared_region_translate(sep_u64 iova) {
    sep_u32 tcr;
    sep_u32 ttbr;
    sep_u64 table;
    sep_u64 entry = 0;
    int level;

    if (!sep_boot_platform()->walks_dart) {
        return iova;
    }
    tcr = sep_peek32(SEP_DART_BASE + SEP_DART_TCR + SEP_DART_STREAM * 4,
                     SEP_ATTR_DEVICE);
    ttbr = sep_peek32(SEP_DART_BASE + SEP_DART_TTBR + SEP_DART_STREAM * 4,
                      SEP_ATTR_DEVICE);
    if ((tcr & SEP_DART_TCR_BYPASS) != 0) {
        return iova;
    }
    if ((ttbr & SEP_DART_TTBR_VALID) == 0) {
        /*
         * Nothing programmed.  Everything before the kernel comes up is
         * addressed directly, so that is what an unprogrammed stream means
         * here rather than an error.
         */
        return iova;
    }
    table = (sep_u64)(ttbr >> 2) << SEP_DART_PAGE_BITS;
    for (level = 1; level >= 0; level--) {
        const sep_u32 index =
            (sep_u32)((iova >> (SEP_DART_PAGE_BITS +
                                level * SEP_DART_INDEX_BITS)) &
                      ((1u << SEP_DART_INDEX_BITS) - 1));

        entry = sep_peek64(table + (sep_u64)index * 8, SEP_ATTR_NORMAL);
        if ((entry & 1) == 0) {
            sep_protocol_event_log_append(0x80020009);
            sep_panic_with_reason_and_pc(
                341,
                (sep_u64)(uintptr_t)__builtin_return_address(0)
            );
        }
        if (level > 0) {
            table = (entry & 0xfffffffff0ULL) << 4;
        }
    }
    return ((entry & 0xfffffffff0ULL) << 4) |
        (iova & ((1ULL << SEP_DART_PAGE_BITS) - 1));
}

void sep_shared_region_copy_page(
    sep_u64 source_address,
    void *destination,
    sep_size_t length
) {
    sep_u64 physical_address;

    if (length > OPEN_SECURE_ROM_SHARED_PAGE_SIZE) {
        panic_copy_status(OPEN_SECURE_ROM_SHARED_REGION_PAGE_LENGTH);
    }
    if ((source_address & (OPEN_SECURE_ROM_SHARED_PAGE_SIZE - 1)) != 0) {
        panic_copy_status(OPEN_SECURE_ROM_SHARED_REGION_SOURCE_ALIGNMENT);
    }
    /*
     * The address that arrives is one of dart-sep's and those are thirty-two
     * bits wide; what it translates to is not, because main memory begins at
     * 0x8_0000_0000.  So the width check belongs before the walk and not
     * after it.
     */
    if ((source_address >> 32) != 0) {
        panic_copy_status(OPEN_SECURE_ROM_SHARED_REGION_SOURCE_RANGE);
    }
    physical_address = sep_shared_region_translate(source_address);
    sep_runtime_map_page(0x02100000, physical_address, 2, 1, 1, 0);
    sep_dcache_clean_invalidate_range(
        (const void *)(uintptr_t)0x02100000,
        OPEN_SECURE_ROM_SHARED_PAGE_SIZE
    );
    sep_memmove(
        destination,
        (const void *)(uintptr_t)0x02100000,
        length
    );
#ifndef OPEN_SECURE_ROM_HOST_TEST
    __asm__ volatile("dsb sy" ::: "memory");
#endif
    sep_runtime_unmap_page(0x02100000);
}

void sep_shared_region_load_image(
    sep_u64 shared_region_base,
    void *destination,
    sep_size_t capacity
) {
    if ((capacity & (OPEN_SECURE_ROM_SHARED_PAGE_SIZE - 1)) != 0) {
        sep_panic_with_reason_and_pc(
            337,
            (sep_u64)(uintptr_t)__builtin_return_address(0)
        );
    }
    if (capacity >= 0x20000001ULL) {
        sep_panic_with_reason_and_pc(
            338,
            (sep_u64)(uintptr_t)__builtin_return_address(0)
        );
    }

    sep_shared_region_copy_page(
        shared_region_base,
        destination,
        OPEN_SECURE_ROM_SHARED_PAGE_SIZE
    );
    sep_size_t encoded_length = 0;
    if (!sep_der_sequence_encoded_length(
            destination,
            OPEN_SECURE_ROM_SHARED_PAGE_SIZE,
            &encoded_length
        )) {
        sep_protocol_event_log_append(0x80020002);
        sep_panic_with_reason_and_pc(
            350,
            (sep_u64)(uintptr_t)__builtin_return_address(0)
        );
    }
    if (encoded_length > capacity) {
        sep_protocol_event_log_append(0x80020001);
        sep_panic_with_reason_and_pc(
            356,
            (sep_u64)(uintptr_t)__builtin_return_address(0)
        );
    }
    if (encoded_length < OPEN_SECURE_ROM_SHARED_PAGE_SIZE) {
        sep_panic_with_reason_and_pc(
            359,
            (sep_u64)(uintptr_t)__builtin_return_address(0)
        );
    }

    for (sep_size_t offset = OPEN_SECURE_ROM_SHARED_PAGE_SIZE;
         offset < encoded_length;
         offset += OPEN_SECURE_ROM_SHARED_PAGE_SIZE) {
        if (shared_region_base > UINT64_MAX - offset) {
            sep_panic_reason_1c();
        }
        if ((uintptr_t)destination > UINTPTR_MAX - offset) {
            sep_panic_reason_2f();
        }
        const sep_size_t remaining = encoded_length - offset;
        const sep_size_t chunk =
            remaining < OPEN_SECURE_ROM_SHARED_PAGE_SIZE
                ? remaining
                : OPEN_SECURE_ROM_SHARED_PAGE_SIZE;
        sep_shared_region_copy_page(
            shared_region_base + offset,
            (sep_u8 *)destination + offset,
            chunk
        );
    }
    sep_memzero(
        (sep_u8 *)destination + encoded_length,
        capacity - encoded_length
    );
}
