# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file is auto-generated.  To update, run:
# ./bazel/gen_bazel_targets.py

load(":ec_target.bzl", "ec_target")

def all_targets():
    ec_target(
        name = "adl_ish_lite",
        board = "adl_ish_lite",
        chip = "ish",
        core = "minute-ia",
        real_board = "tglrvp_ish",
        zephyr = False,
    )
    ec_target(
        name = "adlrvp_mchp",
        board = "adlrvp_mchp",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "adlrvp_npcx",
        board = "adlrvp_npcx",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "adlrvpm_ite",
        baseboard = "intelrvp",
        board = "adlrvpm_ite",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "adlrvpp_ite",
        baseboard = "intelrvp",
        board = "adlrvpp_ite",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "adlrvpp_mchp1521",
        baseboard = "intelrvp",
        board = "adlrvpp_mchp1521",
        chip = "mchp",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "adlrvpp_mchp1727",
        baseboard = "intelrvp",
        board = "adlrvpp_mchp1727",
        chip = "mchp",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "adlrvpp_npcx",
        baseboard = "intelrvp",
        board = "adlrvpp_npcx",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "akemi",
        baseboard = "hatch",
        board = "akemi",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "aleena",
        baseboard = "grunt",
        board = "aleena",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "ambassador",
        board = "ambassador",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "ampton",
        baseboard = "octopus",
        board = "ampton",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "anahera",
        baseboard = "brya",
        board = "anahera",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "anraggar",
        board = "anraggar",
    )
    ec_target(
        name = "arcada_ish",
        board = "arcada_ish",
        chip = "ish",
        core = "minute-ia",
        zephyr = False,
    )
    ec_target(
        name = "asurada",
        baseboard = "asurada",
        board = "asurada",
        chip = "it83xx",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "asurada_scp",
        baseboard = "mtscp-rv32i",
        board = "asurada_scp",
        chip = "mt_scp",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "atlas",
        board = "atlas",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "aurash",
        baseboard = "brask",
        board = "aurash",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "axii",
        board = "axii",
    )
    ec_target(
        name = "baklava",
        baseboard = "honeybuns",
        board = "baklava",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "banshee",
        baseboard = "brya",
        board = "banshee",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "beadrix",
        baseboard = "dedede",
        board = "beadrix",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "beetley",
        baseboard = "dedede",
        board = "beetley",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "bellis",
        baseboard = "kukui",
        board = "bellis",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "berknip",
        baseboard = "zork",
        board = "berknip",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "bland",
        board = "bland",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "blipper",
        baseboard = "dedede",
        board = "blipper",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "bloog",
        baseboard = "octopus",
        board = "bloog",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "bloonchipper",
        baseboard = "bloonchipper",
        board = "bloonchipper",
        chip = "stm32",
        core = "cortex-m",
        real_board = "hatch_fp",
        zephyr = False,
    )
    ec_target(
        name = "bloonchipper-druid",
        baseboard = "bloonchipper",
        board = "bloonchipper-druid",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "bloonchipper_zephyr",
        board = "bloonchipper",
        extra_modules = ["hal_stm32", "cmsis"],
    )
    ec_target(
        name = "bobba",
        baseboard = "octopus",
        board = "bobba",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "boldar",
        baseboard = "volteer",
        board = "boldar",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "boten",
        baseboard = "dedede",
        board = "boten",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "boxy",
        baseboard = "dedede",
        board = "boxy",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "brask",
        baseboard = "brask",
        board = "brask",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "brox",
        board = "brox",
    )
    ec_target(
        name = "brox-ish",
        board = "brox-ish",
        extra_modules = ["cmsis", "hal_intel_public"],
    )
    ec_target(
        name = "brox-ish-ec",
        board = "brox-ish-ec",
    )
    ec_target(
        name = "brox-tokenized",
        board = "brox-tokenized",
        extra_modules = ["picolibc", "pigweed"],
    )
    ec_target(
        name = "brya",
        baseboard = "brya",
        board = "brya",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "brya_pdc",
        board = "brya_pdc",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "brya_zephyr",
        board = "brya",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "buccaneer",
        baseboard = "helipilot",
        board = "buccaneer",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "bugzzy",
        baseboard = "dedede",
        board = "bugzzy",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "bujia",
        baseboard = "brask",
        board = "bujia",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "burnet",
        baseboard = "kukui",
        board = "burnet",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "c2d2",
        board = "c2d2",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "cappy2",
        baseboard = "keeby",
        board = "cappy2",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "careena",
        baseboard = "grunt",
        board = "careena",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "casta",
        baseboard = "octopus",
        board = "casta",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "cerise",
        baseboard = "kukui",
        board = "cerise",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "cherry",
        baseboard = "cherry",
        board = "cherry",
        chip = "it83xx",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "cherry_scp",
        baseboard = "mtscp-rv32i",
        board = "cherry_scp",
        chip = "mt_scp",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "cherry_scp_core1",
        baseboard = "mtscp-rv32i",
        board = "cherry_scp_core1",
        chip = "mt_scp",
        core = "riscv-rv32i",
        real_board = "cherry_scp",
        zephyr = False,
    )
    ec_target(
        name = "chinchou",
        board = "chinchou",
    )
    ec_target(
        name = "chocodile_vpdmcu",
        board = "chocodile_vpdmcu",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "chronicler",
        baseboard = "volteer",
        board = "chronicler",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "ciri",
        board = "ciri",
    )
    ec_target(
        name = "coachz",
        baseboard = "trogdor",
        board = "coachz",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "coffeecake",
        board = "coffeecake",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "collis",
        baseboard = "volteer",
        board = "collis",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "constitution",
        baseboard = "brask",
        board = "constitution",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "copano",
        baseboard = "volteer",
        board = "copano",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "coral",
        board = "coral",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "corori",
        baseboard = "keeby",
        board = "corori",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "corori2",
        baseboard = "dedede",
        board = "corori2",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "corsola_scp",
        board = "corsola_scp",
        chip = "mt_scp",
        core = "cortex-m",
        real_board = "kukui_scp",
        zephyr = False,
    )
    ec_target(
        name = "cozmo",
        baseboard = "kukui",
        board = "cozmo",
        chip = "it83xx",
        core = "riscv-rv32i",
        real_board = "icarus",
        zephyr = False,
    )
    ec_target(
        name = "craask",
        board = "craask",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "craaskov",
        board = "craaskov",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "cret",
        baseboard = "dedede",
        board = "cret",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "crota",
        baseboard = "brya",
        board = "crota",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "crystaldrift",
        board = "crystaldrift",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "dalboz",
        baseboard = "zork",
        board = "dalboz",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "damu",
        baseboard = "kukui",
        board = "damu",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "dartmonkey",
        board = "dartmonkey",
        chip = "stm32",
        core = "cortex-m",
        real_board = "nocturne_fp",
        zephyr = False,
    )
    ec_target(
        name = "deku",
        board = "deku",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "delbin",
        baseboard = "volteer",
        board = "delbin",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "dev-posix",
        board = "dev-posix",
    )
    ec_target(
        name = "dewatt",
        baseboard = "guybrush",
        board = "dewatt",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "dexi",
        baseboard = "dedede",
        board = "dexi",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "dibbi",
        baseboard = "dedede",
        board = "dibbi",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "dingdong",
        board = "dingdong",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "dirinboz",
        baseboard = "zork",
        board = "dirinboz",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "discovery",
        board = "discovery",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "discovery-stm32f072",
        board = "discovery-stm32f072",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "dita",
        baseboard = "dedede",
        board = "dita",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "dochi",
        baseboard = "brya",
        board = "dochi",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "dojo",
        baseboard = "cherry",
        board = "dojo",
        chip = "it83xx",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "don",
        board = "don",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "dood",
        baseboard = "octopus",
        board = "dood",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "dooly",
        board = "dooly",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "drallion_ish",
        board = "drallion_ish",
        chip = "ish",
        core = "minute-ia",
        zephyr = False,
    )
    ec_target(
        name = "dratini",
        baseboard = "hatch",
        board = "dratini",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "drawcia",
        baseboard = "dedede",
        board = "drawcia",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "driblee",
        baseboard = "keeby",
        board = "driblee",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "drobit",
        baseboard = "volteer",
        board = "drobit",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "drobit_ecmodeentry",
        baseboard = "volteer",
        board = "drobit_ecmodeentry",
        chip = "npcx",
        core = "cortex-m",
        real_board = "drobit",
        zephyr = False,
    )
    ec_target(
        name = "duck",
        board = "duck",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "eel",
        board = "eel",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "eldrid",
        baseboard = "volteer",
        board = "eldrid",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "elemi",
        baseboard = "volteer",
        board = "elemi",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "elm",
        board = "elm",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "endeavour",
        board = "endeavour",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "eve",
        board = "eve",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "ezkinil",
        baseboard = "zork",
        board = "ezkinil",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "felwinter",
        baseboard = "brya",
        board = "felwinter",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "fennel",
        baseboard = "kukui",
        board = "fennel",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "fizz",
        board = "fizz",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "fleex",
        baseboard = "octopus",
        board = "fleex",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "fluffy",
        board = "fluffy",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "foob",
        baseboard = "octopus",
        board = "foob",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "frostflow",
        board = "frostflow",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "fusb307bgevb",
        board = "fusb307bgevb",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "gaelin",
        baseboard = "brask",
        board = "gaelin",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "galtic",
        baseboard = "dedede",
        board = "galtic",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "garg",
        baseboard = "octopus",
        board = "garg",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "gelarshie",
        baseboard = "trogdor",
        board = "gelarshie",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "gelatin",
        board = "gelatin",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "genesis",
        board = "genesis",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "geralt",
        board = "geralt",
    )
    ec_target(
        name = "geralt_scp",
        baseboard = "mtscp-rv32i",
        board = "geralt_scp",
        chip = "mt_scp",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "geralt_scp_core1",
        baseboard = "mtscp-rv32i",
        board = "geralt_scp_core1",
        chip = "mt_scp",
        core = "riscv-rv32i",
        real_board = "geralt_scp",
        zephyr = False,
    )
    ec_target(
        name = "gimble",
        baseboard = "brya",
        board = "gimble",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "gingerbread",
        baseboard = "honeybuns",
        board = "gingerbread",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "gladios",
        baseboard = "brask",
        board = "gladios",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "glassway",
        board = "glassway",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "gooey",
        baseboard = "keeby",
        board = "gooey",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "goroh",
        baseboard = "goroh",
        board = "goroh",
        chip = "it83xx",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "gothrax",
        board = "gothrax",
    )
    ec_target(
        name = "greenbayupoc",
        board = "greenbayupoc",
    )
    ec_target(
        name = "grunt",
        baseboard = "grunt",
        board = "grunt",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "gumboz",
        baseboard = "zork",
        board = "gumboz",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "guybrush",
        baseboard = "guybrush",
        board = "guybrush",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "haboki",
        baseboard = "keeby",
        board = "haboki",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "hammer",
        board = "hammer",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "hatch",
        baseboard = "hatch",
        board = "hatch",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "hatch_fp",
        baseboard = "bloonchipper",
        board = "hatch_fp",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "hayato",
        baseboard = "asurada",
        board = "hayato",
        chip = "it83xx",
        core = "riscv-rv32i",
        real_board = "asurada",
        zephyr = False,
    )
    ec_target(
        name = "helios",
        baseboard = "hatch",
        board = "helios",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "helipilot",
        baseboard = "helipilot",
        board = "helipilot",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "hoho",
        board = "hoho",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "homestar",
        baseboard = "trogdor",
        board = "homestar",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "host",
        board = "host",
        chip = "host",
        core = "host",
        zephyr = False,
    )
    ec_target(
        name = "hyperdebug",
        board = "hyperdebug",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "icarus",
        baseboard = "kukui",
        board = "icarus",
        chip = "it83xx",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "it82002_evb",
        board = "it82002_evb",
    )
    ec_target(
        name = "it83xx_evb",
        baseboard = "ite_evb",
        board = "it83xx_evb",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "it8xxx2_evb",
        baseboard = "ite_evb",
        board = "it8xxx2_evb",
        chip = "it83xx",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "it8xxx2_evb_zephyr",
        board = "it8xxx2_evb",
    )
    ec_target(
        name = "it8xxx2_pdevb",
        baseboard = "ite_evb",
        board = "it8xxx2_pdevb",
        chip = "it83xx",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "jacuzzi",
        baseboard = "kukui",
        board = "jacuzzi",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "jewel",
        board = "jewel",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "jinlon",
        baseboard = "hatch",
        board = "jinlon",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "joxer",
        board = "joxer",
    )
    ec_target(
        name = "juniper",
        baseboard = "kukui",
        board = "juniper",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "jacuzzi",
        zephyr = False,
    )
    ec_target(
        name = "kakadu",
        baseboard = "kukui",
        board = "kakadu",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "kano",
        baseboard = "brya",
        board = "kano",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "kappa",
        baseboard = "kukui",
        board = "kappa",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "karis",
        board = "karis",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "karma",
        baseboard = "kalista",
        board = "karma",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "katsu",
        baseboard = "kukui",
        board = "katsu",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "kelpie",
        board = "kelpie",
    )
    ec_target(
        name = "kindred",
        baseboard = "hatch",
        board = "kindred",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "kingler",
        board = "kingler",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "kingoftown",
        baseboard = "trogdor",
        board = "kingoftown",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "kinox",
        baseboard = "brask",
        board = "kinox",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "kodama",
        baseboard = "kukui",
        board = "kodama",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "kohaku",
        baseboard = "hatch",
        board = "kohaku",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "krabby",
        board = "krabby",
    )
    ec_target(
        name = "kracko",
        baseboard = "dedede",
        board = "kracko",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "krane",
        baseboard = "kukui",
        board = "krane",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "kukui",
        zephyr = False,
    )
    ec_target(
        name = "kukui",
        baseboard = "kukui",
        board = "kukui",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "kukui_scp",
        board = "kukui_scp",
        chip = "mt_scp",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "kuldax",
        baseboard = "brask",
        board = "kuldax",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "kyogre",
        board = "kyogre",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "lalala",
        baseboard = "keeby",
        board = "lalala",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "lantis",
        baseboard = "dedede",
        board = "lantis",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "lazor",
        baseboard = "trogdor",
        board = "lazor",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "lazor_zephyr",
        board = "lazor",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "liara",
        baseboard = "grunt",
        board = "liara",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "lick",
        baseboard = "octopus",
        board = "lick",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "lindar",
        baseboard = "volteer",
        board = "lindar",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "lisbon",
        baseboard = "brask",
        board = "lisbon",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "lotso",
        board = "lotso",
    )
    ec_target(
        name = "lux",
        board = "lux",
        chip = "npcx",
        core = "cortex-m",
        real_board = "poppy",
        zephyr = False,
    )
    ec_target(
        name = "madoo",
        baseboard = "dedede",
        board = "madoo",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "magikarp",
        board = "magikarp",
    )
    ec_target(
        name = "magnemite",
        board = "magnemite",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "magolor",
        baseboard = "dedede",
        board = "magolor",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "makomo",
        baseboard = "kukui",
        board = "makomo",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "marasov",
        baseboard = "brya",
        board = "marasov",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "markarth",
        board = "markarth",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "marzipan",
        baseboard = "trogdor",
        board = "marzipan",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "masterball",
        board = "masterball",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "max32660-eval",
        board = "max32660-eval",
        chip = "max32660",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "mchpevb1",
        board = "mchpevb1",
        chip = "mchp",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "meep",
        baseboard = "octopus",
        board = "meep",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "metaknight",
        baseboard = "dedede",
        board = "metaknight",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "minimal-it8xxx2",
        board = "minimal-it8xxx2",
    )
    ec_target(
        name = "minimal-npcx9",
        board = "minimal-npcx9",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "minimal-posix",
        board = "minimal-posix",
    )
    ec_target(
        name = "minimuffin",
        board = "minimuffin",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "zinger",
        zephyr = False,
    )
    ec_target(
        name = "mithrax",
        baseboard = "brya",
        board = "mithrax",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "moli",
        baseboard = "brask",
        board = "moli",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "moonball",
        board = "moonball",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "moonbuggy",
        board = "moonbuggy",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "morphius",
        baseboard = "zork",
        board = "morphius",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "mrbland",
        baseboard = "trogdor",
        board = "mrbland",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "mtlrvpp_m1723",
        board = "mtlrvpp_m1723",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "mtlrvpp_mchp",
        board = "mtlrvpp_mchp",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "mtlrvpp_npcx",
        board = "mtlrvpp_npcx",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "mtlrvpp_pd",
        board = "mtlrvpp_pd",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "munna",
        baseboard = "kukui",
        board = "munna",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "mushu",
        baseboard = "hatch",
        board = "mushu",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "myst",
        board = "myst",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "naktal",
        board = "naktal",
    )
    ec_target(
        name = "nami",
        board = "nami",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nami_fp",
        board = "nami_fp",
        chip = "stm32",
        core = "cortex-m",
        real_board = "nocturne_fp",
        zephyr = False,
    )
    ec_target(
        name = "nautilus",
        board = "nautilus",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nereid",
        board = "nereid",
    )
    ec_target(
        name = "nereid_cx",
        board = "nereid_cx",
    )
    ec_target(
        name = "nightfury",
        baseboard = "hatch",
        board = "nightfury",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nipperkin",
        baseboard = "guybrush",
        board = "nipperkin",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nivviks",
        board = "nivviks",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "nocturne",
        board = "nocturne",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nocturne_fp",
        board = "nocturne_fp",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nokris",
        board = "nokris",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "nova",
        baseboard = "brask",
        board = "nova",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "npcx7",
        board = "npcx7",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "npcx7_evb",
        board = "npcx7_evb",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "npcx9",
        board = "npcx9",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "npcx9_evb",
        board = "npcx9_evb",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "npcx_evb",
        board = "npcx_evb",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "npcx_evb_arm",
        board = "npcx_evb_arm",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nucleo-dartmonkey",
        baseboard = "nucleo-h743zi",
        board = "nucleo-dartmonkey",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nucleo-f072rb",
        board = "nucleo-f072rb",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "nucleo-f411re",
        board = "nucleo-f411re",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nucleo-f412zg",
        baseboard = "nucleo-f412zg",
        board = "nucleo-f412zg",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nucleo-g431rb",
        board = "nucleo-g431rb",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nucleo-h743zi",
        baseboard = "nucleo-h743zi",
        board = "nucleo-h743zi",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "nuwani",
        baseboard = "grunt",
        board = "nuwani",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "oak",
        board = "oak",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "omnigul",
        baseboard = "brya",
        board = "omnigul",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "osiris",
        baseboard = "brya",
        board = "osiris",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "ovis",
        board = "ovis",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "palkia",
        baseboard = "hatch",
        board = "palkia",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "panqueque",
        baseboard = "honeybuns",
        board = "panqueque",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "pazquel",
        baseboard = "trogdor",
        board = "pazquel",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "pdeval-stm32f072",
        board = "pdeval-stm32f072",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "phaser",
        baseboard = "octopus",
        board = "phaser",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "pico",
        baseboard = "kukui",
        board = "pico",
        chip = "it83xx",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "pirika",
        baseboard = "dedede",
        board = "pirika",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "pirrha",
        board = "pirrha",
    )
    ec_target(
        name = "polyberry",
        board = "polyberry",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "pompom",
        baseboard = "trogdor",
        board = "pompom",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "ponyta",
        board = "ponyta",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "poppy",
        board = "poppy",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "primus",
        baseboard = "brya",
        board = "primus",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "prism",
        board = "prism",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "puff",
        board = "puff",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "pujjo",
        board = "pujjo",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "pujjoga",
        board = "pujjoga",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "quackingstick",
        baseboard = "trogdor",
        board = "quackingstick",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "quandiso",
        board = "quandiso",
    )
    ec_target(
        name = "quiche",
        baseboard = "honeybuns",
        board = "quiche",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "rammus",
        board = "rammus",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "rauru",
        board = "rauru",
    )
    ec_target(
        name = "redrix",
        baseboard = "brya",
        board = "redrix",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "reef",
        board = "reef",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "reef_it8320",
        board = "reef_it8320",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "reef_mchp",
        board = "reef_mchp",
        chip = "mchp",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "rex",
        board = "rex",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "rex-ish",
        board = "rex-ish",
        extra_modules = ["cmsis", "hal_intel_public"],
    )
    ec_target(
        name = "rex-ish-ec",
        board = "rex-ish-ec",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "riven",
        board = "riven",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "roach",
        board = "roach",
    )
    ec_target(
        name = "sasuke",
        baseboard = "dedede",
        board = "sasuke",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "sasukette",
        baseboard = "dedede",
        board = "sasukette",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "scout",
        board = "scout",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "screebo",
        board = "screebo",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "servo_micro",
        board = "servo_micro",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "servo_v4",
        board = "servo_v4",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "servo_v4p1",
        board = "servo_v4p1",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "shotzo",
        baseboard = "dedede",
        board = "shotzo",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "shuboz",
        baseboard = "zork",
        board = "shuboz",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "skitty",
        board = "skitty",
    )
    ec_target(
        name = "skyrim",
        board = "skyrim",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "soraka",
        board = "soraka",
        chip = "npcx",
        core = "cortex-m",
        real_board = "poppy",
        zephyr = False,
    )
    ec_target(
        name = "spherion",
        baseboard = "asurada",
        board = "spherion",
        chip = "it83xx",
        core = "riscv-rv32i",
        zephyr = False,
    )
    ec_target(
        name = "spikyrock",
        board = "spikyrock",
    )
    ec_target(
        name = "staff",
        board = "staff",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "star",
        board = "star",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "starmie",
        board = "starmie",
    )
    ec_target(
        name = "steelix",
        board = "steelix",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "stern",
        baseboard = "kukui",
        board = "stern",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "stm32f446e-eval",
        board = "stm32f446e-eval",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "stm32l476g-eval",
        board = "stm32l476g-eval",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "storo",
        baseboard = "dedede",
        board = "storo",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "stryke",
        baseboard = "hatch",
        board = "stryke",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "sundance",
        board = "sundance",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "sweetberry",
        board = "sweetberry",
        chip = "stm32",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "taeko",
        baseboard = "brya",
        board = "taeko",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "taniks",
        baseboard = "brya",
        board = "taniks",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "taranza",
        baseboard = "dedede",
        board = "taranza",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "tentacruel",
        board = "tentacruel",
    )
    ec_target(
        name = "terrador",
        baseboard = "volteer",
        board = "terrador",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "tglrvp_ish",
        board = "tglrvp_ish",
        chip = "ish",
        core = "minute-ia",
        zephyr = False,
    )
    ec_target(
        name = "tigertail",
        board = "tigertail",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "tomato",
        baseboard = "cherry",
        board = "tomato",
        chip = "it83xx",
        core = "riscv-rv32i",
        real_board = "cherry",
        zephyr = False,
    )
    ec_target(
        name = "treeya",
        baseboard = "grunt",
        board = "treeya",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "trembyle",
        baseboard = "zork",
        board = "trembyle",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "trogdor",
        baseboard = "trogdor",
        board = "trogdor",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "twinkie",
        board = "twinkie",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "uldren",
        board = "uldren",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "vell",
        baseboard = "brya",
        board = "vell",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "veluza",
        board = "veluza",
    )
    ec_target(
        name = "vilboz",
        baseboard = "zork",
        board = "vilboz",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "voema",
        baseboard = "volteer",
        board = "voema",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "volet",
        baseboard = "volteer",
        board = "volet",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "volmar",
        baseboard = "brya",
        board = "volmar",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "volteer",
        baseboard = "volteer",
        board = "volteer",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "volteer_ish",
        board = "volteer_ish",
        chip = "ish",
        core = "minute-ia",
        zephyr = False,
    )
    ec_target(
        name = "volteer_npcx797fc",
        baseboard = "volteer",
        board = "volteer_npcx797fc",
        chip = "npcx",
        core = "cortex-m",
        real_board = "volteer",
        zephyr = False,
    )
    ec_target(
        name = "voltorb",
        board = "voltorb",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "voxel",
        baseboard = "volteer",
        board = "voxel",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "voxel_ecmodeentry",
        baseboard = "volteer",
        board = "voxel_ecmodeentry",
        chip = "npcx",
        core = "cortex-m",
        real_board = "voxel",
        zephyr = False,
    )
    ec_target(
        name = "voxel_npcx797fc",
        baseboard = "volteer",
        board = "voxel_npcx797fc",
        chip = "npcx",
        core = "cortex-m",
        real_board = "voxel",
        zephyr = False,
    )
    ec_target(
        name = "waddledee",
        baseboard = "dedede",
        board = "waddledee",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "waddledoo",
        baseboard = "dedede",
        board = "waddledoo",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "waddledoo2",
        baseboard = "keeby",
        board = "waddledoo2",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "wand",
        board = "wand",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "wheelie",
        baseboard = "dedede",
        board = "wheelie",
        chip = "it83xx",
        core = "nds32",
        zephyr = False,
    )
    ec_target(
        name = "whiskers",
        board = "whiskers",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "willow",
        baseboard = "kukui",
        board = "willow",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
    ec_target(
        name = "winterhold",
        board = "winterhold",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "woobat",
        board = "woobat",
    )
    ec_target(
        name = "woomax",
        baseboard = "zork",
        board = "woomax",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "wormdingler",
        baseboard = "trogdor",
        board = "wormdingler",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "wugtrio",
        board = "wugtrio",
    )
    ec_target(
        name = "xivu",
        board = "xivu",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "xivur",
        board = "xivur",
        extra_modules = ["cmsis"],
    )
    ec_target(
        name = "xol",
        baseboard = "brya",
        board = "xol",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "yaviks",
        board = "yaviks",
    )
    ec_target(
        name = "yavilla",
        board = "yavilla",
    )
    ec_target(
        name = "yavista",
        board = "yavista",
    )
    ec_target(
        name = "yorp",
        baseboard = "octopus",
        board = "yorp",
        chip = "npcx",
        core = "cortex-m",
        zephyr = False,
    )
    ec_target(
        name = "zed",
        board = "zed",
        chip = "stm32",
        core = "cortex-m0",
        real_board = "hammer",
        zephyr = False,
    )
    ec_target(
        name = "zinger",
        board = "zinger",
        chip = "stm32",
        core = "cortex-m0",
        zephyr = False,
    )
