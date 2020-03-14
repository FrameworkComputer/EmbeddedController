# IDE Support

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

Due to the way most EC code has been structured, you can typically only safely
inspect a configuration for a single image (RO or RW) for a single board. Thus,
you need to specify the specific board/image pair when requesting defines and
includes.

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
5. Add the EC specific file associations and style settings.
   Modify `.vscode/settings.json` to have the following elements:
   ```json
   {
       "editor.rulers": [80],
       /* C, Makefiles, ASM, Linkerfiles, Properties */
       "editor.insertSpaces": false,
       "editor.tabSize": 8,
       /* Some exceptions based on current trends */
       "[markdown]": {
           "editor.insertSpaces": true,
           "editor.tabSize": 2
       },
       "[python]": {
           "editor.insertSpaces": true,
           "editor.tabSize": 2
       },
       "[shellscript]": {
           "editor.insertSpaces": true,
           "editor.tabSize": 2
       },
       "[yaml]": {
           "editor.insertSpaces": true,
           "editor.tabSize": 2
       },
       "files.associations": {
           "Makefile.*": "makefile",
           "*.inc": "c",
           "*.wrap": "c",
           "*.tasklist": "c",
           "*.irqlist": "c",
           "*.mocklist": "c",
           "*.testlist": "c"
       }
   }
   ```
