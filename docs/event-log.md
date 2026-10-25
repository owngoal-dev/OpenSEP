# Reading OpenSEP events

OpenSEP records events in memory. It does not write text to a UART or add
diagnostic mailbox messages. The four boot milestones use the existing ring;
image-import, policy and handoff events retain their existing values.
(`src:src/runtime/event_log.c:6`, `src:src/runtime/main.c`,
`src:src/protocol/mailbox.c:173`, `src:src/runtime/runtime_transfer.c:63`.)

The offline decoder prints short, aligned lines and preserves unknown codes:

```sh
python3 tools/decode_event_log.py --words 0x10001 0x10002 0x10004 0x10006 0x20003
```

```text
OpenSEP | 5 retained events | oldest -> newest
[OpenSEP] 01 INFO  boot    0x00010001  Platform initialization complete
[OpenSEP] 02 INFO  mailbox 0x00010002  Ready for AP requests
[OpenSEP] 03 INFO  mailbox 0x00010004  AKE-ready phase accepted
[OpenSEP] 04 INFO  memory  0x00010006  Runtime regions prepared
[OpenSEP] 05 INFO  image   0x00020003  Importing shared image
```

## Capturing a paused guest

The ring consists of sixteen little-endian 32-bit words, with
`event_log_next` and `event_log_total` as separate 32-bit counters. Its
symbols are local to `src/runtime/event_log.c`; their addresses depend on
the linked ROM. Locate them in the matching debug database rather than
assuming an address. Capture the ring and both counters while the SEP is
paused, then pass the counters as numbers:

```sh
python3 tools/decode_event_log.py --file events.bin --format ring --next 4 --total 20
```

`--format snapshot` instead accepts the 64-byte chronological output of
`sep_protocol_event_log_snapshot`, including trailing zero slots before
the ring has filled. Both forms retain only the most recent sixteen events;
an absent early milestone may have been overwritten. (`src:src/runtime/event_log.c:11`,
`src:src/runtime/event_log.c:64`.)

## Panic records

A regular panic record has size `0x1a0` and embeds the event snapshot at byte
40. An exception record has the same size and embeds it at byte 104. Both
travel through `sep_panic_mailbox_send_record`; pass only the record body,
without its two framing words. Select the layout explicitly:

```sh
python3 tools/decode_event_log.py --file panic.bin --format panic
python3 tools/decode_event_log.py --file exception.bin --format exception
```

The decoder reads only event codes, never the diagnostic payload, firmware
keys, tickets, identity or backtrace. The separate eight-word
`diagnostic_record` is unchanged and is not accepted as decoder input.
(`src:src/runtime/exception/panic.c:47`,
`src:src/runtime/exception/panic.c:129`,
`src:include/open_secure_rom/runtime/system_closure.h:30`,
`src:src/runtime/event_log.c:9`.)

The legacy `0x00020006` value is shared by the image-load completion and
the key-selection marker; the decoder deliberately does not infer which
site produced it. New milestones use previously unused values and do not
renumber these records. (`src:src/runtime/runtime_transfer.c:69`,
`src:src/image/shared/shared_image_payload_keybag.c:121`,
`src:include/open_secure_rom/runtime/event_log.h:6`.)
