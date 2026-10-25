"""Render an OpenSEP event snapshot or panic record without printing payloads."""

import argparse
import struct
from pathlib import Path


# src:src/runtime/main.c and src/protocol/mailbox.c; values declared in
# include/open_secure_rom/runtime/event_log.h.
EVENTS = {
    0x00010001: ("INFO", "boot", "Platform initialization complete"),
    0x00010002: ("INFO", "mailbox", "Ready for AP requests"),
    0x00010004: ("INFO", "mailbox", "AKE-ready phase accepted"),
    0x00010006: ("INFO", "memory", "Runtime regions prepared"),
    # src:src/runtime/runtime_transfer.c.
    0x00010003: ("INFO", "runtime", "Provider context present"),
    0x00010005: ("INFO", "runtime", "Mode configuration complete"),
    0x00010008: ("INFO", "handoff", "Image ready; boot acknowledgement follows"),
    0x00010009: ("INFO", "handoff", "Runtime mapping prepared"),
    0x00020003: ("INFO", "image", "Importing shared image"),
    0x00020004: ("INFO", "image", "Root-key canaries valid"),
    0x00020005: ("INFO", "image", "Root-key canaries unavailable; image marked unencrypted"),
    0x00020006: ("INFO", "image", "Image-load or key-selection checkpoint"),
    0x00020007: ("INFO", "policy", "Research policy accepted"),
    0x80050021: ("ERROR", "image", "Payload keybag unavailable"),
    0x80030005: ("ERROR", "image", "Payload decryption failed"),
    0x80050006: ("ERROR", "policy", "Encrypted image lacks required research policy"),
    0x80050013: ("ERROR", "runtime", "Runtime initialization status rejected"),
    0x80040012: ("ERROR", "memory", "Runtime region size is zero"),
    # src:src/image/shared/shared_region.c.
    0x80020002: ("ERROR", "image", "Invalid shared-image DER length"),
    0x80020001: ("ERROR", "image", "Shared image exceeds region capacity"),
    # src:src/runtime/architecture.c.
    0x000A0001: ("INFO", "timer", "Delay entered"),
    0x000A0002: ("INFO", "timer", "Delay complete"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--file", type=Path, help="Binary data captured from the guest")
    source.add_argument("--words", nargs="+", type=lambda text: int(text, 0), help="Chronological event words, in decimal or 0x-prefixed hex")
    parser.add_argument("--format", choices=("snapshot", "ring", "panic", "exception"), default="snapshot")
    parser.add_argument("--next", dest="next_slot", type=int, help="Raw ring's event_log_next value")
    parser.add_argument("--total", type=int, help="Raw ring's event_log_total value")
    args = parser.parse_args()

    if args.words is not None:
        if args.format != "snapshot" or args.next_slot is not None or args.total is not None:
            parser.error("--words accepts chronological snapshot words only")
        if len(args.words) > 16 or any(word < 0 or word > 0xFFFFFFFF for word in args.words):
            parser.error("expected at most 16 unsigned 32-bit words")
        words = args.words
    else:
        try:
            data = args.file.read_bytes()
        except OSError as error:
            parser.error(str(error))
        expected_size = 416 if args.format in ("panic", "exception") else 64
        if len(data) != expected_size:
            parser.error(f"{args.format} requires exactly {expected_size} bytes; got {len(data)}")
        offset = {"snapshot": 0, "ring": 0, "panic": 40, "exception": 104}[args.format]
        words = list(struct.unpack_from("<16I", data, offset))
        if args.format == "ring":
            if args.next_slot is None or not 0 <= args.next_slot < 16 or args.total is None or not 0 <= args.total <= 0xFFFFFFFF:
                parser.error("ring requires --next in 0..15 and --total in 0..4294967295")
            if args.next_slot != args.total % 16:
                parser.error("ring counters disagree; capture them while the guest is paused")
            count = min(args.total, 16)
            start = args.next_slot if args.total >= 16 else 0
            words = [words[(start + index) % 16] for index in range(count)]
        elif args.next_slot is not None or args.total is not None:
            parser.error("--next and --total apply only to raw ring data")

    events = [word for word in words if word != 0]
    print(f"OpenSEP | {len(events)} retained events | oldest -> newest")
    for index, code in enumerate(events, 1):
        level, stage, message = EVENTS.get(code, ("EVENT", "unknown", "Unrecognized event; code preserved"))
        print(f"[OpenSEP] {index:02d} {level:5} {stage:7} 0x{code:08x}  {message}")


if __name__ == "__main__":
    main()
