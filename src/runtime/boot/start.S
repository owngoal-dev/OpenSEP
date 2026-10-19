#include "open_secure_rom/runtime/boot/bootstrap.h"

.section .text.start,"ax",@progbits
.p2align 7
.global sep_reset_entry
.type sep_reset_entry,@function
sep_reset_entry:
    dsb sy
    isb
    mrs x8, cntpct_el0

    /*
     * Hypervisor.framework enables EL2 for the whole VM rather than per vCPU,
     * so this coprocessor can come out of reset there even though nothing on
     * it runs at EL2 and the check below refuses to start anywhere but EL1.
     * Drop down and carry on exactly as when the board withholds EL2.
     */
    mrs x9, currentel
    cmp x9, #8
    b.ne .Lbootstrap_el1
    mrs x9, hcr_el2
    orr x9, x9, #(1 << 31)      /* RW: EL1 is AArch64 */
    /*
     * With EL2 present, EL1 accesses to the pointer authentication keys trap
     * to EL2 unless APK and API allow them, and this ROM uses them.
     */
    orr x9, x9, #(3 << 40)      /* APK | API */
    msr hcr_el2, x9
    adr x9, .Lbootstrap_el1
    msr elr_el2, x9
    mov x9, #0x3c5              /* EL1h, DAIF masked */
    msr spsr_el2, x9
    isb
    eret
.Lbootstrap_el1:
    mov x10, #0
    movk x10, #0x0401, lsl #16
    mov x11, #0xa4
    str x11, [x10, #40]
    mrs x9, currentel
    cmp x9, #4
    b.ne .Lbootstrap_halt

    /*
     * Waking from the SEP's own power-down: straight back into the runtime.
     *
     * The runtime's deep-sleep path (image+0x4444, MMU off) saves what it
     * needs, writes 1 to 0x2_3D2E_4804 and stops in wfi with the core's power
     * about to go.  What comes up next is this ROM, and on that path the
     * runtime is already in RAM: its entry at image+0x0 reads
     * s3_7_c15_c2_0 (ACC_PWR_DN_SAVE) and, finding zero, takes its resume
     * stub at image+0x469C, which reinstates the TTBRs from the block at
     * image+0x20000 and returns into the thread that went down (measured on
     * the 22G100 SEP runtime).  A cold boot never has the word
     * set -- only that path writes it -- so nothing else here changes.
     *
     * Only t8110 has that path.  Virtualization.framework backs nothing at
     * this IPA on vresearch1 unless the guest has about 7.5 GiB of memory or
     * more: below that the load takes a synchronous abort before
     * VBAR_EL1 is set and the framework ends the SEP core, and above it the
     * word is guest RAM, which can read 1.
     */
#if defined(OPEN_SECURE_ROM_WARM_BOOT)
    ldr x10, =SEP_BOOT_WARM_MARK
    ldr w11, [x10]
    cmp w11, #1
    b.ne .Lbootstrap_cold
    str wzr, [x10]
    msr s3_7_c15_c2_0, xzr
    isb
    ldr x10, =SEP_BOOT_RUNTIME_ENTRY
    br x10
#endif
.Lbootstrap_cold:

    adr x10, sep_vector_table
    msr vbar_el1, x10
    mrs x0, sctlr_el1
    orr x0, x0, #SEP_BOOT_SCTLR_EARLY_BITS
    msr sctlr_el1, x0
    isb

    /* All translation tables are reset-owned physical RAM. */
    ldr x0, =SEP_BOOT_ROOT_TABLE
    ldr x1, =SEP_BOOT_TABLE_LIMIT
.Lzero_translation_tables:
    stp xzr, xzr, [x0], #16
    cmp x0, x1
    b.lo .Lzero_translation_tables

    /* The root covers ROM, runtime RAM plus the mailbox alias, and MMIO. */
    ldr x0, =SEP_BOOT_ROOT_TABLE
    ldr x1, =(SEP_BOOT_ROM_PAGE_TABLE | SEP_BOOT_TABLE_DESCRIPTOR_FLAGS)
    str x1, [x0, #(SEP_BOOT_ROOT_INDEX(SEP_BOOT_ROM_BASE) * 8)]
    ldr x1, =(SEP_BOOT_RAM_PAGE_TABLE | SEP_BOOT_TABLE_DESCRIPTOR_FLAGS)
    str x1, [x0, #(SEP_BOOT_ROOT_INDEX(SEP_BOOT_RUNTIME_RAM_BASE) * 8)]
    ldr x1, =SEP_BOOT_DEVICE_BLOCK_DESCRIPTOR
    str x1, [x0, #(SEP_BOOT_ROOT_INDEX(SEP_BOOT_PLATFORM_MMIO) * 8)]

    /* These reset-owned blocks make the shared IMG4 and runtime executable. */
    add x3, x0, #(SEP_BOOT_ROOT_INDEX(SEP_BOOT_LOW_APERTURE_VA) * 8)
    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_LOW_APERTURE_PA, SEP_BOOT_LOW_APERTURE_BLOCK_FLAGS)
    ldr x4, =SEP_BOOT_DEVICE_BLOCK_SIZE
    mov x2, #SEP_BOOT_LOW_APERTURE_BLOCK_COUNT
.Lmap_low_aperture_block:
    str x1, [x3], #8
    add x1, x1, x4
    subs x2, x2, #1
    b.ne .Lmap_low_aperture_block

    add x3, x0, #(SEP_BOOT_ROOT_INDEX(SEP_BOOT_RUNTIME_WINDOW_VA) * 8)
    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_RUNTIME_WINDOW_PA, SEP_BOOT_RUNTIME_WINDOW_BLOCK_FLAGS)
    mov x2, #SEP_BOOT_RUNTIME_WINDOW_BLOCK_COUNT
.Lmap_runtime_window_block:
    str x1, [x3], #8
    add x1, x1, x4
    subs x2, x2, #1
    b.ne .Lmap_runtime_window_block

    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_RUNTIME_WINDOW_EXTENSION_PA, SEP_BOOT_RUNTIME_WINDOW_BLOCK_FLAGS)
    mov x2, #SEP_BOOT_RUNTIME_WINDOW_EXTENSION_BLOCK_COUNT
.Lmap_runtime_extension_block:
    str x1, [x3], #8
    add x1, x1, x4
    subs x2, x2, #1
    b.ne .Lmap_runtime_extension_block

    /* The official entry maps twenty 16 KiB executable ROM pages. */
    ldr x0, =SEP_BOOT_ROM_PAGE_TABLE
    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_ROM_BASE, SEP_BOOT_ROM_PAGE_FLAGS)
    mov x2, #(SEP_BOOT_ROM_MAPPED_SIZE / SEP_BOOT_PAGE_SIZE)
