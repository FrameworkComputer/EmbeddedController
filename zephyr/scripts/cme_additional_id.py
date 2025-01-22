# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=line-too-long
"Dict of components for CME that are identified by probing a different component"

import collections


# A namedtuple to store the information for each compatible
AdditionalInfo = collections.namedtuple(
    "AdditionalInfo",
    [
        "name",
        "ctype",
        "port",
        "command_1",
        "command_2",
    ],
)

# A Dictionary that stores component information that are identified by config information
# on a different component.
# currently only used for PDC + retimer
ADDITIONAL_DICTIONARY = {
    "realtek,rts54": [
        AdditionalInfo(
            "realtek,jhl8040",
            "mux",
            None,
            {
                "reg": "0x3A",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "01" on the 31 and 32 byte
                "multi_byte_mask": "0x00000000000000000000000000000000000000000000000000000000000000ffff0000000000",
                "multi_byte_value": "0x0000000000000000000000000000000000000000000000000000000000000030310000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "realtek,jhl8040",
            "mux",
            None,
            {
                "reg": "0x3A",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "04" on the 31 and 32 byte
                "multi_byte_mask": "0x00000000000000000000000000000000000000000000000000000000000000ffff0000000000",
                "multi_byte_value": "0x0000000000000000000000000000000000000000000000000000000000000030340000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "realtek,jhl8040",
            "mux",
            None,
            {
                "reg": "0x3A",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "05" on the 31 and 32 byte
                "multi_byte_mask": "0x00000000000000000000000000000000000000000000000000000000000000ffff0000000000",
                "multi_byte_value": "0x0000000000000000000000000000000000000000000000000000000000000030350000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "parade,ps8762",
            "mux",
            0,
            {
                "reg": "0x3A",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "06" on the 31 and 32 byte
                "multi_byte_mask": "0x00000000000000000000000000000000000000000000000000000000000000ffff0000000000",
                "multi_byte_value": "0x0000000000000000000000000000000000000000000000000000000000000030360000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "ite,it5205",
            "mux",
            1,
            {
                "reg": "0x3A",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "06" on the 31 and 32 byte
                "multi_byte_mask": "0x00000000000000000000000000000000000000000000000000000000000000ffff0000000000",
                "multi_byte_value": "0x0000000000000000000000000000000000000000000000000000000000000030360000000000",
                "bytes": 38,
            },
        ),
        AdditionalInfo(
            "realtek,jhl8040",
            "mux",
            None,
            {
                "reg": "0x3A",
                "bytes": 0,
            },
            {
                "reg": "0x80",
                # the ascii characters "08" on the 31 and 32 byte
                "multi_byte_mask": "0x00000000000000000000000000000000000000000000000000000000000000ffff0000000000",
                "multi_byte_value": "0x0000000000000000000000000000000000000000000000000000000000000030380000000000",
                "bytes": 38,
            },
        ),
    ],
}
