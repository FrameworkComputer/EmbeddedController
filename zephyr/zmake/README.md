# Zmake

<!-- Auto-generated contents!  Run "zmake generate-readme" to update. -->

[TOC]

## Usage

**Usage:** `zmake [-h] [--checkout CHECKOUT] [-j JOBS] [--goma] [-l {DEBUG,INFO,WARNING,ERROR,CRITICAL} | -D] [-L] [--log-label] [--modules-dir MODULES_DIR] [--projects-dir PROJECTS_DIR] [--zephyr-base ZEPHYR_BASE] subcommand ...`

Chromium OS's meta-build tool for Zephyr

#### Positional Arguments

|   |   |
|---|---|
| `subcommand` | Subcommand to run |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `--checkout CHECKOUT` | Path to ChromiumOS checkout |
| `-j JOBS`, `--jobs JOBS` | Degree of multiprogramming to use |
| `--goma` | Enable hyperspeed compilation with Goma! (Googlers only) |
| `-l {DEBUG,INFO,WARNING,ERROR,CRITICAL}`, `--log-level {DEBUG,INFO,WARNING,ERROR,CRITICAL}` | Set the logging level (default=INFO) |
| `-D`, `--debug` | Alias for --log-level=DEBUG |
| `-L`, `--no-log-label` | Turn off logging labels |
| `--log-label` | Turn on logging labels |
| `--modules-dir MODULES_DIR` | The path to a directory containing all modules needed.  If unspecified, zmake will assume you have a Chrome OS checkout and try locating them in the checkout. |
| `--projects-dir PROJECTS_DIR` | Base directory to search for BUILD.py files. Can be repeated. |
| `--zephyr-base ZEPHYR_BASE` | Path to Zephyr OS repository |

## Subcommands

### zmake configure

**Usage:** `zmake configure [-h] [--bringup] [--clobber] [-v VERSION] [--static] [--save-temps] [--allow-warnings] [--cmake-trace] [-B BUILD_DIR] [-c] [--delete-intermediates] [-D CMAKE_DEFS] [-t TOOLCHAIN] [--extra-cflags EXTRA_CFLAGS] (-a | project_name [project_name ...])`

#### Positional Arguments

|   |   |
|---|---|
| `project_name` | Name(s) of the project(s) to build |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `--bringup` | Enable bringup debugging features |
| `--clobber` | Delete existing build directories, even if configuration is unchanged |
| `-v VERSION`, `--version VERSION` | Base version string to use in build |
| `--static` | Generate static version information for reproducible builds and official builds |
| `--save-temps` | Save the temporary files containing preprocessor output |
| `--allow-warnings` | Do not treat warnings as errors |
| `--cmake-trace` | None |
| `-B BUILD_DIR`, `--build-dir BUILD_DIR` | Root build directory, project files will be in ${build_dir}/${project_name} |
| `-c`, `--coverage` | Enable CONFIG_COVERAGE Kconfig. |
| `--delete-intermediates` | Delete intermediate files to save disk space |
| `-D CMAKE_DEFS`, `--cmake-define CMAKE_DEFS` | None |
| `-t TOOLCHAIN`, `--toolchain TOOLCHAIN` | Name of toolchain to use |
| `--extra-cflags EXTRA_CFLAGS` | Additional CFLAGS to use for target builds |
| `-a`, `--all` | Select all projects |

### zmake build

**Usage:** `zmake build [-h] [--bringup] [--clobber] [-v VERSION] [--static] [--save-temps] [--allow-warnings] [--cmake-trace] [-B BUILD_DIR] [-c] [--delete-intermediates] [-D CMAKE_DEFS] [-t TOOLCHAIN] [--extra-cflags EXTRA_CFLAGS] (-a | project_name [project_name ...])`

#### Positional Arguments

|   |   |
|---|---|
| `project_name` | Name(s) of the project(s) to build |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `--bringup` | Enable bringup debugging features |
| `--clobber` | Delete existing build directories, even if configuration is unchanged |
| `-v VERSION`, `--version VERSION` | Base version string to use in build |
| `--static` | Generate static version information for reproducible builds and official builds |
| `--save-temps` | Save the temporary files containing preprocessor output |
| `--allow-warnings` | Do not treat warnings as errors |
| `--cmake-trace` | None |
| `-B BUILD_DIR`, `--build-dir BUILD_DIR` | Root build directory, project files will be in ${build_dir}/${project_name} |
| `-c`, `--coverage` | Enable CONFIG_COVERAGE Kconfig. |
| `--delete-intermediates` | Delete intermediate files to save disk space |
| `-D CMAKE_DEFS`, `--cmake-define CMAKE_DEFS` | None |
| `-t TOOLCHAIN`, `--toolchain TOOLCHAIN` | Name of toolchain to use |
| `--extra-cflags EXTRA_CFLAGS` | Additional CFLAGS to use for target builds |
| `-a`, `--all` | Select all projects |

### zmake compare-builds

**Usage:** `zmake compare-builds [-h] [--ref1 REF1] [--ref2 REF2] [-k] [-n] [-b] [-d] [-t TOOLCHAIN] [--extra-cflags EXTRA_CFLAGS] (-a | project_name [project_name ...])`

#### Positional Arguments

|   |   |
|---|---|
| `project_name` | Name(s) of the project(s) to build |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `--ref1 REF1` | 1st git reference (commit, branch, etc), default=HEAD |
| `--ref2 REF2` | 2nd git reference (commit, branch, etc), default=HEAD~ |
| `-k`, `--keep-temps` | Keep temporary build directories on exit |
| `-n`, `--compare-configs` | Compare configs of build outputs |
| `-b`, `--compare-binaries-disable` | Don't compare binaries of build outputs |
| `-d`, `--compare-devicetrees` | Compare devicetrees of build outputs |
| `-t TOOLCHAIN`, `--toolchain TOOLCHAIN` | Name of toolchain to use |
| `--extra-cflags EXTRA_CFLAGS` | Additional CFLAGS to use for target builds |
| `-a`, `--all` | Select all projects |

### zmake list-projects

**Usage:** `zmake list-projects [-h] [--format FMT]`

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `--format FMT` | Output format to print projects (str.format(config=project.config) is called on this for each project). |

### zmake generate-readme

**Usage:** `zmake generate-readme [-h] [-o OUTPUT_FILE] [--diff]`

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `-o OUTPUT_FILE`, `--output-file OUTPUT_FILE` | File to write to.  It will only be written if changed. |
| `--diff` | If specified, diff the README with the expected contents instead of writing out. |
