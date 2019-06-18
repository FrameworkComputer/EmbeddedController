# -*- makefile -*-
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#

CORE:=cortex-m
CFLAGS_CPU+=-march=armv7-m -mcpu=cortex-m3

ifeq ($(CONFIG_DCRYPTO),y)
INCLUDE_ROOT := $(abspath ./include)
CRYPTOCLIB := $(realpath ../../third_party/cryptoc)
CPPFLAGS += -I$(abspath .)
CPPFLAGS += -I$(abspath ./builtin)
CPPFLAGS += -I$(abspath ./chip/$(CHIP))
CPPFLAGS += -I$(INCLUDE_ROOT)
CPPFLAGS += -I$(CRYPTOCLIB)/include
endif

# Required chip modules
chip-y = clock.o gpio.o hwtimer.o pre_init.o system.o
chip-$(CONFIG_BOARD_ID_SUPPORT) += board_id.o
chip-$(CONFIG_SN_BITS_SUPPORT) += sn_bits.o
ifeq ($(CONFIG_POLLING_UART),y)
chip-y += polling_uart.o
else
chip-y += uart.o
chip-y += uartn.o
chip-$(CONFIG_UART_BITBANG)+= uart_bitbang.o
endif # undef CONFIG_POLLING_UART

chip-$(CONFIG_DCRYPTO)+= crypto_api.o

chip-$(CONFIG_DCRYPTO)+= dcrypto/aes.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/aes_cmac.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/app_cipher.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/app_key.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/bn.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/dcrypto_bn.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/dcrypto_p256.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/compare.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/dcrypto_runtime.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/gcm.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/hkdf.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/hmac.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/hmac_drbg.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/key_ladder.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/p256.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/p256_ec.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/p256_ecies.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/rsa.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha1.o
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha256.o
ifeq ($(CONFIG_UPTO_SHA512),y)
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha384.o
ifeq ($(CONFIG_DCRYPTO_SHA512),y)
chip-$(CONFIG_DCRYPTO)+= dcrypto/dcrypto_sha512.o
else
chip-$(CONFIG_DCRYPTO)+= dcrypto/sha512.o
endif
endif
chip-$(CONFIG_DCRYPTO)+= dcrypto/x509.o

chip-$(CONFIG_SPI_MASTER)+=spi_master.o

chip-y+= jitter.o
chip-y+= pmu.o
chip-y+= trng.o
chip-y+= runlevel.o
chip-$(CONFIG_CCD_ITE_PROGRAMMING)+= ite_flash.o
chip-$(CONFIG_CCD_ITE_PROGRAMMING)+= ite_sync.o
chip-$(CONFIG_ENABLE_H1_ALERTS)+= alerts.o
chip-$(CONFIG_USB_FW_UPDATE)+= usb_upgrade.o
chip-$(CONFIG_NON_HC_FW_UPDATE)+= upgrade_fw.o post_reset.o upgrade.o
chip-$(CONFIG_SPS)+= sps.o
chip-$(CONFIG_TPM_SPS)+=sps_tpm.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o

chip-$(CONFIG_USB)+=usb.o usb_endpoints.o
chip-$(CONFIG_USB_CONSOLE)+=usb_console.o
chip-$(CONFIG_USB_HID_KEYBOARD)+=usb_hid_keyboard.o
chip-$(CONFIG_USB_BLOB)+=blob.o
chip-$(CONFIG_USB_SPI)+=usb_spi.o
chip-$(CONFIG_RDD)+=rdd.o
chip-$(CONFIG_RBOX)+=rbox.o
chip-$(CONFIG_STREAM_USB)+=usb-stream.o
chip-$(CONFIG_STREAM_USART)+=usart.o
chip-$(CONFIG_I2C_MASTER)+= i2cm.o
chip-$(CONFIG_I2C_SLAVE)+= i2cs.o

chip-$(CONFIG_LOW_POWER_IDLE)+=idle.o

chip-$(CONFIG_FLASH_PHYSICAL) += flash.o
dirs-y += chip/g/dcrypto

ifneq ($(CONFIG_CUSTOMIZED_RO),)
custom-ro_objs-y  = chip/g/clock.o
custom-ro_objs-y += chip/g/dcrypto/sha256.o
custom-ro_objs-y += chip/g/loader/key_ladder.o
custom-ro_objs-y += chip/g/loader/debug_printf.o
custom-ro_objs-y += chip/g/loader/launch.o
custom-ro_objs-y += chip/g/loader/main.o
custom-ro_objs-y += chip/g/loader/rom_flash.o
custom-ro_objs-y += chip/g/loader/setup.o
custom-ro_objs-y += chip/g/loader/verify.o
custom-ro_objs-y += chip/g/pmu.o
custom-ro_objs-y += chip/g/system.o
custom-ro_objs-y += chip/g/trng.o
custom-ro_objs-y += chip/g/uart.o
custom-ro_objs-y += chip/g/uartn.o
custom-ro_objs-y += common/printf.o
custom-ro_objs-y += common/util.o
custom-ro_objs-y += core/cortex-m/init.o
custom-ro_objs-y += core/cortex-m/vecttable.o
custom-ro_objs-y += core/cortex-m/panic.o
dirs-y += chip/g/dcrypto
dirs-y += chip/g/loader
endif

# Do not build any test on chip/g
test-list-y=

%.hex: %.flat

