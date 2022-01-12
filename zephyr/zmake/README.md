# Zmake

<!-- Auto-generated contents!  Run "zmake generate-readme" to update. -->

[TOC]

## Usage

**Usage:** `zmake [-h] [--checkout CHECKOUT] [-D] [-j JOBS] [-l {DEBUG,INFO,WARNING,ERROR,CRITICAL}] [-L] [--log-label] [--modules-dir MODULES_DIR] [--zephyr-base ZEPHYR_BASE] subcommand ...`

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
| `-D`, `--debug` | Turn on debug features (e.g., stack trace, verbose logging) |
| `-j JOBS`, `--jobs JOBS` | Degree of multiprogramming to use |
| `-l LOG_LEVEL`, `--log-level LOG_LEVEL` | Set the logging level (default=INFO) |
| `-L`, `--no-log-label` | Turn off logging labels |
| `--log-label` | Turn on logging labels |
| `--modules-dir MODULES_DIR` | The path to a directory containing all modules needed.  If unspecified, zmake will assume you have a Chrome OS checkout and try locating them in the checkout. |
| `--zephyr-base ZEPHYR_BASE` | Path to Zephyr OS repository |

## Subcommands

### zmake configure

**Usage:** `zmake configure [-h] [-t TOOLCHAIN] [--bringup] [--allow-warnings] [-B BUILD_DIR] [-b] [--test] project_name_or_dir [-c]`

#### Positional Arguments

|   |   |
|---|---|
| `project_name_or_dir` | Path to the project to build |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `-t TOOLCHAIN`, `--toolchain TOOLCHAIN` | Name of toolchain to use |
| `--bringup` | Enable bringup debugging features |
| `--allow-warnings` | Do not treat warnings as errors |
| `-B BUILD_DIR`, `--build-dir BUILD_DIR` | Build directory |
| `-b`, `--build` | Run the build after configuration |
| `--test` | Test the .elf file after configuration |
| `-c`, `--coverage` | Enable CONFIG_COVERAGE Kconfig. |

### zmake build

**Usage:** `zmake build [-h] build_dir [-w]`

#### Positional Arguments

|   |   |
|---|---|
| `build_dir` | The build directory used during configuration |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `-w`, `--fail-on-warnings` | Exit with code 2 if warnings are detected |

### zmake list-projects

**Usage:** `zmake list-projects [-h] [--format FORMAT] [search_dir]`

#### Positional Arguments

|   |   |
|---|---|
| `search_dir` | Optional directory to search for BUILD.py files in. |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `--format FORMAT` | Output format to print projects (str.format(config=project.config) is called on this for each project). |

### zmake test

**Usage:** `zmake test [-h] build_dir`

#### Positional Arguments

|   |   |
|---|---|
| `build_dir` | The build directory used during configuration |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |

### zmake testall

**Usage:** `zmake testall [-h]`

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |

### zmake coverage

**Usage:** `zmake coverage [-h] build_dir`

#### Positional Arguments

|   |   |
|---|---|
| `build_dir` | The build directory used during configuration |

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |

### zmake generate-readme

**Usage:** `zmake generate-readme [-h] [-o OUTPUT_FILE] [--diff]`

#### Optional Arguments

|   |   |
|---|---|
| `-h`, `--help` | show this help message and exit |
| `-o OUTPUT_FILE`, `--output-file OUTPUT_FILE` | File to write to.  It will only be written if changed. |
| `--diff` | If specified, diff the README with the expected contents instead of writing out. |
