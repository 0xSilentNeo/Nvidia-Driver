# SPDX-License-Identifier: MIT
#
# Builds MT7925.kext without Xcode (command line tools only).
#
#   make                 build build/MT7925.kext for ARCHS (default x86_64)
#   make ARCHS="x86_64 arm64e"   fat binary (arm64e is what the Apple Silicon
#                        kernel loads; keep it compiling, not yet tested)
#   make check-syntax    clang -fsyntax-only on the C++ files (needs an SDK)
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
LKPI_SRCS  := $(wildcard $(TOP)/src/linuxkpi/src/*.c)
# src/lkpi80211 is still FreeBSD-flavoured (sys/*.h); opt in once ported.
LKPI80211_SRCS ?=

CONFIG_DEFS ?= -DCONFIG_PCI=1 -DCONFIG_MT76_CONNAC_LIB=1 -DCONFIG_MT792x_LIB=1 \
               -DCONFIG_MT7925_COMMON=1 -DCONFIG_MT7925E=1 -DCONFIG_ACPI=1 \
               -DCONFIG_ARCH_DMA_ADDR_T_64BIT=1 -DCONFIG_IPV6=1
LKPI_CFLAGS := -std=gnu11 -D__KERNEL__ -DLKPI_VERSION=1 \
               -I$(KPI_INC) -I$(MT76_DIR) -include linux/kconfig.h $(CONFIG_DEFS) \
               -Wall -Wno-unused-parameter -Wno-gnu -Wno-missing-field-initializers \
               -Wno-sign-compare -Wno-address-of-packed-member -Wno-pointer-sign \
               -Wno-unused-function -Wno-microsoft-anon-tag -Wno-c11-extensions \
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

CXXFLAGS := $(COMMON_FLAGS) -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions \
            -I$(KPI_INC) -Wno-unused-parameter
# C: the Linux sources and the C LinuxKPI layer.
CFLAGS   := $(COMMON_FLAGS) $(LKPI_CFLAGS) -I$(MT76_DIR)/mt7925 -Wno-unused-but-set-variable
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

.PHONY: all kext check-sdk check-syntax bundle load unload clean fwtable

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
