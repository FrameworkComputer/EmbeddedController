# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Component Manifest Engine"""

from copy import deepcopy
import json
import logging
from pathlib import Path
import sys
from typing import List, Optional

import zmake.version

from scripts import chip_id
from scripts import util


# The devicetrees for motionsensors store i2c address as strings. They are
# converted into addresses in a series of .h files. To build the component
# manifest, these addresses are copied into the dictionary below.
# Update the dictionary as needed when more motionsensors are added.
SENSOR_I2C_ADDRESSES = {
    "BMI160_ADDR0_FLAGS": "0x68",
    "BMI260_ADDR0_FLAGS": "0x68",
    "BMA4_I2C_ADDR_PRIMARY": "0x18",
    "BMA4_I2C_ADDR_SECONDARY": "0x19",
    "BMI3_ADDR_I2C_PRIM": "0x68",
    "BMI3_ADDR_I2C_SEC": "0x69",
    "BMA2x2_I2C_ADDR1_FLAGS": "0x18",
    "BMA2x2_I2C_ADDR2_FLAGS": "0x19",
    "BMA2x2_I2C_ADDR3_FLAGS": "0x10",
    "BMA2x2_I2C_ADDR4_FLAGS": "0x11",
    "KX022_ADDR0_FLAGS": "0x1e",
    "KX022_ADDR1_FLAGS": "0x1f",
    "KXCJ9_ADDR0_FLAGS": "0x0E",
    "KXCJ9_ADDR1_FLAGS": "0x0D",
    "LIS2DS_ADDR0_FLAGS": "0x1a",
    "LIS2DS_ADDR1_FLAGS": "0x1e",
    "LIS2DWL_ADDR0_FLAGS": "0x18",
    "LIS2DWL_ADDR1_FLAGS": "0x19",
    "LIS2DH_ADDR0_FLAGS": "0x18",
    "LIS2DH_ADDR1_FLAGS": "0x19",
    "ICM426XX_ADDR0_FLAGS": "0x68",
    "ICM426XX_ADDR1_FLAGS": "0x69",
    "ICM42607_ADDR0_FLAGS": "0x68",
    "ICM42607_ADDR1_FLAGS": "0x69",
    "LSM6DSM_ADDR0_FLAGS": "0x6a",
    "LSM6DSM_ADDR1_FLAGS": "0x6b",
    "LSM6DSO_ADDR0_FLAGS": "0x6a",
    "LSM6DSO_ADDR1_FLAGS": "0x6b",
    "TCS3400_I2C_ADDR_FLAGS": "0x39",
}

# A list of all ALS listed under the motionsense compatible.
# function matches substring so partnumber prefixes can be used:
# e.g tcs34 to match all TCS34XX devices.
# matching is case sensitive, all Zephyr compatibles are lowercase.
ALS_PREFIXES = [
    "tcs34",
    "isl29",
    "opt30",
]

# A list of different possible suffixes in the compatible name for each ctype
CTYPE_SUFFIXES = {
    "pdc": ["pdc"],
    "ppc": ["ppc"],
    "tcpc": ["tcpc"],
    "bc12": ["bc12"],
    "charger": [],  # chargers do not use suffixes
    "als": ["clear"],
    "accel": ["accel", "gyro"],
}


def parse_args(argv: Optional[List[str]] = None):
    """Returns parsed command-line arguments"""
    parser = util.EdtArgumentParser(
        prog="component_manifest_engine",
        description="Zephyr EC component manifest gereration",
    )

    parser.add_argument(
        "--manifest-file",
        type=Path,
        help="Path to the component manifest JSON file in JSON format",
        required=True,
    )

    parser.add_argument(
        "-v",
        "--version",
        help="Base version string to use in build",
    )

    parser.add_argument(
        "--static",
        action="store_true",
        dest="static_version",
        help="Generate static version information for reproducible builds and official builds",
    )
    return parser.parse_args(argv)


