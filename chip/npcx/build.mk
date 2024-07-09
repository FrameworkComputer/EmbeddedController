# -*- makefile -*-
# Copyright 2014 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NPCX chip specific files build
#

# NPCX SoC has a Cortex-M4F ARM core
CORE:=cortex-m
# Allow the full Cortex-M4 instruction set
CFLAGS_CPU+=-mcpu=cortex-m4

# Disable overlapping section warning that linker emits due to NPCX_RO_HEADER.
LDFLAGS_EXTRA+=-Wl,--no-check-sections

# Assign default CHIP_FAMILY as npcx5 for old boards used npcx5 series
ifeq ($(CHIP_FAMILY),)
CHIP_FAMILY:=npcx5
endif

# Required chip modules
chip-y=header.o clock.o gpio.o hwtimer.o system.o uart.o uartn.o sib.o
chip-y+=rom_chip.o
chip-y+=system-$(CHIP_FAMILY).o
chip-y+=gpio-$(CHIP_FAMILY).o

# Optional chip modules
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_AUDIO_CODEC)+=apm.o wov.o
chip-$(CONFIG_AUDIO_CODEC_DMIC)+=audio_codec_dmic.o
chip-$(CONFIG_AUDIO_CODEC_I2S_RX)+=audio_codec_i2s_rx.o
chip-$(CONFIG_FANS)+=fan.o
chip-$(CONFIG_FLASH_PHYSICAL)+=flash.o
chip-$(CONFIG_I2C)+=i2c.o i2c-$(CHIP_FAMILY).o
chip-$(CONFIG_HOSTCMD_X86)+=lpc.o
chip-$(CONFIG_HOST_INTERFACE_ESPI)+=espi.o
chip-$(CONFIG_PECI)+=peci.o
chip-$(CONFIG_HOST_INTERFACE_SHI)+=shi.o
chip-$(CONFIG_CEC_BITBANG)+=cec_bitbang.o
chip-$(CONFIG_OTP_KEY)+=otp_key.o
# pwm functions are implemented with the fan functions
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_SPI)+=spi.o
chip-$(CONFIG_RNG)+=trng.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
ifdef CONFIG_MPU
chip-$(CONFIG_RAM_LOCK)+=ram_lock.o
endif
ifndef CONFIG_KEYBOARD_DISCRETE
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o
endif

chip-$(CONFIG_PS2)+=ps2.o
# Only npcx9 or later chip family can support LCT module
ifneq ($(CHIP_FAMILY),$(filter $(CHIP_FAMILY),npcx5 npcx7))
chip-y+=lct.o
chip-y+=uartn_dma.o
endif

chip-$(CONFIG_SHA256_HW_ACCELERATE)+=sha256_chip.o

chip-$(CONFIG_USART_HOST_COMMAND)+=uart_host_command.o

ifneq (,$(filter y,$(CONFIG_FINGERPRINT_MCU) $(CONFIG_HOST_INTERFACE_SHI)))
chip-y+=host_command_common.o
endif

# spi monitor program fw for openocd and UUT(UART Update Tool)
npcx-monitor-fw=chip/npcx/spiflashfw/npcx_monitor
npcx-monitor-fw-bin=${out}/$(npcx-monitor-fw).bin
PROJECT_EXTRA+=${npcx-monitor-fw-bin}
# Monitor header is only used for UUT which is not supported on npcx5.
ifneq "$(CHIP_FAMILY)" "npcx5"
npcx-monitor-hdr=chip/npcx/spiflashfw/monitor_hdr
npcx-monitor-hdr-ro-bin=${out}/$(npcx-monitor-hdr)_ro.bin
npcx-monitor-hdr-rw-bin=${out}/$(npcx-monitor-hdr)_rw.bin
PROJECT_EXTRA+=${npcx-monitor-hdr-ro-bin} ${npcx-monitor-hdr-rw-bin}
endif

# ECST tool is for filling the header used by booter of npcx EC
show_esct_cmd=$(if $(V),,echo '  ECST   ' $(subst $(out)/,,$@) ; )

# Get the firmware length from the mapfile.  This can differ from the file
# size when the CONFIG_CHIP_INIT_ROM_REGION is used. Note that the -fwlen
# parameter for the ecst utility must be in hex.
cmd_fwlen=$(shell awk '\
  /__flash_used/ {flash_used = strtonum("0x" $$1)} \
  END {printf ("%x", flash_used)}' $(1))

# ECST options for header
bld_ecst=${out}/util/ecst -chip $(CHIP_VARIANT) \
	-usearmrst -mode bt -ph -i $(1) -o $(2) -nohcrc -nofcrc -flashsize 8 \
	-fwlen $(call cmd_fwlen, $(patsubst %.flat,%.smap,$(2))) \
	-spimaxclk 50 -spireadmode dual 1> /dev/null

# Replace original one with the flat file including header
moveflat=mv -f $(1) $(2)

# Commands for ECST
cmd_ecst=$(show_esct_cmd)$(call moveflat,$@,$@.tmp);$(call bld_ecst,$@.tmp,$@)

# Commands to append npcx header in ec.RO.flat
cmd_org_ec_elf_to_flat = $(OBJCOPY) --set-section-flags .roshared=share \
                         -O binary $(patsubst %.flat,%.elf,$@) $@
cmd_npcx_ro_elf_to_flat=$(cmd_org_ec_elf_to_flat);$(cmd_ecst)
cmd_ec_elf_to_flat = $(if $(filter %.RO.flat, $@), \
                     $(cmd_npcx_ro_elf_to_flat), $(cmd_org_ec_elf_to_flat) )
