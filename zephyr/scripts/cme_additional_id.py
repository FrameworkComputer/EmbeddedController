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
            "intel,jhl8040",
            "mux",
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
        ),
        AdditionalInfo(
            "intel,jhl9040",
            "mux",
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
        ),
        AdditionalInfo(
            "intel,jhl9040",
            "mux",
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
        ),
        AdditionalInfo(
            "parade,ps8762",
            "mux",
            0,
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
        ),
        AdditionalInfo(
            "ite,it5205",
            "mux",
            1,
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
        ),
        AdditionalInfo(
            "intel,jhl9040",
            "mux",
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
        ),
        AdditionalInfo(
            "parade,ps8747",
            "mux",
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
        ),
        AdditionalInfo(
            "ti,tusb546",
            "mux",
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
        ),
        AdditionalInfo(
            "ite,it5205",
            "mux",
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
        ),
        AdditionalInfo(
            "ite,it5205",
            "mux",
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
        ),
        AdditionalInfo(
            "ti,tusb1044",
            "mux",
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
        ),
        AdditionalInfo(
            "ti,tusb1044",
            "mux",
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
        ),
        AdditionalInfo(
            "intel,jhl9040",
            "mux",
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
        ),
        AdditionalInfo(
            "parade,ps8762",
            "mux",
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
        ),
        AdditionalInfo(
            "intel,jhl9040",
            "mux",
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
        ),
        AdditionalInfo(
            "realtek,rts5460z",
            "mux",
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
        ),
        AdditionalInfo(
            "parade,ps8747",
            "mux",
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
        ),
        AdditionalInfo(
            "ite,it5205",
            "mux",
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
        ),
        AdditionalInfo(
            "ti,tusb546",
            "mux",
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
        ),
    ],
}
