# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CORE_RW_OUT:=$(out)/RW/core/$(CORE)
CORE_RO_OUT:=$(out)/RO/core/$(CORE)

cmd_asm_define_to_h = grep ASM_DEFINE $< \
	| sed 's/.*__ASM_DEFINE__\s\(.*\)\s\#\(.*\)"/\#define \1 \2/g' > $@

$(CORE_RO_OUT)/asm_offsets.h:$(CORE_RO_OUT)/asm_offsets.s
	$(call quiet,asm_define_to_h,     )
$(CORE_RW_OUT)/asm_offsets.h:$(CORE_RW_OUT)/asm_offsets.s
	$(call quiet,asm_define_to_h,     )

$(CORE_RW_OUT)/asm_offsets.s: core/$(CORE)/asm_offsets.c
$(CORE_RW_OUT)/asm_offsets.h: $(CORE_RW_OUT)/asm_offsets.s

$(CORE_RO_OUT)/asm_offsets.s: core/$(CORE)/asm_offsets.c
$(CORE_RO_OUT)/asm_offsets.h: $(CORE_RO_OUT)/asm_offsets.s
