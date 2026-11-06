# SPDX-License-Identifier: MIT
#
# Top-level build for the MT7925 macOS driver.
#
#   make check-headers   front-end compile of the vendored mt76 sources against
#                        the LinuxKPI headers; works on Linux and macOS hosts
#                        with clang and needs no SDK.  This is the CI gate for
#                        the compatibility layer.
#   make check-net80211  front-end compile of the vendored FreeBSD net80211
#                        station-mode build set and the bsdkpi shim against
#                        the bsdkpi + LinuxKPI headers (phase 3, milestone 1).
#   make host-tests      build and run the userland unit tests in tests/host.
#   make kext            build MT7925.kext (macOS with Xcode command line tools).
#   make inventory       regenerate docs/LINUX_API_INVENTORY.md.
#
CLANG      ?= clang
PYTHON     ?= python3
BUILD      := build

MT76_DIR   := src/mt76
KPI_INC    := src/linuxkpi/include
# FreeBSD net80211 (verbatim) and the FreeBSD-kernel-API shim it runs on.
# <net80211/...> resolves through the src/net80211/net80211 -> upstream link.
N80211_DIR := src/net80211/upstream
N80211_INC := src/net80211
BSDKPI_DIR := src/bsdkpi
BSDKPI_INC := src/bsdkpi/include

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

# net80211 station-mode build set (docs/PHASE3_NET80211.md "Build set");
# paths relative to $(N80211_DIR).  Deferred: _hostap, _mesh/_hwmp, _tdma,
# _wds, _adhoc, _superg, _amrr/_rssadapt, _acl, _alq, _ddb, _ratectl_none.
NET80211_SRCS := ieee80211.c ieee80211_proto.c ieee80211_node.c ieee80211_input.c \
                 ieee80211_output.c ieee80211_sta.c ieee80211_scan.c ieee80211_scan_sta.c \
                 ieee80211_scan_sw.c ieee80211_ht.c ieee80211_vht.c ieee80211_crypto.c \
                 ieee80211_crypto_none.c ieee80211_crypto_wep.c ieee80211_crypto_tkip.c \
                 ieee80211_crypto_ccmp.c ieee80211_crypto_gcmp.c ieee80211_crypto_gcm.c \
                 ieee80211_power.c ieee80211_phy.c ieee80211_regdomain.c ieee80211_action.c \
                 ieee80211_ageq.c ieee80211_ratectl.c ieee80211_radiotap.c ieee80211_monitor.c \
                 ieee80211_ioctl.c ieee80211_dfs.c ieee80211_freebsd.c
# Stubs for the deferred modules' entry points that ieee80211.c calls
# unconditionally (hostap/adhoc/wds attach, dwds mcast); see the file.
NET80211_EXTRA_SRCS := $(N80211_INC)/net80211_stubs.c
# bsdkpi: own sources plus the vendored reference AES (public domain).
BSDKPI_SRCS := $(wildcard $(BSDKPI_DIR)/src/*.c) $(wildcard $(BSDKPI_DIR)/crypto/rijndael/*.c)

# Kconfig symbols the source set expects.  CONFIG_PM is deliberately off
# until suspend/resume is wired to IOKit power management (phase 5).
CONFIG_DEFS := -DCONFIG_PCI=1 -DCONFIG_MT76_CONNAC_LIB=1 -DCONFIG_MT792x_LIB=1 \
               -DCONFIG_MT7925_COMMON=1 -DCONFIG_MT7925E=1 -DCONFIG_ACPI=1 \
               -DCONFIG_ARCH_DMA_ADDR_T_64BIT=1 -DCONFIG_IPV6=1

LKPI_CFLAGS := -std=gnu11 -D__KERNEL__ -DLKPI_VERSION=1 \
               -I$(KPI_INC) -I$(N80211_INC) -I$(MT76_DIR) -include linux/kconfig.h $(CONFIG_DEFS) \
               -Wall -Wno-unused-parameter -Wno-gnu -Wno-missing-field-initializers \
               -Wno-sign-compare -Wno-address-of-packed-member -Wno-pointer-sign \
               -Wno-unused-function -Wno-microsoft-anon-tag -Wno-c11-extensions \
               -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
               -Werror=int-conversion -Werror=return-type

# FreeBSD sources are kernel code (_KERNEL); the bsdkpi include directory must
# come first so that its sys/types.h, sys/cdefs.h etc. shadow the host's.
NET80211_CFLAGS := -I$(BSDKPI_INC) -I$(BSDKPI_DIR) -I$(N80211_INC) $(LKPI_CFLAGS) -D_KERNEL=1

export LKPI_CFLAGS NET80211_CFLAGS KPI_INC MT76_DIR MT76_SRCS N80211_DIR NET80211_SRCS NET80211_EXTRA_SRCS BSDKPI_SRCS

.PHONY: all check-headers check-net80211 link-check inventory host-tests kext clean

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

# Same gate for the 802.11 stack: every file of the net80211 station-mode
# build set, the bsdkpi shim and the vendored AES against bsdkpi + LinuxKPI.
# Warnings are shown for files that compile, so that "ok" means clean.
check-net80211: | $(BUILD)
	@fail=0; for f in $(NET80211_SRCS); do \
		if $(CLANG) -fsyntax-only $(NET80211_CFLAGS) -DKBUILD_MODNAME='"net80211"' \
		    $(N80211_DIR)/$$f 2> $(BUILD)/check.log; then \
			printf '  ok    %s\n' $$f; cat $(BUILD)/check.log; \
		else \
			printf '  FAIL  %s\n' $$f; cat $(BUILD)/check.log; fail=1; \
		fi; \
	done; \
	for f in $(NET80211_EXTRA_SRCS) $(BSDKPI_SRCS); do \
		if $(CLANG) -fsyntax-only $(NET80211_CFLAGS) -DKBUILD_MODNAME='"bsdkpi"' \
		    $$f 2> $(BUILD)/check.log; then \
			printf '  ok    %s\n' $$f; cat $(BUILD)/check.log; \
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
