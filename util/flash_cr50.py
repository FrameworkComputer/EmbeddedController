#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Flash Cr50 using gsctool or cr50-rescue.

gsctool example:
util/flash_cr50.py --image cr50.bin.prod
util/flash_cr50.py --release prod

cr50-rescue example:
util/flash_cr50.py --image cr50.bin.prod -c cr50-rescue -p 9999
util/flash_cr50.py --release prod -c cr50-rescue -p 9999
"""

import argparse
import os
import pprint
import re
import select
import shutil
import subprocess
import sys
import tempfile
import threading
import time

from chromite.lib import cros_build_lib
from chromite.lib import cros_logging as logging

CR50_FIRMWARE_BASE = '/opt/google/cr50/firmware/cr50.bin.'
RELEASE_PATHS = {
    'prepvt': CR50_FIRMWARE_BASE + 'prepvt',
    'prod': CR50_FIRMWARE_BASE + 'prod',
}
# Dictionary mapping a setup to controls used to verify that the setup is
# correct. The keys are strings and the values are lists of strings.
REQUIRED_CONTROLS = {
    'cr50_uart': [
        r'raw_cr50_uart_pty:\S+',
        r'cr50_ec3po_interp_connect:\S+',
    ],
    'cr50_reset_odl': [
        r'cr50_reset_odl:\S+',
    ],
    'ec_uart': [
        r'ec_board:\S+',
    ],
    'flex': [
        r'servo_type:.*servo_.[^4]',
    ],
    'type-c_servo_v4': [
        r'servo_v4_type:type-c',
        r'servo_v4_role:\S+',
    ],
}
# Supported methods to resetting cr50.
SUPPORTED_RESETS = (
    'battery_cutoff',
    'cr50_reset_odl',
    'manual_reset',
)


class Error(Exception):
    """Exception class for flash_cr50 utility."""


def run_command(cmd, check_error=True):
    """Run the given command.

    Args:
        cmd: The command to run as a list of arguments.
        check_error: Raise an error if the command fails.

    Returns:
        (exit_status, The command output)

    Raises:
        The command error if the command fails and check_error is True.
    """
    try:
        result = cros_build_lib.run(cmd,
                                    check=check_error,
                                    print_cmd=True,
                                    capture_output=True,
                                    encoding='utf-8',
                                    stderr=subprocess.STDOUT,
                                    debug_level=logging.DEBUG,
                                    log_output=True)
    except cros_build_lib.RunCommandError as cmd_error:
        if check_error:
            raise
        # OSErrors are handled differently. They're raised even if check is
        # False. Return the errno and message for OS errors.
        return cmd_error.exception.errno, cmd_error.msg
    return result.returncode, result.stdout.strip()


class Cr50Image(object):
    """Class to handle cr50 image conversions."""

    SUFFIX_LEN = 6
    RW_NAME_BASE = 'cr50.rw.'

    def __init__(self, image, artifacts_dir):
        """Create an image object that can be used by cr50 updaters."""
        self._remove_dir = False
        if not os.path.exists(image):
            raise Error('Could not find image: %s' % image)
        if not artifacts_dir:
            self._remove_dir = tempfile.mkdtemp()
            artifacts_dir = self._remove_dir
        if not os.path.isdir(artifacts_dir):
            raise Error('Directory does not exist: %s' % artifacts_dir)
        self._original_image = image
        self._artifacts_dir = artifacts_dir
        self._generate_file_names()

    def __del__(self):
        """Remove temporary files."""
        if self._remove_dir:
            shutil.rmtree(self._remove_dir)

    def _generate_file_names(self):
        """Create some filenames to use for image conversion artifacts."""
        self._tmp_rw_bin = os.path.join(self._artifacts_dir,
                                        self.RW_NAME_BASE + '.bin')
        self._tmp_rw_hex = os.path.join(self._artifacts_dir,
                                        self.RW_NAME_BASE + '.hex')
        self._tmp_cr50_bin = os.path.join(self._artifacts_dir,
                                          self.RW_NAME_BASE + '.orig.bin')

    def extract_rw_a_hex(self):
        """Extract RW_A.hex from the original image."""
        run_command(['cp', self.get_bin(), self._tmp_cr50_bin])
        run_command(['dd', 'if=' + self._tmp_cr50_bin, 'of=' + self._tmp_rw_bin,
                     'skip=16384', 'count=233472', 'bs=1'])
        run_command(['objcopy', '-I', 'binary', '-O', 'ihex',
                     '--change-addresses', '0x44000', self._tmp_rw_bin,
                     self._tmp_rw_hex])

    def get_rw_hex(self):
        """cr50-rescue uses the .hex file."""
        if not os.path.exists(self._tmp_rw_hex):
            self.extract_rw_a_hex()
        return self._tmp_rw_hex

    def get_bin(self):
        """Get the filename of the update image."""
        return self._original_image

    def get_original_basename(self):
        """Get the basename of the original image."""
        return os.path.basename(self._original_image)


class Servo(object):
    """Class to interact with servo."""

    # Wait 3 seconds for device to settle after running the dut control command.
    SHORT_WAIT = 3

    def __init__(self, port):
        """Initialize servo class.

        Args:
            port: The servo port for the device being updated.
        """
        self._port = port

    def dut_control(self, cmd, check_error=True, wait=False):
        """Run dut control commands

        Args:
            cmd: the command to run
            check_error: Raise RunCommandError if the command returns a non-zero
                         exit status.
            wait: If True, wait SHORT_WAIT seconds after running the command

        Returns:
            (exit_status, output string) - The exit_status will be non-zero if
            the command failed and check_error is True.

        Raises:
            RunCommandError if the command fails and check_error is False
        """
        dut_control_cmd = ['dut-control', cmd, '-p', self._port]
        exit_status, output = run_command(dut_control_cmd, check_error)
        if wait:
            time.sleep(self.SHORT_WAIT)
        return exit_status, output.split(':', 1)[-1]

    def get_raw_cr50_pty(self):
        """Return raw_cr50_pty. Disable ec3po, so the raw pty can be used."""
        # Disconnect EC3PO, raw_cr50_uart_pty will control the cr50
        # output and input.
        self.dut_control('cr50_ec3po_interp_connect:off', wait=True)
        return self.dut_control('raw_cr50_uart_pty')[1]

    def get_cr50_version(self):
        """Return the current cr50 version string."""
        # Make sure ec3po is enabled, so getting cr50_version works.
        self.dut_control('cr50_ec3po_interp_connect:on', wait=True)
        return self.dut_control('cr50_version')[1]


class Cr50Reset(object):
    """Class to enter and exit cr50 reset."""

    # A list of requirements for the setup. The requirement strings must match
    # something in the REQUIRED_CONTROLS dictionary.
    REQUIRED_SETUP = ()

    def __init__(self, servo, name):
        """Make sure the setup supports the given reset_type.

        Args:
            servo: The Servo object for the device.
            name: The reset type.
        """
        self._servo = servo
        self._reset_name = name
        self.verify_setup()
        self._original_watchdog_state = self.ccd_watchdog_enabled()
        self._servo_type = self._servo.dut_control('servo_type')[1]

    def verify_setup(self):
        """Verify the setup has all required controls to flash cr50.

        Raises:
            Error if something is wrong with the setup.
        """
        # If this failed before and didn't cleanup correctly, the device may be
        # cutoff. Try to set the servo_v4_role to recover the device before
        # checking the device state.
        self._servo.dut_control('servo_v4_role:src', check_error=False)

        logging.info('Requirements for %s: %s', self._reset_name,
                     pprint.pformat(self.REQUIRED_SETUP))

        # Get the specific control requirements for the necessary categories.
        required_controls = []
        for category in self.REQUIRED_SETUP:
            required_controls.extend(REQUIRED_CONTROLS[category])

        logging.debug('Required controls for %r:\n%s', self._reset_name,
                      pprint.pformat(required_controls))
        setup_issues = []
        # Check the setup has all required controls in the correct state.
        for required_control in required_controls:
            control, exp_response = required_control.split(':')
            returncode, output = self._servo.dut_control(control, False)
            logging.debug('%s: got %s expect %s', control, output, exp_response)
            match = re.search(exp_response, output)
            if returncode:
                setup_issues.append('%s: %s' % (control, output))
            elif not match:
                setup_issues.append('%s: need %s found %s' %
                                    (control, exp_response, output))
            else:
                logging.debug('matched control: %s:%s', control, match.string)
                # Save controls, so they can be restored during cleanup.
                setattr(self, '_' + control, output)

        if setup_issues:
            raise Error('Cannot run update using %s. Setup issues: %s' %
                        (self._reset_name, setup_issues))
        logging.info('Device Setup: ok')
        logging.info('Reset Method: %s', self._reset_name)

    def cleanup(self):
        """Try to get the device out of reset and restore all controls."""
        logging.info('Cleaning up')
        self.restore_control('cr50_ec3po_interp_connect')

        # Toggle the servo v4 role if possible to try and get the device out of
        # cutoff.
        self._servo.dut_control('servo_v4_role:snk', check_error=False)
        self._servo.dut_control('servo_v4_role:src', check_error=False)
        self.restore_control('servo_v4_role')

        # Restore the ccd watchdog.
        self.enable_ccd_watchdog(self._original_watchdog_state)

    def restore_control(self, control):
        """Restore the control setting, if it has been saved.

        Args:
            control: The name of the servo control to restore.
        """
        setting = getattr(self, control, None)
        if setting is None:
            return
        self._servo.dut_control('%s:%s' % (control, setting))

    def ccd_watchdog_enabled(self):
        """Return True if servod is monitoring ccd"""
        if 'ccd_cr50' not in self._servo_type:
            return False
        watchdog_state = self._servo.dut_control('watchdog')[1]
        logging.debug(watchdog_state)
        return not re.search('ccd:.*disconnect ok', watchdog_state)

    def enable_ccd_watchdog(self, enable):
        """Control the CCD watchdog.

        Servo will die if it's watching CCD and cr50 is held in reset. Disable
        the CCD watchdog, so it's ok for CCD to disconnect.

        This function does nothing if ccd_cr50 isn't in the servo type.

        Args:
            enable: If True, enable the CCD watchdog. Otherwise disable it.
        """
        if 'ccd_cr50' not in self._servo_type:
            logging.debug('Servo is not watching ccd device.')
            return

        if enable:
            self._servo.dut_control('watchdog_add:ccd')
        else:
            self._servo.dut_control('watchdog_remove:ccd')

        if self.ccd_watchdog_enabled() != enable:
            raise Error('Could not %sable ccd watchdog' %
                        ('en' if enable else 'dis'))

    def enter_reset(self):
        """Disable the CCD watchdog then run the reset cr50 function."""
        logging.info('Using %r to enter reset', self._reset_name)
        # Disable the CCD watchdog before putting servo into reset otherwise
        # servo will die in the middle of flashing cr50.
        self.enable_ccd_watchdog(False)
        try:
            self.run_reset()
        except Exception as e:
            logging.warning('%s enter reset failed: %s', self._reset_name, e)
            raise

    def exit_reset(self):
        """Exit cr50 reset."""
        logging.info('Recovering from %s', self._reset_name)
        try:
            self.recover_from_reset()
        except Exception as e:
            logging.warning('%s exit reset failed: %s', self._reset_name, e)
            raise

    def run_reset(self):
        """Start the cr50 reset process.

        Cr50 doesn't have to enter reset in this function. It just needs to do
        whatever setup is necessary for the exit reset function.
        """
        raise NotImplementedError()

    def recover_from_reset(self):
        """Recover from Cr50 reset.

        Cr50 has to hard or power-on reset during this function for rescue to
        work. Uart is disabled on deep sleep recovery, so deep sleep is not a
        valid reset.
        """
        raise NotImplementedError()


class Cr50ResetODLReset(Cr50Reset):
    """Class for using the servo cr50_reset_odl to reset cr50."""

    REQUIRED_SETUP = (
        # Rescue is done through Cr50 uart. It requires a flex cable not ccd.
        'flex',
        # cr50_reset_odl is used to hold cr50 in reset. This control only exists
        # if it actually resets cr50.
        'cr50_reset_odl',
        # Cr50 rescue is done through cr50 uart.
        'cr50_uart',
    )

    def cleanup(self):
        """Use the Cr50 reset signal to hold Cr50 in reset."""
        try:
            self.restore_control('cr50_reset_odl')
        finally:
            super(Cr50ResetODLReset, self).cleanup()

    def run_reset(self):
        """Use cr50_reset_odl to hold Cr50 in reset."""
        logging.info('cr50_reset_odl:on')
        self._servo.dut_control('cr50_reset_odl:on')

    def recover_from_reset(self):
        """Release the reset signal."""
        logging.info('cr50_reset_odl:off')
        self._servo.dut_control('cr50_reset_odl:off')


class BatteryCutoffReset(Cr50Reset):
    """Class for using a battery cutoff through EC commands to reset cr50."""

    REQUIRED_SETUP = (
        # Rescue is done through Cr50 uart. It requires a flex cable not ccd.
        'flex',
        # We need type c servo v4 to recover from battery_cutoff.
        'type-c_servo_v4',
        # Cr50 rescue is done through cr50 uart.
        'cr50_uart',
        # EC console needs to be read-write to issue cutoff command.
        'ec_uart',
    )

    def run_reset(self):
        """Use EC commands to cutoff the battery."""
        self._servo.dut_control('servo_v4_role:snk')

        if self._servo.dut_control('ec_board', check_error=False)[0]:
            logging.warning('EC is unresponsive. Cutoff may not work.')

        self._servo.dut_control('ec_uart_cmd:cutoff', check_error=False,
                                wait=True)
        self._servo.dut_control('ec_uart_cmd:reboot', check_error=False,
                                wait=True)

        if not self._servo.dut_control('ec_board', check_error=False)[0]:
            raise Error('EC still responsive after cutoff')
        logging.info('Device is cutoff')

    def recover_from_reset(self):
        """Connect power using servo v4 to recover from cutoff."""
        logging.info('"Connecting" adapter')
        self._servo.dut_control('servo_v4_role:src', wait=True)


class ManualReset(Cr50Reset):
    """Class for using a manual reset to reset Cr50."""

    REQUIRED_SETUP = (
        # Rescue is done through Cr50 uart. It requires a flex cable not ccd.
        'flex',
        # Cr50 rescue is done through cr50 uart.
        'cr50_uart',
    )

    PROMPT_WAIT = 5
    USER_RESET_TIMEOUT = 60

    def run_reset(self):
        """Nothing to do. User will reset cr50."""

    def recover_from_reset(self):
        """Wait for the user to reset cr50."""
        end_time = time.time() + self.USER_RESET_TIMEOUT
        while time.time() < end_time:
            logging.info('Press enter after you reset cr50')
            user_input = select.select([sys.stdin], [], [], self.PROMPT_WAIT)[0]
            if user_input:
                logging.info('User reset done')
                return
        logging.warning('User input timeout: assuming cr50 reset')


class FlashCr50(object):
    """Class for updating cr50."""

    NAME = 'FlashCr50'
    PACKAGE = ''
    DEFAULT_UPDATER = ''

    def __init__(self, cmd):
        """Verify the update command exists.

        Args:
            cmd: The updater command.

        Raises:
            Error if no valid updater command was found.
        """
        updater = self.get_updater(cmd)
        if not updater:
            emerge_msg = (('Try emerging ' + self.PACKAGE) if self.PACKAGE
                          else '')
            raise Error('Could not find %s command.%s' % (self, emerge_msg))
        self._updater = updater

    def get_updater(self, cmd):
        """Find a valid updater command.

        Args:
            cmd: the updater command.

        Returns:
            A command string or None if none of the commands ran successfully.
            The command string will be the one supplied or the DEFAULT_UPDATER
            command.
        """
        if not self.updater_works(cmd):
            return cmd

        use_default = (self.DEFAULT_UPDATER and
                       not self.updater_works(self.DEFAULT_UPDATER))
        if use_default:
            logging.debug('%r failed using %r to update.', cmd,
                          self.DEFAULT_UPDATER)
            return self.DEFAULT_UPDATER
        return None

    @staticmethod
    def updater_works(cmd):
        """Verify the updater command.

        Returns:
          non-zero status if the command failed.
        """
        logging.debug('Testing update command %r.', cmd)
        exit_status, output = run_command([cmd, '-h'], check_error=False)
        if 'Usage' in output:
            return 0
        if exit_status:
            logging.debug('Could not run %r (%s): %s', cmd, exit_status, output)
        return exit_status

    def update(self, image):
        """Try to update cr50 to the given image."""
        raise NotImplementedError()

    def __str__(self):
        """Use the updater name for the tostring."""
        return self.NAME


class GsctoolUpdater(FlashCr50):
    """Class to flash cr50 using gsctool."""

    NAME = 'gsctool'
    PACKAGE = 'ec-utils'
    DEFAULT_UPDATER = '/usr/sbin/gsctool'

    # Common failures exit with this status. Use STANDARD_ERRORS to map the
    # exit status to reasons for the failure.
    STANDARD_ERROR_REGEX = r'Error: status (\S+)'
    STANDARD_ERRORS = {
        '0x8': 'Rejected image with old header.',
        '0x9': 'Update too soon.',
        '0xc': 'Board id mismatch',
    }

    def __init__(self, cmd, serial=None):
        """Generate the gsctool command.

        Args:
            cmd: gsctool updater command.
            serial: The serial number of the CCD device being updated.
        """
        super(GsctoolUpdater, self).__init__(cmd)
        self._gsctool_cmd = [self._updater]
        if serial:
            self._gsctool_cmd.extend(['-n', serial])

    def update(self, image):
        """Use gsctool to update cr50.

        Args:
            image: Cr50Image object.
        """
        update_cmd = self._gsctool_cmd[:]
        update_cmd.append(image.get_bin())
        exit_status, output = run_command(update_cmd, check_error=False)
        if not exit_status or (exit_status == 1 and 'image updated' in output):
            logging.info('update ok')
            return
        if exit_status == 3:
            match = re.search(self.STANDARD_ERROR_REGEX, output)
            if match:
                update_error = match.group(1)
                logging.info('Update error %s', update_error)
                raise Error(self.STANDARD_ERRORS[update_error])
        raise Error('gsctool update error: %s' % output.splitlines()[-1])


class Cr50RescueUpdater(FlashCr50):
    """Class to flash cr50 through servo micro uart."""

    NAME = 'cr50-rescue'
    PACKAGE = 'cr50-utils'
    DEFAULT_UPDATER = '/usr/bin/cr50-rescue'

    WAIT_FOR_UPDATE = 120
    RESCUE_RESET_DELAY = 5

    def __init__(self, cmd, port, reset_type):
        """Initialize cr50-rescue updater.

        cr50-rescue can only be done through servo, because it needs access to
        a lot of dut-controls and cr50 uart through servo micro. During rescue
        Cr50 has to do a hard reset, so the user should supply a valid reset
        method for the setup that's being used.

        Args:
            cmd: The cr50-rescue command.
            port: The servo port of the device being updated.
            reset_type: A string (one of SUPPORTED_RESETS) that describes how
                        to reset Cr50 during cr50-rescue.
        """
        super(Cr50RescueUpdater, self).__init__(cmd)
        self._servo = Servo(port)
        self._rescue_thread = None
        self._rescue_process = None
        self._cr50_reset = self.get_cr50_reset(reset_type)

    def get_cr50_reset(self, reset_type):
        """Get the cr50 reset object for the given reset_type.

        Args:
            reset_type: a string describing how cr50 will be reset. It must be
                        in SUPPORTED_RESETS.

        Returns:
            The Cr50Reset object for the given reset_type.
        """
        assert reset_type in SUPPORTED_RESETS, '%s is unsupported.' % reset_type
        if reset_type == 'battery_cutoff':
            return BatteryCutoffReset(self._servo, reset_type)
        elif reset_type == 'cr50_reset_odl':
            return Cr50ResetODLReset(self._servo, reset_type)
        return ManualReset(self._servo, reset_type)

    def update(self, image):
        """Use cr50-rescue to update cr50 then cleanup.

        Args:
            image: Cr50Image object.
        """
        update_file = image.get_rw_hex()
        try:
            self.run_update(update_file)
        finally:
            self.restore_state()

    def start_rescue_process(self, update_file):
        """Run cr50-rescue in a process, so it can be killed it if it hangs."""
        pty = self._servo.get_raw_cr50_pty()

        rescue_cmd = [self._updater, '-v', '-i', update_file, '-d', pty]
        logging.info('Starting cr50-rescue: %s',
                     cros_build_lib.CmdToStr(rescue_cmd))

        self._rescue_process = subprocess.Popen(rescue_cmd)
        self._rescue_process.communicate()
        logging.info('Rescue Finished')

    def start_rescue_thread(self, update_file):
        """Start cr50-rescue."""
        self._rescue_thread = threading.Thread(target=self.start_rescue_process,
                                               args=[update_file])
        self._rescue_thread.start()

    def run_update(self, update_file):
        """Run the Update"""
        # Enter reset before starting rescue, so any extra cr50 messages won't
        # interfere with cr50-rescue.
        self._cr50_reset.enter_reset()

        self.start_rescue_thread(update_file)

        time.sleep(self.RESCUE_RESET_DELAY)
        # Resume from cr50 reset.
        self._cr50_reset.exit_reset()

        self._rescue_thread.join(self.WAIT_FOR_UPDATE)

        logging.info('cr50_version:%s', self._servo.get_cr50_version())

    def restore_state(self):
        """Try to get the device out of reset and restore all controls"""
        try:
            self._cr50_reset.cleanup()
        finally:
            self.cleanup_rescue_thread()

    def cleanup_rescue_thread(self):
        """Cleanup the rescue process and handle any errors."""
        if not self._rescue_thread:
            return
        if self._rescue_thread.is_alive():
            logging.info('Killing cr50-rescue process')
            self._rescue_process.terminate()
            self._rescue_thread.join()

        self._rescue_thread = None
        if self._rescue_process.returncode:
            logging.info('cr50-rescue failed.')
            logging.info('stderr: %s', self._rescue_process.stderr)
            logging.info('stdout: %s', self._rescue_process.stdout)
            logging.info('returncode: %s', self._rescue_process.returncode)
            raise Error('cr50-rescue failed (%d)' %
                        self._rescue_process.returncode)


def parse_args(argv):
    """Parse commandline arguments.

    Args:
        argv: command line args

    Returns:
        options: an argparse.Namespace.
    """
    usage = ('%s -i $IMAGE [ -c cr50-rescue -p $SERVO_PORT [ -r '
             '$RESET_METHOD]]' % os.path.basename(argv[0]))
    parser = argparse.ArgumentParser(usage=usage, description=__doc__)
    parser.add_argument('-d', '--debug', action='store_true', default=False,
                        help='enable debug messages.')
    parser.add_argument('-i', '--image', type=str,
                        help='Path to cr50 binary image.')
    parser.add_argument('-R', '--release', type=str,
                        choices=RELEASE_PATHS.keys(),
                        help='Type of cr50 release. Use instead of the image '
                        'arg.')
    parser.add_argument('-c', '--updater-cmd', type=str, default='gsctool',
                        help='Tool to update cr50. Either gsctool or '
                        'cr50-rescue')
    parser.add_argument('-s', '--serial', type=str, default='',
                        help='serial number to pass to gsctool.')
    parser.add_argument('-p', '--port', type=str, default='',
                        help='port servod is listening on (required for '
                        'rescue).')
    parser.add_argument('-r', '--reset-type', default='battery_cutoff',
                        choices=SUPPORTED_RESETS,
                        type=str, help='The method for cr50 reset.')
    parser.add_argument('-a', '--artifacts-dir', default=None, type=str,
                        help='Location to store artifacts')
    opts = parser.parse_args(argv[1:])
    if 'cr50-rescue' in opts.updater_cmd and not opts.port:
        raise parser.error('Servo port is required for cr50 rescue')
    return opts


def get_updater(opts):
    """Get the updater object."""
    if 'cr50-rescue' in opts.updater_cmd:
        return Cr50RescueUpdater(opts.updater_cmd, opts.port, opts.reset_type)
    if 'gsctool' in opts.updater_cmd:
        return GsctoolUpdater(opts.updater_cmd, opts.serial)
    raise Error('Unsupported update command %r' % opts.updater_cmd)


def main(argv):
    """Update cr50 using gsctool or cr50-rescue."""
    opts = parse_args(argv)

    loglevel = logging.INFO
    log_format = '%(asctime)s - %(levelname)7s'
    if opts.debug:
        loglevel = logging.DEBUG
        log_format += ' - %(lineno)3d:%(funcName)-15s'
    log_format += ' - %(message)s'
    logging.basicConfig(level=loglevel, format=log_format)

    image = Cr50Image(RELEASE_PATHS.get(opts.release, opts.image),
                      opts.artifacts_dir)
    flash_cr50 = get_updater(opts)

    logging.info('Using %s to update to %s', flash_cr50,
                 image.get_original_basename())
    flash_cr50.update(image)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