ifneq ($(CONFIG_RW_B),)
$(out)/$(PROJECT).obj: $(out)/RW/ec.RW_B.flat
endif

CR50_OPTS=

ifneq ($(CR50_DEV),)
CPPFLAGS += -DCR50_DEV=$(CR50_DEV)
CR50_OPTS+=CR50_DEV
endif

ifneq ($(CR50_SQA),)
CPPFLAGS += -DCR50_SQA=$(CR50_SQA)
CR50_OPTS+=CR50_SQA
endif

# Test if more than one Cr50 build option is specified
ifneq ($(wordlist 2,3,$(CR50_OPTS)),)
$(error Incompatible CR50 build options specified: $(CR50_OPTS))
endif

MANIFEST := util/signer/ec_RW-manifest-dev.json
CR50_RO_KEY ?= rom-testkey-A.pem

# Make sure signing happens only when the signer is available.
REAL_SIGNER = /usr/bin/cr50-codesigner
ifneq ($(wildcard $(REAL_SIGNER)),)
SIGNED_IMAGES = 1
SIGNER := $(REAL_SIGNER)
endif

ifeq ($(CHIP_MK_INCLUDED_ONCE),)

CHIP_MK_INCLUDED_ONCE := 1
# We'll have to tweak the manifest no matter what, but different ways
# depending on the way the image is built.
SIGNER_MANIFEST := $(shell mktemp /tmp/h1.signer.XXXXXX)
RW_SIGNER_EXTRAS += -j $(SIGNER_MANIFEST) -x util/signer/fuses.xml

ifneq ($(CR50_SWAP_RMA_KEYS),)

ifneq ($(CONFIG_RMA_AUTH_USE_P256),)
CURVE := p256
else
CURVE := x25519
endif

RMA_KEY_BASE := board/$(BOARD)/rma_key_blob.$(CURVE)
RW_SIGNER_EXTRAS += --swap $(RMA_KEY_BASE).test,$(RMA_KEY_BASE).prod
endif

endif

ifeq ($(H1_DEVIDS),)
# Signing with non-secret test key.
CR50_RW_KEY = loader-testkey-A.pem
# Make sure manifset Key ID field matches the actual key.
DUM := $(shell sed 's/1187158727/764428053/' $(MANIFEST) > $(SIGNER_MANIFEST))
else
# The private key comes from the sighing fob.
CR50_RW_KEY = cr50_rom0-dev-blsign.pem.pub

ifneq ($(CHIP_MK_INCLUDED_ONCE),)
#
# When building a node locked cr50 image for an H1 device with prod RO, the
# manifest needs to be modifed to include the device ID of the chip the image
# is built for.
#
# The device ID consists of two 32 bit numbers which can be retrieved by
# running the 'sysinfo' command on the cr50 console. These two numbers
# need to be spliced into the signer manifest after the '"fuses": {' line
# for the signer to pick them up. Pass the numbers on the make command line
# like this:
#
# H1_DEVIDS='<num 1> <num 2>' make ...
#
ifneq ($(CR50_DEV),)

#
# When building a debug image, we don't want rollback protection to be in the
# way - a debug image, which is guaranteed to be node locked should run on any
# H1, whatever its info mask state is. The awk script below clears out the
# info {} section of the manifest.
#
DUMMY := $(shell /usr/bin/awk 'BEGIN {skip = 0}; \
	/^},/ {skip = 0}; \
	{if (!skip) {print };} \
	/\"info\": {/ {skip = 1};' $(MANIFEST) > $(SIGNER_MANIFEST))
else
DUMMY := $(shell /bin/cp $(MANIFEST) $(SIGNER_MANIFEST))
endif
REPLACEMENT := $(shell printf \
	'\\n    \\"DEV_ID0\\": %s,\\n    \\"DEV_ID1\\": %s,' $(H1_DEVIDS))
NODE_JSON :=  $(shell sed -i \
	"s/\"fuses\": {/\"fuses\": {$(REPLACEMENT)/" $(SIGNER_MANIFEST))

endif  # CHIP_MK_INCLUDED_ONCE defined
endif  # H1_DEVIDS defined


# This file is included twice by the Makefile, once to determine the CHIP info
# # and then again after defining all the CONFIG_ and HAS_TASK variables. We use
# # a guard so that recipe definitions and variable extensions only happen the
# # second time.
ifneq ($(CHIP_MK_INCLUDED_ONCE),)
$(out)/RW/ec.RW_B.flat: $(out)/RW/ec.RW.flat
$(out)/RW/ec.RW.flat $(out)/RW/ec.RW_B.flat: SIGNER_EXTRAS = $(RW_SIGNER_EXTRAS)

ifeq ($(CONFIG_DCRYPTO),y)

CRYPTOC_OBJS = $(shell find $(out)/cryptoc -name '*.o')
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: LDFLAGS_EXTRA += $(CRYPTOC_OBJS)
$(out)/RW/ec.RW.elf $(out)/RW/ec.RW_B.elf: cryptoc_objs


# Force the external build each time, so it can look for changed sources.
.PHONY: cryptoc_objs
cryptoc_objs:
	$(MAKE) obj=$(realpath $(out))/cryptoc SUPPORT_UNALIGNED=1 \
		CONFIG_UPTO_SHA512=$(CONFIG_UPTO_SHA512) -C $(CRYPTOCLIB) objs
endif   # end CONFIG_DCRYPTO

endif   # CHIP_MK_INCLUDED_ONCE is nonempty
