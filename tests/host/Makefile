# SPDX-License-Identifier: MIT
# Host (Linux/macOS user space) tests for the C half of the LinuxKPI layer.
#
#   make -C tests/host run
#
# test_core    : header-only primitives (lists, bitops, bitfields, byteorder)
# test_runtime : src/linuxkpi/src/*.c against the libc os.h backend
#
CLANG   ?= clang
ROOT    := ../..
KPI_INC := $(ROOT)/src/linuxkpi/include
MT76    := $(ROOT)/src/mt76
BIN     := bin

# Same front-end flags as the kext build (see the top-level Makefile), plus
# host sanitizers.  The mt7925 NAN stubs need the driver headers and are not
# linked into the tests.
LKPI_SRCS := $(filter-out $(ROOT)/src/linuxkpi/src/mt7925_nan_stubs.c $(ROOT)/src/linuxkpi/src/modules.c, \
             $(wildcard $(ROOT)/src/linuxkpi/src/*.c))

CFLAGS  := -std=gnu11 -g -O1 -D__KERNEL__ -DLKPI_HOST_TEST=1 \
           -I$(KPI_INC) -I$(MT76) -I. -include linux/kconfig.h \
           -DCONFIG_PCI=1 -DCONFIG_ARCH_DMA_ADDR_T_64BIT=1 -DKBUILD_MODNAME='"test"' \
           -Wall -Wno-unused-parameter -Wno-gnu -Wno-missing-field-initializers \
           -Wno-sign-compare -Wno-address-of-packed-member -Wno-pointer-sign \
           -Wno-unused-function -Wno-microsoft-anon-tag -Wno-c11-extensions \
           -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
           -fno-omit-frame-pointer
LDFLAGS := -lpthread

# SANITIZE=1 adds ASan/UBSan when the compiler runtime is installed.
ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined
endif

.PHONY: all run clean

all: $(BIN)/test_core $(BIN)/test_runtime

$(BIN):
	mkdir -p $(BIN)

$(BIN)/test_core: test_core.c os_host.c | $(BIN)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN)/test_runtime: test_runtime.c os_host.c test_modules_stub.c $(LKPI_SRCS) | $(BIN)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: all
	./$(BIN)/test_core
	./$(BIN)/test_runtime

clean:
	rm -rf $(BIN)
