# Zmake

<!-- Auto-generated contents!  Run "zmake generate-readme" to update. -->

[TOC]

## Usage

**Usage:** `zmake [-h] [--checkout CHECKOUT] [-j JOBS] [--goma] [-l {DEBUG,INFO,WARNING,ERROR,CRITICAL} | -D] [-L] [--log-label] [--modules-dir MODULES_DIR] [--zephyr-base ZEPHYR_BASE] subcommand ...`

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
| `--zephyr-base ZEPHYR_BASE` | Path to Zephyr OS repository |

## Subcommands

### zmake configure

**Usage:** `zmake configure [-h] [-t TOOLCHAIN] [--bringup] [--clobber] [--allow-warnings] [-B BUILD_DIR] [-c] [--extra-cflags EXTRA_CFLAGS] (-a | --host-tests-only | project_name [project_name ...])`

#### Positional Arguments

|   |   |
|---|---|
| `project_name` | Name(s) of the project(s) to build |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `-t TOOLCHAIN`, `--toolchain TOOLCHAIN` | Name of toolchain to use |
| `--bringup` | Enable bringup debugging features |
| `--clobber` | Delete existing build directories, even if configuration is unchanged |
| `--allow-warnings` | Do not treat warnings as errors |
| `-B BUILD_DIR`, `--build-dir BUILD_DIR` | Root build directory, project files will be in ${build_dir}/${project_name} |
| `-c`, `--coverage` | Enable CONFIG_COVERAGE Kconfig. |
| `--extra-cflags EXTRA_CFLAGS` | Additional CFLAGS to use for target builds |
| `-a`, `--all` | Select all projects |
| `--host-tests-only` | Select all test projects |

### zmake build

**Usage:** `zmake build [-h] [-t TOOLCHAIN] [--bringup] [--clobber] [--allow-warnings] [-B BUILD_DIR] [-c] [--extra-cflags EXTRA_CFLAGS] (-a | --host-tests-only | project_name [project_name ...])`

#### Positional Arguments

|   |   |
|---|---|
| `project_name` | Name(s) of the project(s) to build |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `-t TOOLCHAIN`, `--toolchain TOOLCHAIN` | Name of toolchain to use |
| `--bringup` | Enable bringup debugging features |
| `--clobber` | Delete existing build directories, even if configuration is unchanged |
| `--allow-warnings` | Do not treat warnings as errors |
| `-B BUILD_DIR`, `--build-dir BUILD_DIR` | Root build directory, project files will be in ${build_dir}/${project_name} |
| `-c`, `--coverage` | Enable CONFIG_COVERAGE Kconfig. |
| `--extra-cflags EXTRA_CFLAGS` | Additional CFLAGS to use for target builds |
| `-a`, `--all` | Select all projects |
| `--host-tests-only` | Select all test projects |

### zmake list-projects

**Usage:** `zmake list-projects [-h] [--format FMT] [search_dir]`

#### Positional Arguments

|   |   |
|---|---|
| `search_dir` | Optional directory to search for BUILD.py files in. |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `--format FMT` | Output format to print projects (str.format(config=project.config) is called on this for each project). |

### zmake test

**Usage:** `zmake test [-h] [--no-rebuild] [-t TOOLCHAIN] [--bringup] [--clobber] [--allow-warnings] [-B BUILD_DIR] [-c] [--extra-cflags EXTRA_CFLAGS] (-a | --host-tests-only | project_name [project_name ...])`

#### Positional Arguments

|   |   |
|---|---|
| `project_name` | Name(s) of the project(s) to build |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `--no-rebuild` | Do not configure or build before running tests. |
| `-t TOOLCHAIN`, `--toolchain TOOLCHAIN` | Name of toolchain to use |
| `--bringup` | Enable bringup debugging features |
| `--clobber` | Delete existing build directories, even if configuration is unchanged |
| `--allow-warnings` | Do not treat warnings as errors |
| `-B BUILD_DIR`, `--build-dir BUILD_DIR` | Root build directory, project files will be in ${build_dir}/${project_name} |
| `-c`, `--coverage` | Enable CONFIG_COVERAGE Kconfig. |
| `--extra-cflags EXTRA_CFLAGS` | Additional CFLAGS to use for target builds |
| `-a`, `--all` | Select all projects |
| `--host-tests-only` | Select all test projects |

### zmake testall

**Usage:** `zmake testall [-h] [--clobber] [-B BUILD_DIR]`

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `--clobber` | Delete existing build directories, even if configuration is unchanged |
| `-B BUILD_DIR`, `--build-dir BUILD_DIR` | Build directory |

### zmake generate-readme

**Usage:** `zmake generate-readme [-h] [-o OUTPUT_FILE] [--diff]`

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `-o OUTPUT_FILE`, `--output-file OUTPUT_FILE` | File to write to.  It will only be written if changed. |
| `--diff` | If specified, diff the README with the expected contents instead of writing out. |
