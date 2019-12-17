# Nucleo F412ZG

This is a simpler EC example for the ST Nucleo F412ZG
development board.

# Quick Start

The Nucleo dev boards have lots of developer friendly features,
like an in-circuit debugger/programmer/UART-bridge, programmable
LEDs, and a button, to name a few.

The built-in debugger can be connected to using a Micro USB cable.
It provides three great interfaces to the host.
1. Mass storage interface for drag-drop programming
2. Full ST-Link in-circuit debugger
3. UART bridge for logs/consoles

We will use a few of these interfaces below to program and interact
with out Nucleo dev board.

## Build

```bash
make BOARD=nucleo-f412zg -j
```

## Program

The easiest way to flash the Nucleo board is to Copy-Paste/Drag-Drop
the firmware image onto the exposed mass storage drive.

Open a file browser and `Copy` the file in `build/nucleo-f412zg/ec.bin`.
Now, find the removable storage that the Nucleo device has presented,
and `Paste` the file into the directory.

## Interact

After the Nucelo finishes programming, you can open the EC console.
On GNU/Linux, this is mapped to `/dev/ttyACM0`.

Install `minicom` and issue the following command:

```bash
minicom -D/dev/ttyACM0
```

# Unit Testing

A fun EC feature is that unit tests can be run on-device.

This is made possible by an alternative build rule that generates a
test image per unit test. These test images use a unit test specific taskset
and console command to trigger them.

## Create

To enable an existing unit test, add it to the [build.mk](build.mk)'s
`test-list-y` variable.

See the main [README.md](/README.md) on how to write a new unit test.

## Build

To build all unit test images for this board, run the following command:

```bash
make BOARD=nucleo-f412zg tests
```

You can build a specific unit test image by changing `tests` to `test-aes`,
for the `aes` unit test.

## Flash

Copy/paste the `build/nucleo-f412zg/${TEST}/${TEST}.bin` file to the
Nucleo's mass storage drive, where `${TEST}` is the name of the unit test,
like `aes`.

## Run

1. Connect to UART console
   ```bash
   minicom -D/dev/ttyACM0
   ```
2. Run the `runtest` command