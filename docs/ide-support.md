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
`Makefile.ide`                                        | Makefile
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
    (outside) $ mkdir -p .vscode
    ```

    ```bash
    (chroot) $ ./util/ide-config.sh vscode all:RW all:RO | tee .vscode/c_cpp_properties.json
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
    (outside) $ cp .vscode/settings.json.default .vscode/settings.json
    ```

## VSCode CrOS IDE

CrOS IDE is a VSCode extension to enable code completion and navigation for
ChromeOS source files.

Support for `platform/ec` is not available out of the box (yet), but can be
manually enabled following these steps.

### Prerequisites

Install CrOS IDE following the [quickstart guide]

<!-- mdformat off(b/139308852) -->
*** note
NOTE: CrOS IDE uses the VSCode extension `clangd` for code completion and
navigation. The installation of CrOS IDE disables the built-in
`C/C++ IntelliSense` because it is not compatible with `clangd`.
***
<!-- mdformat on -->

### Configure EC Board

1.  Enter the EC repository:

    ```bash
    (chroot) $ cd ~/chromiumos/src/platform/ec
    ```

1.  Create a `compile_commands.json` for the all EC boards:

    ```bash
    (chroot) $ make all-ide-compile-cmds -j
    ```

1.  Select a particular board:

    ```bash
    (chroot) $ export BOARD=bloonchipper
    ```

1.  Copy the new `compile_commands.json` in the root of the EC repository:

    ```bash
    cp build/${BOARD}/RW/compile_commands.json .
    ```

Note: a single `compile_commands.json` can only cover one specific build
configuration. Only the `compile_commands.json`placed in the root of the EC
repository is considered active. When the build configuration changes (e.g. user
wants to use a different board), repeat steps 3 and 4 to replace the active
`compile_commands.json` file.

To create a `compile_commands.json` for a specific EC board:

```bash
(chroot) $ make BOARD=${BOARD} ide-compile-cmds
```
