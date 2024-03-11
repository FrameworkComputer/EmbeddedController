#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build firmware with clang instead of gcc."""

import argparse
import concurrent
from concurrent.futures import ThreadPoolExecutor
import logging
import multiprocessing
import os
import shutil
import subprocess
import sys
import typing


# Add to this list as compilation errors are fixed for boards.
BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG = [
    # Fingerprint boards
    "dartmonkey",
    "bloonchipper",
    "bloonchipper-druid",
    "buccaneer",
    "helipilot",
    "nami_fp",
    "nucleo-dartmonkey",
    "nucleo-f412zg",
    "nucleo-h743zi",
    # Boards that use CHIP:=stm32 and *not* CHIP_FAMILY:=stm32f0
    # git grep  --name-only 'CHIP:=stm32' | xargs grep -L 'CHIP_FAMILY:=stm32f0' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "baklava",
    "discovery",
    "gingerbread",
    "hatch_fp",
    "hyperdebug",
    "nocturne_fp",
    "nucleo-f411re",
    "nucleo-g431rb",
    "panqueque",
    "polyberry",
    "quiche",
    "stm32f446e-eval",
    "stm32l476g-eval",
    "sweetberry",
    # Boards that use CHIP:=stm32 *and* CHIP_FAMILY:=stm32f0
    # git grep  --name-only 'CHIP:=stm32' | xargs grep -L 'CHIP_FAMILY:=stm32f0' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "bland",
    "c2d2",
    "coffeecake",
    "dingdong",
    "discovery-stm32f072",
    "don",
    "duck",
    "eel",
    "elm",
    "fluffy",
    "fusb307bgevb",
    "gelatin",
    "hammer",
    "hoho",
    "jewel",
    "kakadu",
    "kappa",
    "katsu",
    "kukui",
    "magnemite",
    "masterball",
    "minimuffin",
    "moonball",
    "nucleo-f072rb",
    "pdeval-stm32f072",
    "prism",
    "servo_micro",
    "servo_v4",
    "servo_v4p1",
    "staff",
    "star",
    "tigertail",
    "twinkie",
    "wand",
    "zed",
    "zinger",
    # Boards that use CHIP:=mchp
    # git grep --name-only 'CHIP:=mchp' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "adlrvpp_mchp1521",
    "adlrvpp_mchp1727",
    "mchpevb1",
    "reef_mchp",
    # Boards that use CHIP:=max32660
    # git grep --name-only 'CHIP:=max32660' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "max32660-eval",
    # Boards that use CHIP:=npcx
    # git grep --name-only 'CHIP:=npcx' | sed 's#^board/\(.*\)/build.mk#"\1",#'
    "adlrvpp_npcx",
    "akemi",
    "aleena",
    "ambassador",
    "anahera",
    "atlas",
    "aurash",
    "banshee",
    "berknip",
    "bloog",
    "bobba",
    "boldar",
    "brask",
    "brya",
    "bugzzy",
    "cappy2",
    "careena",
    "casta",
    "chronicler",
    "collis",
    "constitution",
    "copano",
    "coral",
    "corori",
    "crota",
    "dalboz",
    "delbin",
    "dewatt",
    "dirinboz",
    "dochi",
    "dood",
    "dooly",
    "dratini",
    "driblee",
    "drobit",
    "drobit_ecmodeentry",
    "eldrid",
    "elemi",
    "endeavour",
    "eve",
    "ezkinil",
    "felwinter",
    "fleex",
    "foob",
    "gaelin",
    "genesis",
    "gimble",
    "gladios",
    "grunt",
    "gumboz",
    "guybrush",
    "hatch",
    "helios",
    "homestar",
    "jinlon",
    "kano",
    "karma",
    "kindred",
    "kingoftown",
    "kinox",
    "kohaku",
    "kuldax",
    "lalala",
    "lazor",
    "liara",
    "lick",
    "lindar",
    "lisbon",
    "lux",
    "madoo",
    "magolor",
    "marasov",
    "marzipan",
    "meep",
    "metaknight",
    "mithrax",
    "moli",
    "moonbuggy",
    "morphius",
    "mrbland",
    "nami",
    "nautilus",
    "nightfury",
    "nipperkin",
    "nocturne",
    "nova",
    "npcx7_evb",
    "npcx9_evb",
    "npcx_evb",
    "npcx_evb_arm",
    "nuwani",
    "omnigul",
    "osiris",
    "palkia",
    "pazquel",
    "phaser",
    "pompom",
    "poppy",
    "primus",
    "puff",
    "quackingstick",
    "rammus",
    "redrix",
    "reef",
    "sasuke",
    "scout",
    "shuboz",
    "soraka",
    "stryke",
    "taeko",
    "taniks",
    "treeya",
    "trembyle",
    "trogdor",
    "vell",
    "vilboz",
    "voema",
    "volet",
    "volmar",
    "volteer_npcx797fc",
    "voxel",
    "voxel_ecmodeentry",
    "voxel_npcx797fc",
    "waddledoo2",
    "whiskers",
    "woomax",
    "wormdingler",
    "xol",
    "yorp",
    # CHIP=mt_scp *and* CHIP_VARIANT=mt818x
    # git grep --name-only 'CHIP:=mt_scp' | xargs grep -L 'CHIP_VARIANT:=mt818' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "corsola_scp",
    "kukui_scp",
    # Boards that use CORE:=minute-ia
    "adl_ish_lite",
    "arcada_ish",
    "drallion_ish",
    "tglrvp_ish",
    "volteer_ish",
]

