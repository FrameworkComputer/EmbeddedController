#!/usr/bin/env python
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Program to fetch power logging data from a sweetberry device
   or other usb device that exports a USB power logging interface.
"""

# Note: This is a py2/3 compatible file.

from __future__ import print_function

import argparse
import array
from distutils import sysconfig
import json
import logging
import os
import pprint
import struct
import sys
import time
import traceback

from stats_manager import StatsManager  # pylint:disable=import-error
import usb  # pylint:disable=import-error


# Directory where hdctools installs configuration files into.
LIB_DIR = os.path.join(
    sysconfig.get_python_lib(standard_lib=False), "servo", "data"
)

# Potential config file locations: current working directory, the same directory
# as powerlog.py file or LIB_DIR.
CONFIG_LOCATIONS = [
    os.getcwd(),
    os.path.dirname(os.path.realpath(__file__)),
    LIB_DIR,
]


def logoutput(msg):
    print(msg)
    sys.stdout.flush()


def process_filename(filename):
    """Find the file path from the filename.

    If filename is already the complete path, return that directly. If filename is
    just the short name, look for the file in the current working directory, in
    the directory of the current .py file, and then in the directory installed by
    hdctools. If the file is found, return the complete path of the file.

    Args:
        filename: complete file path or short file name.

    Returns:
        a complete file path.

    Raises:
        IOError if filename does not exist.
    """
    # Check if filename is absolute path.
    if os.path.isabs(filename) and os.path.isfile(filename):
        return filename
    # Check if filename is relative to a known config location.
    for dirname in CONFIG_LOCATIONS:
        file_at_dir = os.path.join(dirname, filename)
        if os.path.isfile(file_at_dir):
            return file_at_dir
    raise IOError("No such file or directory: '%s'" % filename)


class Spower(object):
    """Power class to access devices on the bus.

    Usage:
      bus = Spower()

    Instance Variables:
      _dev: pyUSB device object
      _read_ep: pyUSB read endpoint for this interface
      _write_ep: pyUSB write endpoint for this interface
    """

    # INA interface type.
    INA_POWER = 1
    INA_BUSV = 2
    INA_CURRENT = 3
    INA_SHUNTV = 4
    # INA_SUFFIX is used to differentiate multiple ina types for the same power
    # rail. No suffix for when ina type is 0 (non-existent) and when ina type is 1
    # (power, no suffix for backward compatibility).
    INA_SUFFIX = ["", "", "_busv", "_cur", "_shuntv"]

    # usb power commands
    CMD_RESET = 0x0000
    CMD_STOP = 0x0001
    CMD_ADDINA = 0x0002
    CMD_START = 0x0003
    CMD_NEXT = 0x0004
    CMD_SETTIME = 0x0005

    # Map between header channel number (0-47)
    # and INA I2C bus/addr on sweetberry.
    CHMAP = {
        0: (3, 0x40),
        1: (1, 0x40),
        2: (2, 0x40),
        3: (0, 0x40),
        4: (3, 0x41),
        5: (1, 0x41),
        6: (2, 0x41),
        7: (0, 0x41),
        8: (3, 0x42),
        9: (1, 0x42),
        10: (2, 0x42),
        11: (0, 0x42),
        12: (3, 0x43),
        13: (1, 0x43),
        14: (2, 0x43),
        15: (0, 0x43),
        16: (3, 0x44),
        17: (1, 0x44),
        18: (2, 0x44),
        19: (0, 0x44),
        20: (3, 0x45),
        21: (1, 0x45),
        22: (2, 0x45),
        23: (0, 0x45),
        24: (3, 0x46),
        25: (1, 0x46),
        26: (2, 0x46),
        27: (0, 0x46),
        28: (3, 0x47),
        29: (1, 0x47),
        30: (2, 0x47),
        31: (0, 0x47),
        32: (3, 0x48),
        33: (1, 0x48),
        34: (2, 0x48),
        35: (0, 0x48),
        36: (3, 0x49),
        37: (1, 0x49),
        38: (2, 0x49),
        39: (0, 0x49),
        40: (3, 0x4A),
        41: (1, 0x4A),
        42: (2, 0x4A),
        43: (0, 0x4A),
        44: (3, 0x4B),
        45: (1, 0x4B),
        46: (2, 0x4B),
        47: (0, 0x4B),
    }

    def __init__(
        self, board, vendor=0x18D1, product=0x5020, interface=1, serialname=None
    ):
        self._logger = logging.getLogger(__name__)
        self._board = board

        # Find the stm32.
        dev_g = usb.core.find(idVendor=vendor, idProduct=product, find_all=True)
        dev_list = list(dev_g)
        if not dev_list:
            raise Exception("Power", "USB device not found")

        # Check if we have multiple stm32s and we've specified the serial.
        dev = None
        if serialname:
            for d in dev_list:
                dev_serial = "PyUSB dioesn't have a stable interface"
                try:
                    dev_serial = usb.util.get_string(d, 256, d.iSerialNumber)
                except ValueError:
                    # Incompatible pyUsb version.
                    dev_serial = usb.util.get_string(d, d.iSerialNumber)
                if dev_serial == serialname:
                    dev = d
                    break
            if dev is None:
                raise Exception(
                    "Power", "USB device(%s) not found" % serialname
                )
        else:
            dev = dev_list[0]

        self._logger.debug("Found USB device: %04x:%04x", vendor, product)
        self._dev = dev

        # Get an endpoint instance.
        try:
            dev.set_configuration()
        except usb.USBError:
            pass
        cfg = dev.get_active_configuration()

        intf = usb.util.find_descriptor(
            cfg,
            custom_match=lambda i: i.bInterfaceClass == 255
            and i.bInterfaceSubClass == 0x54,
        )

        self._intf = intf
        self._logger.debug("InterfaceNumber: %s", intf.bInterfaceNumber)

        read_ep = usb.util.find_descriptor(
            intf,
            # match the first IN endpoint
            custom_match=lambda e: usb.util.endpoint_direction(
                e.bEndpointAddress
            )
            == usb.util.ENDPOINT_IN,
        )

        self._read_ep = read_ep
        self._logger.debug("Reader endpoint: 0x%x", read_ep.bEndpointAddress)

        write_ep = usb.util.find_descriptor(
            intf,
            # match the first OUT endpoint
            custom_match=lambda e: usb.util.endpoint_direction(
                e.bEndpointAddress
            )
            == usb.util.ENDPOINT_OUT,
        )

        self._write_ep = write_ep
        self._logger.debug("Writer endpoint: 0x%x", write_ep.bEndpointAddress)

        self.clear_ina_struct()

        self._logger.debug("Found power logging USB endpoint.")

    def clear_ina_struct(self):
        """Clear INA description struct."""
        self._inas = []

    def append_ina_struct(
        self, name, rs, port, addr, data=None, ina_type=INA_POWER
    ):
        """Add an INA descriptor into the list of active INAs.

        Args:
          name:	Readable name of this channel.
          rs:	Sense resistor value in ohms, floating point.
          port:	I2C channel this INA is connected to.
          addr: 	I2C addr of this INA.
          data:	Misc data for special handling, board specific.
          ina_type: INA function to use, power, voltage, etc.
        """
        ina = {}
        ina["name"] = name
        ina["rs"] = rs
        ina["port"] = port
        ina["addr"] = addr
        ina["type"] = ina_type
        # Calculate INA231 Calibration register
        # (see INA231 spec p.15)
        # CurrentLSB = uA per div = 80mV / (Rsh * 2^15)
        # CurrentLSB uA = 80000000nV / (Rsh mOhm * 0x8000)
        ina["uAscale"] = 80000000.0 / (rs * 0x8000)
        ina["uWscale"] = 25.0 * ina["uAscale"]
        ina["mVscale"] = 1.25
        ina["uVscale"] = 2.5
        ina["data"] = data
        self._inas.append(ina)

    def wr_command(self, write_list, read_count=1, wtimeout=100, rtimeout=1000):
        """Write command to logger logic.

        This function writes byte command values list to stm, then reads
        byte status.

        Args:
          write_list: list of command byte values [0~255].
          read_count: number of status byte values to read.

        Interface:
          write: [command, data ... ]
          read: [status ]

        Returns:
          bytes read, or None on failure.
        """
        self._logger.debug(
            "Spower.wr_command(write_list=[%s] (%d), read_count=%s)",
            list(bytearray(write_list)),
            len(write_list),
            read_count,
        )

        # Clean up args from python style to correct types.
        write_length = 0
        if write_list:
            write_length = len(write_list)
        if not read_count:
            read_count = 0

        # Send command to stm32.
        if write_list:
            cmd = write_list
            ret = self._write_ep.write(cmd, wtimeout)

        self._logger.debug("RET: %s ", ret)

        # Read back response if necessary.
        if read_count:
            bytesread = self._read_ep.read(512, rtimeout)
            self._logger.debug("BYTES: [%s]", bytesread)

            if len(bytesread) != read_count:
                pass

            self._logger.debug("STATUS: 0x%02x", int(bytesread[0]))
            if read_count == 1:
                return bytesread[0]
            else:
                return bytesread

        return None

    def clear(self):
        """Clear pending reads on the stm32"""
        try:
            while True:
                ret = self.wr_command(
                    b"", read_count=512, rtimeout=100, wtimeout=50
                )
                self._logger.debug(
                    "Try Clear: read %s", "success" if ret == 0 else "failure"
                )
        except:
            pass

    def send_reset(self):
        """Reset the power interface on the stm32"""
        cmd = struct.pack("<H", self.CMD_RESET)
        ret = self.wr_command(cmd, rtimeout=50, wtimeout=50)
        self._logger.debug(
            "Command RESET: %s", "success" if ret == 0 else "failure"
        )

    def reset(self):
        """Try resetting the USB interface until success.

        Use linear back off strategy when encounter the error with 10ms increment.

        Raises:
          Exception on failure.
        """
        max_reset_retry = 100
        for count in range(1, max_reset_retry + 1):
            self.clear()
            try:
                self.send_reset()
                return
            except Exception as e:
                self.clear()
                self.clear()
                self._logger.debug(
                    "TRY %d of %d: %s", count, max_reset_retry, e
                )
                time.sleep(count * 0.01)
        raise Exception("Power", "Failed to reset")

    def stop(self):
        """Stop any active data acquisition."""
        cmd = struct.pack("<H", self.CMD_STOP)
        ret = self.wr_command(cmd)
        self._logger.debug(
            "Command STOP: %s", "success" if ret == 0 else "failure"
        )

    def start(self, integration_us):
        """Start data acquisition.

        Args:
          integration_us: int, how many us between samples, and
                          how often the data block must be read.

        Returns:
          actual sampling interval in ms.
        """
        cmd = struct.pack("<HI", self.CMD_START, integration_us)
        read = self.wr_command(cmd, read_count=5)
        actual_us = 0
        if len(read) == 5:
            ret, actual_us = struct.unpack("<BI", read)
            self._logger.debug(
                "Command START: %s %dus",
                "success" if ret == 0 else "failure",
                actual_us,
            )
        else:
            self._logger.debug("Command START: FAIL")

        return actual_us

    def add_ina_name(self, name_tuple):
        """Add INA from board config.

        Args:
          name_tuple:	name and type of power rail in board config.

        Returns:
          True if INA added, False if the INA is not on this board.

        Raises:
          Exception on unexpected failure.
        """
        name, ina_type = name_tuple

        for datum in self._brdcfg:
            if datum["name"] == name:
                rs = int(float(datum["rs"]) * 1000.0)
                board = datum["sweetberry"]

                if board == self._board:
                    if "port" in datum and "addr" in datum:
                        port = datum["port"]
                        addr = datum["addr"]
                    else:
                        channel = int(datum["channel"])
                        port, addr = self.CHMAP[channel]
                    self.add_ina(port, ina_type, addr, 0, rs, data=datum)
                    return True
                else:
                    return False
        raise Exception("Power", "Failed to find INA %s" % name)

    def set_time(self, timestamp_us):
        """Set sweetberry time to match host time.

        Args:
          timestamp_us: host timestmap in us.
        """
        # 0x0005 , 8 byte timestamp
        cmd = struct.pack("<HQ", self.CMD_SETTIME, timestamp_us)
        ret = self.wr_command(cmd)

        self._logger.debug(
            "Command SETTIME: %s", "success" if ret == 0 else "failure"
        )

    def add_ina(self, bus, ina_type, addr, extra, resistance, data=None):
        """Add an INA to the data acquisition list.

        Args:
          bus: which i2c bus the INA is on. Same ordering as Si2c.
          ina_type: Ina interface: INA_POWER/BUSV/etc.
          addr: 7 bit i2c addr of this INA
          extra: extra data for nonstandard configs.
          resistance: int, shunt resistance in mOhm
        """
        # 0x0002, 1B: bus, 1B:INA type, 1B: INA addr, 1B: extra, 4B: Rs
        cmd = struct.pack(
            "<HBBBBI", self.CMD_ADDINA, bus, ina_type, addr, extra, resistance
        )
        ret = self.wr_command(cmd)
        if ret == 0:
            if data:
                name = data["name"]
            else:
                name = "ina%d_%02x" % (bus, addr)
            self.append_ina_struct(
                name, resistance, bus, addr, data=data, ina_type=ina_type
            )
        self._logger.debug(
            "Command ADD_INA: %s", "success" if ret == 0 else "failure"
        )

    def report_header_size(self):
        """Helper function to calculate power record header size."""
        result = 2
        timestamp = 8
        return result + timestamp

    def report_size(self, ina_count):
        """Helper function to calculate full power record size."""
        record = 2

        datasize = self.report_header_size() + ina_count * record
        # Round to multiple of 4 bytes.
        datasize = int(((datasize + 3) // 4) * 4)

        return datasize

    def read_line(self):
        """Read a line of data from the setup INAs

        Returns:
          list of dicts of the values read by ina/type tuple, otherwise None.
          [{ts:100, (vbat, power):450}, {ts:200, (vbat, power):440}]
        """
        try:
            expected_bytes = self.report_size(len(self._inas))
            cmd = struct.pack("<H", self.CMD_NEXT)
            bytesread = self.wr_command(cmd, read_count=expected_bytes)
        except usb.core.USBError as e:
            self._logger.error("READ LINE FAILED %s", e)
            return None

        if len(bytesread) == 1:
            if bytesread[0] != 0x6:
                self._logger.debug(
                    "READ LINE FAILED bytes: %d ret: %02x",
                    len(bytesread),
                    bytesread[0],
                )
            return None

        if len(bytesread) % expected_bytes != 0:
            self._logger.debug(
                "READ LINE WARNING: expected %d, got %d",
                expected_bytes,
                len(bytesread),
            )

        packet_count = len(bytesread) // expected_bytes

        values = []
        for i in range(0, packet_count):
            start = i * expected_bytes
            end = (i + 1) * expected_bytes
            record = self.interpret_line(bytesread[start:end])
            values.append(record)

        return values

    def interpret_line(self, data):
        """Interpret a power record from INAs

        Args:
          data: one single record of bytes.

        Output:
          stdout of the record in csv format.

        Returns:
          dict containing name, value of recorded data.
        """
        status, size = struct.unpack("<BB", data[0:2])
        if len(data) != self.report_size(size):
            self._logger.error(
                "READ LINE FAILED st:%d size:%d expected:%d len:%d",
                status,
                size,
                self.report_size(size),
                len(data),
            )
        else:
            pass

        timestamp = struct.unpack("<Q", data[2:10])[0]
        self._logger.debug(
            "READ LINE: st:%d size:%d time:%dus", status, size, timestamp
        )
        ftimestamp = float(timestamp) / 1000000.0

        record = {"ts": ftimestamp, "status": status, "berry": self._board}

        for i in range(0, size):
            idx = self.report_header_size() + 2 * i
            name = self._inas[i]["name"]
            name_tuple = (self._inas[i]["name"], self._inas[i]["type"])

            raw_val = struct.unpack("<h", data[idx : idx + 2])[0]

            if self._inas[i]["type"] == Spower.INA_POWER:
                val = raw_val * self._inas[i]["uWscale"]
            elif self._inas[i]["type"] == Spower.INA_BUSV:
                val = raw_val * self._inas[i]["mVscale"]
            elif self._inas[i]["type"] == Spower.INA_CURRENT:
                val = raw_val * self._inas[i]["uAscale"]
            elif self._inas[i]["type"] == Spower.INA_SHUNTV:
                val = raw_val * self._inas[i]["uVscale"]

            self._logger.debug(
                "READ %d %s: %fs: 0x%04x %f", i, name, ftimestamp, raw_val, val
            )
            record[name_tuple] = val

        return record

    def load_board(self, brdfile):
        """Load a board config.

        Args:
          brdfile:	Filename of a json file decribing the INA wiring of this board.
        """
        with open(process_filename(brdfile)) as data_file:
            data = json.load(data_file)

        # TODO: validate this.
        self._brdcfg = data
        self._logger.debug(pprint.pformat(data))


class powerlog(object):
    """Power class to log aggregated power.

    Usage:
      obj = powerlog()

    Instance Variables:
      _data: a StatsManager object that records sweetberry readings and calculates
             statistics.
      _pwr[]: Spower objects for individual sweetberries.
    """

    def __init__(
        self,
        brdfile,
        cfgfile,
        serial_a=None,
        serial_b=None,
        sync_date=False,
        use_ms=False,
        use_mW=False,
        print_stats=False,
        stats_dir=None,
        stats_json_dir=None,
        print_raw_data=True,
        raw_data_dir=None,
    ):
        """Init the powerlog class and set the variables.

        Args:
          brdfile: string name of json file containing board layout.
          cfgfile: string name of json containing list of rails to read.
          serial_a: serial number of sweetberry A.
          serial_b: serial number of sweetberry B.
          sync_date: report timestamps synced with host datetime.
          use_ms: report timestamps in ms rather than us.
          use_mW: report power as milliwatts, otherwise default to microwatts.
          print_stats: print statistics for sweetberry readings at the end.
          stats_dir: directory to save sweetberry readings statistics; if None then
                     do not save the statistics.
          stats_json_dir: directory to save means of sweetberry readings in json
                          format; if None then do not save the statistics.
          print_raw_data: print sweetberry readings raw data in real time, default
                          is to print.
          raw_data_dir: directory to save sweetberry readings raw data; if None then
                        do not save the raw data.
        """
        self._logger = logging.getLogger(__name__)
        self._data = StatsManager()
        self._pwr = {}
        self._use_ms = use_ms
        self._use_mW = use_mW
        self._print_stats = print_stats
        self._stats_dir = stats_dir
        self._stats_json_dir = stats_json_dir
        self._print_raw_data = print_raw_data
        self._raw_data_dir = raw_data_dir

        if not serial_a and not serial_b:
            self._pwr["A"] = Spower("A")
        if serial_a:
            self._pwr["A"] = Spower("A", serialname=serial_a)
        if serial_b:
            self._pwr["B"] = Spower("B", serialname=serial_b)

        with open(process_filename(cfgfile)) as data_file:
            names = json.load(data_file)
        self._names = self.process_scenario(names)

        for key in self._pwr:
            self._pwr[key].load_board(brdfile)
            self._pwr[key].reset()

        # Allocate the rails to the appropriate boards.
        used_boards = []
        for name in self._names:
            success = False
            for key in self._pwr.keys():
                if self._pwr[key].add_ina_name(name):
                    success = True
                    if key not in used_boards:
                        used_boards.append(key)
            if not success:
                raise Exception(
                    "Failed to add %s (maybe missing "
                    "sweetberry, or bad board file?)" % name
                )

        # Evict unused boards.
        for key in list(self._pwr.keys()):
            if key not in used_boards:
                self._pwr.pop(key)

        for key in self._pwr.keys():
            if sync_date:
                self._pwr[key].set_time(time.time() * 1000000)
            else:
                self._pwr[key].set_time(0)

    def process_scenario(self, name_list):
        """Return list of tuples indicating name and type.

        Args:
          json originated list of names, or [name, type]
        Returns:
          list of tuples of (name, type) defaulting to type "POWER"
        Raises: exception, invalid INA type.
        """
        names = []
        for entry in name_list:
            if isinstance(entry, list):
                name = entry[0]
                if entry[1] == "POWER":
                    type = Spower.INA_POWER
                elif entry[1] == "BUSV":
                    type = Spower.INA_BUSV
                elif entry[1] == "CURRENT":
                    type = Spower.INA_CURRENT
                elif entry[1] == "SHUNTV":
                    type = Spower.INA_SHUNTV
                else:
                    raise Exception(
                        "Invalid INA type",
                        "Type of %s [%s] not recognized,"
                        " try one of POWER, BUSV, CURRENT"
                        % (entry[0], entry[1]),
                    )
            else:
                name = entry
                type = Spower.INA_POWER

            names.append((name, type))
        return names

    def start(self, integration_us_request, seconds, sync_speed=0.8):
        """Starts sampling.

        Args:
          integration_us_request: requested interval between sample values.
          seconds: time until exit, or None to run until cancel.
          sync_speed: A usb request is sent every [.8] * integration_us.
        """
        # We will get back the actual integration us.
        # It should be the same for all devices.
        integration_us = None
        for key in self._pwr:
            integration_us_new = self._pwr[key].start(integration_us_request)
            if integration_us:
                if integration_us != integration_us_new:
                    raise Exception(
                        "FAIL",
                        # pylint:disable=bad-string-format-type
                        "Integration on A: %dus != integration on B %dus"
                        % (integration_us, integration_us_new),
                    )
            integration_us = integration_us_new

        # CSV header
        title = "ts:%dus" % integration_us
        for name_tuple in self._names:
            name, ina_type = name_tuple

            if ina_type == Spower.INA_POWER:
                unit = "mW" if self._use_mW else "uW"
            elif ina_type == Spower.INA_BUSV:
                unit = "mV"
            elif ina_type == Spower.INA_CURRENT:
                unit = "uA"
            elif ina_type == Spower.INA_SHUNTV:
                unit = "uV"

            title += ", %s %s" % (name, unit)
            name_type = name + Spower.INA_SUFFIX[ina_type]
            self._data.SetUnit(name_type, unit)
        title += ", status"
        if self._print_raw_data:
            logoutput(title)

        forever = False
        if not seconds:
            forever = True
        end_time = time.time() + seconds
        try:
            pending_records = []
            while forever or end_time > time.time():
                if integration_us > 5000:
                    time.sleep((integration_us / 1000000.0) * sync_speed)
                for key in self._pwr:
                    records = self._pwr[key].read_line()
                    if not records:
                        continue

                    for record in records:
                        pending_records.append(record)

                pending_records.sort(key=lambda r: r["ts"])

                aggregate_record = {"boards": set()}
                for record in pending_records:
                    if record["berry"] not in aggregate_record["boards"]:
                        for rkey in record.keys():
                            aggregate_record[rkey] = record[rkey]
                        aggregate_record["boards"].add(record["berry"])
                    else:
                        self._logger.info(
                            "break %s, %s",
                            record["berry"],
                            aggregate_record["boards"],
                        )
                        break

                    if aggregate_record["boards"] == set(self._pwr.keys()):
                        csv = "%f" % aggregate_record["ts"]
                        for name in self._names:
                            if name in aggregate_record:
                                multiplier = (
                                    0.001
                                    if (
                                        self._use_mW
                                        and name[1] == Spower.INA_POWER
                                    )
                                    else 1
                                )
                                value = aggregate_record[name] * multiplier
                                csv += ", %.2f" % value
                                name_type = name[0] + Spower.INA_SUFFIX[name[1]]
                                self._data.AddSample(name_type, value)
                            else:
                                csv += ", "
                        csv += ", %d" % aggregate_record["status"]
                        if self._print_raw_data:
                            logoutput(csv)

                        aggregate_record = {"boards": set()}
                        for r in range(0, len(self._pwr)):
                            pending_records.pop(0)

        except KeyboardInterrupt:
            self._logger.info("\nCTRL+C caught.")

        finally:
            for key in self._pwr:
                self._pwr[key].stop()
            self._data.CalculateStats()
            if self._print_stats:
                print(self._data.SummaryToString())
            save_dir = "sweetberry%s" % time.time()
            if self._stats_dir:
                stats_dir = os.path.join(self._stats_dir, save_dir)
                self._data.SaveSummary(stats_dir)
            if self._stats_json_dir:
                stats_json_dir = os.path.join(self._stats_json_dir, save_dir)
                self._data.SaveSummaryJSON(stats_json_dir)
            if self._raw_data_dir:
                raw_data_dir = os.path.join(self._raw_data_dir, save_dir)
                self._data.SaveRawData(raw_data_dir)


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    # Command line argument description.
    parser = argparse.ArgumentParser(
        description="Gather CSV data from sweetberry"
    )
    parser.add_argument(
        "-b",
        "--board",
        type=str,
        help="Board configuration file, eg. my.board",
        default="",
    )
    parser.add_argument(
        "-c",
        "--config",
        type=str,
        help="Rail config to monitor, eg my.scenario",
        default="",
    )
    parser.add_argument(
        "-A",
        "--serial",
        type=str,
        help="Serial number of sweetberry A",
        default="",
    )
    parser.add_argument(
        "-B",
        "--serial_b",
        type=str,
        help="Serial number of sweetberry B",
        default="",
    )
    parser.add_argument(
        "-t",
        "--integration_us",
        type=int,
        help="Target integration time for samples",
        default=100000,
    )
    parser.add_argument(
        "-s",
        "--seconds",
        type=float,
        help="Seconds to run capture",
        default=0.0,
    )
    parser.add_argument(
        "--date",
        default=False,
        help="Sync logged timestamp to host date",
        action="store_true",
    )
    parser.add_argument(
        "--ms",
        default=False,
        help="Print timestamp as milliseconds",
        action="store_true",
    )
    parser.add_argument(
        "--mW",
        default=False,
        help="Print power as milliwatts, otherwise default to microwatts",
        action="store_true",
    )
    parser.add_argument(
        "--slow",
        default=False,
        help="Intentionally overflow",
        action="store_true",
    )
    parser.add_argument(
        "--print_stats",
        default=False,
        action="store_true",
        help="Print statistics for sweetberry readings at the end",
    )
    parser.add_argument(
        "--save_stats",
        type=str,
        nargs="?",
        dest="stats_dir",
        metavar="STATS_DIR",
        const=os.path.dirname(os.path.abspath(__file__)),
        default=None,
        help="Save statistics for sweetberry readings to %(metavar)s if "
        "%(metavar)s is specified, %(metavar)s will be created if it does "
        "not exist; if %(metavar)s is not specified but the flag is set, "
        "stats will be saved to where %(prog)s is located; if this flag is "
        "not set, then do not save stats",
    )
    parser.add_argument(
        "--save_stats_json",
        type=str,
        nargs="?",
        dest="stats_json_dir",
        metavar="STATS_JSON_DIR",
        const=os.path.dirname(os.path.abspath(__file__)),
        default=None,
        help="Save means for sweetberry readings in json to %(metavar)s if "
        "%(metavar)s is specified, %(metavar)s will be created if it does "
        "not exist; if %(metavar)s is not specified but the flag is set, "
        "stats will be saved to where %(prog)s is located; if this flag is "
        "not set, then do not save stats",
    )
    parser.add_argument(
        "--no_print_raw_data",
        dest="print_raw_data",
        default=True,
        action="store_false",
        help="Not print raw sweetberry readings at real time, default is to "
        "print",
    )
    parser.add_argument(
        "--save_raw_data",
        type=str,
        nargs="?",
        dest="raw_data_dir",
        metavar="RAW_DATA_DIR",
        const=os.path.dirname(os.path.abspath(__file__)),
        default=None,
        help="Save raw data for sweetberry readings to %(metavar)s if "
        "%(metavar)s is specified, %(metavar)s will be created if it does "
        "not exist; if %(metavar)s is not specified but the flag is set, "
        "raw data will be saved to where %(prog)s is located; if this flag "
        "is not set, then do not save raw data",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        default=False,
        help="Very chatty printout",
        action="store_true",
    )

    args = parser.parse_args(argv)

    root_logger = logging.getLogger(__name__)
    if args.verbose:
        root_logger.setLevel(logging.DEBUG)
    else:
        root_logger.setLevel(logging.INFO)

    # if powerlog is used through main, log to sys.stdout
    if __name__ == "__main__":
        stdout_handler = logging.StreamHandler(sys.stdout)
        stdout_handler.setFormatter(
            logging.Formatter("%(levelname)s: %(message)s")
        )
        root_logger.addHandler(stdout_handler)

    integration_us_request = args.integration_us
    if not args.board:
        raise Exception("Power", "No board file selected, see board.README")
    if not args.config:
        raise Exception("Power", "No config file selected, see board.README")

    brdfile = args.board
    cfgfile = args.config
    seconds = args.seconds
    serial_a = args.serial
    serial_b = args.serial_b
    sync_date = args.date
    use_ms = args.ms
    use_mW = args.mW
    print_stats = args.print_stats
    stats_dir = args.stats_dir
    stats_json_dir = args.stats_json_dir
    print_raw_data = args.print_raw_data
    raw_data_dir = args.raw_data_dir

    boards = []

    sync_speed = 0.8
    if args.slow:
        sync_speed = 10.2

    # Set up logging interface.
    powerlogger = powerlog(
        brdfile,
        cfgfile,
        serial_a=serial_a,
        serial_b=serial_b,
        sync_date=sync_date,
        use_ms=use_ms,
        use_mW=use_mW,
        print_stats=print_stats,
        stats_dir=stats_dir,
        stats_json_dir=stats_json_dir,
        print_raw_data=print_raw_data,
        raw_data_dir=raw_data_dir,
    )

    # Start logging.
    powerlogger.start(integration_us_request, seconds, sync_speed=sync_speed)


if __name__ == "__main__":
    main()
