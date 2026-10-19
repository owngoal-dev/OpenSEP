LLVM_PREFIX ?= /opt/homebrew/opt/llvm/bin
CC := $(LLVM_PREFIX)/clang
HOST_CC ?= clang
AR := $(LLVM_PREFIX)/llvm-ar
LD := /opt/homebrew/bin/ld.lld
OBJCOPY := $(LLVM_PREFIX)/llvm-objcopy
READOBJ := $(LLVM_PREFIX)/llvm-readobj
UV := uv run --no-project

BUILD := build
INCLUDES := -I include

# The SEP ROM this one stands in for: vresearch1 (Virtualization.framework's
# AVPSEPBooter) or t8110 (the iphone-cli QEMU machine's d16).  See
# docs/platforms.md.
PLATFORM ?= vresearch1
PLATFORM_ID_vresearch1 := SEP_BOOT_PLATFORM_VRESEARCH1
PLATFORM_ID_t8110 := SEP_BOOT_PLATFORM_T8110
PLATFORM_ID := $(or $(PLATFORM_ID_$(PLATFORM)),\
	$(error Unknown PLATFORM '$(PLATFORM)': use one of \
	$(patsubst PLATFORM_ID_%,%,$(filter PLATFORM_ID_%,$(.VARIABLES)))))
CFLAGS := --target=aarch64-none-elf -std=c17 -O3 -ffast-math -g \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions \
	-mgeneral-regs-only -mno-outline-atomics -mstrict-align \
	-mbranch-protection=bti -fno-pic -fno-pie -fno-common \
	-fno-vectorize -fno-slp-vectorize -Wall -Wextra -Werror $(INCLUDES) \
	-DOPEN_SECURE_ROM_PLATFORM=$(PLATFORM_ID)
ASFLAGS := --target=aarch64-none-elf -g -c $(INCLUDES) \
	$(if $(filter t8110,$(PLATFORM)),-DOPEN_SECURE_ROM_WARM_BOOT)
LDFLAGS := -T linker.ld --no-undefined --build-id=none \
	--fatal-warnings -nostdlib -static

rwildcard = $(foreach entry,$(wildcard $1*),\
	$(call rwildcard,$(entry)/,$2) $(filter $(subst *,%,$2),$(entry)))

C_SOURCES := $(sort $(call rwildcard,src/,*.c))
ASM_SOURCES := $(sort $(call rwildcard,src/,*.S))
TEST_SOURCES := $(sort $(call rwildcard,tests/,*.c))
C_OBJECTS := $(patsubst src/%.c,$(BUILD)/obj/c/%.o,$(C_SOURCES))
ASM_OBJECTS := $(patsubst src/%.S,$(BUILD)/obj/asm/%.o,$(ASM_SOURCES))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)
resolve_unique = $(if $(filter 1,$(words $(1))),$(1),\
	$(error Expected one $(2), found $(words $(1)): $(1)))
test_source = $(call resolve_unique,$(filter %/$(1),$(TEST_SOURCES)),test $(1))
ELF := $(BUILD)/OpenSEP.elf
ROM := $(BUILD)/OpenSEP.bin
HOST_TEST_CFLAGS := -std=c17 -O3 -ffast-math -Wall -Wextra -Werror \
	-DOPEN_SECURE_ROM_HOST_TEST -ffunction-sections -fdata-sections -I include \
	-DOPEN_SECURE_ROM_PLATFORM=$(PLATFORM_ID)
HOST_TEST_NAMES := $(sort $(patsubst %_tests.c,%,$(notdir $(TEST_SOURCES))))
HOST_TESTS := $(addprefix $(BUILD)/,$(addsuffix _tests,$(HOST_TEST_NAMES)))
HOST_OBJECTS := $(patsubst src/%.c,$(BUILD)/obj/host/%.o,$(C_SOURCES))
HOST_LIBRARY := $(BUILD)/libOpenSEPHost.a

.PHONY: all verify clean

all: $(ROM)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/obj/c/%.o: src/%.c | $(BUILD)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@

# Only boot_platform.c depends on PLATFORM.  The stamp is brought up to date
# while the Makefile is read, so it is current before any target is judged.
$(shell mkdir -p $(BUILD) && { echo '$(PLATFORM)' | cmp -s - $(BUILD)/platform || \
	echo '$(PLATFORM)' > $(BUILD)/platform; })
$(BUILD)/obj/c/runtime/boot/boot_platform.o: $(BUILD)/platform
$(BUILD)/obj/host/runtime/boot/boot_platform.o: $(BUILD)/platform

$(BUILD)/obj/asm/%.o: src/%.S | $(BUILD)
	mkdir -p $(@D)
	$(CC) $(ASFLAGS) $< -o $@

$(BUILD)/obj/host/%.o: src/%.c | $(BUILD)
	mkdir -p $(@D)
	$(HOST_CC) $(HOST_TEST_CFLAGS) -c $< -o $@

$(ELF): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(ROM): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(HOST_LIBRARY): $(HOST_OBJECTS)
	$(RM) $@
	$(AR) rcs $@ $^

define HOST_TEST_template
$(BUILD)/$(1)_tests: $(call test_source,$(1)_tests.c) $(HOST_LIBRARY) | $(BUILD)
	$(HOST_CC) $(HOST_TEST_CFLAGS) $$< $(HOST_LIBRARY) -Wl,-dead_strip -o $$@
endef

$(foreach test,$(HOST_TEST_NAMES),$(eval $(call HOST_TEST_template,$(test))))

verify: $(ROM) $(HOST_TESTS)
	@set -e; for test in $(HOST_TESTS); do $$test; done
	$(UV) tools/verify_coverage.py
	@test $$(wc -c < $(ROM)) -le $$((0x29000))
	@test -z "$$($(READOBJ) --relocations $(ELF) | rg 'R_AARCH64_' || true)"

clean:
	rm -rf $(BUILD)
