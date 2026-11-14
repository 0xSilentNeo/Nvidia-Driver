# SPDX-License-Identifier: MIT
#
# Builds MT7925.kext without Xcode (command line tools only).
#
#   make                 build build/MT7925.kext for ARCHS (default x86_64)
#   make ARCHS="x86_64 arm64e"   fat binary (arm64e is what the Apple Silicon
#                        kernel loads; keep it compiling, not yet tested)
#   make check-syntax    clang -fsyntax-only on the C++ files (needs an SDK)
#   make check-xnu XNU_HDRS=/abs/tree   the same compile for every source of
#                        the kext against an SDK stand-in assembled from Apple's
#                        open-source xnu + IOPCIFamily (no Mac needed; see
#                        scripts/xnu-kernel-headers.sh and "check-xnu" below)
#   make check-xnu-symbols XNU_HDRS=/abs/tree   every external symbol of the
#                        kext's objects against the KPI exports lists
#   make load / unload   kmutil helpers (sudo; see README.md for SIP/consent)
#   make clean
#
# Variables: KEXT_SDK (defaults to `xcrun --show-sdk-path`), ARCHS,
# EMBED_FIRMWARE (1: link the blobs from ../../firmware into the binary; the
# default when that directory has them), MIN_OS (11.0), V=1 for verbose.
#
# Invoked from the top-level Makefile (`make kext`) MT76_SRCS arrives in the
# environment and is reused; MT76_DIR/KPI_INC/LKPI_CFLAGS are re-derived here
# because the top-level versions hold paths relative to the repository root.

TOP        := ../..
BUILD      := $(TOP)/build/kext
KEXT_NAME  := MT7925
BUNDLE_ID  := org.mt7925port.MT7925
VERSION    := 1.0.0
KEXT       := $(BUILD)/$(KEXT_NAME).kext
BINARY     := $(KEXT)/Contents/MacOS/$(KEXT_NAME)

ARCHS      ?= x86_64
MIN_OS     ?= 11.0
XCRUN      := $(shell command -v xcrun 2>/dev/null)
ifneq ($(XCRUN),)
KEXT_SDK   ?= $(shell xcrun --sdk macosx --show-sdk-path 2>/dev/null)
CC         := xcrun clang
CXX        := xcrun clang++
LIPO       := xcrun lipo
else
KEXT_SDK   ?=
CC         := clang
CXX        := clang++
LIPO       := lipo
endif
SDK        := $(KEXT_SDK)
KERNEL_HDRS := $(SDK)/System/Library/Frameworks/Kernel.framework/Headers

ifeq ($(V),1)
Q :=
else
Q := @
endif

# ---- source lists ---------------------------------------------------------
MT76_DIR   := $(TOP)/src/mt76
KPI_INC    := $(TOP)/src/linuxkpi/include
# Keep in sync with the top-level Makefile (used when invoked directly).
MT76_SRCS  ?= mmio.c util.c dma.c mac80211.c eeprom.c tx.c agg-rx.c mcu.c \
              scan.c channel.c pci.c \
              mt76_connac_mcu.c mt76_connac_mac.c mt76_connac3_mac.c \
              mt792x_core.c mt792x_mac.c mt792x_dma.c mt792x_acpi_sar.c \
              mt7925/mac.c mt7925/mcu.c mt7925/regd.c mt7925/main.c mt7925/init.c \
              mt7925/pci.c mt7925/pci_mac.c mt7925/pci_mcu.c mt7925/cached_cal.c
