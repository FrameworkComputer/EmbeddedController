# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=line-too-long
# pylint: disable=too-many-lines
"Dict of component ids for CME"

import collections


# A namedtuple to store the information for each compatible
CompatibleInfo = collections.namedtuple(
    "CompatibleInfo",
    [
        "name",
        "component_type",
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
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x12",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,bma255": [
        CompatibleInfo(
            "bosch,bma255",
            None,
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0xfa",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,bmi3xx": [
        CompatibleInfo(
            "bosch,bmi323",
            None,
            None,
            None,
            {
                "reg": "0x00",
                "multi_byte_mask": "0x0000ff00",
                "multi_byte_value": "0x00004300",
                "bytes": 4,
            },
            None,
        ),
    ],
    "cros-ec,bmi160": [
        CompatibleInfo(
            "bosch,bmi160",
            None,
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0xd1",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "bosch,bmi168",
            None,
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0xd2",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,bmi260": [
        CompatibleInfo(
            "bosch,bmi260",
            None,
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x27",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "bosch,bmi220",
            None,
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x26",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,icm426xx": [
        CompatibleInfo(
            "invensense,icm42608",
            None,
            None,
            None,
            {
                "reg": "0x75",
                "mask": "0xff",
                "value": "0x39",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "invensense,icm42605",
            None,
            None,
            None,
            {
                "reg": "0x75",
                "mask": "0xff",
                "value": "0x42",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,icm42607": [
        CompatibleInfo(
            "invensense,icm42607p",
            None,
            None,
            None,
            {
                "reg": "0x75",
                "mask": "0xff",
                "value": "0x60",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "invensense,icm42608p",
            None,
            None,
            None,
            {
                "reg": "0x75",
                "mask": "0xff",
                "value": "0x3f",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,kx022": [
        CompatibleInfo(
            "kionix,kx022",
            None,
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x14",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,lis2de": [
        CompatibleInfo(
            "st,lis2de",
            None,
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x33",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,lis2ds": [
        CompatibleInfo(
            "st,lis2ds",
            None,
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x43",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,lis2dw12": [
        CompatibleInfo(
            "st,lis2dw12",
            None,
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x44",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,lsm6dsm": [
        CompatibleInfo(
            "st,lsm6dsm",
            None,
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x6a",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "st,lsm6ds3",
            None,
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x69",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,lsm6dso": [
        CompatibleInfo(
            "st,lsm6dso",
            None,
            None,
            None,
            {
                "reg": "0x0f",
                "mask": "0xff",
                "value": "0x6C",
                "bytes": 1,
            },
            None,
        ),
    ],
    "cros-ec,tcs3400": [
        CompatibleInfo(
            "ams,tcs340015",
            None,
            None,
            None,
            {
                "reg": "0x92",
                "mask": "0xff",
                "value": "0x90",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "ams,tcs340037",
            None,
            None,
            None,
            {
                "reg": "0x92",
                "mask": "0xff",
                "value": "0x93",
                "bytes": 1,
            },
            None,
        ),
    ],
    "parade,ps8xxx": [
        CompatibleInfo(
            "parade,ps8705",
            None,
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x05",
                "bytes": 1,
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x87",
                "bytes": 1,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "parade,ps8745",
            None,
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x15",
                "bytes": 1,
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x88",
                "bytes": 1,
            },
            {
                "reg": "0x2c",
                # this mask is untriggerable on purpose to indicate the use of overrides
                "mask": "0x00",
                "value": "0x01",
                "override_addr": "0x8",
                "override_mask": "0x02",
                "override_value": "0x02",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "parade,ps8751",
            None,
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x51",
                "bytes": 1,
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x87",
                "bytes": 1,
            },
            {
                "reg": "0x04",
                "mask": "0xff",
                "value": "0x01",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "parade,ps8755",
            None,
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x55",
                "bytes": 1,
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x87",
                "bytes": 1,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "parade,ps8805",
            None,
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x05",
                "bytes": 1,
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x88",
                "bytes": 1,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "parade,ps8815",
            None,
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x15",
                "bytes": 1,
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x88",
                "bytes": 1,
            },
            {
                "reg": "0x2c",
                # the default value is auto pass
                "mask": "0x00",
                "value": "0x00",
                "override_addr": "0x8",
                "override_mask": "0x02",
                "override_value": "0x00",
                "bytes": 1,
            },
            None,
        ),
    ],
    "nuvoton,nct38xx": [
        CompatibleInfo(
            "nuvoton,nct3807",
            None,
            None,
            None,
            {
                "reg": "0x04",
                "mask": "0xff",
                "value": "0x00",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "nuvoton,nct3808",
            None,
            None,
            None,
            {
                "reg": "0x04",
                "mask": "0xff",
                "value": "0x08",
                "bytes": 1,
            },
            None,
        ),
    ],
    "ti,opt3001": [
        CompatibleInfo(
            "ti,opt3001",
            None,
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x01",
                "bytes": 1,
            },
            {
                "reg": "0x01",
                "mask": "0xff",
                "value": "0x30",
                "bytes": 1,
            },
        ),
    ],
    "fairchild,fusb302": [
        CompatibleInfo(
            "fairchild,fusb302",
            None,
            None,
            None,
            {
                "reg": "0x01",
                "mask": "0xff",
                "value": "0x90",
                "bytes": 1,
            },
            None,
        ),
    ],
    # TODO(b/394715973): rt1715 and rt1716 use the same DID.
    # Figure out how to differentiate between the 2
    #    "richtek,rt1715": [
    #        CompatibleInfo(
    #            "richtek,rt1715",
    #            {
    #                "reg": "0x02",
    #                "mask": "0xff",
    #                "value": "0x11",
    #                "bytes": 1,
    #            },
    #            {
    #                "reg": "0x03",
    #                "mask": "0xff",
    #                "value": "0x17",
    #                "bytes": 1,
    #            },
    #            {
    #                "reg": "0x04",
    #                "mask": "0xff",
    #                "value": "0x73",
    #                "bytes": 1,
    #            },
    #            {
    #                "reg": "0x05",
    #                "mask": "0xff",
    #                "value": "0x21",
    #                "bytes": 1,
    #            },
    #        ),
    #    ],
    "richtek,rt1715": [
        CompatibleInfo(
            "richtek,rt1716",
            None,
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x11",
                "bytes": 1,
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x17",
                "bytes": 1,
            },
            {
                "reg": "0x04",
                "mask": "0xff",
                "value": "0x73",
                "bytes": 1,
            },
            {
                "reg": "0x05",
                "mask": "0xff",
                "value": "0x21",
                "bytes": 1,
            },
        ),
    ],
    "richtek,rt1739": [
        CompatibleInfo(
            "richtek,rt1739",
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x39",
                "bytes": 1,
            },
            {
                "reg": "0x01",
                "mask": "0xff",
                "value": "0x17",
                "bytes": 1,
            },
            {
                "reg": "0x02",
                "mask": "0xff",
                "value": "0x14",
                "bytes": 1,
            },
            {
                "reg": "0x03",
                "mask": "0xff",
                "value": "0x46",
                "bytes": 1,
            },
        ),
    ],
    "richtek,rt9478": [
        CompatibleInfo(
            "richtek,rt9478",
            None,
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x1c",
                "bytes": 1,
            },
            {
                "reg": "0xfe",
                "mask": "0xff",
                "value": "0x1e",
                "bytes": 1,
            },
        ),
    ],
    "richtek,rt9490": [
        CompatibleInfo(
            "richtek,rt9490",
            None,
            None,
            None,
            {
                "reg": "0x48",
                "mask": "0xff",
                "value": "0x60",
                "bytes": 1,
            },
            None,
        ),
    ],
    "intersil,isl923x": [
        CompatibleInfo(
            "intersil,isl9238",
            None,
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x0c",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "intersil,isl9237",
            None,
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x0a",
                "bytes": 1,
            },
            None,
        ),
    ],
    "intersil,isl9241": [
        CompatibleInfo(
            "intersil,isl9241",
            None,
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x0e",
                "bytes": 1,
            },
            None,
        ),
    ],
    "ti,bq25710": [
        CompatibleInfo(
            "ti,bq25710",
            None,
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x89",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "ti,bq25720",
            None,
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0xe1",
                "bytes": 1,
            },
            None,
        ),
    ],
    "renesas,raa489000": [
        CompatibleInfo(
            "renesas,raa489000",
            "charger",
            None,
            None,
            {
                "reg": "0xff",
                "mask": "0xff",
                "value": "0x11",
                "bytes": 1,
            },
            None,
        ),
        CompatibleInfo(
            "renesas,raa489000",
            "tcpc",
            None,
            None,
            {
                "reg": "0x00",
                "multi_byte_mask": "0xffff",
                "multi_byte_value": "0x5b04",
                "bytes": 2,
            },
            None,
        ),
    ],
    "siliconmitus,sm5803": [
        CompatibleInfo(
            "siliconmitus,sm5803",
            None,
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0x11",
                "bytes": 1,
            },
            None,
        ),
    ],
    "kinetic,ktu1125": [
        CompatibleInfo(
            "kinetic,ktu1125",
            None,
            None,
            None,
            {
                "reg": "0x00",
                "mask": "0xff",
                "value": "0xa5",
                "bytes": 1,
            },
            None,
        ),
    ],
    "realtek,rts54": [
        CompatibleInfo(
            "realtek,rts5453p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG00" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730300000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG01" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730310000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG03" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730330000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG04" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730340000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG05" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730350000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG06" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730360000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG08" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730380000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG09" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730390000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0B" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730420000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0C" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730430000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0D" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730440000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0E" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730450000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0F" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730460000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0G" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730470000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0H" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730480000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p-vb",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0L" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f47304C0000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p-vb",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0N" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f47304E0000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p-vb",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0O" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f47304F0000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p-vb",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0P" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730500000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p-vb",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0R" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730520000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0S" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730530000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p-vb",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0T" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730540000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p-vb",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0U" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730550000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5452p-vb",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0W" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730570000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
        CompatibleInfo(
            "realtek,rts5453p-vb",
            None,
            {
                # the combined command string is 0x3A 0x3 0x0 0x0 0x26
                "reg": "0x3A",
                "write_data": "0x03000026",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "GOOG0X" on the 27-32 bytes
                "multi_byte_mask": "0x000000000000000000000000000000000000000000000000000000ffffffffffff0000000000",
                "multi_byte_value": "0x000000000000000000000000000000000000000000000000000000474f4f4730580000000000",
                "bytes": 38,
            },
            None,
            None,
        ),
    ],
}
