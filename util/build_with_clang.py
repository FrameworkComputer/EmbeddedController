#!/usr/bin/env python3

# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build firmware with clang instead of gcc."""
import argparse
import concurrent
import logging
import multiprocessing
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

# Add to this list as compilation errors are fixed for boards. Boards that are
# commented out need fixes in order to compile; uncomment once fixed.
BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG = [
    # Fingerprint boards
    "dartmonkey",
    "bloonchipper",
    "nucleo-f412zg",
    "nucleo-h743zi",
    # Boards that use CHIP:=stm32 and *not* CHIP_FAMILY:=stm32f0
    # git grep  --name-only 'CHIP:=stm32' | xargs grep -L 'CHIP_FAMILY:=stm32f0' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "baklava",
    # "bellis",
    "discovery",
    "gingerbread",
    "hatch_fp",
    "hyperdebug",
    # "munna",
    "nocturne_fp",
    "nucleo-f411re",
    "nucleo-g431rb",
    "panqueque",
    "polyberry",
    "quiche",
    "stm32f446e-eval",
    "stm32l476g-eval",
    "sweetberry",
    # Boards that use CHIP:=mchp
    # git grep --name-only 'CHIP:=mchp' | sed 's#board/\(.*\)/build.mk#"\1",#'
    # "adlrvpp_mchp1521",
    # "adlrvpp_mchp1727",
    # "mchpevb1",
    # "reef_mchp",
    # Boards that use CHIP:=max32660
    # git grep --name-only 'CHIP:=max32660' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "max32660-eval",
    # Boards that use CHIP:=npcx
    # git grep --name-only 'CHIP:=npcx' | sed 's#^board/\(.*\)/build.mk#"\1",#'
    "adlrvpp_npcx",
    "agah",
    "akemi",
    "aleena",
    "ambassador",
    "anahera",
    "atlas",
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
    "coachz",
    "collis",
    "copano",
    "coral",
    "corori",
    # "corori2",
    "cret",
    "crota",
    "dalboz",
    "delbin",
    "dirinboz",
    "dood",
    "dooly",
    "dratini",
    "driblee",
    "drobit",
    "eldrid",
    "elemi",
    "endeavour",
    # "eve",
    "ezkinil",
    "felwinter",
    "fizz",
    "fleex",
    "foob",
    "gaelin",
    # "garg",
    # "gelarshie",
    "genesis",
    "gimble",
    "grunt",
    "gumboz",
    "hatch",
    "helios",
    "herobrine",
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
    "madoo",
    "magolor",
    "marzipan",
    "meep",
    "metaknight",
    "mithrax",
    "moli",
    "moonbuggy",
    "morphius",
    "mrbland",
    # "mushu",
    "nami",
    "nautilus",
    "nightfury",
    # "nocturne",
    "npcx7_evb",
    "npcx9_evb",
    "npcx_evb",
    "npcx_evb_arm",
    "nuwani",
    "osiris",
    "palkia",
    "pazquel",
    "phaser",
    "pompom",
    # "poppy",
    "primus",
    "puff",
    "quackingstick",
    "rammus",
    "redrix",
    # "reef",
    "sasuke",
    "scout",
    "shuboz",
    "stryke",
    "taeko",
    "taniks",
    # "terrador",
    "treeya",
    "trembyle",
    "trogdor",
    "vell",
    "vilboz",
    "voema",
    "volet",
    "volmar",
    # "volteer",
    "voxel",
    # "waddledoo",
    "waddledoo2",
    "woomax",
    "wormdingler",
    "yorp",
]


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


def main() -> int:
    parser = argparse.ArgumentParser()

    log_level_choices = ["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"]
    parser.add_argument(
        "--log_level", "-l", choices=log_level_choices, default="DEBUG"
    )

    parser.add_argument(
        "--num_threads", "-j", type=int, default=multiprocessing.cpu_count()
    )

    args = parser.parse_args()
    logging.basicConfig(level=args.log_level)

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
            except Exception:
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