# The kext still links the phase-1 recording stub (mac80211_stub.c, the
# top-level MAC80211_STUB=1 configuration, with LKPI_NO_NET80211 so that
# runtime.c does not start bsdkpi).  The real stack (src/bsdkpi, src/net80211,
# src/lkpi80211) builds, links and runs on the host (make link-check-full,
# tests/host/test_probe), and `make check-xnu` compiles it (bsdkpi's
# #include_next <sys/types.h>/<sys/cdefs.h> included) against an open-source
# Kernel.framework with the per-directory flags (NET80211_CFLAGS,
# LKPI80211_CFLAGS with the prelude) and resolves its symbols against the
# KPI exports (check-xnu-symbols STACK=real); what is left is wiring those
# per-directory rules in here and testing on a Mac.
LKPI_SRCS  := $(wildcard $(TOP)/src/linuxkpi/src/*.c)
LKPI80211_SRCS ?=

CONFIG_DEFS ?= -DCONFIG_PCI=1 -DCONFIG_MT76_CONNAC_LIB=1 -DCONFIG_MT792x_LIB=1 \
               -DCONFIG_MT7925_COMMON=1 -DCONFIG_MT7925E=1 -DCONFIG_ACPI=1 \
               -DCONFIG_ARCH_DMA_ADDR_T_64BIT=1 -DCONFIG_IPV6=1
# -fwrapv -fno-strict-aliasing: see KERNEL_CODEGEN in the top-level Makefile.
# LKPI_NO_NET80211: see the note at LKPI_SRCS above.
LKPI_CFLAGS := -std=gnu11 -fwrapv -fno-strict-aliasing -D__KERNEL__ -DLKPI_VERSION=1 \
               -DLKPI_NO_NET80211=1 \
               -I$(KPI_INC) -I$(TOP)/src/net80211 -I$(MT76_DIR) -include linux/kconfig.h $(CONFIG_DEFS) \
               -Wall -Wno-unused-parameter -Wno-gnu -Wno-missing-field-initializers \
               -Wno-sign-compare -Wno-address-of-packed-member -Wno-pointer-sign \
               -Wno-unused-function -Wno-microsoft-anon-tag -Wno-c11-extensions -Wno-c23-extensions \
               -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
               -Werror=int-conversion -Werror=return-type

KEXT_CXX_SRCS := MT7925PCIe.cpp lkpi_os_iokit.cpp
KEXT_C_SRCS   := MT7925_info.c

# lkpi_param_set() lives in the C layer; compile a stub until it exists so
# the kext still links.
ifeq ($(shell grep -ls 'lkpi_param_set' $(TOP)/src/linuxkpi/src/*.c 2>/dev/null),)
KEXT_C_SRCS += lkpi_param_stub.c
endif

# ---- firmware -------------------------------------------------------------
FW_DIR    := $(TOP)/firmware
FW_FILES  := mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin \
             mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin \
             mediatek/mt7927/WIFI_MT6639_PATCH_MCU_2_1_hdr.bin \
             mediatek/mt7927/WIFI_RAM_CODE_MT6639_2_1.bin \
             mediatek/mt7928/WIFI_MT7935_PATCH_MCU_1_1_hdr.bin \
             mediatek/mt7928/WIFI_RAM_CODE_MT7935_1_1.bin \
             mediatek/mt7928/CBMCU_CODE_MT7935_1_1.bin \
             mediatek/mt7928/WIFI_MT7935_PHY_RAM_CODE_1_1.bin
FW_PRESENT := $(foreach f,$(FW_FILES),$(if $(wildcard $(FW_DIR)/$(f)),$(f)))
EMBED_FIRMWARE ?= $(if $(FW_PRESENT),1,0)
ifeq ($(EMBED_FIRMWARE),1)
FW_EMBED := $(FW_PRESENT)
else
FW_EMBED :=
endif

# ---- flags ----------------------------------------------------------------
# Public Kernel.framework headers only (no PrivateHeaders).
COMMON_FLAGS := -isysroot $(SDK) -isystem $(KERNEL_HDRS) \
                -mkernel -fno-builtin -fno-common -nostdlib \
                -D__KERNEL__ -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
                -DLKPI_MACOS=1 -DKEXT_VERSION='"$(VERSION)"' \
                -mmacosx-version-min=$(MIN_OS) -O2 -g -Wall -Wextra \
                -fno-asynchronous-unwind-tables
# x86_64 kernel code must not use the red zone (-mkernel implies it, be explicit).
ARCH_FLAGS_x86_64 := -mno-red-zone
ARCH_FLAGS_arm64e := -fno-ptrauth-intrinsics
ARCH_FLAGS_arm64  :=

CXXFLAGS_EXTRA ?=
CXXFLAGS := $(COMMON_FLAGS) -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions \
            -I$(KPI_INC) -Wno-unused-parameter $(CXXFLAGS_EXTRA)
# C: the Linux sources and the C LinuxKPI layer, held to the warning set of
# the host gates (make check-headers): -Wall plus LKPI_CFLAGS, without the
# -Wextra the kext's own code gets (the vendored FreeBSD net80211 headers
# that net/mac80211.h pulls in trip -Wignored-qualifiers, for example).
CFLAGS   := $(filter-out -Wextra,$(COMMON_FLAGS)) $(LKPI_CFLAGS) -I$(MT76_DIR)/mt7925 -Wno-unused-but-set-variable
# C: the kext's own C files (kmod info, stubs, generated firmware table).
CFLAGS_KEXT := $(COMMON_FLAGS) -std=gnu11
ASFLAGS  := -isysroot $(SDK) -mmacosx-version-min=$(MIN_OS)

LDFLAGS  := -isysroot $(SDK) -mmacosx-version-min=$(MIN_OS) -nostdlib \
            -Xlinker -kext -lkmodc++ -lkmod -lcc_kext

# ---- objects --------------------------------------------------------------
# Objects live in build/kext/obj/<arch>/..., one tree per arch; the final
# binary is lipo'd when more than one arch is requested.
objdir   = $(BUILD)/obj/$(1)
obj_cxx  = $(patsubst %.cpp,$(call objdir,$(1))/kext/%.o,$(KEXT_CXX_SRCS))
obj_kc   = $(patsubst %.c,$(call objdir,$(1))/kext/%.o,$(KEXT_C_SRCS))
obj_mt76 = $(patsubst %.c,$(call objdir,$(1))/mt76/%.o,$(MT76_SRCS))
obj_lkpi = $(patsubst $(TOP)/src/linuxkpi/src/%.c,$(call objdir,$(1))/lkpi/%.o,$(LKPI_SRCS))
obj_lkpi80211 = $(patsubst $(TOP)/src/lkpi80211/%.c,$(call objdir,$(1))/lkpi80211/%.o,$(LKPI80211_SRCS))
obj_fw   = $(call objdir,$(1))/fw/fw_table.o $(call objdir,$(1))/fw/fw_blobs.o
objs     = $(call obj_cxx,$(1)) $(call obj_kc,$(1)) $(call obj_mt76,$(1)) \
           $(call obj_lkpi,$(1)) $(call obj_lkpi80211,$(1)) $(call obj_fw,$(1))

THIN_BINARIES := $(foreach a,$(ARCHS),$(BUILD)/$(KEXT_NAME).$(a))

.PHONY: all kext check-sdk check-syntax bundle load unload clean fwtable \
        check-xnu check-xnu-symbols xnu-sdk _check-xnu _check-xnu-symbols

all: kext

kext: check-sdk $(BINARY) bundle
	@echo "built $(KEXT)"

check-sdk:
ifeq ($(SDK),)
	$(error No macOS SDK found. Install Xcode command line tools or set KEXT_SDK=/path/to/MacOSX.sdk)
endif
	@test -d "$(KERNEL_HDRS)" || { echo "Kernel.framework headers not found in $(SDK)"; exit 1; }

# -- generated firmware table (shared by every arch) ------------------------
FW_GEN_DIR := $(BUILD)/gen
$(FW_GEN_DIR)/fw_table.c $(FW_GEN_DIR)/fw_blobs.S: gen-fw-table.sh Makefile $(addprefix $(FW_DIR)/,$(FW_EMBED))
	$(Q)sh gen-fw-table.sh $(FW_GEN_DIR) $(FW_DIR) $(FW_EMBED)

fwtable: $(FW_GEN_DIR)/fw_table.c

# -- per-arch rules ---------------------------------------------------------
define ARCH_RULES
$(call objdir,$(1))/kext/%.o: %.cpp lkpi_os_iokit.hpp MT7925PCIe.hpp
	@mkdir -p $$(dir $$@)
	$(Q)$(CXX) -arch $(1) $(ARCH_FLAGS_$(1)) $(CXXFLAGS) -c $$< -o $$@

$(call objdir,$(1))/kext/%.o: %.c
	@mkdir -p $$(dir $$@)
	$(Q)$(CC) -arch $(1) $(ARCH_FLAGS_$(1)) $(CFLAGS_KEXT) -c $$< -o $$@

$(call objdir,$(1))/mt76/%.o: $(MT76_DIR)/%.c
	@mkdir -p $$(dir $$@)
	$(Q)$(CC) -arch $(1) $(ARCH_FLAGS_$(1)) $(CFLAGS) -DKBUILD_MODNAME='"$$(basename $$(notdir $$<))"' -c $$< -o $$@

$(call objdir,$(1))/lkpi/%.o: $(TOP)/src/linuxkpi/src/%.c
	@mkdir -p $$(dir $$@)
	$(Q)$(CC) -arch $(1) $(ARCH_FLAGS_$(1)) $(CFLAGS) -DKBUILD_MODNAME='"lkpi"' -c $$< -o $$@

$(call objdir,$(1))/lkpi80211/%.o: $(TOP)/src/lkpi80211/%.c
	@mkdir -p $$(dir $$@)
	$(Q)$(CC) -arch $(1) $(ARCH_FLAGS_$(1)) $(CFLAGS) -DKBUILD_MODNAME='"lkpi80211"' -c $$< -o $$@

$(call objdir,$(1))/fw/fw_table.o: $(FW_GEN_DIR)/fw_table.c
	@mkdir -p $$(dir $$@)
	$(Q)$(CC) -arch $(1) $(ARCH_FLAGS_$(1)) $(CFLAGS_KEXT) -c $$< -o $$@

$(call objdir,$(1))/fw/fw_blobs.o: $(FW_GEN_DIR)/fw_blobs.S
	@mkdir -p $$(dir $$@)
	$(Q)$(CC) -arch $(1) $(ASFLAGS) -c $$< -o $$@

$(BUILD)/$(KEXT_NAME).$(1): $(call objs,$(1))
	$(Q)$(CC) -arch $(1) $(LDFLAGS) -o $$@ $$^
endef
$(foreach a,$(ARCHS),$(eval $(call ARCH_RULES,$(a))))

$(BINARY): $(THIN_BINARIES)
	@mkdir -p $(dir $@)
ifeq ($(words $(ARCHS)),1)
	$(Q)cp $< $@
else
	$(Q)$(LIPO) -create $^ -output $@
endif

# -- bundle -----------------------------------------------------------------
# Firmware is installed twice: at the relative path the driver asks for
# (Contents/Resources/mediatek/mt7925/X.bin) and flat (Contents/Resources/X.bin),
# because lkpi_os_firmware_load() tries both spellings with
# OSKextRequestResource (CFBundleCopyResourceURL may not accept a path).
bundle: $(BINARY)
	$(Q)mkdir -p $(KEXT)/Contents/Resources
	$(Q)cp Info.plist $(KEXT)/Contents/Info.plist
	$(Q)for f in $(FW_PRESENT); do \
		mkdir -p $(KEXT)/Contents/Resources/$$(dirname $$f); \
		cp $(FW_DIR)/$$f $(KEXT)/Contents/Resources/$$f; \
		cp $(FW_DIR)/$$f $(KEXT)/Contents/Resources/$$(basename $$f); \
	done
	$(Q)if [ -f $(FW_DIR)/LICENCE.mediatek ]; then cp $(FW_DIR)/LICENCE.mediatek $(KEXT)/Contents/Resources/; fi
	$(Q)if [ -z "$(FW_PRESENT)" ]; then echo "note: no firmware in $(FW_DIR); run scripts/fetch-firmware.sh"; fi

# -- developer helpers ------------------------------------------------------
check-syntax: check-sdk
	@for f in $(KEXT_CXX_SRCS); do \
		for a in $(ARCHS); do \
			if $(CXX) -arch $$a $(ARCH_FLAGS_$$a) $(CXXFLAGS) -fsyntax-only $$f; then \
				printf '  ok    %s (%s)\n' $$f $$a; else printf '  FAIL  %s (%s)\n' $$f $$a; fail=1; fi; \
		done; \
	done; exit $${fail:-0}

# -- check-xnu: the kext's compile without a Mac ------------------------------
# XNU_HDRS is an SDK stand-in (System/Library/Frameworks/Kernel.framework/
# Headers + the few usr/include headers those include + KPI/<arch>/*.exports)
# made by scripts/xnu-kernel-headers.sh from Apple's open-source xnu and
# IOPCIFamily; give an absolute path.  With XNU_SRC and IOPCIFAMILY_SRC
# instead, the tree is assembled into $(BUILD)/xnu-sdk first.
#
# The check re-runs this Makefile with KEXT_SDK=$(XNU_HDRS), so every flag is
# exactly the one the real build uses; only `-arch A` becomes
# `-target A-apple-darwin` (clang on a non-Apple host needs the triple).
# Compiled with -fsyntax-only, reported in sections:
#   kext C++ / kext C   the IOKit half (+ the generated firmware table);
#   C set (kext)        what the kext links today: mt76 + LinuxKPI including
#                       mac80211_stub.c, LKPI_NO_NET80211=1, the kext's CFLAGS;
#   C set (real stack)  the top-level MAC80211_STUB=0 set: mt76 + LinuxKPI
#                       without the stub + bsdkpi + net80211 + lkpi80211, each
#                       with its top-level per-directory flags on top of the
#                       kext's kernel flags.  Reported separately; it is not
#                       linked into the kext yet.
# "ok" means no diagnostics at all; warnings are printed and counted.
# check-xnu-symbols compiles the kext set to Mach-O objects and resolves every
# external symbol against the exports lists of the OSBundleLibraries in
# Info.plist (scripts/xnu-kext-symbols.sh); STACK=real checks the real-stack
# set instead.
XNU_HDRS        ?=
XNU_SRC         ?=
IOPCIFAMILY_SRC ?=
XNU_ARCHS       ?= x86_64
XNU_CC          ?= $(CC)
XNU_CXX         ?= $(CXX)
STACK           ?= kext
XNU_TRIPLE_x86_64 := x86_64-apple-darwin
XNU_TRIPLE_arm64e := arm64e-apple-darwin
XNU_TRIPLE_arm64  := arm64-apple-darwin
# -fno-ptrauth-intrinsics exists in Apple clang only.
XNU_ARCH_FLAGS_x86_64 := $(ARCH_FLAGS_x86_64)
XNU_ARCH_FLAGS_arm64e :=
XNU_ARCH_FLAGS_arm64  :=
XNU_CHECK_DIR   := $(abspath $(BUILD)/xnu-check)
# Kernel flags for the real-stack set: the kext's, minus its LinuxKPI flags.
XNU_STACK_FLAGS  = $(filter-out -Wextra,$(COMMON_FLAGS)) -Wno-unused-but-set-variable
# Upstream LLVM (15+) lowers Mach-O global destructors (the gMetaClass object
# of every OSDefineMetaClassAndStructors) to __cxa_atexit(), which no KPI
# exports: XNU's OSRuntime runs a kext's __mod_term_func instead.  Apple clang
# evidently keeps __mod_term_func for kexts (every C++ kext links), so the
# object check asks upstream clang for the same; set it empty for Apple clang.
XNU_CXX_CODEGEN ?= -mllvm -disable-atexit-based-global-dtor-lowering

ifeq ($(XNU_HDRS),)
ifneq ($(XNU_SRC),)
XNU_HDRS_AUTO := 1
override XNU_HDRS := $(abspath $(BUILD)/xnu-sdk)
endif
endif
XNU_SUBMAKE = $(MAKE) --no-print-directory KEXT_SDK=$(abspath $(XNU_HDRS)) XNU_HDRS=$(abspath $(XNU_HDRS)) \
              XNU_ARCHS="$(XNU_ARCHS)" XNU_CC="$(XNU_CC)" XNU_CXX="$(XNU_CXX)" STACK=$(STACK) \
              XNU_CXX_CODEGEN="$(XNU_CXX_CODEGEN)"

xnu-sdk:
ifeq ($(XNU_HDRS),)
	$(error set XNU_HDRS=/abs/path made by scripts/xnu-kernel-headers.sh, or XNU_SRC=<xnu checkout> IOPCIFAMILY_SRC=<IOPCIFamily checkout>)
endif
ifeq ($(XNU_HDRS_AUTO),1)
	$(Q)sh $(TOP)/scripts/xnu-kernel-headers.sh $(XNU_SRC) $(IOPCIFAMILY_SRC) $(XNU_HDRS)
endif
	@test -f "$(XNU_HDRS)/MANIFEST" -a -d "$(XNU_HDRS)/System/Library/Frameworks/Kernel.framework/Headers" || \
		{ echo "$(XNU_HDRS): not a tree made by scripts/xnu-kernel-headers.sh"; exit 1; }

check-xnu: xnu-sdk
	$(Q)$(XNU_SUBMAKE) _check-xnu

check-xnu-symbols: xnu-sdk
	$(Q)$(XNU_SUBMAKE) _check-xnu-symbols

# chk SECTION FILE COMPILER FLAGS...: one -fsyntax-only compile, reported.
# Runs in a single shell per arch so the counters survive.
define XNU_CHK_SH
chk() { sec=$$1; f=$$2; shift 2; \
	if "$$@" -fsyntax-only "$$f" > $(XNU_CHECK_DIR)/log 2>&1; then \
		if [ -s $(XNU_CHECK_DIR)/log ]; then printf '  warn  %s\n' "$$f"; cat $(XNU_CHECK_DIR)/log; \
			echo "$$sec warn" >> $(XNU_CHECK_DIR)/tally; \
		else printf '  ok    %s\n' "$$f"; echo "$$sec ok" >> $(XNU_CHECK_DIR)/tally; fi; \
	else printf '  FAIL  %s\n' "$$f"; cat $(XNU_CHECK_DIR)/log; echo "$$sec FAIL" >> $(XNU_CHECK_DIR)/tally; fi; }
endef

# Top-level values of the real-stack set (paths relative to the repository).
XNU_TOPVAR = $$($(MAKE) --no-print-directory -s -C $(TOP) MAC80211_STUB=0 print-$(1))

_check-xnu: fwtable
	@mkdir -p $(XNU_CHECK_DIR); rm -f $(XNU_CHECK_DIR)/tally; $(XNU_CHK_SH); \
	for a in $(XNU_ARCHS); do \
		case $$a in x86_64) T="$(XNU_TRIPLE_x86_64) $(XNU_ARCH_FLAGS_x86_64)";; \
		arm64e) T="$(XNU_TRIPLE_arm64e) $(XNU_ARCH_FLAGS_arm64e)";; \
		arm64) T="$(XNU_TRIPLE_arm64) $(XNU_ARCH_FLAGS_arm64)";; *) echo "unknown arch $$a"; exit 2;; esac; \
		echo "== $$a: kext C++"; \
		for f in $(KEXT_CXX_SRCS); do chk "$$a/kext-C++" $$f $(XNU_CXX) -target $$T $(CXXFLAGS); done; \
		echo "== $$a: kext C"; \
		for f in $(KEXT_C_SRCS) $(FW_GEN_DIR)/fw_table.c; do chk "$$a/kext-C" $$f $(XNU_CC) -target $$T $(CFLAGS_KEXT); done; \
		echo "== $$a: C set (kext: mt76 + LinuxKPI with mac80211_stub.c)"; \
		for f in $(MT76_SRCS); do chk "$$a/C-kext" $(MT76_DIR)/$$f $(XNU_CC) -target $$T $(CFLAGS) \
			-DKBUILD_MODNAME="\"$$(basename $$f .c)\""; done; \
		for f in $(LKPI_SRCS); do chk "$$a/C-kext" $$f $(XNU_CC) -target $$T $(CFLAGS) -DKBUILD_MODNAME='"lkpi"'; done; \
		echo "== $$a: C set (real 802.11 stack, MAC80211_STUB=0)"; \
		L="$(call XNU_TOPVAR,LKPI_CFLAGS)"; N="$(call XNU_TOPVAR,NET80211_CFLAGS)"; \
		G="$(call XNU_TOPVAR,LKPI80211_CFLAGS)"; ND="$(call XNU_TOPVAR,N80211_DIR)"; \
		for f in $(call XNU_TOPVAR,MT76_SRCS); do (cd $(TOP) && chk "$$a/C-stack" src/mt76/$$f $(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$L \
			-Isrc/mt76/mt7925 -DKBUILD_MODNAME="\"$$(basename $$f .c)\""); done; \
		for f in $(call XNU_TOPVAR,LKPI_SRCS); do (cd $(TOP) && chk "$$a/C-stack" $$f $(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$L \
			-Isrc/mt76/mt7925 -DKBUILD_MODNAME='"lkpi"'); done; \
		for f in $(call XNU_TOPVAR,NET80211_SRCS); do (cd $(TOP) && chk "$$a/C-stack" $$ND/$$f $(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$N \
			-DKBUILD_MODNAME='"net80211"'); done; \
		for f in $(call XNU_TOPVAR,NET80211_EXTRA_SRCS) $(call XNU_TOPVAR,BSDKPI_SRCS); do (cd $(TOP) && chk "$$a/C-stack" $$f \
			$(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$N -DKBUILD_MODNAME='"bsdkpi"'); done; \
		for f in $(call XNU_TOPVAR,LKPI80211_SRCS); do (cd $(TOP) && chk "$$a/C-stack" $$f $(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$G \
			-DKBUILD_MODNAME='"lkpi80211"'); done; \
	done; \
	echo "== summary (ok / with warnings / failed)"; \
	sort $(XNU_CHECK_DIR)/tally | uniq -c | awk '{ n[$$2] = n[$$2] " " $$3 "=" $$1 } END { for (s in n) print "  " s ":" n[s] }' | sort; \
	! grep -q ' FAIL$$' $(XNU_CHECK_DIR)/tally

# -- check-xnu-symbols ---------------------------------------------------------
# Same sets, compiled to Mach-O objects (-c), then every symbol they leave
# undefined must be exported by a bundle in OSBundleLibraries or by libkmod.
define XNU_CC_SH
cco() { o=$$1; shift; mkdir -p "$$(dirname "$$o")"; "$$@" -c -o "$$o" || { echo "FAIL $$o"; echo x >> $(XNU_CHECK_DIR)/ofail; }; }
endef

_check-xnu-symbols: fwtable
	@rm -rf "$(XNU_CHECK_DIR)/obj"; mkdir -p "$(XNU_CHECK_DIR)/obj"; rm -f "$(XNU_CHECK_DIR)/ofail"; $(XNU_CC_SH); \
	for a in $(XNU_ARCHS); do \
		case $$a in x86_64) T="$(XNU_TRIPLE_x86_64) $(XNU_ARCH_FLAGS_x86_64)"; kpi=x86_64;; \
		arm64e) T="$(XNU_TRIPLE_arm64e) $(XNU_ARCH_FLAGS_arm64e)"; kpi=arm64;; \
		arm64) T="$(XNU_TRIPLE_arm64) $(XNU_ARCH_FLAGS_arm64)"; kpi=arm64;; *) echo "unknown arch $$a"; exit 2;; esac; \
		O=$(XNU_CHECK_DIR)/obj/$$a; \
		for f in $(KEXT_CXX_SRCS); do cco $$O/kext/$${f%.cpp}.o $(XNU_CXX) -target $$T $(CXXFLAGS) $(XNU_CXX_CODEGEN) $$f; done; \
		for f in $(KEXT_C_SRCS); do cco $$O/kext/$${f%.c}.o $(XNU_CC) -target $$T $(CFLAGS_KEXT) $$f; done; \
		cco $$O/fw/fw_table.o $(XNU_CC) -target $$T $(CFLAGS_KEXT) $(FW_GEN_DIR)/fw_table.c; \
		cco $$O/fw/fw_blobs.o $(XNU_CC) -target $$T $(ASFLAGS) $(FW_GEN_DIR)/fw_blobs.S; \
		if [ "$(STACK)" = real ]; then \
			L="$(call XNU_TOPVAR,LKPI_CFLAGS)"; N="$(call XNU_TOPVAR,NET80211_CFLAGS)"; \
			G="$(call XNU_TOPVAR,LKPI80211_CFLAGS)"; ND="$(call XNU_TOPVAR,N80211_DIR)"; \
				for f in $(call XNU_TOPVAR,MT76_SRCS); do (cd $(TOP) && cco $$O/mt76/$${f%.c}.o $(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$L \
				-Isrc/mt76/mt7925 -DKBUILD_MODNAME="\"$$(basename $$f .c)\"" src/mt76/$$f); done; \
			for f in $(call XNU_TOPVAR,LKPI_SRCS); do (cd $(TOP) && cco $$O/lkpi/$$(basename $$f .c).o $(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$L \
				-Isrc/mt76/mt7925 -DKBUILD_MODNAME='"lkpi"' $$f); done; \
			for f in $(call XNU_TOPVAR,NET80211_SRCS); do (cd $(TOP) && cco $$O/net80211/$${f%.c}.o $(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$N \
				-DKBUILD_MODNAME='"net80211"' $$ND/$$f); done; \
			for f in $(call XNU_TOPVAR,NET80211_EXTRA_SRCS) $(call XNU_TOPVAR,BSDKPI_SRCS); do (cd $(TOP) && \
				cco $$O/bsdkpi/$$(echo $$f | tr / _ | sed 's/\.c$$//').o $(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$N -DKBUILD_MODNAME='"bsdkpi"' $$f); done; \
			for f in $(call XNU_TOPVAR,LKPI80211_SRCS); do (cd $(TOP) && cco $$O/lkpi80211/$$(basename $$f .c).o $(XNU_CC) -target $$T $(XNU_STACK_FLAGS) $$G \
				-DKBUILD_MODNAME='"lkpi80211"' $$f); done; \
		else \
			for f in $(MT76_SRCS); do cco $$O/mt76/$${f%.c}.o $(XNU_CC) -target $$T $(CFLAGS) \
				-DKBUILD_MODNAME="\"$$(basename $$f .c)\"" $(MT76_DIR)/$$f; done; \
			for f in $(LKPI_SRCS); do cco $$O/lkpi/$$(basename $$f .c).o $(XNU_CC) -target $$T $(CFLAGS) \
				-DKBUILD_MODNAME='"lkpi"' $$f; done; \
		fi; \
		[ ! -f $(XNU_CHECK_DIR)/ofail ] || exit 1; \
		echo "== $$a ($(STACK) set): $$(find $$O -name '*.o' | wc -l) objects"; \
		sh $(TOP)/scripts/xnu-kext-symbols.sh $(XNU_HDRS)/KPI/$$kpi Info.plist $$(find $$O -name '*.o' | sort) || exit 1; \
	done

# Loading an unsigned kext needs SIP (csrutil) relaxed and, on macOS 11+,
# user approval of the kext in System Settings > Privacy & Security followed
# by a reboot (kmutil stages into the auxiliary kext collection).  See README.md.
load: kext
	sudo chown -R root:wheel $(KEXT)
	sudo kmutil load -p $(KEXT)

unload:
	sudo kmutil unload -b $(BUNDLE_ID)

clean:
	rm -rf $(BUILD)
