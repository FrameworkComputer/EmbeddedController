Project Configuration
=====================

[TOC]

## Setting up a new program ("reference board" or "baseboard")

Unlike the legacy EC codebase, Zephyr projects all live together in
one big happy directory.  The intent of this design is to encourage
code-sharing between projects, and reduce the amount of copy/paste
that is required to bring up a new project.  This directory can, but
does not have to, correlate to the unified build Chrome OS board,
however firmware authors can always choose a different structure if it
makes sense for the specific scenario.  As a hypothetical example,
similar Chromeboxes and Chromebooks may wish to share the Zephyr EC
project directory instead of use separate directories, even if they
are using a different unified build board.

To set up a new EC program, create a new directory under
[`zephyr/projects`](../../zephyr/projects) with the following files:

- `BUILD.py` - specifies which builds can be made from this directory,
  and what the device-tree overlays and Kconfig files are for each
  build.
- `CMakeLists.txt` - Baseboard-specific C files can be listed here.
- `prj.conf` (optional) - Default Kconfig settings for all projects.
- `Kconfig` (optional) - Set options for your reference design here,
  which variants can use to install optional C sources.

An in-depth example of each file is given below:

### BUILD.py

`BUILD.py` is a Python-based config file for setting up your reference
board and the associated variants.  The name `BUILD.py` is important
and case-sensitive: `zmake` searches for files by this
name.

When `BUILD.py` is sourced, the following two globals are defined:

- `here`: A `pathlib.Path` object containing the path to the directory
  `BUILD.py` is located in.
- `register_project`: A function which informs `zmake` of a new
  project to be built.  Your `BUILD.py` file needs to call this
  function one or more times.

`register_project` takes the following keyword arguments:

- `project_name` (required): The name of the project (typically the
  Google codename).  This name must be unique amongst all projects
  known to `zmake`, and `zmake` will error if you choose a conflicting
  name.
- `zephyr_board` (required): The name of the Zephyr board to use for
  the project.  The Zephyr build system expects a Zephyr board
  directory under `boards/${ARCH}/${ZEPHYR_BOARD_NAME}`.  **Note:**
  the concept of a Zephyr board does not align with the Chrome OS
  concept of a board: for most projects this will typically be the
  name of the EC chip used, not the name of the model or overlay.
- `supported_toolchains` (required): A list of the toolchain names
  supported by the build.  Valid values are `coreboot-sdk`, `host`,
  `llvm`, and `zephyr`.  Note that only `coreboot-sdk` and `llvm` are
  supported in the chroot, and all projects must be able to build in
  the chroot, so your project must at least list one of `coreboot-sdk`
  or `llvm`.
- `output_packer` (required): An output packer type which defines
  which builds get generated, and how they get assembled together into
  a binary.
- `modules` (optional): A list of module names required by the
  project.  The default, if left unspecified, is to use all modules
  known by `zmake`.  Generally speaking, there is no harm to including
  unnecessary modules as modules are typically guarded by Kconfig
  options, so the only reason to set this is if your project needs to
  build in a limited environment where not all modules are available.
- `is_test` (optional): `True` if the code should be executed as a
  test after compilation, `False` otherwise.  Defaults to `False`.
- `dts_overlays` (optional): A list of files which should be
  concatenated together and applied as a Zephyr device-tree overlay.
  Defaults to no overlays (empty list).
- `project_dir` (optional): The path to where `CMakeLists.txt` and
  `Kconfig` can be found for the project, defaulting to `here`.

Note that most projects will not want to call `register_project`
directly, but instead one of the helper functions, which sets even
more defaults for you:

- `register_host_project`: Define a project which runs in the chroot
  (not on hardware).
- `register_host_test`: Just like `register_host_project`, but
  `is_test` gets set to `True`.
- `register_raw_project`: Register a project which builds a single
  `.bin` file, no RO+RW packing, no FMAP.
- `register_binman_project`: Register a project which builds RO and RW
  sections, packed together, and including FMAP.
- `register_npcx_project`: Just like `register_binman_project`, but
  expects a file generated named `zephyr.npcx.bin` for the RO section
  with Nuvoton's header.

You can find the implementation of these functions in
[`zephyr/zmake/zmake/configlib.py`](../../zephyr/zmake/zmake/configlib.py).

`BUILD.py` files are auto-formatted with `black`.  After editing a
`BUILD.py` file, please run `black BUILD.py` on it.

### CMakeLists.txt

This file, should at minimum contain the following:

``` cmake
cmake_minimum_required(VERSION 3.20.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ec)
```

You may additionally want to specify any C files your project needs
using `zephyr_library_sources`. If you need to add extra include
directories, use `cros_ec_library_include_directories`.

### prj.conf and prj_${project_name}.conf

`prj.conf` has default Kconfig settings for all projects, and
`prj_${project_name}.conf` can contain overrides for certain projects.
The format is `KEY=VALUE`, as typical for Kconfig.

### Kconfig

If certain projects need project-specific C files or ifdefs, the only
way to do so is to create a `Kconfig` file with the options schema you
want, and use it to toggle the inclusion of certain files.

The file must end with a single line that reads
`source "Kconfig.zephyr"`.  Note that this file is optional, so it's
recommended to only include it if you really need it.

## Setting up a new variant of an EC program

**Unlike our legacy EC, there are no files or directories to copy and
paste to setup a new variant in Zephyr code.**

Simply add a `register_project`-based call to the existing `BUILD.py`
for your reference board.

Below is an example of how programs may wish to structure this in
`BUILD.py`:

``` python
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def register_variant(project_name, chip="it8xx2", extra_dts_overlays=()):
    return register_binman_project(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[
            here / "base_power_sequence.dts",
            here / "i2c.dts",
            **extra_dts_overlays,
        ],
    )


# Reference board
register_variant(
    project_name="asurada",
    extra_dts_overlays=[here / "reference_gpios.dts"],
)

# Variants
register_variant(
    project_name="hayato",
    extra_dts_overlays=[here / "hayato_gpios.dts"],
)
```

If a project is going to be a simple variant of another project (e.g.,
project `bar` is exactly identical to project `foo` but has just a few
device-tree/Kconfig changes), you can spin a new variant using the
return value of the register functions:

``` python
foo = register_variant(project_name="foo")
bar = foo.variant(
    project_name="bar",
    dts_overlays=[here / "bar_extras.dts"],
)
```

With this simple variant syntax, lists (like Kconfig files and DTS
overlays) are concatenated.  This means it's not possible to remove
files during variant registration for this syntax, so it's only
recommended for the simple case.
