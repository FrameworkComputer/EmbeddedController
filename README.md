# Framework Laptop Embedded Controller (EC)

This repository contains the source code used for<br>
the **Framework Laptop Embedded Controller**.

---

## Warning

The Embedded Controller on your Framework Laptop handles<br>
low level functions, including ***power sequencing*** the system.

Modifying the EC code can cause your system ***to not power on***, <br>
***boot*** or even ***cause damage*** to the `Mainboard`, `Battery` or other <br>
parts of the system as well as to devices attached to the system.

**Hardware damage caused by EC firmware** <br>
**modifications would not be covered under** <br>
**the Framework Limited Warranty.**

---

## Building

### Environment Configuration

*The Framework Laptop EC code can be built easily* <br>
*outside the Chromium development environment* <br>
*as a standalone project with a minimal set of tools.*

The development tools can be installed with:

#### **[Ubuntu]**

```
sudo apt install gcc-arm-none-eabi libftdi1-dev
```

#### **[Fedora]**

```
sudo dnf install arm-none-eabi-gcc-cs libftdi-devel
```

#### **[Arch Linux]**

```
sudo pacman -S arm-none-eabi-gcc libftdi
```


<br>

## Instructions
*for `Intel 11th Gen Core Processors`.*

The project can be built with:

```
make BOARD=hx20 CROSS_COMPILE=arm-none-eabi-
```

The output artifact is located at:

```
build/hx20/ec.bin
```

It can be flashed to the **EC** **SPI** flash **ROM**.

<br>

### Flashing Configuration

*for `Intel 11th Gen Core Processors`.*

<br>

***Do not erase*** or ***overwrite*** the sectors:
- `0x3C000` ➝ `0x3FFFF`
- `0x79000` ➝ `0x7FFFF`

<br>

Currently the EC only runs from the `ro` region.

```
00000000:00000fff bootsector
00001000:00039fff lfwro
00040000:00078fff rw
```

---

## Controller Information

The EC is a `MEC1521H-B0-I-SZ` `WFBGA144` which has `256kB` of **RAM**.

Most changes are limited to the following folders, <br>
however there are some modifications in common.

```
board/hx20
chip/mchp
```

<br>

### Boot Process

The EC has a **Checksum** and **Header Verification** for it's code image.

Arbitrarily modifying code, for example with a reverse<br>
engineering tool will cause the EC to fail to boot.

Compiling the code from source will correctly generate the<br> required checksum information to allow the EC to boot.

<br>

### Debugging

The EC has debug header in the *upper right corner*<br>
of the **Mainboard** next to the on-board power button.

This is the `10 pin` EC debug connector **JECDB**.

`Pin 1` is nearest the power button. <br>
The EC is configured for `2 Wire SWD`.

| Pin | Description | | Pin | Description |
|:---:|:-----------:| |:---:|:-----------:|
| `1` | `EC_VCC_3.3`| | `6` | `UART_TX`
| `2` | `TDI`       | | `7` | `UART_RX`
| `3` | `TMS`       | | `8` |
| `4` | `CLK`       | | `9` | `EC_RESETI`
| `5` | `TDO`       | |`10` | `GND`

---

## Background

The Framework Laptop EC is based upon <br>
the **[Google Chromium EC repository][Google EC]**.

We upstream common features where they <br>
fit into the design decisions of Chrome OS.

However, there are a number of features and changes <br>
that will be unlikely to be upstreamed because they are <br>
unnecessary for Chrome OS operation or do not fit the <br>
philosophy of Chrome OS.

For example, we implement **Memory Mapped Regions** <br>
that are not used in Chrome OS such as the **UCSI Driver**.

For more information, check **[Google EC Documentation][Google Documentation]**.

<!----------------------------------------------------------------------------->

[Ubuntu]: https://ubuntu.com/
[Fedora]: https://getfedora.org/
[Arch Linux]: https://archlinux.org/

[Google Documentation]: docs/Google%20Controller.md
[Google EC]: https://chromium.googlesource.com/chromiumos/platform/ec