def insert_expect(prop, comp, expect):
    """insert an expect into the property of a component

    Args:
        prop: String of the component property.
        comp: Component object.
        expect: Expect field to be inserted.
    """
    if expect is None:
        return

    if "expect" not in comp[prop]:
        comp[prop].update({"expect": []})

    for preexisting_expect in comp[prop]["expect"]:
        if preexisting_expect["reg"] == expect["reg"]:
            logging.error(
                "%s has multiple values expected from the same register: %s",
                comp["component_name"],
                expect["reg"],
            )
            sys.exit(1)

    comp[prop]["expect"].append(expect)


def disambiguify(component):
    """updates information in the a component that may be ambiguous

    Args:
        component: Component object.

    Returns:
        A list of updated components.
    """
    ret = []

    name = component["component_name"]
    if name in chip_id.DISAMBIGUATION_DICTIONARY:
        for comp_info in chip_id.DISAMBIGUATION_DICTIONARY[name]:
            new_comp = deepcopy(component)
            new_comp["component_name"] = comp_info.name

            insert_expect("i2c", new_comp, comp_info.pid_low_expect)
            insert_expect("i2c", new_comp, comp_info.pid_high_expect)
            insert_expect("i2c", new_comp, comp_info.did_low_expect)
            insert_expect("i2c", new_comp, comp_info.did_high_expect)

            ret.append(new_comp)
    else:
        ret.append(component)

    return ret


class Manifest:
    """Manifest class to operate the component manifest."""

    def __init__(self, ec_version):
        self.manifest = {
            "manifest_version": 1,
            "ec_version": ec_version,
            "component_list": [],
        }

    def insert_component(
        self, ctype, name, i2c_port, i2c_addr, usbc_port=None, ssfc=None
    ):
        """Insert the component inform to the component manifest.

        Args:
            ctype: String of component type.
            name: String of component name.
            i2c_port: I2C remote port number.
            i2c_address: I2C device address (7-bit).
            usbc_port: USB-C port number.
            ssfc: SSFC element to be inserted as is.
        """
        component = {
            "component_type": ctype,
            "component_name": name,
            "i2c": {"port": i2c_port, "addr": i2c_addr},
        }
        if usbc_port is not None:
            component.update({"usbc": {"port": usbc_port}})
        if ssfc:
            component.update({"ssfc": ssfc})

        for comp in self.manifest["component_list"]:
            if comp == component:
                return

        comp_list = disambiguify(component)

        for comp in comp_list:
            if comp not in self.manifest["component_list"]:
                self.manifest["component_list"].append(comp)

    def json_dump(self, filepath):
        """Dump the component manifest to a JSON file."""
        with open(filepath, "w", encoding="utf-8") as outfile:
            outfile.write(json.dumps(self.manifest, indent=4))


def node_is_valid(node, i2c_node, i2c_portmap):
    """Checks if a given node can be inserted into the manifest

    Args:
        node: Devicetree node object.
        i2c_node: Devicetree node object for the i2c.
        i2c_portmap: Dict of the mapping from I2C name to remote port number.
    """
    if "compatible" not in node.props:
        logging.info("Compatible not found: %s", node.name)
        return False

    if i2c_node is None or i2c_node.name not in i2c_portmap:
        logging.info(
            "Component %s(%s) not on I2C bus, skip it",
            node.name,
            node.props["compatible"].val[0],
        )
        return False

    return True


