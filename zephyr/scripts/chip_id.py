# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"Dict of component ids for CME"

import collections


# A namedtuple to store the information for each compatible
CompatibleInfo = collections.namedtuple(
    "CompatibleInfo",
    [
        "name",
        "pid_low_expect",
        "pid_high_expect",
        "did_low_expect",
        "did_high_expect",
    ],
)

# A Dictionary that stores additional information that may not be stored based
# on their compatible.
# chip_ID and WHO_AM_I are also listed under DID.
DISAMBIGUATION_DICTIONARY = {
    "cros-ec,bma4xx": [
        CompatibleInfo(
            "bosch,bma422",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x12",
            },
            None,
        ),
    ],
    "cros-ec,bma255": [
        CompatibleInfo(
            "bosch,bma255",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0xfa",
            },
            None,
        ),
    ],
    "cros-ec,bmi3xx": [
        CompatibleInfo(
            "bosch,bmi323",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x43",
            },
            None,
        ),
    ],
    "cros-ec,bmi160": [
        CompatibleInfo(
            "bosch,bmi160",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0xd1",
            },
            None,
        ),
        CompatibleInfo(
            "bosch,bmi168",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0xd2",
            },
            None,
        ),
    ],
    "cros-ec,bmi260": [
        CompatibleInfo(
            "bosch,bmi260",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x27",
            },
            None,
        ),
        CompatibleInfo(
            "bosch,bmi220",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x26",
            },
            None,
        ),
    ],
    "cros-ec,icm426xx": [
        CompatibleInfo(
            "invensense,icm42608",
            None,
            None,
            {
                "reg": "0x75",
                "mask": "0xff",
                "value": "0x39",
            },
            None,
        ),
        CompatibleInfo(
            "invensense,icm42605",
            None,
            None,
            {
                "reg": "0x75",
                "mask": "0xff",
                "value": "0x42",
            },
            None,
        ),
    ],
    "cros-ec,icm42607": [
        CompatibleInfo(
            "invensense,icm42607p",
            None,
            None,
            {
                "reg": "0x75",
                "mask": "0xff",
                "value": "0x60",
            },
            None,
        ),
        CompatibleInfo(
            "invensense,icm42608p",
            None,
            None,
            {
                "reg": "0x75",
                "mask": "0xff",
                "value": "0x3f",
            },
            None,
        ),
    ],
    "cros-ec,kx022": [
        CompatibleInfo(
            "kionix,kx022",
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x14",
            },
            None,
        ),
    ],
    "cros-ec,lis2de": [
        CompatibleInfo(
            "st,lis2de",
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x33",
            },
            None,
        ),
    ],
    "cros-ec,lis2ds": [
        CompatibleInfo(
            "st,lis2ds",
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x43",
            },
            None,
        ),
    ],
    "cros-ec,lis2dw12": [
        CompatibleInfo(
            "st,lis2dw12",
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x44",
            },
            None,
        ),
    ],
    "cros-ec,lsm6dsm": [
        CompatibleInfo(
            "st,lsm6dsm",
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x6a",
            },
            None,
        ),
        CompatibleInfo(
            "st,lsm6ds3",
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x69",
            },
            None,
        ),
    ],
    "cros-ec,lsm6dso": [
        CompatibleInfo(
            "st,lsm6dso",
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x6C",
            },
            None,
        ),
    ],
    "cros-ec,tcs3400": [
        CompatibleInfo(
            "ams,tcs340015",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x90",
            },
            None,
        ),
        CompatibleInfo(
            "ams,tcs340037",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x93",
            },
            None,
        ),
    ],
    "parade,ps8xxx": [
        CompatibleInfo(
            "parade,ps8705",
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x05",
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x87",
            },
            None,
            None,
        ),
        CompatibleInfo(
            "parade,ps8745",
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x45",
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x87",
            },
            None,
            None,
        ),
        CompatibleInfo(
            "parade,ps8751",
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x51",
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x87",
            },
            {
                "reg": "0x04",
                "mask": "0xff",
                "value": "0x01",
            },
            None,
        ),
        CompatibleInfo(
            "parade,ps8755",
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x55",
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x87",
            },
            None,
            None,
        ),
        CompatibleInfo(
            "parade,ps8805",
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x05",
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x88",
            },
            None,
            None,
        ),
        CompatibleInfo(
            "parade,ps8815",
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x15",
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x88",
            },
            None,
            None,
        ),
    ],
    "nuvoton,nct38xx": [
        CompatibleInfo(
            "nuvoton,nct3807",
            None,
            None,
            {
                "reg": "0x04",
                "mask": "0xff",
                "value": "0x00",
            },
            None,
        ),
        CompatibleInfo(
            "nuvoton,nct3808",
            None,
            None,
            {
                "reg": "0x04",
                "mask": "0xff",
                "value": "0x08",
            },
            None,
        ),
    ],
    "ti,opt3001": [
        CompatibleInfo(
            "ti,opt3001",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x01",
            },
            {
                "reg": "0x01",
                "mask": "0xff",
                "value": "0x30",
            },
        ),
    ],
    "fairchild,fusb302": [
        CompatibleInfo(
            "fairchild,fusb302",
            None,
            None,
            {
                "reg": "0x01",
                "mask": "0xff",
                "value": "0x90",
            },
            None,
        ),
    ],
    "richtek,rt1715": [
        CompatibleInfo(
            "richtek,rt1715",
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x11",
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x17",
            },
            {
                "reg": "0x04",
                "mask": "0xff",
                "value": "0x73",
            },
            {
                "reg": "0x05",
                "mask": "0xff",
                "value": "0x21",
            },
        ),
    ],
    "richtek,rt1716": [
        CompatibleInfo(
            "richtek,rt1716",
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x11",
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x17",
            },
            {
                "reg": "0x04",
                "mask": "0xff",
                "value": "0x73",
            },
            {
                "reg": "0x05",
                "mask": "0xff",
                "value": "0x21",
            },
        ),
    ],
    "richtek,rt1739": [
        CompatibleInfo(
            "richtek,rt1739",
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x39",
            },
            {
                "reg": "0x01",
                "mask": "0xff",
                "value": "0x17",
            },
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x14",
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x46",
            },
        ),
    ],
    "richtek,rt9490": [
        CompatibleInfo(
            "richtek,rt9490",
            None,
            None,
            {
                "reg": "0x48",
                "mask": "0xff",
                "value": "0x60",
            },
            None,
        ),
    ],
    "intersil,isl923x": [
        CompatibleInfo(
            "intersil,isl9238",
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x0c",
            },
            None,
        ),
        CompatibleInfo(
            "intersil,isl9237",
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x0a",
            },
            None,
        ),
    ],
    "intersil,isl9241": [
        CompatibleInfo(
            "intersil,isl9241",
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x0e",
            },
            None,
        ),
    ],
    "ti,bq25710": [
        CompatibleInfo(
            "ti,bq25710",
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x89",
            },
            None,
        ),
    ],
    "renesas,raa489000": [
        CompatibleInfo(
            "renesas,raa489000",
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x11",
            },
            None,
        ),
    ],
    "siliconmitus,sm5803": [
        CompatibleInfo(
            "siliconmitus,sm5803",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x11",
            },
            None,
        ),
    ],
    "kinetic,ktu1125": [
        CompatibleInfo(
            "kinetic,ktu1125",
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0xa5",
            },
            None,
        ),
    ],
}
