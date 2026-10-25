# OpenSEP

An open-source SEP boot ROM for Apple's virtual iPhone and iPad, compatible
with AVPSEPBooter. Boots the SEP firmware of iOS 15 through iOS 27 on
[vphone-cli and iphone-cli](#compatibility).

- Serves the SEP mailbox protocol, from startup through AKE
- Loads and evaluates the personalised SEP firmware (IMG4/DER/X.509), then
  hands over to it
- Plain C17 and AArch64 assembly, no Apple code inside

Use it in place of AVPSEPBooter as the SEP core's ROM: load
`build/OpenSEP.bin` at `0x02000000`.

> Not affiliated with Apple Inc. Apple and iPhone are trademarks of Apple Inc.

## Compatibility

| Host | Device | iOS 15 | iOS 16 | iOS 17 | iOS 18 | iOS 26 | iOS 27 |
| :-- | :-- | :-: | :-: | :-: | :-: | :-: | :-: |
| vphone-cli | iPhone17,3 | | | | | ✅ | ✅ |
| vphone-cli | iPad15,3 | | | | | ✅ | ✅ |
| iphone-cli | iPhone14,4 | ✅ | ✅ | ✅ | ✅ | ✅ | ❔ |

✅ boots &nbsp;·&nbsp; ❔ not verified &nbsp;·&nbsp; blank: not tested

vphone-cli runs on Virtualization.framework and uses the `vresearch1` build
(`make`): 26.4 (23E246) and 27.0.1 (24A446) reach SpringBoard on both
devices, with [OpenAVB](https://github.com/owngoal-dev/OpenAVB) as the AP
boot ROM. iphone-cli runs on QEMU (d16) and uses the `t8110` build
(`make PLATFORM=t8110`): 15.0.1 (19A348), 16.0 (20A362), 17.0 (21A329),
18.6.2 (22G100) and 26.0 (23A341) reach the activation page. The 27.0
(24A437) run on iphone-cli did not complete because of a test-harness
failure, so that combination is not yet verified.
[docs/platforms.md](docs/platforms.md) explains how the two builds differ.

## Build

The build runs on macOS with the Xcode Command Line Tools and:

```sh
brew install llvm lld uv ripgrep
```

```sh
make                   # build/OpenSEP.bin for vresearch1 (the default)
make PLATFORM=t8110    # the same ROM with the t8110 handoff
make verify            # ROM gates plus every host test under tests/
make clean
```

Set `LLVM_PREFIX=` or `HOST_CC=` to use a toolchain outside Homebrew's
default paths. `OPENSEP_SEPFW_IMAGE` and `OPENSEP_RESEARCH_DIR` enable two
optional checks in `make verify`; see [Internals](docs/internals.md).

## Outputs

| File | What it is |
| --- | --- |
| `build/OpenSEP.bin` | The ROM: a flat image linked at `0x02000000`, at most `0x29000` bytes, with no relocations. `build/OpenSEP.elf` keeps DWARF for debugging. |
| `build/<name>_tests` | Host test binaries, one per `tests/**/<name>_tests.c`, built from the same sources. |

## Documentation

- [External contract](docs/external-contract.md): the memory map, mailbox
  protocol and SEP firmware image a host must agree on.
- [Platforms](docs/platforms.md): the per-platform handoff table, its
  evidence, and how to add a platform.
- [SEP firmware keys](docs/sep-firmware-keys.md): where the per-build keys
  come from and the planned move to host-supplied keys.
- [Internals](docs/internals.md): the toolchain, the checks in
  `make verify` and the source layout.
- [Event log](docs/event-log.md): boot milestones and offline decoding of
  a paused guest's ring or panic record.
- [Open items](docs/open-items.md): known, deferred work.

## Acknowledgements

Thanks to [@lbr77](https://github.com/lbr77) for the initial work this
project builds on.

## License

OpenSEP is released under the MIT License; see [`LICENSE`](LICENSE).

The cryptographic primitives are implemented from the public
specifications: FIPS 197 (AES), FIPS 180-4 (SHA-1/SHA-2), NIST SP 800-38A/B/C
(ECB/CBC, CMAC, CCM), RFC 2104 / FIPS 198-1 (HMAC), NIST SP 800-108 (KDF in
counter mode), FIPS 186-4, SEC 1 and NIST SP 800-56A (P-384, ECDH), and
PKCS #1 v2.2 / RFC 8017 (RSA), whose constants they reproduce. For
compatibility, the internal API and ABI -- function names, signatures and
structure layouts -- follow the routines used by the SEP ROM interface that
OpenSEP replaces, and a few constants (such as fault-detection canaries) are
values of that interface.

The IMG4 DER test fixtures in `tests/image/img4/img4_der_tests.c` are built
to the public IMG4 schema as documented by PongoOS `lib/img4`
(checkra1n/PongoOS, MIT); no PongoOS code is included.

Two pieces of Apple data are embedded: the per-build SEP firmware keys in
`src/platform/platform.c`, which are publicly published (see
[SEP firmware keys](docs/sep-firmware-keys.md)), and the public Apple DDI
Secure Boot Root CA - G1 certificate in `src/image/img4/img4_chain.c`. Both
are data, not code.
