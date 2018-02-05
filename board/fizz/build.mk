# -*- makefile -*-
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_VARIANT:=npcx5m6g

board-y=board.o
board-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
board-y+=led.o

brd_ver = 0

# Build CBI blob for one model
# $1: Prefix for output files
# $2: OEM ID
# $3: SKU ID
make_cbi = $(out)/util/cbi-util --create $(1)_$(3).bin \
	--board_version $(brd_ver) --oem_id $(2) --sku_id $(3) --size 256;

# Build CBI blobs for all SKU IDs
# $1: OEM ID
make_all_cbi = $(foreach s,$(sku_ids),$(call make_cbi,$(out)/$@,$(1),$(s)))

cbi_kench: sku_ids = 0 1 2 3 4 5 6
cbi_kench: $(out)/util/cbi-util
	$(call make_all_cbi, 0)

cbi_teemo: sku_ids = 0 1 4 5
cbi_teemo: $(out)/util/cbi-util
	$(call make_all_cbi, 1)

cbi_sion: sku_ids = 0 1 2 3 4 5 6
cbi_sion: $(out)/util/cbi-util
	$(call make_all_cbi, 2)

PROJECT_EXTRA += cbi_kench cbi_teemo cbi_sion