def find_i2c_portmap(edtlib, edt):
    """Iterate all the I2C ports and find the mapping.

    Args:
        edtlib: Module object for the edtlib library.
        edt: EDT object representation of a devicetree

    Returns:
        A dict of the mapping between I2C name and remote port number.
    """
    try:
        named_i2c_ports = edt.get_node("/named-i2c-ports")
    except edtlib.EDTError:
        # If the named-i2c-ports node doesn't exist, return success.
        logging.error("No named-i2c-ports node found")
        return {}

    # These compats are EC-chip-specific. List all the compats here.
    i2c_compat_list = [
        "nuvoton,npcx-i2c-port",
        "ite,it8xxx2-i2c",
        "ite,enhance-i2c",
        "microchip,xec-i2c-v2",
        "zephyr,i2c-emul-controller",
        "intel,sedi-i2c",
    ]

    # Append all I2C chip names to a list; its index is the port number.
    i2c_chip_names = []
    for compat in i2c_compat_list:
        for node in edt.compat2okay[compat]:
            i2c_chip_names.append(node.name)

    if not i2c_chip_names:
        logging.info("No I2C port found")
        return {}

    # Find the mapping of the remote_port.
    i2c_portmap = {}
    for label, node in named_i2c_ports.children.items():
        i2c_name = node.props["i2c-port"].val.name
        if "remote-port" in node.props:
            i2c_portmap[i2c_name] = node.props["remote-port"].val
        elif i2c_name in i2c_chip_names:
            i2c_portmap[i2c_name] = i2c_chip_names.index(i2c_name)

        if i2c_name in i2c_portmap:
            logging.debug(
                "i2c remote portmap: %s(%s) -> %d",
                i2c_name,
                label,
                i2c_portmap[i2c_name],
            )
        else:
            logging.warning(
                "Unknown or disabled I2C port: %s(%s)", i2c_name, label
            )

    return i2c_portmap


def build_ssfc_map(edt, project_name):
    """Creates a ssfc dictionary for future insertion

    Because all ssfc masks have to be evaluated in sequence to calculate
    the offset of any individual mask, ssfc dictionary is populated all at
    once then inserted into the manifest one at a time.

    Args:
        edt: EDT object representation of a devicetree.
        project_name: string name of the project or test.

    Returns:
        A dictionary of the mapping between ssfc-value node name and a ssfc
        mask/value manifest element
    """

    cbi_ssfc_nodes = edt.compat2okay["cros-ec,cbi-ssfc"]

    if len(cbi_ssfc_nodes) > 1:
        raise util.DevicetreeError("More than 1 cbi-ssfc node found")

    if len(cbi_ssfc_nodes) == 0:
        logging.info("Project %s does not support SSFC", project_name)
        return None

    ssfc_map = {}
    ssfc_mask_offset = 0
    for ssfc_field in cbi_ssfc_nodes[0].children.values():
        mask_size = ssfc_field.props["size"].val
        mask = (1 << mask_size) - 1
        mask <<= ssfc_mask_offset
        ssfc_mask_offset += mask_size

        for ssfc_value_node in ssfc_field.children.values():
            ssfc_map.update(
                {
                    ssfc_value_node.name: {
                        "mask": hex(mask),
                        "value": ssfc_value_node.props["value"].val,
                    }
                }
            )

    return ssfc_map


def find_ssfc_default(edt, ssfc_map, node, prop):
    """find the ssfc node if sensor is default

    Args:
        edt: EDT object representation of a devicetree
        ssfc_map: Dict of the mapping from ssfc name to ssfc manifest element.
        node: Devicetree node object.
        prop: Devicetree property used to link the ssfc.

    Returns:
        SSFC element to be inserted into the manifest as is
    """

    ssfc_node_list = edt.compat2okay["cros-ec,cbi-ssfc-value"]

    # TODO: add ways to find other ssfc defaults as other sensors are supported
    if prop == "location":
        if node.props[prop].val == "MOTIONSENSE_LOC_BASE":
            enum_name = "BASE_SENSOR"
        elif node.props[prop].val == "MOTIONSENSE_LOC_LID":
            enum_name = "LID_SENSOR"
        else:
            return None

    for snode in ssfc_node_list:
        if (
            snode.props["default"].val
            and enum_name == snode.parent.props["enum-name"].val
        ):
            return ssfc_map[snode.name]

    return None