.Lmap_rom_page:
    str x1, [x0], #8
    add x1, x1, #SEP_BOOT_PAGE_SIZE
    subs x2, x2, #1
    b.ne .Lmap_rom_page

    /* The official sparse layout retains guard holes and remaps both stacks. */
    ldr x0, =SEP_BOOT_RAM_PAGE_TABLE
    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_RUNTIME_RAM_BASE, SEP_BOOT_RAM_PAGE_FLAGS)
    str x1, [x0, #(SEP_BOOT_PAGE_INDEX(SEP_BOOT_RUNTIME_RAM_BASE) * 8)]
    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_EL0_STACK_PHYSICAL_BOTTOM, SEP_BOOT_RAM_PAGE_FLAGS)
    str x1, [x0, #(SEP_BOOT_PAGE_INDEX(SEP_BOOT_EL0_STACK_BOTTOM) * 8)]
    add x1, x1, #SEP_BOOT_PAGE_SIZE
    str x1, [x0, #((SEP_BOOT_PAGE_INDEX(SEP_BOOT_EL0_STACK_BOTTOM) + 1) * 8)]
    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_EL1_STACK_PHYSICAL_BOTTOM, SEP_BOOT_RAM_PAGE_FLAGS)
    str x1, [x0, #(SEP_BOOT_PAGE_INDEX(SEP_BOOT_EL1_STACK_BOTTOM) * 8)]

    /* Three runtime extension pages are shifted into the next VA window. */
    add x3, x0, #(SEP_BOOT_PAGE_INDEX(SEP_BOOT_RUNTIME_EXTENSION_VA) * 8)
    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_RUNTIME_EXTENSION_PA, SEP_BOOT_RAM_PAGE_FLAGS)
    mov x2, #3
.Lmap_runtime_extension_page:
    str x1, [x3], #8
    add x1, x1, #SEP_BOOT_PAGE_SIZE
    subs x2, x2, #1
    b.ne .Lmap_runtime_extension_page

    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_RUNTIME_TAIL_PA, SEP_BOOT_RAM_PAGE_FLAGS)
    str x1, [x0, #(SEP_BOOT_PAGE_INDEX(SEP_BOOT_RUNTIME_TAIL_VA) * 8)]

    /* Runtime mapping helpers address the four reset-owned tables via aliases. */
    add x3, x0, #(SEP_BOOT_PAGE_INDEX(SEP_BOOT_TABLE_ALIAS_VA) * 8)
    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_ROOT_TABLE, SEP_BOOT_RAM_PAGE_FLAGS)
    mov x2, #4
.Lmap_translation_table_page:
    str x1, [x3], #8
    add x1, x1, #SEP_BOOT_PAGE_SIZE
    subs x2, x2, #1
    b.ne .Lmap_translation_table_page

    /* The recovered mailbox VA aliases the physical coprocessor aperture. */
    ldr x1, =SEP_BOOT_PAGE_DESCRIPTOR(SEP_BOOT_MAILBOX_PA, SEP_BOOT_DEVICE_PAGE_FLAGS)
    str x1, [x0, #(SEP_BOOT_PAGE_INDEX(SEP_BOOT_MAILBOX_VA) * 8)]

    ldr x0, =SEP_BOOT_MAIR_EL1
    msr mair_el1, x0
    ldr x0, =SEP_BOOT_TCR_EL1
    msr tcr_el1, x0
    ldr x0, =SEP_BOOT_ROOT_TABLE
    msr ttbr0_el1, x0
    isb

    msr daifclr, #4
    isb
    mrs x0, sctlr_el1
    mov x1, #SEP_BOOT_SCTLR_ENABLE_BITS
    orr x0, x0, x1
    msr sctlr_el1, x0
    dsb sy
    isb

    /* The runtime entry initializes SP_EL1 first, then selects SP_EL0. */
    msr spsel, #1
    mov x0, x8
    b sep_runtime_set_stacks_and_enter

.Lbootstrap_halt:
    wfi
    wfe
    b .Lbootstrap_halt
.size sep_reset_entry, .-sep_reset_entry

.ltorg

.p2align 11
.global sep_vector_table
sep_vector_table:
.rept 16
    b sep_fatal_wait_loop
    .rept 31
        nop
    .endr
.endr
