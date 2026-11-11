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
#   make check-lkpi80211 front-end compile of FreeBSD's mac80211 -> net80211 glue
#                        (src/lkpi80211) against bsdkpi + LinuxKPI + net80211.
#   make link-check-full the complete program (driver + LinuxKPI + bsdkpi +
#                        net80211 + glue) has no undefined or duplicate symbol.
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
# mac80211_stub.c is the phase-1 recording stand-in for the 802.11 stack; the
# real stack (net80211 + src/lkpi80211) replaced it.  MAC80211_STUB=1 selects
# the old behaviour: the stub is built and the stack is left out.
MAC80211_STUB ?= 0
LKPI_STUB_SRC := src/linuxkpi/src/mac80211_stub.c
ifeq ($(MAC80211_STUB),1)
LKPI_SRCS  := $(wildcard src/linuxkpi/src/*.c)
# runtime.c then does not start bsdkpi/net80211.
CONFIG_DEFS_STUB := -DLKPI_NO_NET80211=1
else
LKPI_SRCS  := $(filter-out $(LKPI_STUB_SRC),$(wildcard src/linuxkpi/src/*.c))
endif

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
                 ieee80211_ioctl.c ieee80211_dfs.c ieee80211_freebsd.c \
                 ieee80211_ratectl_none.c ieee80211_amrr.c
# Stubs for the deferred modules' entry points that ieee80211.c calls
# unconditionally (hostap/adhoc/wds attach, dwds mcast); see the file.
NET80211_EXTRA_SRCS := $(N80211_INC)/net80211_stubs.c
# bsdkpi: own sources plus the vendored reference AES (public domain).
BSDKPI_SRCS := $(wildcard $(BSDKPI_DIR)/src/*.c) $(wildcard $(BSDKPI_DIR)/crypto/rijndael/*.c)

# FreeBSD's mac80211 -> net80211 glue (vendored; patches/0004) and the
# functions it lacks.  linuxkpi_80211_pm.c is not built: it is a newbus
# driver (device_t methods, DRIVER_MODULE) for FreeBSD's suspend/resume
# children; here power events arrive through IOKit and call
# lkpi_80211_suspend()/lkpi_80211_resume() in linux_80211.c directly.
LKPI80211_DIR  := src/lkpi80211
LKPI80211_SRCS := $(LKPI80211_DIR)/linux_80211.c $(LKPI80211_DIR)/linux_80211_macops.c \
                  $(LKPI80211_DIR)/lkpi80211_compat.c

# Kconfig symbols the source set expects.  CONFIG_PM is deliberately off
# until suspend/resume is wired to IOKit power management (phase 5).
CONFIG_DEFS := -DCONFIG_PCI=1 -DCONFIG_MT76_CONNAC_LIB=1 -DCONFIG_MT792x_LIB=1 \
               -DCONFIG_MT7925_COMMON=1 -DCONFIG_MT7925E=1 -DCONFIG_ACPI=1 \
               -DCONFIG_ARCH_DMA_ADDR_T_64BIT=1 -DCONFIG_IPV6=1 $(CONFIG_DEFS_STUB)

# -fwrapv -fno-strict-aliasing: both kernels this code comes from are built
# that way and rely on it.  net80211's ieee80211_time_after() is
# ((int)(b) - (int)(a) < 0) and scans "forever" with ticks + INT_MAX; without
# -fwrapv clang folds that to (int)(b) < (int)(a) and every scan ends at once.
KERNEL_CODEGEN := -fwrapv -fno-strict-aliasing
LKPI_CFLAGS := -std=gnu11 $(KERNEL_CODEGEN) -D__KERNEL__ -DLKPI_VERSION=1 \
               -I$(KPI_INC) -I$(N80211_INC) -I$(MT76_DIR) -include linux/kconfig.h $(CONFIG_DEFS) \
               -Wall -Wno-unused-parameter -Wno-gnu -Wno-missing-field-initializers \
               -Wno-sign-compare -Wno-address-of-packed-member -Wno-pointer-sign \
               -Wno-unused-function -Wno-microsoft-anon-tag -Wno-c11-extensions -Wno-c23-extensions \
               -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
               -Werror=int-conversion -Werror=return-type

# FreeBSD sources are kernel code (_KERNEL); the bsdkpi include directory must
# come first so that its sys/types.h, sys/cdefs.h etc. shadow the host's.
NET80211_CFLAGS := -I$(BSDKPI_INC) -I$(BSDKPI_DIR) -I$(N80211_INC) $(LKPI_CFLAGS) -D_KERNEL=1

# The glue sees both kernel APIs in one translation unit; the force-included
# prelude fixes the header order (see the file).  -Wno-format-security: the
# glue passes wiphy_name() as the format of alloc_ordered_workqueue().
LKPI80211_CFLAGS := $(NET80211_CFLAGS) -I$(LKPI80211_DIR) -include lkpi80211_prelude.h \
                    -Wno-format-security

export LKPI80211_CFLAGS LKPI80211_SRCS
export LKPI_CFLAGS NET80211_CFLAGS KPI_INC MT76_DIR MT76_SRCS N80211_DIR NET80211_SRCS NET80211_EXTRA_SRCS BSDKPI_SRCS

.PHONY: all check-headers check-net80211 check-lkpi80211 link-check link-check-net80211 link-check-full inventory host-tests kext clean

# make print-VAR prints a variable (used by scripts/link-check.sh).
print-%:
	@echo '$($*)'

all: check-headers

check-headers: | $(BUILD)
	@fail=0; for f in $(MT76_SRCS); do \
		if $(CLANG) -fsyntax-only $(LKPI_CFLAGS) -DKBUILD_MODNAME='"$$(basename $$f .c)"' \
		    $(MT76_DIR)/$$f 2> $(BUILD)/check.log; then \
			printf '  ok    %s\n' $$f; cat $(BUILD)/check.log; \
		else \
			printf '  FAIL  %s\n' $$f; cat $(BUILD)/check.log; fail=1; \
		fi; \
	done; \
	for f in $(sort $(LKPI_SRCS) $(LKPI_STUB_SRC)); do \
		if $(CLANG) -fsyntax-only $(LKPI_CFLAGS) -I$(MT76_DIR)/mt7925 -DKBUILD_MODNAME='"lkpi"' \
		    $$f 2> $(BUILD)/check.log; then \
			printf '  ok    %s\n' $$f; cat $(BUILD)/check.log; \
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

# And for the mac80211 -> net80211 glue (phase 3, milestone 2).
check-lkpi80211: | $(BUILD)
	@fail=0; for f in $(LKPI80211_SRCS); do \
		if $(CLANG) -fsyntax-only $(LKPI80211_CFLAGS) -DKBUILD_MODNAME='"lkpi80211"' \
		    $$f 2> $(BUILD)/check.log; then \
			printf '  ok    %s\n' $$f; cat $(BUILD)/check.log; \
		else \
			printf '  FAIL  %s\n' $$f; cat $(BUILD)/check.log; fail=1; \
		fi; \
	done; exit $$fail

# Compile the driver and the LinuxKPI C layer to host objects and list the
# symbols nobody defines: what the 802.11 stack (or, with MAC80211_STUB=1,
# nothing) must still provide.
link-check: | $(BUILD)
	MAC80211_STUB=$(MAC80211_STUB) scripts/link-check.sh

# The complete program: driver + LinuxKPI + bsdkpi + net80211 + glue, without
# mac80211_stub.c.  Fails on any undefined symbol beyond lkpi/os.h and
# libc/libkern and on any duplicate strong symbol.
link-check-full: | $(BUILD)
	scripts/link-check-full.sh

# The same for the 802.11 stack: net80211 build set + bsdkpi + AES, resolved
# against src/linuxkpi/src, lkpi/os.h and libkern.
link-check-net80211: | $(BUILD)
	scripts/link-check-net80211.sh

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
