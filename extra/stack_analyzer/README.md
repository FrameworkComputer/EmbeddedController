Stack Size Analysis Tool for EC Firmware
========================================

This tool does static analysis on EC firmwares to get the maximum stack usage of
each function and task. The maximum stack usage of a function includes the stack
used by itself and the functions it calls.

Usage
-----

Make sure the firmware of your target board has been built.

In `src/platform/ec`, run
```
make BOARD=${BOARD} SECTION=${SECTION} ANNOTATION=${ANNOTATION} analyzestack
```
The `${SECTION}` can be `RO` or `RW`. The `${ANNOTATION}` is a optional
annotation file, see the example_annotation.yaml.
