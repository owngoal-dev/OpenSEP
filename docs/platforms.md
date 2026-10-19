# Platforms

OpenSEP stands in for a SEP boot ROM, and the SEP firmware it hands over to
was built against one particular ROM. Most of what OpenSEP does is the same
for every platform: reset, the mailbox protocol, image decoding and trust
evaluation, loading the payload into the runtime region. Where platforms
differ is the handoff, meaning how the firmware is entered and what it is
handed.

## Choosing a platform

```sh
make                    # PLATFORM=vresearch1, the default
make PLATFORM=t8110
```

The choice is made at build time. It ends up as one row of the table in
`src/runtime/boot/boot_platform.c`, and only that file depends on it.
The firmware's code cannot be used to detect the platform: the vphone600
SEPOS and the iPhone14,4 SEPOS of the same release start with the same
instructions and only diverge at the fork after them. The image manifest's
CHIP and BORD properties could tell them apart, but OpenSEP does not use
them for this yet (see the end of this page).

## The table

| | `vresearch1` | `t8110` |
|---|---|---|
| Stands in for | `AVPSEPBooter.vresearch1.bin`, the SEP ROM of Apple's Virtualization.framework | the t8110 SEPROM, as the iphone-cli QEMU machine models it for d16 (iPhone14,4) |
| Firmware | `sep-firmware.vphone600.RELEASE.im4p` (unencrypted) | `sep-firmware.d16.RELEASE.im4p` (encrypted; key table in `src/platform/platform.c`) |
| Entry | region one's base + the mode's offset (`0x0600_0000`) | `0x3_4000_0000`, aliased by the emulator onto the region |
| Handoff pointer | `x0` | `s3_7_c15_c2_0`, with bit zero as the cold-boot flag (x0 carries the same value) |
| `x1` | `0` | entry + `0x68_0000` |
| `runtime_destination` (+0x18) | the entry | `0x9_0000_0000` |
| `runtime_region_length` (+0x20) | region one's length, capped at `0x18000` | `0x800_0000` |
| Translation tables | built by the firmware | built by the ROM (`write_runtime_tables`) |

### Evidence

- `vresearch1`: read out of `AVPSEPBooter.vresearch1.bin` from macOS 26
  (sha256 `86d008eb…`, linked at `0x0200_0000`).
  - The entry is computed at `0x2001FD8` from the region table and the mode
    offsets at `0x2024300`.
  - The jump happens at `0x2001C44` → `0x2001AC0`, with x0 the handoff,
    x1 zero and the PC the entry.
  - The `runtime_region_length` cap is applied in `0x2001D00`.
  - The firmware side was checked against the 23E5207q vphone600 SEPOS:
    `mov x19, x0` at `+0x4008`, `cbz x19` at `+0x404C`, and the magic read
    through x0 at `+0x6328`.
  - Booted on 2026-10-18 together with OpenAVB on four Virtualization.framework
    guests: iPhone17,3 on iOS 26.4 (23E246) and 27.0.1 (24A446), and iPad15,3
    on iPadOS 26.4 (23E246) and 27.0.1 (24A446). All four reached SpringBoard.
- `t8110`: measured on the iphone-cli QEMU machine against 21A329 and
  checked against the 23A341 d16 SEPOS.
  - The firmware reads `s3_7_c15_c2_0` and forks on bit zero.
  - It checks `runtime_destination` against its memory base.
  - It turns its MMU on with tables the ROM builds.
  - The comments next to the table row and in `boot_handoff.c` carry the
    details.
  - `tests/runtime/boot_handoff_tests.c` pins the values this platform
    produces.

## Adding a platform

1. Read the stock ROM's handoff: where it enters the firmware, which
   registers it sets, what it writes into the handoff, and whether it
   builds translation tables. Read the firmware's entry too: which register
   it takes the handoff from, and which handoff fields it reads before its
   MMU is on.
2. Append a `SEP_BOOT_PLATFORM_*` name, just before
   `SEP_BOOT_PLATFORM_COUNT`, in
   `include/open_secure_rom/runtime/boot/boot_platform.h`, and add a row to
   the table in `boot_platform.c`. The build fails if the last name has no
   row; a missing row in the middle is filled with zeroes and silently
   behaves like `vresearch1`, so add each row as you add its name. Give the
   row a comment that cites the addresses its values come from.
3. Map the name in the `Makefile` (`PLATFORM_ID_<name>`).
4. Add a case to `tests/runtime/boot_handoff_tests.c` for the header and the
   entry registers the row produces.
5. If the platform's mailbox, memory map or warm path differs, those are not
   in the table yet. Add a field rather than an `#if`, so that every platform
   keeps building from one source.

## Not yet covered by the table

- **Warm boot.** Only the `t8110` build checks the warm mark at
  `0x2_3D2E_4804` and re-enters the runtime at `0x3_4000_0000` when it reads
  `1`. The `vresearch1` build always starts cold: Virtualization.framework
  backs nothing at that IPA for a guest with less than about 7.5 GiB of
  memory, where the load aborts before the vector table is set and the
  framework stops the SEP core, and for a larger guest it is guest RAM.
- **Selection at run time.** The row is fixed when the ROM is built. Two ways
  to choose it at run time instead:
  - read the manifest's CHIP/BORD, which the ROM already evaluates;
  - have the host hand the row to the ROM, in the same way the boot ROM's
    patch store hands over boot patches.
  Either would let one image serve every platform. The second would also let
  a new chip boot before the ROM is changed.
