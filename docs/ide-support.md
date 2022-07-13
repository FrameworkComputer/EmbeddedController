# IDE Support

This document explains how to configure IDEs to better support the EC codebase.

[TOC]

## Odd File Types

EC uses a few odd file types/names. Some are included from other header files
and used to generate data structures, thus it is important for your IDE to index
them.

Patterns                                              | Vague Type
----------------------------------------------------- | ----------
`README.*`                                            | Text
`Makefile.rules`, `Makefile.toolchain`                | Makefile
`gpio.wrap`                                           | C Header
`gpio.inc`                                            | C Header
`*.tasklist`, `*.irqlist`, `*.mocklist`, `*.testlist` | C Header

## IDE Configuration Primitives

EC firmware presents some unique challenges because it is designed to support a
number of different MCUs and board configurations, each of which is split across
separate RO (Read-Only) and RW (Read-Write) applications. For this reason, you
must specify the specific board/image pair when requesting defines and includes.

Command                                      | Description
-------------------------------------------- | ------------------------------
`make print-defines BOARD=$BOARD BLD=RW/RO`  | List compiler injected defines
`make print-includes BOARD=$BOARD BLD=RW/RO` | List compiler include paths

## VSCode

You can use the `ide-config.sh` tool to generate a VSCode configuration that
includes selectable sub-configurations for every board/image pair.

1.  From the root `ec` directory, do the following:

    ```bash
    mkdir -p .vscode
    ./util/ide-config.sh vscode all:RW all:RO | tee .vscode/c_cpp_properties.json
    ```

2.  Open VSCode and navigate to some C source file.

3.  Run `C/C++ Reset IntelliSense Database` from the `Ctrl-Shift-P` menu

4.  Select the config in the bottom right, next to the `Select Language Mode`.
    You will only see this option when a C/C++ file is open. Additionally, you
    can select a configuration by pressing `Ctrl-Shift-P` and selecting the
    `C/C++ Select a Configuration...` option.

5.  Add the EC specific file associations and style settings. Do the following
    to copy the default settings to `.vscode/settings.json`:

    ```bash
    cp .vscode/settings.json.default .vscode/settings.json
    ```

## VSCode CrOS IDE

CrOS IDE is a VSCode extension to enable code completion and navigation for
ChromeOS source files.

Support for `platform/ec` is not available out of the box (yet), but can be
manually enabled following these steps.

### Prerequisites

1.  Install CrOS IDE following the [quickstart guide]
1.  Install `bear`, a utility to generate the compilation database

    ```
    (chroot) $ sudo emerge bear
    ```

[quickstart guide]: https://chromium.googlesource.com/chromiumos/chromite/+/main/ide_tooling/docs/quickstart.md

### Configure EC Board

1.  Build the image and create new compile_commands.json using `bear`

    ```
    (chroot) $ cd ~/chromiumos/src/platform/ec
    export BOARD=bloonchipper
    make clean BOARD=${BOARD}
    bear make -j BOARD=${BOARD}
    mv compile_commands.json compile_commands_inside_chroot.json
    ```

1.  Generate the new compile_commands.json (use the absolute path outside chroot
    as first argument)

    ```bash
    (chroot) $ cd ~/chromiumos/chromite/ide_tooling/scripts
    python compdb_no_chroot.py /home/${USER}/chromiumos \
      < ~/chromiumos/src/platform/ec/compile_commands_inside_chroot.json \
      > ~/chromiumos/src/platform/ec/compile_commands.json
    ```
    The command will overwrite the file `compile_commands.json`, if it already
    exists.
