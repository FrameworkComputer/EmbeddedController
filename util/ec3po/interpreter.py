#!/usr/bin/python2
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""EC-3PO EC Interpreter

interpreter provides the interpretation layer between the EC UART and the user.
It recives commands through its command pipe, formats the commands for the EC,
and sends the command to the EC.  It also presents data from the EC to either be
displayed via the interactive console interface, or some other consumer.  It
additionally supports automatic command retrying if the EC drops a character in
a command.
"""
from __future__ import print_function
from chromite.lib import cros_logging as logging
import os
import Queue
import select


COMMAND_RETRIES = 3  # Number of attempts to retry a command.
EC_MAX_READ = 1024  # Max bytes to read at a time from the EC.


class Interpreter(object):
  """Class which provides the interpretation layer between the EC and user.

  This class essentially performs all of the intepretation for the EC and the
  user.  It handles all of the automatic command retrying as well as the
  formation of commands.

  Attributes:
    ec_uart_pty: A string representing the EC UART to connect to.
    cmd_pipe: A multiprocessing.Connection object which represents the
      Interpreter side of the command pipe.  This must be a bidirectional pipe.
      Commands and responses will utilize this pipe.
    dbg_pipe: A multiprocessing.Connection object which represents the
      Interpreter side of the debug pipe. This must be a unidirectional pipe
      with write capabilities.  EC debug output will utilize this pipe.
    cmd_retries: An integer representing the number of attempts the console
      should retry commands if it receives an error.
    log_level: An integer representing the numeric value of the log level.
    inputs: A list of objects that the intpreter selects for reading.
      Initially, these are the EC UART and the command pipe.
    outputs: A list of objects that the interpreter selects for writing.
    ec_cmd_queue: A FIFO queue used for sending commands down to the EC UART.
    cmd_in_progress: A string that represents the current command sent to the
      EC that is pending reception verification.
  """
  def __init__(self, ec_uart_pty, cmd_pipe, dbg_pipe, log_level=logging.INFO):
    """Intializes an Interpreter object with the provided args.

    Args:
      ec_uart_pty: A string representing the EC UART to connect to.
      cmd_pipe: A multiprocessing.Connection object which represents the
        Interpreter side of the command pipe.  This must be a bidirectional
        pipe.  Commands and responses will utilize this pipe.
      dbg_pipe: A multiprocessing.Connection object which represents the
        Interpreter side of the debug pipe. This must be a unidirectional pipe
        with write capabilities.  EC debug output will utilize this pipe.
      cmd_retries: An integer representing the number of attempts the console
        should retry commands if it receives an error.
      log_level: An optional integer representing the numeric value of the log
        level.  By default, the log level will be logging.INFO (20).
    """
    self.ec_uart_pty = open(ec_uart_pty, 'a+')
    self.cmd_pipe = cmd_pipe
    self.dbg_pipe = dbg_pipe
    self.cmd_retries = COMMAND_RETRIES
    self.log_level = log_level
    self.inputs = [self.ec_uart_pty, self.cmd_pipe]
    self.outputs = []
    self.ec_cmd_queue = Queue.Queue()
    self.cmd_in_progress = ''

  def EnqueueCmd(self, packed_cmd):
    """Enqueue a packed console command to be sent to the EC UART.

    Args:
      packed_cmd: A string which contains the packed command to be sent.
    """
    # Enqueue a packed command to be sent to the EC.
    self.ec_cmd_queue.put(packed_cmd)
    logging.debug('Commands now in queue: %d', self.ec_cmd_queue.qsize())
    # Add the EC UART as an output to be serviced.
    self.outputs.append(self.ec_uart_pty)

  def PackCommand(self, raw_cmd):
    r"""Packs a command for use with error checking.

    For error checking, we pack console commands in a particular format.  The
    format is as follows:

      &&[x][x][x][x]&{cmd}\n\n
      ^ ^    ^^    ^^  ^  ^-- 2 newlines.
      | |    ||    ||  |-- the raw console command.
      | |    ||    ||-- 1 ampersand.
      | |    ||____|--- 2 hex digits representing the CRC8 of cmd.
      | |____|-- 2 hex digits reprsenting the length of cmd.
      |-- 2 ampersands

    Args:
      raw_cmd: A pre-packed string which contains the raw command.

    Returns:
      A string which contains the packed command.
    """
    # The command format is as follows.
    # &&[x][x][x][x]&{cmd}\n\n
    packed_cmd = []
    packed_cmd.append('&&')
    # The first pair of hex digits are the length of the command.
    packed_cmd.append('%02x' % len(raw_cmd))
    # Then the CRC8 of cmd.
    packed_cmd.append('%02x' % Crc8(raw_cmd))
    packed_cmd.append('&')
    # Now, the raw command followed by 2 newlines.
    packed_cmd.append(raw_cmd)
    packed_cmd.append('\n\n')
    return ''.join(packed_cmd)

  def ProcessCommand(self, command):
    """Captures the input determines what actions to take.

    Args:
      command: A string representing the command sent by the user.
    """
    command = command.strip()
    # There's nothing to do if the command is empty.
    if len(command) == 0:
      return

    # All other commands need to be packed first before they go to the EC.
    packed_cmd = self.PackCommand(command)
    logging.debug('packed cmd: ' + packed_cmd)
    self.EnqueueCmd(packed_cmd)
    # TODO(aaboagye): Make a dict of commands and keys and eventually, handle
    # partial matching based on unique prefixes.

  def CheckECResponse(self):
    """Checks the response from the EC for any errors."""
    # An invalid response is at most 4 bytes.
    data = os.read(self.ec_uart_pty.fileno(), 4)
    if '&E' not in data:
      # No error received.  Clear the command in progress.
      self.cmd_in_progress = ''
      # Reset the retry count.
      self.cmd_retries = COMMAND_RETRIES
      # Forward the data to the user.
      self.dbg_pipe.send(data)
    elif self.cmd_retries > 0:
      # The EC encountered an error.  We'll have to retry again.
      logging.warning('EC replied with error.  Retrying.')
      self.cmd_retries -= 1
      logging.warning('Retries remaining: %d', self.cmd_retries)
      # Add the EC UART to the writers again.
      self.outputs.append(self.ec_uart_pty)
    else:
      # We're out of retries, so just give up.
      logging.error('Command failed.  No retries left.')
      # Clear the command in progress.
      self.cmd_in_progress = ''
      # Reset the retry count.
      self.cmd_retries = COMMAND_RETRIES

  def SendCmdToEC(self):
    """Sends a command to the EC."""
    # If we're retrying a command, just try to send it again.
    if self.cmd_retries < COMMAND_RETRIES:
      cmd = self.cmd_in_progress
    else:
      # If we're not retrying, we should not be writing to the EC if we have no
      # items in our command queue.
      assert not self.ec_cmd_queue.empty()
      # Get the command to send.
      cmd = self.ec_cmd_queue.get()

    # Send the command.
    logging.debug('Sending command to EC.')
    self.ec_uart_pty.write(cmd)
    self.ec_uart_pty.flush()

    # Now, that we've sent the command we will need to make sure the EC
    # received it without an error.  Store the current command as in
    # progress.  We will clear this if the EC responds with a non-error.
    self.cmd_in_progress = cmd
    # Remove the EC UART from the writers while we wait for a response.
    self.outputs.remove(self.ec_uart_pty)


def Crc8(data):
  """Calculates the CRC8 of data.

  The generator polynomial used is: x^8 + x^2 + x + 1.
  This is the same implementation that is used in the EC.

  Args:
    data: A string of data that we wish to calculate the CRC8 on.

  Returns:
    crc >> 8: An integer representing the CRC8 value.
  """
  crc = 0
  for byte in data:
    crc ^= (ord(byte) << 8)
    for _ in range(8):
      if crc & 0x8000:
        crc ^= (0x1070 << 3)
      crc <<= 1
  return crc >> 8


def StartLoop(interp):
  """Starts an infinite loop of servicing the user and the EC.

  StartLoop checks to see if there are any commands to process, processing them
  if any, and forwards EC output to the user.

  When sending a command to the EC, we send the command once and check the
  response to see if the EC encountered an error when receiving the command.  An
  error condition is reported to the interpreter by a string with at least one
  '&' and 'E'.  The full string is actually '&&EE', however it's possible that
  the leading ampersand or trailing 'E' could be dropped.  If an error is
  encountered, the interpreter will retry up to the amount configured.

  Args:
    interp: An Interpreter object that has been properly initialised.
  """
  while True:
    readable, writeable, _ = select.select(interp.inputs, interp.outputs, [])

    for obj in readable:
      # Handle any debug prints from the EC.
      if obj is interp.ec_uart_pty:
        logging.debug('EC has data')
        if interp.cmd_in_progress:
          # A command was just sent to the EC.  We need to check to see if the
          # EC is telling us that it received a corrupted command.
          logging.debug('Command in progress so checking response...')
          interp.CheckECResponse()

        # Read what the EC sent us.
        data = os.read(obj.fileno(), EC_MAX_READ)
        logging.debug('got: \'%s\'', data)
        # For now, just forward everything the EC sends us.
        logging.debug('Forwarding to user...')
        interp.dbg_pipe.send(data)

      # Handle any commands from the user.
      elif obj is interp.cmd_pipe:
        logging.debug('Command data available.  Begin processing.')
        data = interp.cmd_pipe.recv()
        # Process the command.
        interp.ProcessCommand(data)

    for obj in writeable:
      # Send a command to the EC.
      if obj is interp.ec_uart_pty:
        interp.SendCmdToEC()
