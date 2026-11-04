# SPDX-License-Identifier: MIT
#
# Top-level build for the MT7925 macOS driver.
#
#   make check-headers   front-end compile of the vendored mt76 sources against
#                        the LinuxKPI headers; works on Linux and macOS hosts
#                        with clang and needs no SDK.  This is the CI gate for
#                        the compatibility layer.
#   make host-tests      build and run the userland unit tests in tests/host.
#   make kext            build MT7925.kext (macOS with Xcode command line tools).
#   make inventory       regenerate docs/LINUX_API_INVENTORY.md.
#
CLANG      ?= clang
PYTHON     ?= python3
BUILD      := build

MT76_DIR   := src/mt76
KPI_INC    := src/linuxkpi/include

# mt76 core, connac, mt792x and mt7925 PCIe objects (see src/mt76/UPSTREAM).
MT76_CORE_SRCS := mmio.c util.c dma.c mac80211.c eeprom.c tx.c agg-rx.c mcu.c \
                  scan.c channel.c pci.c
MT76_CONNAC_SRCS := mt76_connac_mcu.c mt76_connac_mac.c mt76_connac3_mac.c
MT792X_SRCS := mt792x_core.c mt792x_mac.c mt792x_dma.c mt792x_acpi_sar.c
# mt7925/nan.c (Wi-Fi Aware) is replaced by src/linuxkpi/src/mt7925_nan_stubs.c:
# macOS has no NAN consumer and the mac80211 NAN API is still moving.
MT7925_SRCS := mt7925/mac.c mt7925/mcu.c mt7925/regd.c mt7925/main.c mt7925/init.c \
               mt7925/pci.c mt7925/pci_mac.c mt7925/pci_mcu.c mt7925/cached_cal.c
MT76_SRCS  := $(MT76_CORE_SRCS) $(MT76_CONNAC_SRCS) $(MT792X_SRCS) $(MT7925_SRCS)

# C half of the LinuxKPI layer (OS-independent; the IOKit half is C++ in src/kext).
LKPI_SRCS  := $(wildcard src/linuxkpi/src/*.c)

# Kconfig symbols the source set expects.  CONFIG_PM is deliberately off
# until suspend/resume is wired to IOKit power management (phase 5).
CONFIG_DEFS := -DCONFIG_PCI=1 -DCONFIG_MT76_CONNAC_LIB=1 -DCONFIG_MT792x_LIB=1 \
               -DCONFIG_MT7925_COMMON=1 -DCONFIG_MT7925E=1 -DCONFIG_ACPI=1 \
               -DCONFIG_ARCH_DMA_ADDR_T_64BIT=1 -DCONFIG_IPV6=1

LKPI_CFLAGS := -std=gnu11 -D__KERNEL__ -DLKPI_VERSION=1 \
               -I$(KPI_INC) -I$(MT76_DIR) -include linux/kconfig.h $(CONFIG_DEFS) \
               -Wall -Wno-unused-parameter -Wno-gnu -Wno-missing-field-initializers \
               -Wno-sign-compare -Wno-address-of-packed-member -Wno-pointer-sign \
               -Wno-unused-function -Wno-microsoft-anon-tag -Wno-c11-extensions \
               -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
               -Werror=int-conversion -Werror=return-type

export LKPI_CFLAGS KPI_INC MT76_DIR MT76_SRCS

.PHONY: all check-headers link-check inventory host-tests kext clean

# make print-VAR prints a variable (used by scripts/link-check.sh).
print-%:
	@echo '$($*)'

all: check-headers

check-headers: | $(BUILD)
	@fail=0; for f in $(MT76_SRCS); do \
		if $(CLANG) -fsyntax-only $(LKPI_CFLAGS) -DKBUILD_MODNAME='"$$(basename $$f .c)"' \
		    $(MT76_DIR)/$$f 2> $(BUILD)/check.log; then \
			printf '  ok    %s\n' $$f; \
		else \
			printf '  FAIL  %s\n' $$f; cat $(BUILD)/check.log; fail=1; \
		fi; \
	done; \
	for f in $(LKPI_SRCS); do \
		if $(CLANG) -fsyntax-only $(LKPI_CFLAGS) -I$(MT76_DIR)/mt7925 -DKBUILD_MODNAME='"lkpi"' \
		    $$f 2> $(BUILD)/check.log; then \
			printf '  ok    %s\n' $$f; \
		else \
			printf '  FAIL  %s\n' $$f; cat $(BUILD)/check.log; fail=1; \
		fi; \
	done; exit $$fail

# Compile everything to host objects and list the symbols nobody defines:
# the exact set the kext must still provide (phase-3 mac80211 glue etc.).
link-check: | $(BUILD)
	scripts/link-check.sh

inventory:
	$(PYTHON) -I scripts/api-inventory.py

host-tests:
	$(MAKE) -C tests/host run

kext:
	$(MAKE) -C src/kext

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf ./build
	$(MAKE) -C tests/host clean
	$(MAKE) -C src/kext clean