def compatible_name_parser(ctype, name):
    """Parses the compatible into actual component name.

    Args:
        ctype: type of component
        name: compatible name for component

    Returns:
        Corrected component name
    """

    compatible = name.rsplit("-", 1)
    if len(compatible) > 1:
        for suffix in CTYPE_SUFFIXES[ctype]:
            if compatible[1] == suffix:
                return compatible[0]

    # TODO: add more cases for other compatibles as need arrises.

    # it is valid for compatibles to have a hyphen without any ctype
    return name


def insert_i2c_component(ctype, node, usbc_port, i2c_portmap, manifest):
    """Insert the I2C component to the manifest.

    Args:
        node: Devicetree node object.
        usbc_port: USB-C port number
        i2c_portmap: Dict of the mapping from I2C name to remote port number.
        manifest: Manifest object.
    """

    # go up the devicetree to find the i2c ancestor node
    i2c_node = node.parent
    while i2c_node and i2c_node.name not in i2c_portmap:
        i2c_node = i2c_node.parent

    reg_node = node
    while reg_node and "reg" not in reg_node.props:
        reg_node = reg_node.parent

    if not node_is_valid(node, i2c_node, i2c_portmap):
        return

    # TODO(b/308031064): Add the probe methods if multiple components share the
    # same compatible. These components need to be identified.

    # TODO(b/308031075): Add the SSFC field.

    manifest.insert_component(
        ctype,
        compatible_name_parser(ctype, node.props["compatible"].val[0]),
        i2c_portmap[i2c_node.name],
        hex(reg_node.props["reg"].val[0]),
        usbc_port,
    )


def iterate_usbc_components(edtlib, edt, i2c_portmap, manifest):
    """Iterate all USB-C components and insert them to the manifest.

    Args:
        edtlib: Module object for the edtlib library.
        edt: EDT object representation of a devicetree
        i2c_portmap: Dict of the mapping from I2C name to remote port number.
        manifest: Manifest object.
    """
    try:
        usbc = edt.get_node("/usbc")
    except edtlib.EDTError:
        # If the usbc node doesn't exist, return success.
        logging.info("No usbc node found")
        return

    for node in usbc.children.values():
        # Ignore the child node without a port number.
        if "reg" not in node.props:
            continue
        port = node.props["reg"].val[0]

        for prop in ["chg", "chg_alt"]:
            if prop in node.props:
                chg = node.props[prop].val
                insert_i2c_component(
                    "charger", chg, port, i2c_portmap, manifest
                )

        if "bc12" in node.props:
            bc12 = node.props["bc12"].val
            insert_i2c_component("bc12", bc12, port, i2c_portmap, manifest)

        for prop in ["ppc", "ppc_alt"]:
            if prop in node.props:
                ppc = node.props[prop].val
                insert_i2c_component("ppc", ppc, port, i2c_portmap, manifest)

        if "tcpc" in node.props:
            tcpc = node.props["tcpc"].val
            insert_i2c_component("tcpc", tcpc, port, i2c_portmap, manifest)

        if "pdc" in node.props:
            pdc = node.props["pdc"].val
            insert_i2c_component("pdc", pdc, port, i2c_portmap, manifest)


