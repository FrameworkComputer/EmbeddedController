# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utilities for using lightweight console functions."""

# Note: This is a py2/3 compatible file.

import datetime
import sys
import time
from typing import Iterable, Optional, Set, Tuple

import six
import usb  # pylint:disable=import-error

from . import pty_driver
from . import stm32uart


_USB_SCAN_WAIT = 0.1  # seconds between scans for devices on USB


def get_subprocess_args():
    if six.PY3:
        return {"encoding": "utf-8"}
    return {}


class TinyServoError(Exception):
    """Exceptions."""


def log(output):
    """Print output to console, logfiles can be added here.

    Args:
      output: string to output.
    """
    sys.stdout.write(output)
    sys.stdout.write("\n")
    sys.stdout.flush()


def check_usb(vidpid: Iterable[str], serialname=None):
    """Check if |vidpid| is present on the system's USB.

    Args:
      vidpid: iterable of string representations of the usb vid:pid,
              eg. '18d1:2001', all of which can match.
      serialname: serialname if specified.

    Returns:
      True if found, False, otherwise.
    """
    for _unused_dev in get_usb_dev(vidpid, serialname):
        return True

    return False


def _parse_vidpid_string(vidpid: str) -> Tuple[int, int]:
    vidpidst = vidpid.split(":")
    vid = int(vidpidst[0], 16)
    pid = int(vidpidst[1], 16)
    return vid, pid


def _match_device(dev, devs: Set[Tuple[int, int]], serial: str) -> bool:
    return (dev.idVendor, dev.idProduct) in devs and (
        serial is None or serial == usb.util.get_string(dev, dev.iSerialNumber)
    )


def get_usb_dev(vidpid: Iterable[str], serialname=None):
    """Return the USB pyusb devie struct

    Return the dev struct of the first USB device with VID:PID vidpid,
    or None if no device is found. If more than one device check serial
    if supplied.

    Args:
      vidpid: iterable of string representations of the usb vid:pid,
              eg. '18d1:2001', all of which can match.
      serialname: serialname if specified.

    Returns:
      Iterable of pyusb devices, may be empty
    """

    devs = set([_parse_vidpid_string(dev) for dev in vidpid])

    return usb.core.find(
        find_all=True, custom_match=lambda d: _match_device(d, devs, serialname)
    )


def check_usb_dev(vidpid: Iterable[str], serialname=None) -> Optional[int]:
    """Return the USB dev number

    Return the dev number of the first USB device with VID:PID vidpid,
    or None if no device is found. If more than one device check serial
    if supplied.

    Args:
      vidpid: iterable of string representations of the usb vid:pid,
              eg. '18d1:2001', all of which can match.
      serialname: serialname if specified.

    Returns:
      usb device number if exactly one device found, None otherwise.
    """
    devs_iter = iter(get_usb_dev(vidpid, serialname=serialname))
    first = next(devs_iter, None)
    additional = next(devs_iter, None)

    if first is not None and additional is None:
        return first.address

    return None


def wait_for_usb_remove(
    vidpid: Iterable[str],
    serialname: Optional[str] = None,
    timeout: Optional[float] = None,
) -> None:
    """Wait for USB device with vidpid/serialname to be absent

    Args:
      vidpid: iterable of string representations of the usb vid:pid,
              eg. '18d1:2001', all of which can match.
      serialname: serialname if specified.
      timeout: timeout in seconds, None for no timeout.

    Raises:
      TinyServoError: on timeout.
    """
    if timeout:
        finish = datetime.datetime.now() + datetime.timedelta(seconds=timeout)
    while True:
        devs = set(get_usb_dev(vidpid, serialname))
        if not devs:
            return
        time.sleep(_USB_SCAN_WAIT)
        if timeout:
            if datetime.datetime.now() > finish:
                raise TinyServoError(
                    "Timeout", "Timeout waiting for USB %s to be gone" % vidpid
                )


def wait_for_usb(
    vidpid: Iterable[str],
    serialname: Optional[str] = None,
    timeout: Optional[float] = None,
) -> Set:
    """Wait for usb device with vidpid to be present/absent.

    Args:
      vidpid: iterable of string representations of the usb vid:pid,
              eg. '18d1:2001', all of which can match.
      serialname: serialname if specified.
      timeout: timeout in seconds, None for no timeout.

    Returns:
      If devices found, return set of pyUSB device objects

    Raises:
      TinyServoError: on timeout.
    """
    if timeout:
        finish = datetime.datetime.now() + datetime.timedelta(seconds=timeout)
    while True:
        devs = set(get_usb_dev(vidpid, serialname))
        if devs:
            return devs
        time.sleep(_USB_SCAN_WAIT)
        if timeout:
            if datetime.datetime.now() > finish:
                raise TinyServoError(
                    "Timeout", "Timeout waiting for USB %s" % vidpid
                )


def do_serialno(serialno, pty):
    """Set serialnumber 'serialno' via ec console 'pty'.

    Commands are:
    # > serialno set 1234
    # Saving serial number
    # Serial number: 1234

    Args:
      serialno: string serial number to set.
      pty: tinyservo console to send commands.

    Raises:
      TinyServoError: on failure to set.
      ptyError: on command interface error.
    """
    cmd = r"serialno set %s" % serialno
    regex = r"Serial number:\s+(\S+)"

    results = pty._issue_cmd_get_results(cmd, [regex])[0]
    sn = results[1].strip().strip("\n\r")

    if sn == serialno:
        log("Success !")
        log("Serial set to %s" % sn)
    else:
        log("Serial number set to %s but saved as %s." % (serialno, sn))
        raise TinyServoError(
            "Serial Number",
            "Serial number set to %s but saved as %s." % (serialno, sn),
        )


def setup_tinyservod(vidpid, interface, serialname=None, debuglog=False):
    """Set up a pty

    Set up a pty to the ec console in order
    to send commands. Returns a pty_driver object.

    Args:
      vidpid: string vidpid of device to access.
      interface: not used.
      serialname: string serial name of device requested, optional.
      debuglog: chatty printout (boolean)

    Returns:
      pty object

    Raises:
      UsbError, SusbError: on device not found
    """
    vidstr, pidstr = vidpid.split(":")
    vid = int(vidstr, 16)
    pid = int(pidstr, 16)
    suart = stm32uart.Suart(
        vendor=vid,
        product=pid,
        interface=interface,
        serialname=serialname,
        debuglog=debuglog,
    )
    suart.run()
    pty = pty_driver.ptyDriver(suart, [])

    return pty
