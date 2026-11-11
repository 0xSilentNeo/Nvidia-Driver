# SPDX-License-Identifier: MIT
# Host (Linux/macOS user space) tests for the C half of the LinuxKPI layer.
#
#   make -C tests/host run
#
# test_core    : header-only primitives (lists, bitops, bitfields, byteorder)
# test_runtime : src/linuxkpi/src/*.c against the libc os.h backend
# test_probe   : the real mt7925 driver probed against the software device
#                model in mt7925_model.c (firmware bring-up included)
#
CLANG   ?= clang
ROOT    := ../..
KPI_INC := $(ROOT)/src/linuxkpi/include
MT76    := $(ROOT)/src/mt76
BIN     := bin

# Same front-end flags as the kext build (see the top-level Makefile), plus
# host sanitizers.  The mt7925 NAN stubs need the driver headers and are not
# linked into the tests.
# test_runtime is LinuxKPI alone: no 802.11 stack (LKPI_NO_NET80211 keeps
# runtime.c from starting bsdkpi), with the recording mac80211 stub.
LKPI_SRCS := $(filter-out $(ROOT)/src/linuxkpi/src/mt7925_nan_stubs.c $(ROOT)/src/linuxkpi/src/modules.c, \
             $(wildcard $(ROOT)/src/linuxkpi/src/*.c))

# -fwrapv -fno-strict-aliasing as in the top-level Makefile (KERNEL_CODEGEN).
CFLAGS  := -std=gnu11 -g -O1 -fwrapv -fno-strict-aliasing -D__KERNEL__ -DLKPI_HOST_TEST=1 \
           -I$(KPI_INC) -I$(ROOT)/src/net80211 -I$(MT76) -I. -include linux/kconfig.h \
           -DCONFIG_PCI=1 -DCONFIG_ARCH_DMA_ADDR_T_64BIT=1 -DKBUILD_MODNAME='"test"' \
           -Wall -Wno-unused-parameter -Wno-gnu -Wno-missing-field-initializers \
           -Wno-sign-compare -Wno-address-of-packed-member -Wno-pointer-sign \
           -Wno-unused-function -Wno-microsoft-anon-tag -Wno-c11-extensions -Wno-c23-extensions \
           -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
           -fno-omit-frame-pointer
LDFLAGS := -lpthread

# SANITIZE=1 adds ASan/UBSan when the compiler runtime is installed.
ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined
endif

.PHONY: all run clean

# The probe test links the complete C program of the kext, host-compiled:
# the real driver objects, LinuxKPI with the real module list, bsdkpi,
# net80211 and the mac80211 -> net80211 glue (src/lkpi80211), and runs
# mt7925_pci_probe() against a modelled fake device, then drives net80211
# (vap create, up, scan) through wlan_host.c.  The source lists and the
# per-layer flags come from the top-level Makefile so that the test builds
# exactly what `make link-check-full` checks.
TOPVAR = $(shell $(MAKE) -s -C $(ROOT) MAC80211_STUB=0 print-$(1))
MT76_SRCS := $(addprefix $(MT76)/, $(call TOPVAR,MT76_SRCS))
PROBE_LKPI_SRCS := $(addprefix $(ROOT)/, $(call TOPVAR,LKPI_SRCS))
NET80211_SRCS := $(addprefix $(ROOT)/$(call TOPVAR,N80211_DIR)/, $(call TOPVAR,NET80211_SRCS)) \
                 $(addprefix $(ROOT)/, $(call TOPVAR,NET80211_EXTRA_SRCS))
BSDKPI_SRCS := $(addprefix $(ROOT)/, $(call TOPVAR,BSDKPI_SRCS))
LKPI80211_SRCS := $(addprefix $(ROOT)/, $(call TOPVAR,LKPI80211_SRCS))

PROBE_CFLAGS := $(CFLAGS) -I$(MT76)/mt7925 -DCONFIG_MT76_CONNAC_LIB=1 -DCONFIG_MT792x_LIB=1 \
                -DCONFIG_MT7925_COMMON=1 -DCONFIG_MT7925E=1 -DCONFIG_ACPI=1 -DCONFIG_IPV6=1 \
                -Wno-unused-variable -Wno-visibility
# FreeBSD side: bsdkpi's headers shadow the host's, so they come first.
BSD_CFLAGS := -I$(ROOT)/src/bsdkpi/include -I$(ROOT)/src/bsdkpi -I$(ROOT)/src/net80211 \
              $(PROBE_CFLAGS) -D_KERNEL=1 -Wno-c23-extensions
GLUE_CFLAGS := $(BSD_CFLAGS) -I$(ROOT)/src/lkpi80211 -include lkpi80211_prelude.h -Wno-format-security

OBJ := $(BIN)/obj
# objs(<prefix>,<sources>): bin/obj/<prefix>_<basename>.o
objs = $(addprefix $(OBJ)/$(1)_,$(notdir $(2:.c=.o)))
# One pattern rule per (prefix, source directory, flags).
define cc_rule
$(OBJ)/$(1)_%.o: $(2)/%.c | $(OBJ)
	$$(CLANG) $(3) -c -o $$@ $$<
endef
$(eval $(call cc_rule,mt76,$(MT76),$$(PROBE_CFLAGS)))
$(eval $(call cc_rule,mt7925,$(MT76)/mt7925,$$(PROBE_CFLAGS)))
$(eval $(call cc_rule,lkpi,$(ROOT)/src/linuxkpi/src,$$(PROBE_CFLAGS)))
$(eval $(call cc_rule,n80211,$(ROOT)/src/net80211/upstream,$$(BSD_CFLAGS)))
$(eval $(call cc_rule,n80211,$(ROOT)/src/net80211,$$(BSD_CFLAGS)))
$(eval $(call cc_rule,bsdkpi,$(ROOT)/src/bsdkpi/src,$$(BSD_CFLAGS)))
$(eval $(call cc_rule,bsdkpi,$(ROOT)/src/bsdkpi/crypto/rijndael,$$(BSD_CFLAGS)))
$(eval $(call cc_rule,lkpi80211,$(ROOT)/src/lkpi80211,$$(GLUE_CFLAGS)))
# wlan_host.c talks to net80211 and sees the glue's private state.
$(OBJ)/test_wlan_host.o: wlan_host.c | $(OBJ)
	$(CLANG) $(GLUE_CFLAGS) -c -o $@ $<
$(eval $(call cc_rule,test,.,$$(PROBE_CFLAGS)))

PROBE_OBJS := $(call objs,test,test_probe.c mt7925_model.c os_host.c wlan_host.c) \
              $(call objs,mt76,$(filter-out $(MT76)/mt7925/%,$(MT76_SRCS))) \
              $(call objs,mt7925,$(filter $(MT76)/mt7925/%,$(MT76_SRCS))) \
              $(call objs,lkpi,$(PROBE_LKPI_SRCS)) \
              $(call objs,n80211,$(NET80211_SRCS)) $(call objs,bsdkpi,$(BSDKPI_SRCS)) \
              $(call objs,lkpi80211,$(LKPI80211_SRCS))
# Headers are not tracked per object: any header change rebuilds everything.
HDRS := $(shell find $(ROOT)/src . -name '*.h')
$(PROBE_OBJS): $(HDRS) Makefile $(ROOT)/Makefile

all: $(BIN)/test_core $(BIN)/test_runtime $(BIN)/test_probe

$(BIN)/test_probe: $(PROBE_OBJS) | $(BIN)
	$(CLANG) $(PROBE_CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ): | $(BIN)
	mkdir -p $(OBJ)

$(BIN):
	mkdir -p $(BIN)

$(BIN)/test_core: test_core.c os_host.c | $(BIN)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN)/test_runtime: test_runtime.c os_host.c test_modules_stub.c $(LKPI_SRCS) | $(BIN)
	$(CLANG) $(CFLAGS) -DLKPI_NO_NET80211=1 -o $@ $^ $(LDFLAGS)

run: all
	./$(BIN)/test_core
	./$(BIN)/test_runtime
	LKPI_FIRMWARE_DIR=$(ROOT)/firmware ./$(BIN)/test_probe

clean:
	rm -rf $(BIN)
