# Hammer care and feeding

Original: [go/hammercare](http://go/hammercare)

Last updated: 2021-03-18

[TOC]

## Servo

### Start servod

```
cros_sdk --no-ns-pid
sudo servod --port=9000 -b hammer -c hammer.xml
```

### UART console

The simplest solution for most people is to use the `dut-console` script.

First, add this line into your .bashrc (or other shell init script; needed once
only):
``` bash
alias dut-console="~/chromiumos/src/platform/dev/contrib/dut-console"
```

Then simply run `dut-console -c ec`. `dut-console` uses `cu` under the hood, and
works like ssh - to leave, press `<ENTER> <~> <.> <ENTER>`.


``` bash
src/platform/dev/contrib/dut-console -p 9000 -c ec
```

## Build EC

(Inside chroot)
``` bash
cd ~/trunk/src/platform/ec
make BOARD=<BOARD> -j
```

## Flash EC

### Prerequisites

#### Find the USB VID:PID of the device

USB VID:PID is listed in [hammer/variants.h](../board/hammer/variants.h).
Many scripts below requires correct PID to work.

#### Stop hammerd

Remove rootfs verification:
``` bash
/usr/share/vboot/bin/make_dev_ssd.sh --remove_rootfs_verification --force
```

Reboot the DUT then rename hammerd
``` bash
mv /usr/bin/hammerd /usr/bin/hammerd.bak
```

### Hammer connected to Chromebook, flash via USB

(Inside chroot) Copy-paste the script below to a file named
"flash_hammer.usbremote", run
``` bash
bash flash_hammer.usbremote <BOARD> <VID:PID> <IP> [ro]
```

``` bash
#!/bin/bash
# flash_hammer.usbremote
set -x -e

BOARD=$1
ID=$2
IP=$3
EXTRA="-d $ID"

ssh $IP sh -c "'rm -f /usr/local/ec.bin'"
scp ~/trunk/src/platform/ec/build/${BOARD}/ec.bin $IP:/usr/local/ec.bin

if [ "$4" = 'ro' ]; then
   ssh $IP sh -x -c "'usb_updater2 $EXTRA -j;
   sleep 1.0;
   usb_updater2 $EXTRA /usr/local/ec.bin;
   sleep 0.5;
   usb_updater2 $EXTRA -s;
   usb_updater2 $EXTRA /usr/local/ec.bin'"
else
   ssh $IP sh -x -c "'usb_updater2 $EXTRA -w;
   usb_updater2 $EXTRA -r; sleep 0.5;
   usb_updater2 $EXTRA -s;
   usb_updater2 $EXTRA /usr/local/ec.bin'"
fi
```

### Hammer connected to Chromebook, flash via servo

(Inside chroot) Copy-paste the script below to a file named "flash_hammer",
run `bash flash_hammer <IP> </path/to/ec.bin>`

``` bash
#!/bin/bash
# Recommended to use a USB 3.0 Ethernet adapter for this to work, otherwise the
# network on the DUT will temporarily go down when the root USB hub is taken
# down.

set -e
IP=$1
shift

# USB 2.0 root hub
USBID="usb1"

set -x

# unbind, then rebind, the root hub (in the mean time, we'll start programming)
ssh $IP sh -c "'echo $USBID > /sys/bus/usb/drivers/usb/unbind; sleep 3; echo $USBID > /sys/bus/usb/drivers/usb/bind'" &

util/flash_ec --board=hammer --port 9000 --image "$@"
```

### Hammer connected via servo only

Do not connect hammer to Chromebook in this case, or at least make sure
Chromebook is either suspended (S3) or off (when put into programming mode,
STM32 always prefers USB interface when available)

For Servo V2:

``` bash
dut-control -p 9000 spi1_vref:pp3300 spi1_buf_en:on spi1_buf_on_flex_en:on
util/flash_ec --board=hammer --port=9000 [--image=/path/to/ec.bin]

# To disable power from servo to Hammer
dut-control -p 9000 spi1_vref:off spi1_buf_en:off spi1_buf_on_flex_en:off
```

For Servo Micro (there is only one buffer in the power delivery path,
so don't include the spi1_buf_on_flex_en control):

``` bash
dut-control -p 9000 spi1_vref:pp3300 spi1_buf_en:on
util/flash_ec --board=hammer --port=9000 [--image=/path/to/ec.bin]

# To disable power from servo to Hammer
dut-control -p 9000 spi1_vref:off spi1_buf_en:off
```

### Hammer connected via POGO-PIN-USB to Linux

So this is very similar to Hammer connected to poppy, flash via USB, but you
are directly running commands on the machine connected to Hammer, so you don’t
need to SSH to it.

``` bash
#!/bin/bash
# flash_hammer.usblocal

EXTRA=
EC=build/${BOARD:-hammer}/ec.bin
UPDATER=usb_updater2

if [ -n "$ID" ]; then
   EXTRA="-d $ID"
fi

if [ "$1" = 'ro' ]; then
   "${UPDATER}" $EXTRA -j;
   sleep 1.0;
   "${UPDATER}" $EXTRA "${EC}";
   sleep 1.0;
   "${UPDATER}" $EXTRA -s;
   "${UPDATER}" $EXTRA "${EC}";
else
   "${UPDATER}" $EXTRA -w;
   "${UPDATER}" $EXTRA -r;
   sleep 1.0;
   "${UPDATER}" $EXTRA -s;
   "${UPDATER}" $EXTRA "${EC}";
fi

# To use this script: BOARD=<BOARD> ID=<VID:PID> ./flash_hammer.usblocal [ro]
```

## Update touchpad firmware

(Inside DUT)
``` bash
usb_updater2 --tp_update <FILE> --device=<VID:PID>
```
or
``` bash
ec_touchpad_updater -p <PID> <FILE>
```