def insert_motionsense_component(
    node, edt, i2c_portmap, ssfc_map, manifest, is_alt
):
    """Insert the motion sense component to the manifest.

    Args:
        node: Devicetree node object.
        edt: EDT object representation of a devicetree.
        i2c_portmap: Dict of the mapping from I2C name to remote port number.
        ssfc_map: Dict of the mapping from ssfc name to ssfc manifest element.
        manifest: Manifest object.
        is_alt: boolean for if this component is alt sensor.
    """
    if "port" in node.props:
        i2c = node.props["port"].val.props["i2c-port"].val
    else:
        logging.info(
            "Component %s(%s) not on I2C bus, skip it",
            node.name,
            node.props["compatible"].val[0],
        )
        return
    if not node_is_valid(node, i2c, i2c_portmap):
        return

    i2c_addr = node.props["i2c-spi-addr-flags"].val
    if i2c_addr in SENSOR_I2C_ADDRESSES:
        i2c_addr_val = SENSOR_I2C_ADDRESSES[i2c_addr]
    else:
        logging.error(
            "i2c address for %s is %s. The reg value is unknown. \
            Please add the address value to SENSOR_I2C_ADDRESSES",
            node.props["compatible"].val[0],
            i2c_addr,
        )
        sys.exit(1)

    compatible_name = node.props["compatible"].val[0]

    ctype = "accel"
    for part_number in ALS_PREFIXES:
        if "," + part_number in compatible_name:
            ctype = "als"

    if not is_alt:
        ssfc_element = find_ssfc_default(edt, ssfc_map, node, "location")
    elif "alternate-ssfc-indicator" in node.props:
        ssfc_element = ssfc_map[node.props["alternate-ssfc-indicator"].val.name]
    else:
        ssfc_element = None

    manifest.insert_component(
        ctype,
        compatible_name_parser(ctype, compatible_name),
        i2c_portmap[i2c.name],
        i2c_addr_val,
        ssfc=ssfc_element,
    )


def iterate_motionsensor_components(
    edtlib, edt, i2c_portmap, ssfc_map, manifest
):
    """Iterate all motion sensor components and insert them to the manifest.

    Args:
        edtlib: Module object for the edtlib library.
        edt: EDT object representation of a devicetree
        i2c_portmap: Dict of the mapping from I2C name to remote port number.
        ssfc_map: Dict of the mapping from ssfc name to ssfc manifest element.
        manifest: Manifest object.
    """
    try:
        mss = edt.get_node("/motionsense-sensor")
    except edtlib.EDTError:
        # If the motionsense-sensor node doesn't exist, return success.
        logging.info("No motionsense-sensor node found")
        return

    for node in mss.children.values():
        insert_motionsense_component(
            node, edt, i2c_portmap, ssfc_map, manifest, is_alt=False
        )

    try:
        mss_alt = edt.get_node("/motionsense-sensor-alt")
    except edtlib.EDTError:
        # If the motionsense-sensor-alt node doesn't exist, return success.
        logging.info("No motionsense-sensor-alt node found")
        return

    for node in mss_alt.children.values():
        insert_motionsense_component(
            node, edt, i2c_portmap, ssfc_map, manifest, is_alt=True
        )


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """The main function.

    Args:
        argv: Optionally, the command-line to parse, not including argv[0].

    Returns:
        Zero upon success, or non-zero upon failure.
    """
    args = parse_args(argv)

    log_format = "%(levelname)s: %(message)s"
    logging.basicConfig(format=log_format, level=args.log_level)

    edtlib, edt, build_dir = util.load_edt(args.zephyr_base, args.edt_pickle)
    if edtlib is None:
        return 0

    logging.info("Running CME, outputting to %s", args.manifest_file)
    i2c_portmap = find_i2c_portmap(edtlib, edt)
    try:
        ssfc_map = build_ssfc_map(edt, build_dir.name)
    except util.DevicetreeError as err_msg:
        logging.error(err_msg)
        return 1

    # Compute the version string.
    if util.is_test(args.edt_pickle):
        ec_version_string = "test_build"
    else:
        ec_version_string = zmake.version.get_version_string(
            build_dir.name,
            version=args.version,
            static=args.static_version,
        )
    manifest = Manifest(ec_version_string)

    iterate_usbc_components(edtlib, edt, i2c_portmap, manifest)

    iterate_motionsensor_components(
        edtlib, edt, i2c_portmap, ssfc_map, manifest
    )

    # TODO(b/308028560): Iterate all sensor components.

    manifest.json_dump(args.manifest_file)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
