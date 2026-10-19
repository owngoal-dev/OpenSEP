# External contract

What the emulator (or whatever hosts the SEP core) has to provide. The
per-platform differences in the handoff are in [platforms.md](platforms.md).

## Image and memory

- **ROM**: load `build/OpenSEP.bin` (a flat binary) at IPA `0x02000000` and
  start the SEP core there, at EL1 (EL2 is accepted: the reset path drops to
  EL1 itself). The ROM maps `0x50000` bytes from that address executable, so
  the window behind it should be backed.
- **SEP RAM**: RAM at `0x04020000`-`0x04050000`, zero at cold boot. It holds
  initialised data at `0x04020008`, fixed runtime state up to `0x04024000`,
  the EL0/EL1 stacks and the boot translation tables at `0x04040000`.
- **Device block**: the ROM maps a 32 MiB device block at SEP VA `0x30000000`
  onto IPA `0`, and touches a platform register page at `+0x400000` and a
  configuration sink at `+0x600000`. An emulator may answer both inert
  (reads as zero).
- **Runtime window**: SEP VA `0x8_0000_0000` is mapped onto IPA `0x06000000`
  (125 blocks of 32 MiB) followed by IPA `0x1_0000_0000` (3 blocks). The
  SEPOS runtime is written through it and then entered with the MMU off.
  On `vresearch1` the entry is IPA `0x06000000` itself, with the handoff
  pointer in x0. On `t8110` it is IPA `0x3_4000_0000`, where the d16
  firmware expects to run (it checks its own PC), so the emulator must alias
  that IPA onto the same memory. See [docs/platforms.md](platforms.md).
- **Warm boot** (`t8110` only): on reset the ROM reads the word at
  `0x2_3D2E_4804`. If it is `1` (the t8110 runtime's deep-sleep path writes
  it), the ROM clears it and jumps straight back into the runtime at
  `0x3_4000_0000`. On a cold boot it must read as anything other than `1`.
  The `vresearch1` build does not read it.
- **DART**: addresses that arrive over the mailbox are dart-sep IOVAs. The ROM
  walks the DART itself (stream 0 at IPA `0x2_5D2C_0000`: TCR at `+0x1000`,
  TTBR at `+0x1400`, two levels of 11 index bits over 16 KiB pages). A
  bypassed or unprogrammed stream means the address is used as is.

## Mailbox

The mailbox is mapped at SEP VA `0x05000000` onto IPA `0x30250000`:

| offset | register |
|---|---|
| `0x10` | control (bit 0 set during activation) |
| `0x14` | raw status: bit 31 = active, bit 4 = RX event (write 1 to clear) |
| `0x18` | interrupt enable: bit 4 = RX not empty, bit 5 = TX empty |
| `0x1c` | RX status: bit 17 = empty |
| `0x20` | TX status: bit 17 = empty, bit 16 = full |
| `0x100` | RX slot, 16 bytes, read with one `ldp`; reading it pops the entry |
| `0x200` | TX slot, 16 bytes, written with one `stp` |

The ROM waits for events with `wfe`, so the emulator must raise an event (or
an interrupt) when the mailbox state changes.

Every message is the low 64-bit word of a slot (the high word is zero):

```
bits  0-7   frame tag, 0xff for protocol frames
bits  8-15  sequence (echoed in the response)
bits 16-23  opcode (request) / tag (response)
bits 24-31  argument
bits 32-63  payload
```

A request whose frame tag is not `0xff` is answered with its low byte alone.

Boot sequence:

1. The ROM activates the mailbox and sends a startup event,
   `0x00d200ff | 1 << 32` (tag `0xd2`, payload phase 1).
2. The host sets up the session. The response tag is the opcode plus `0x64`;
   an unknown opcode, or one sent in the wrong phase, is answered with tag
   `0xc7` and the opcode as payload.

   | opcode | request | response |
   |---|---|---|
   | `0x01` | query capability | payload 0 |
   | `0x02` | query phase | payload 1 before AKE-ready, 2 after |
   | `0x03` | initialise research seed | payload 160 |
   | `0x04` | read seed word (argument 0-4) | the word; argument > 4 gives `0xffffffff` |
   | `0x05` | publish AKE ready | tag `0x69`; ends phase 1 |
   | `0x06` | publish shared region (phase 2) | none; see below |
   | `0x10` | stream research pattern | next pattern word |
   | `0x11` | set runtime region size, payload in KiB (phase 1 only, must be non-zero) | payload 0 |
   | `0x21` | initialise research value64 | payload 64 |
   | `0x22` | read value64 word (argument 0-1) | the word |

3. After `0x05` the ROM sizes and zeroes the runtime regions using the `0x11`
   value, then sends a second startup event, `0x00d200ff | 2 << 32`.
4. The host sends opcode `0x06` with the SEP firmware's dart-sep IOVA shifted
   right by 12 in the payload (the IOVA must fit in 32 bits) and the image
   mode (0 or 1) in the argument. No response is sent; the ROM loads the
   image.

On a fatal error the ROM sends `0x00ff00ff`, the magic
`0xddeeaaddbbeeeeff` and then its panic record, one 64-bit word per slot,
before parking the core.

## SEP firmware image

The image the host publishes with opcode `0x06` is the personalised SEP
firmware (`RestoreSEP.img4`, i.e. `sep-firmware.<board>.RELEASE.im4p` with its
manifest), copied in 16 KiB pages from the IOVA. It must be one DER SEQUENCE
of at least 16 KiB that fits in the runtime region. The ROM decodes and
evaluates it (IMG4/IM4P/IM4M/X.509), decrypts the payload if it is
encrypted (the vphone600 firmware is not), places it in the runtime window
and hands over to it.

The ROM carries one trust anchor, the public Apple DDI Secure Boot Root CA -
G1 certificate (`src/image/img4/img4_chain.c`; the same certificate that is
embedded in the AVPSEPBooter image shipped with macOS), and evaluates the
manifest's certificate chain against it. Production iPhone SEP firmware
manifests chain to a different Apple root, so for them the evaluation fails.
The result is logged but is not fatal by default; build with
`-DOPEN_SECURE_ROM_ENFORCE_IMAGE_TRUST=1` to make it fatal.

There is no GID key, so the payload key cannot be unwrapped from the image's
keybag. Instead the ROM carries a table of publicly published per-build SEP
firmware keys for iPhone14,4 (`src/platform/platform.c`) and picks one by
trial decryption of the first payload block. Each row is the SEPFirmwareIV/
SEPFirmwareKey published on The Apple Wiki for that build (21A329, 19A348,
24A437, 20A362, 23A341, 22G100; the page for each is cited in the source).
Supporting a further build means adding its row there. A future version may
take the key from the host instead of carrying the table; see
[sep-firmware-keys.md](sep-firmware-keys.md).