NDS32_BOARDS = [
    "adlrvpm_ite",
    "adlrvpp_ite",
    "ampton",
    "beadrix",
    "beetley",
    "blipper",
    "boten",
    "boxy",
    "dexi",
    "dibbi",
    "dita",
    "drawcia",
    "galtic",
    "gooey",
    "haboki",
    "it83xx_evb",
    "kracko",
    "lantis",
    "pirika",
    "reef_it8320",
    "sasukette",
    "shotzo",
    "storo",
    "taranza",
    "waddledee",
    "wheelie",
]

RISCV_BOARDS = [
    "asurada",
    "asurada_scp",
    "cherry",
    "cherry_scp",
    "cozmo",
    "dojo",
    "goroh",
    "hayato",
    "icarus",
    "it8xxx2_evb",
    "it8xxx2_pdevb",
    "pico",
    "spherion",
    "tomato",
]

BOARDS_THAT_FAIL_WITH_CLANG = [
    # Boards that use CHIP:=stm32 *and* CHIP_FAMILY:=stm32f0
    "chocodile_vpdmcu",  # compilation error: b/254710459
    # Boards that use CHIP:=npcx
    "garg",
    # Boards that don't fit in flash with clang
    "bellis",
    "cerise",
    "corori2",
    "cret",
    "munna",
    "mushu",
    "volteer",
    "willow",
    # Not enough flash space with CONFIG_POWER_SLEEP_FAILURE_DETECTION enabled
    "burnet",
    "coachz",
    "corori2",
    "cret",
    "damu",
    "fennel",
    "fizz",
    "gelarshie",
    "jacuzzi",
    "juniper",
    "kodama",
    "krane",
    "mushu",
    "makomo",
    "oak",
    "stern",
    "terrador",
    "waddledoo",
]

# TODO(b/201311714): NDS32 is not supported by LLVM.
BOARDS_THAT_FAIL_WITH_CLANG += NDS32_BOARDS
# TODO(b/201310017): RISC-V is not supported in our LLVM toolchain.
BOARDS_THAT_FAIL_WITH_CLANG += RISCV_BOARDS


def build(board_name: str) -> None:
    """Build with clang for specified board."""
    logging.debug('Building board: "%s"', board_name)

    cmd = [
        "make",
        "BOARD=" + board_name,
        "-j",
    ]

    logging.debug('Running command: "%s"', " ".join(cmd))
    subprocess.run(cmd, env=dict(os.environ, CC="clang"), check=True)


def get_all_boards() -> typing.List[str]:
    """Return the list of all EC boards."""
    cmd = [
        "make",
        "print-boards",
    ]

    logging.debug('Running command: "%s"', " ".join(cmd))
    ret = subprocess.run(cmd, stdout=subprocess.PIPE, check=True)
    all_boards = ret.stdout.decode("utf-8").splitlines()
    return all_boards


def check_boards() -> None:
    """Checks that all boards are explicitly mentioned in this source."""
    all_boards = get_all_boards()
    diff = set(all_boards) ^ set(
        BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG
        + BOARDS_THAT_FAIL_WITH_CLANG
    )
    if len(diff) > 0:
        print(
            "The following boards are missing and must be added to "
            "BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG or "
            "BOARDS_THAT_FAIL_WITH_CLANG:"
        )
        for i in sorted(diff):
            print(i)
        sys.exit(1)


def main() -> int:
    """The mainest function of them all.

    Returns:
        The posix exit status.
    """
    parser = argparse.ArgumentParser()

    log_level_choices = ["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"]
    parser.add_argument(
        "--log_level", "-l", choices=log_level_choices, default="DEBUG"
    )

    parser.add_argument(
        "--num_threads", "-j", type=int, default=multiprocessing.cpu_count()
    )

    group = parser.add_mutually_exclusive_group(required=False)
    group.add_argument(
        "--clean",
        action="store_true",
        help="Remove build directory before compiling",
    )
    group.add_argument(
        "--no-clean",
        dest="clean",
        action="store_false",
        help="Do not remove build directory before compiling",
    )
    parser.set_defaults(clean=True)

    args = parser.parse_args()
    logging.basicConfig(level=args.log_level)

    if args.clean:
        logging.debug("Removing build directory")
        shutil.rmtree("./build", ignore_errors=True)

    check_boards()

    logging.debug("Building with %d threads", args.num_threads)

    failed_boards = []
    with ThreadPoolExecutor(max_workers=args.num_threads) as executor:
        future_to_board = {
            executor.submit(build, board): board
            for board in BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG
        }
        for future in concurrent.futures.as_completed(future_to_board):
            board = future_to_board[future]
            try:
                future.result()
            except subprocess.CalledProcessError:
                failed_boards.append(board)

    if len(failed_boards) > 0:
        failed_boards.sort()
        logging.error(
            "The following boards failed to compile:\n%s",
            "\n".join(failed_boards),
        )
        return 1

    logging.info("All boards compiled successfully!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
