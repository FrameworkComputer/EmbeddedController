#!/usr/bin/env python
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Upload firmware over USB
# Note: This is a py2/3 compatible file.

from __future__ import print_function

import argparse
import array
import json
import os
import struct
import sys
import time
from pprint import pprint

import usb  # pylint:disable=import-error
from ecusb.stm32usb import SusbError

debug = False


def debuglog(msg):
    if debug:
        print(msg)


def log(msg):
    print(msg)
    sys.stdout.flush()


"""Sends firmware update to CROS EC usb endpoint."""


class Supdate(object):
    """Class to access firmware update endpoints.

    Usage:
      d = Supdate()

    Instance Variables:
      _dev: pyUSB device object
      _read_ep: pyUSB read endpoint for this interface
      _write_ep: pyUSB write endpoint for this interface
    """

    USB_SUBCLASS_GOOGLE_UPDATE = 0x53
    USB_CLASS_VENDOR = 0xFF

    def __init__(self):
        pass

    def connect_usb(self, serialname=None):
        """Initial discovery and connection to USB endpoint.

        This searches for a USB device matching the VID:PID specified
        in the config file, optionally matching a specified serialname.

        Args:
          serialname: Find the device with this serial, in case multiple
              devices are attached.

        Returns:
          True on success.
        Raises:
          Exception on error.
        """
        # Find the stm32.
        vendor = self._brdcfg["vid"]
        product = self._brdcfg["pid"]

        dev_g = usb.core.find(idVendor=vendor, idProduct=product, find_all=True)
        dev_list = list(dev_g)
        if dev_list is None:
            raise Exception("Update", "USB device not found")

        # Check if we have multiple stm32s and we've specified the serial.
        dev = None
        if serialname:
            for d in dev_list:
                if usb.util.get_string(d, d.iSerialNumber) == serialname:
                    dev = d
                    break
            if dev is None:
                raise SusbError("USB device(%s) not found" % serialname)
        else:
            dev = dev_list[0]

        debuglog("Found stm32: %04x:%04x" % (vendor, product))
        self._dev = dev

        # Get an endpoint instance.
        try:
            dev.set_configuration()
        except:
            pass
        cfg = dev.get_active_configuration()

        intf = usb.util.find_descriptor(
            cfg,
            custom_match=lambda i: i.bInterfaceClass == self.USB_CLASS_VENDOR
            and i.bInterfaceSubClass == self.USB_SUBCLASS_GOOGLE_UPDATE,
        )

        self._intf = intf
        debuglog("Interface: %s" % intf)
        debuglog("InterfaceNumber: %s" % intf.bInterfaceNumber)

        read_ep = usb.util.find_descriptor(
            intf,
            # match the first IN endpoint
            custom_match=lambda e: usb.util.endpoint_direction(
                e.bEndpointAddress
            )
            == usb.util.ENDPOINT_IN,
        )

        self._read_ep = read_ep
        debuglog("Reader endpoint: 0x%x" % read_ep.bEndpointAddress)

        write_ep = usb.util.find_descriptor(
            intf,
            # match the first OUT endpoint
            custom_match=lambda e: usb.util.endpoint_direction(
                e.bEndpointAddress
            )
            == usb.util.ENDPOINT_OUT,
        )

        self._write_ep = write_ep
        debuglog("Writer endpoint: 0x%x" % write_ep.bEndpointAddress)

        return True

    def wr_command(self, write_list, read_count=1, wtimeout=100, rtimeout=2000):
        """Write command to logger logic..

        This function writes byte command values list to stm, then reads
        byte status.

        Args:
          write_list: list of command byte values [0~255].
          read_count: number of status byte values to read.
          wtimeout: mS to wait for write success
          rtimeout: mS to wait for read success

        Returns:
          status byte, if one byte is read,
          byte list, if multiple bytes are read,
          None, if no bytes are read.

        Interface:
          write: [command, data ... ]
          read: [status ]
        """
        debuglog(
            "wr_command(write_list=[%s] (%d), read_count=%s)"
            % (list(bytearray(write_list)), len(write_list), read_count)
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
            debuglog("RET: %s " % ret)

        # Read back response if necessary.
        if read_count:
            bytesread = self._read_ep.read(512, rtimeout)
            debuglog("BYTES: [%s]" % bytesread)

            if len(bytesread) != read_count:
                debuglog(
                    "Unexpected bytes read: %d, expected: %d"
                    % (len(bytesread), read_count)
                )
                pass

            debuglog("STATUS: 0x%02x" % int(bytesread[0]))
            if read_count == 1:
                return bytesread[0]
            else:
                return bytesread

        return None

    def stop(self):
        """Finalize system flash and exit."""
        cmd = struct.pack(">I", 0xB007AB1E)
        read = self.wr_command(cmd, read_count=4)

        if len(read) == 4:
            log("Finished flashing")
            return

        raise Exception("Update", "Stop failed [%s]" % read)

    def write_file(self):
        """Write the update region packet by packet to USB

        This sends write packets of size 128B out, in 32B chunks.
        Overall, this will write all data in the inactive code region.

        Raises:
          Exception if write failed or address out of bounds.
        """
        region = self._region
        flash_base = self._brdcfg["flash"]
        offset = self._base - flash_base
        if offset != self._brdcfg["regions"][region][0]:
            raise Exception(
                "Update",
                "Region %s offset 0x%x != available offset 0x%x"
                % (region, self._brdcfg["regions"][region][0], offset),
            )

        length = self._brdcfg["regions"][region][1]
        log("Sending")

        # Go to the correct region in the ec.bin file.
        self._binfile.seek(offset)

        # Send 32 bytes at a time. Must be less than the endpoint's max packet size.
        maxpacket = 32

        # While data is left, create update packets.
        while length > 0:
            # Update packets are 128B. We can use any number
            # but the micro must malloc this memory.
            pagesize = min(length, 128)

            # Packet is:
            #  packet size: page bytes transferred plus 3 x 32b values header.
            #  cmd: n/a
            #  base: flash address to write this packet.
            #  data: 128B of data to write into flash_base
            cmd = struct.pack(">III", pagesize + 12, 0, offset + flash_base)
            read = self.wr_command(cmd, read_count=0)

            # Push 'todo' bytes out the pipe.
            todo = pagesize
            while todo > 0:
                packetsize = min(maxpacket, todo)
                data = self._binfile.read(packetsize)
                if len(data) != packetsize:
                    raise Exception("Update", "No more data from file")
                for i in range(0, 10):
                    try:
                        self.wr_command(data, read_count=0)
                        break
                    except:
                        log("Timeout fail")
                todo -= packetsize
            # Done with this packet, move to the next one.
            length -= pagesize
            offset += pagesize

            # Validate that the micro thinks it successfully wrote the data.
            read = self.wr_command("".encode(), read_count=4)
            result = struct.unpack("<I", read)
            result = result[0]
            if result != 0:
                raise Exception(
                    "Update", "Upload failed with rc: 0x%x" % result
                )

    def start(self):
        """Start a transaction and erase currently inactive region.

        This function sends a start command, and receives the base of the
        preferred inactive region. This could be RW, RW_B,
        or RO (if there's no RW_B)

        Note that the region is erased here, so you'd better program the RO if
        you just erased it. TODO(nsanders): Modify the protocol to allow active
        region select or query before erase.
        """

        # Size is 3 uint32 fields
        # packet: [packetsize, cmd, base]
        size = 4 + 4 + 4
        # Return value is [status, base_addr]
        expected = 4 + 4

        cmd = struct.pack("<III", size, 0, 0)
        read = self.wr_command(cmd, read_count=expected)

        if len(read) == 4:
            raise Exception("Update", "Protocol version 0 not supported")
        elif len(read) == expected:
            base, version = struct.unpack(">II", read)
            log("Update protocol v. %d" % version)
            log("Available flash region base: %x" % base)
        else:
            raise Exception(
                "Update", "Start command returned %d bytes" % len(read)
            )

        if base < 256:
            raise Exception("Update", "Start returned error code 0x%x" % base)

        self._base = base
        flash_base = self._brdcfg["flash"]
        self._offset = self._base - flash_base

        # Find our active region.
        for region in self._brdcfg["regions"]:
            if (self._offset >= self._brdcfg["regions"][region][0]) and (
                self._offset
                < (
                    self._brdcfg["regions"][region][0]
                    + self._brdcfg["regions"][region][1]
                )
            ):
                log("Active region: %s" % region)
                self._region = region

    def load_board(self, brdfile):
        """Load firmware layout file.

        example as follows:
        {
          "board": "servo micro",
          "vid": 6353,
          "pid": 20506,
          "flash": 134217728,
          "regions": {
            "RW": [65536, 65536],
            "PSTATE": [61440, 4096],
            "RO": [0, 61440]
          }
        }

        Args:
          brdfile: path to board description file.
        """
        with open(brdfile) as data_file:
            data = json.load(data_file)

        # TODO(nsanders): validate this data before moving on.
        self._brdcfg = data
        if debug:
            pprint(data)

        log("Board is %s" % self._brdcfg["board"])
        # Cast hex strings to int.
        self._brdcfg["flash"] = int(self._brdcfg["flash"], 0)
        self._brdcfg["vid"] = int(self._brdcfg["vid"], 0)
        self._brdcfg["pid"] = int(self._brdcfg["pid"], 0)

        log("Flash Base is %x" % self._brdcfg["flash"])
        self._flashsize = 0
        for region in self._brdcfg["regions"]:
            base = int(self._brdcfg["regions"][region][0], 0)
            length = int(self._brdcfg["regions"][region][1], 0)
            log("region %s\tbase:0x%08x size:0x%08x" % (region, base, length))
            self._flashsize += length

            # Convert these to int because json doesn't support hex.
            self._brdcfg["regions"][region][0] = base
            self._brdcfg["regions"][region][1] = length

        log("Flash Size: 0x%x" % self._flashsize)

    def load_file(self, binfile):
        """Open and verify size of the target ec.bin file.

        Args:
          binfile: path to ec.bin

        Raises:
          Exception on file not found or filesize not matching.
        """
        self._filesize = os.path.getsize(binfile)
        self._binfile = open(binfile, "rb")

        if self._filesize != self._flashsize:
            raise Exception(
                "Update",
                "Flash size 0x%x != file size 0x%x"
                % (self._flashsize, self._filesize),
            )


# Generate command line arguments
parser = argparse.ArgumentParser(description="Update firmware over usb")
parser.add_argument(
    "-b",
    "--board",
    type=str,
    help="Board configuration json file",
    default="board.json",
)
parser.add_argument(
    "-f", "--file", type=str, help="Complete ec.bin file", default="ec.bin"
)
parser.add_argument(
    "-s", "--serial", type=str, help="Serial number", default=""
)
parser.add_argument("-l", "--list", action="store_true", help="List regions")
parser.add_argument(
    "-v", "--verbose", action="store_true", help="Chatty output"
)


def main():
    global debug
    args = parser.parse_args()

    brdfile = args.board
    serial = args.serial
    binfile = args.file
    if args.verbose:
        debug = True

    with open(brdfile) as data_file:
        names = json.load(data_file)

    p = Supdate()
    p.load_board(brdfile)
    p.connect_usb(serialname=serial)
    p.load_file(binfile)

    # List solely prints the config.
    if args.list:
        return

    # Start transfer and erase.
    p.start()
    # Upload the bin file
    log("Uploading %s" % binfile)
    p.write_file()

    # Finalize
    log("Done. Finalizing.")
    p.stop()


if __name__ == "__main__":
    main()
