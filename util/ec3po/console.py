#!/usr/bin/env python
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""EC-3PO Interactive Console Interface

console provides the console interface between the user and the interpreter.  It
handles the presentation of the EC console including editing methods as well as
session-persistent command history.
"""

# Note: This is a py2/3 compatible file.

from __future__ import print_function

import argparse
import binascii
import ctypes
from datetime import datetime
# pylint: disable=cros-logging-import
import logging
import os
import pty
import re
import select
import stat
import sys
import traceback

import six

import interpreter
import threadproc_shim


PROMPT = b'> '
CONSOLE_INPUT_LINE_SIZE = 80  # Taken from the CONFIG_* with the same name.
CONSOLE_MAX_READ = 100  # Max bytes to read at a time from the user.
LOOK_BUFFER_SIZE = 256  # Size of search window when looking for the enhanced EC
                        # image string.

# In console_init(), the EC will print a string saying that the EC console is
# enabled.  Enhanced images will print a slightly different string.  These
# regular expressions are used to determine at reboot whether the EC image is
# enhanced or not.
ENHANCED_IMAGE_RE = re.compile(br'Enhanced Console is enabled '
                               br'\(v([0-9]+\.[0-9]+\.[0-9]+)\)')
NON_ENHANCED_IMAGE_RE = re.compile(br'Console is enabled; ')

# The timeouts are really only useful for enhanced EC images, but otherwise just
# serve as a delay for non-enhanced EC images.  Therefore, we can keep this
# value small enough so that there's not too much of a delay, but long enough
# that enhanced EC images can respond in time.  Once we've detected an enhanced
# EC image, we can increase the timeout for stability just in case it takes a
# bit longer to receive an ACK for some reason.
NON_ENHANCED_EC_INTERROGATION_TIMEOUT = 0.3  # Maximum number of seconds to wait
                                             # for a response to an
                                             # interrogation of a non-enhanced
                                             # EC image.
ENHANCED_EC_INTERROGATION_TIMEOUT = 1.0  # Maximum number of seconds to wait for
                                         # a response to an interrogation of an
                                         # enhanced EC image.
# List of modes which control when interrogations are performed with the EC.
INTERROGATION_MODES = [b'never', b'always', b'auto']
# Format for printing host timestamp
HOST_STRFTIME="%y-%m-%d %H:%M:%S.%f"


class EscState(object):
  """Class which contains an enumeration for states of ESC sequences."""
  ESC_START = 1
  ESC_BRACKET = 2
  ESC_BRACKET_1 = 3
  ESC_BRACKET_3 = 4
  ESC_BRACKET_8 = 5


class ControlKey(object):
  """Class which contains codes for various control keys."""
  BACKSPACE = 0x08
  CTRL_A = 0x01
  CTRL_B = 0x02
  CTRL_D = 0x04
  CTRL_E = 0x05
  CTRL_F = 0x06
  CTRL_K = 0x0b
  CTRL_N = 0xe
  CTRL_P = 0x10
  CARRIAGE_RETURN = 0x0d
  ESC = 0x1b


class Console(object):
  """Class which provides the console interface between the EC and the user.

  This class essentially represents the console interface between the user and
  the EC.  It handles all of the console editing behaviour

  Attributes:
    logger: A logger for this module.
    master_pty: File descriptor to the master side of the PTY.  Used for driving
      output to the user and receiving user input.
    user_pty: A string representing the PTY name of the served console.
    cmd_pipe: A socket.socket or multiprocessing.Connection object which
      represents the console side of the command pipe.  This must be a
      bidirectional pipe.  Console commands and responses utilize this pipe.
    dbg_pipe: A socket.socket or multiprocessing.Connection object which
      represents the console's read-only side of the debug pipe.  This must be a
      unidirectional pipe attached to the intepreter.  EC debug messages use
      this pipe.
    oobm_queue: A queue.Queue or multiprocessing.Queue which is used for out of
      band management for the interactive console.
    input_buffer: A string representing the current input command.
    input_buffer_pos: An integer representing the current position in the buffer
      to insert a char.
    partial_cmd: A string representing the command entered on a line before
      pressing the up arrow keys.
    esc_state: An integer represeting the current state within an escape
      sequence.
    line_limit: An integer representing the maximum number of characters on a
      line.
    history: A list of strings containing the past entered console commands.
    history_pos: An integer representing the current history buffer position.
      This index is used to show previous commands.
    prompt: A string representing the console prompt displayed to the user.
    enhanced_ec: A boolean indicating if the EC image that we are currently
      communicating with is enhanced or not.  Enhanced EC images will support
      packed commands and host commands over the UART.  This defaults to False
      until we perform some handshaking.
    interrogation_timeout: A float representing the current maximum seconds to
      wait for a response to an interrogation.
    receiving_oobm_cmd: A boolean indicating whether or not the console is in
      the middle of receiving an out of band command.
    pending_oobm_cmd: A string containing the pending OOBM command.
    interrogation_mode: A string containing the current mode of whether
      interrogations are performed with the EC or not and how often.
    raw_debug: Flag to indicate whether per interrupt data should be logged to
      debug
    output_line_log_buffer: buffer for lines coming from the EC to log to debug
  """

  def __init__(self, master_pty, user_pty, interface_pty, cmd_pipe, dbg_pipe,
               name=None):
    """Initalises a Console object with the provided arguments.

    Args:
    master_pty: File descriptor to the master side of the PTY.  Used for driving
      output to the user and receiving user input.
    user_pty: A string representing the PTY name of the served console.
    interface_pty: A string representing the PTY name of the served command
      interface.
    cmd_pipe: A socket.socket or multiprocessing.Connection object which
      represents the console side of the command pipe.  This must be a
      bidirectional pipe.  Console commands and responses utilize this pipe.
    dbg_pipe: A socket.socket or multiprocessing.Connection object which
      represents the console's read-only side of the debug pipe.  This must be a
      unidirectional pipe attached to the intepreter.  EC debug messages use
      this pipe.
    name: the console source name
    """
    # Create a unique logger based on the console name
    console_prefix = ('%s - ' % name) if name else ''
    logger = logging.getLogger('%sEC3PO.Console' % console_prefix)
    self.logger = interpreter.LoggerAdapter(logger, {'pty': user_pty})
    self.master_pty = master_pty
    self.user_pty = user_pty
    self.interface_pty = interface_pty
    self.cmd_pipe = cmd_pipe
    self.dbg_pipe = dbg_pipe
    self.oobm_queue = threadproc_shim.Queue()
    self.input_buffer = b''
    self.input_buffer_pos = 0
    self.partial_cmd = b''
    self.esc_state = 0
    self.line_limit = CONSOLE_INPUT_LINE_SIZE
    self.history = []
    self.history_pos = 0
    self.prompt = PROMPT
    self.enhanced_ec = False
    self.interrogation_timeout = NON_ENHANCED_EC_INTERROGATION_TIMEOUT
    self.receiving_oobm_cmd = False
    self.pending_oobm_cmd = b''
    self.interrogation_mode = b'auto'
    self.timestamp_enabled = True
    self.look_buffer = b''
    self.raw_debug = False
    self.output_line_log_buffer = []

  def __str__(self):
    """Show internal state of Console object as a string."""
    string = []
    string.append('master_pty: %s' % self.master_pty)
    string.append('user_pty: %s' % self.user_pty)
    string.append('interface_pty: %s' % self.interface_pty)
    string.append('cmd_pipe: %s' % self.cmd_pipe)
    string.append('dbg_pipe: %s' % self.dbg_pipe)
    string.append('oobm_queue: %s' % self.oobm_queue)
    string.append('input_buffer: %s' % self.input_buffer)
    string.append('input_buffer_pos: %d' % self.input_buffer_pos)
    string.append('esc_state: %d' % self.esc_state)
    string.append('line_limit: %d' % self.line_limit)
    string.append('history: [\'' + '%s' % repr(self.history) + '\']')
    string.append('history_pos: %d' % self.history_pos)
    string.append('prompt: \'%s\'' % self.prompt)
    string.append('partial_cmd: \'%s\''% self.partial_cmd)
    string.append('interrogation_mode: \'%s\'' % self.interrogation_mode)
    string.append('look_buffer: \'%s\'' % self.look_buffer)
    return '\n'.join(string)

  def LogConsoleOutput(self, data):
    """Log to debug user MCU output to master_pty when line is filled.

    The logging also suppresses the Cr50 spinner lines by removing characters
    when it sees backspaces.

    Args:
      data: binary string received from MCU
    """
    data = list(data)

    # This is a list of already filtered characters (or placeholders).
    line = self.output_line_log_buffer

    symbols = {
            b'\n': u'\\n',
            b'\r': u'\\r',
            b'\t': u'\\t'
    }
    # self.logger.debug(u'%s + %r', u''.join(line), ''.join(data))
    while data:
      byte = data.pop(0)
      if byte == '\n':
        line.append(symbols[byte])
        if line:
          self.logger.debug(u'%s', ''.join(line))
        line = []
      elif byte == b'\b':
        # Backspace: trim the last character off the buffer
        if line:
          line.pop(-1)
      elif byte in symbols:
        line.append(symbols[byte])
      elif byte < b' ' or byte > b'~':
        # Turn any character that isn't printable ASCII into escaped hex.
        # ' ' is chr(20), and 0-19 are unprintable control characters.
        # '~' is chr(126), and 127 is DELETE.  128-255 are control and Latin-1.
        line.append(u'\\x%02x' % ord(byte))
      else:
        line.append(u'%s' % byte)
    self.output_line_log_buffer = line

  def PrintHistory(self):
    """Print the history of entered commands."""
    fd = self.master_pty
    # Make it pretty by figuring out how wide to pad the numbers.
    wide = (len(self.history) // 10) + 1
    for i in range(len(self.history)):
      line = b' %*d %s\r\n' % (wide, i, self.history[i])
      os.write(fd, line)

  def ShowPreviousCommand(self):
    """Shows the previous command from the history list."""
    # There's nothing to do if there's no history at all.
    if not self.history:
      self.logger.debug('No history to print.')
      return

    # Don't do anything if there's no more history to show.
    if self.history_pos == 0:
      self.logger.debug('No more history to show.')
      return

    self.logger.debug('current history position: %d.', self.history_pos)

    # Decrement the history buffer position.
    self.history_pos -= 1
    self.logger.debug('new history position.: %d', self.history_pos)

    # Save the text entered on the console if any.
    if self.history_pos == len(self.history)-1:
      self.logger.debug('saving partial_cmd: \'%s\'', self.input_buffer)
      self.partial_cmd = self.input_buffer

    # Backspace the line.
    for _ in range(self.input_buffer_pos):
      self.SendBackspace()

    # Print the last entry in the history buffer.
    self.logger.debug('printing previous entry %d - %s', self.history_pos,
                      self.history[self.history_pos])
    fd = self.master_pty
    prev_cmd = self.history[self.history_pos]
    os.write(fd, prev_cmd)
    # Update the input buffer.
    self.input_buffer = prev_cmd
    self.input_buffer_pos = len(prev_cmd)

  def ShowNextCommand(self):
    """Shows the next command from the history list."""
    # Don't do anything if there's no history at all.
    if not self.history:
      self.logger.debug('History buffer is empty.')
      return

    fd = self.master_pty

    self.logger.debug('current history position: %d', self.history_pos)
    # Increment the history position.
    self.history_pos += 1

    # Restore the partial cmd.
    if self.history_pos == len(self.history):
      self.logger.debug('Restoring partial command of \'%s\'', self.partial_cmd)
      # Backspace the line.
      for _ in range(self.input_buffer_pos):
        self.SendBackspace()
      # Print the partially entered command if any.
      os.write(fd, self.partial_cmd)
      self.input_buffer = self.partial_cmd
      self.input_buffer_pos = len(self.input_buffer)
      # Now that we've printed it, clear the partial cmd storage.
      self.partial_cmd = b''
      # Reset history position.
      self.history_pos = len(self.history)
      return

    self.logger.debug('new history position: %d', self.history_pos)
    if self.history_pos > len(self.history)-1:
      self.logger.debug('No more history to show.')
      self.history_pos -= 1
      self.logger.debug('Reset history position to %d', self.history_pos)
      return

    # Backspace the line.
    for _ in range(self.input_buffer_pos):
      self.SendBackspace()

    # Print the newer entry from the history buffer.
    self.logger.debug('printing next entry %d - %s', self.history_pos,
                      self.history[self.history_pos])
    next_cmd = self.history[self.history_pos]
    os.write(fd, next_cmd)
    # Update the input buffer.
    self.input_buffer = next_cmd
    self.input_buffer_pos = len(next_cmd)
    self.logger.debug('new history position: %d.', self.history_pos)

  def SliceOutChar(self):
    """Remove a char from the line and shift everything over 1 column."""
    fd = self.master_pty
    # Remove the character at the input_buffer_pos by slicing it out.
    self.input_buffer = self.input_buffer[0:self.input_buffer_pos] + \
                        self.input_buffer[self.input_buffer_pos+1:]
    # Write the rest of the line
    moved_col = os.write(fd, self.input_buffer[self.input_buffer_pos:])
    # Write a space to clear out the last char
    moved_col += os.write(fd, b' ')
    # Update the input buffer position.
    self.input_buffer_pos += moved_col
    # Reset the cursor
    self.MoveCursor('left', moved_col)

  def HandleEsc(self, byte):
    """HandleEsc processes escape sequences.

    Args:
      byte: An integer representing the current byte in the sequence.
    """
    # We shouldn't be handling an escape sequence if we haven't seen one.
    assert self.esc_state != 0

    if self.esc_state is EscState.ESC_START:
      self.logger.debug('ESC_START')
      if byte == ord('['):
        self.esc_state = EscState.ESC_BRACKET
        return

      else:
        self.logger.error('Unexpected sequence. %c', byte)
        self.esc_state = 0

    elif self.esc_state is EscState.ESC_BRACKET:
      self.logger.debug('ESC_BRACKET')
      # Left Arrow key was pressed.
      if byte == ord('D'):
        self.logger.debug('Left arrow key pressed.')
        self.MoveCursor('left', 1)
        self.esc_state = 0 # Reset the state.
        return

      # Right Arrow key.
      elif byte == ord('C'):
        self.logger.debug('Right arrow key pressed.')
        self.MoveCursor('right', 1)
        self.esc_state = 0 # Reset the state.
        return

      # Up Arrow key.
      elif byte == ord('A'):
        self.logger.debug('Up arrow key pressed.')
        self.ShowPreviousCommand()
        # Reset the state.
        self.esc_state = 0 # Reset the state.
        return

      # Down Arrow key.
      elif byte == ord('B'):
        self.logger.debug('Down arrow key pressed.')
        self.ShowNextCommand()
        # Reset the state.
        self.esc_state = 0 # Reset the state.
        return

      # For some reason, minicom sends a 1 instead of 7. /shrug
      # TODO(aaboagye): Figure out why this happens.
      elif byte == ord('1') or byte == ord('7'):
        self.esc_state = EscState.ESC_BRACKET_1

      elif byte == ord('3'):
        self.esc_state = EscState.ESC_BRACKET_3

      elif byte == ord('8'):
        self.esc_state = EscState.ESC_BRACKET_8

      else:
        self.logger.error(r'Bad or unhandled escape sequence. got ^[%c\(%d)',
                          chr(byte), byte)
        self.esc_state = 0
        return

    elif self.esc_state is EscState.ESC_BRACKET_1:
      self.logger.debug('ESC_BRACKET_1')
      # HOME key.
      if byte == ord('~'):
        self.logger.debug('Home key pressed.')
        self.MoveCursor('left', self.input_buffer_pos)
        self.esc_state = 0 # Reset the state.
        self.logger.debug('ESC sequence complete.')
        return

    elif self.esc_state is EscState.ESC_BRACKET_3:
      self.logger.debug('ESC_BRACKET_3')
      # DEL key.
      if byte == ord('~'):
        self.logger.debug('Delete key pressed.')
        if self.input_buffer_pos != len(self.input_buffer):
          self.SliceOutChar()
        self.esc_state = 0 # Reset the state.

    elif self.esc_state is EscState.ESC_BRACKET_8:
      self.logger.debug('ESC_BRACKET_8')
      # END key.
      if byte == ord('~'):
        self.logger.debug('End key pressed.')
        self.MoveCursor('right',
                        len(self.input_buffer) - self.input_buffer_pos)
        self.esc_state = 0 # Reset the state.
        self.logger.debug('ESC sequence complete.')
        return

      else:
        self.logger.error('Unexpected sequence. %c', byte)
        self.esc_state = 0

    else:
      self.logger.error('Unexpected sequence. %c', byte)
      self.esc_state = 0

  def ProcessInput(self):
    """Captures the input determines what actions to take."""
    # There's nothing to do if the input buffer is empty.
    if len(self.input_buffer) == 0:
      return

    # Don't store 2 consecutive identical commands in the history.
    if (self.history and self.history[-1] != self.input_buffer
        or not self.history):
      self.history.append(self.input_buffer)

    # Split the command up by spaces.
    line = self.input_buffer.split(b' ')
    self.logger.debug('cmd: %s', self.input_buffer)
    cmd = line[0].lower()

    # The 'history' command is a special case that we handle locally.
    if cmd == 'history':
      self.PrintHistory()
      return

    # Send the command to the interpreter.
    self.logger.debug('Sending command to interpreter.')
    self.cmd_pipe.send(self.input_buffer)

  def CheckForEnhancedECImage(self):
    """Performs an interrogation of the EC image.

    Send a SYN and expect an ACK.  If no ACK or the response is incorrect, then
    assume that the current EC image that we are talking to is not enhanced.

    Returns:
      is_enhanced: A boolean indicating whether the EC responded to the
        interrogation correctly.

    Raises:
      EOFError: Allowed to propagate through from self.dbg_pipe.recv().
    """
    # Send interrogation byte and wait for the response.
    self.logger.debug('Performing interrogation.')
    self.cmd_pipe.send(interpreter.EC_SYN)

    response = ''
    if self.dbg_pipe.poll(self.interrogation_timeout):
      response = self.dbg_pipe.recv()
      self.logger.debug('response: \'%s\'', binascii.hexlify(response))
    else:
      self.logger.debug('Timed out waiting for EC_ACK')

    # Verify the acknowledgment.
    is_enhanced = response == interpreter.EC_ACK

    if is_enhanced:
      # Increase the interrogation timeout for stability purposes.
      self.interrogation_timeout = ENHANCED_EC_INTERROGATION_TIMEOUT
      self.logger.debug('Increasing interrogation timeout to %rs.',
                        self.interrogation_timeout)
    else:
      # Reduce the timeout in order to reduce the perceivable delay.
      self.interrogation_timeout = NON_ENHANCED_EC_INTERROGATION_TIMEOUT
      self.logger.debug('Reducing interrogation timeout to %rs.',
                        self.interrogation_timeout)

    return is_enhanced

  def HandleChar(self, byte):
    """HandleChar does a certain action when it receives a character.

    Args:
      byte: An integer representing the character received from the user.

    Raises:
      EOFError: Allowed to propagate through from self.CheckForEnhancedECImage()
          i.e. from self.dbg_pipe.recv().
    """
    fd = self.master_pty

    # Enter the OOBM prompt mode if the user presses '%'.
    if byte == ord('%'):
      self.logger.debug('Begin OOBM command.')
      self.receiving_oobm_cmd = True
      # Print a "prompt".
      os.write(self.master_pty, b'\r\n% ')
      return

    # Add chars to the pending OOBM command if we're currently receiving one.
    if self.receiving_oobm_cmd and byte != ControlKey.CARRIAGE_RETURN:
      tmp_bytes = six.int2byte(byte)
      self.pending_oobm_cmd += tmp_bytes
      self.logger.debug('%s', tmp_bytes)
      os.write(self.master_pty, tmp_bytes)
      return

    if byte == ControlKey.CARRIAGE_RETURN:
      if self.receiving_oobm_cmd:
        # Terminate the command and place it in the OOBM queue.
        self.logger.debug('End OOBM command.')
        if self.pending_oobm_cmd:
          self.oobm_queue.put(self.pending_oobm_cmd)
          self.logger.debug('Placed \'%s\' into OOBM command queue.',
                            self.pending_oobm_cmd)

        # Reset the state.
        os.write(self.master_pty, b'\r\n' + self.prompt)
        self.input_buffer = b''
        self.input_buffer_pos = 0
        self.receiving_oobm_cmd = False
        self.pending_oobm_cmd = b''
        return

      if self.interrogation_mode == b'never':
        self.logger.debug('Skipping interrogation because interrogation mode'
                          ' is set to never.')
      elif self.interrogation_mode == b'always':
        # Only interrogate the EC if the interrogation mode is set to 'always'.
        self.enhanced_ec = self.CheckForEnhancedECImage()
        self.logger.debug('Enhanced EC image? %r', self.enhanced_ec)

    if not self.enhanced_ec:
      # Send everything straight to the EC to handle.
      self.cmd_pipe.send(six.int2byte(byte))
      # Reset the input buffer.
      self.input_buffer = b''
      self.input_buffer_pos = 0
      self.logger.log(1, 'Reset input buffer.')
      return

    # Keep handling the ESC sequence if we're in the middle of it.
    if self.esc_state != 0:
      self.HandleEsc(byte)
      return

    # When we're at the end of the line, we should only allow going backwards,
    # backspace, carriage return, up, or down.  The arrow keys are escape
    # sequences, so we let the escape...escape.
    if (self.input_buffer_pos >= self.line_limit and
        byte not in [ControlKey.CTRL_B, ControlKey.ESC, ControlKey.BACKSPACE,
                     ControlKey.CTRL_A, ControlKey.CARRIAGE_RETURN,
                     ControlKey.CTRL_P, ControlKey.CTRL_N]):
      return

    # If the input buffer is full we can't accept new chars.
    buffer_full = len(self.input_buffer) >= self.line_limit


    # Carriage_Return/Enter
    if byte == ControlKey.CARRIAGE_RETURN:
      self.logger.debug('Enter key pressed.')
      # Put a carriage return/newline and the print the prompt.
      os.write(fd, b'\r\n')

      # TODO(aaboagye): When we control the printing of all output, print the
      # prompt AFTER printing all the output.  We can't do it yet because we
      # don't know how much is coming from the EC.

      # Print the prompt.
      os.write(fd, self.prompt)
      # Process the input.
      self.ProcessInput()
      # Now, clear the buffer.
      self.input_buffer = b''
      self.input_buffer_pos = 0
      # Reset history buffer pos.
      self.history_pos = len(self.history)
      # Clear partial command.
      self.partial_cmd = b''

    # Backspace
    elif byte == ControlKey.BACKSPACE:
      self.logger.debug('Backspace pressed.')
      if self.input_buffer_pos > 0:
        # Move left 1 column.
        self.MoveCursor('left', 1)
        # Remove the character at the input_buffer_pos by slicing it out.
        self.SliceOutChar()

      self.logger.debug('input_buffer_pos: %d', self.input_buffer_pos)

    # Ctrl+A. Move cursor to beginning of the line
    elif byte == ControlKey.CTRL_A:
      self.logger.debug('Control+A pressed.')
      self.MoveCursor('left', self.input_buffer_pos)

    # Ctrl+B. Move cursor left 1 column.
    elif byte == ControlKey.CTRL_B:
      self.logger.debug('Control+B pressed.')
      self.MoveCursor('left', 1)

    # Ctrl+D. Delete a character.
    elif byte == ControlKey.CTRL_D:
      self.logger.debug('Control+D pressed.')
      if self.input_buffer_pos != len(self.input_buffer):
        # Remove the character by slicing it out.
        self.SliceOutChar()

    # Ctrl+E. Move cursor to end of the line.
    elif byte == ControlKey.CTRL_E:
      self.logger.debug('Control+E pressed.')
      self.MoveCursor('right',
                      len(self.input_buffer) - self.input_buffer_pos)

    # Ctrl+F. Move cursor right 1 column.
    elif byte == ControlKey.CTRL_F:
      self.logger.debug('Control+F pressed.')
      self.MoveCursor('right', 1)

    # Ctrl+K. Kill line.
    elif byte == ControlKey.CTRL_K:
      self.logger.debug('Control+K pressed.')
      self.KillLine()

    # Ctrl+N. Next line.
    elif byte == ControlKey.CTRL_N:
      self.logger.debug('Control+N pressed.')
      self.ShowNextCommand()

    # Ctrl+P. Previous line.
    elif byte == ControlKey.CTRL_P:
      self.logger.debug('Control+P pressed.')
      self.ShowPreviousCommand()

    # ESC sequence
    elif byte == ControlKey.ESC:
      # Starting an ESC sequence
      self.esc_state = EscState.ESC_START

    # Only print printable chars.
    elif IsPrintable(byte):
      # Drop the character if we're full.
      if buffer_full:
        self.logger.debug('Dropped char: %c(%d)', byte, byte)
        return
      # Print the character.
      os.write(fd, six.int2byte(byte))
      # Print the rest of the line (if any).
      extra_bytes_written = os.write(fd,
                                     self.input_buffer[self.input_buffer_pos:])

      # Recreate the input buffer.
      self.input_buffer = (self.input_buffer[0:self.input_buffer_pos] +
                           six.int2byte(byte) +
                           self.input_buffer[self.input_buffer_pos:])
      # Update the input buffer position.
      self.input_buffer_pos += 1 + extra_bytes_written

      # Reset the cursor if we wrote any extra bytes.
      if extra_bytes_written:
        self.MoveCursor('left', extra_bytes_written)

      self.logger.debug('input_buffer_pos: %d', self.input_buffer_pos)

  def MoveCursor(self, direction, count):
    """MoveCursor moves the cursor left or right by count columns.

    Args:
      direction: A string that should be either 'left' or 'right' representing
        the direction to move the cursor on the console.
      count: An integer representing how many columns the cursor should be
        moved.

    Raises:
      AssertionError: If the direction is not equal to 'left' or 'right'.
    """
    # If there's nothing to move, we're done.
    if not count:
      return
    fd = self.master_pty
    seq = b'\033[' + str(count).encode('ascii')
    if direction == 'left':
      # Bind the movement.
      if count > self.input_buffer_pos:
        count = self.input_buffer_pos
      seq += b'D'
      self.logger.debug('move cursor left %d', count)
      self.input_buffer_pos -= count

    elif direction == 'right':
      # Bind the movement.
      if (count + self.input_buffer_pos) > len(self.input_buffer):
        count = 0
      seq += b'C'
      self.logger.debug('move cursor right %d', count)
      self.input_buffer_pos += count

    else:
      raise AssertionError(('The only valid directions are \'left\' and '
                            '\'right\''))

    self.logger.debug('input_buffer_pos: %d', self.input_buffer_pos)
    # Move the cursor.
    if count != 0:
      os.write(fd, seq)

  def KillLine(self):
    """Kill the rest of the line based on the input buffer position."""
    # Killing the line is killing all the text to the right.
    diff = len(self.input_buffer) - self.input_buffer_pos
    self.logger.debug('diff: %d', diff)
    # Diff shouldn't be negative, but if it is for some reason, let's try to
    # correct the cursor.
    if diff < 0:
      self.logger.warning('Resetting input buffer position to %d...',
                          len(self.input_buffer))
      self.MoveCursor('left', -diff)
      return
    if diff:
      self.MoveCursor('right', diff)
      for _ in range(diff):
        self.SendBackspace()
      self.input_buffer_pos -= diff
      self.input_buffer = self.input_buffer[0:self.input_buffer_pos]

  def SendBackspace(self):
    """Backspace a character on the console."""
    os.write(self.master_pty, b'\033[1D \033[1D')

  def ProcessOOBMQueue(self):
    """Retrieve an item from the OOBM queue and process it."""
    item = self.oobm_queue.get()
    self.logger.debug('OOBM cmd: %s', item)
    cmd = item.split(b' ')

    if cmd[0] == b'loglevel':
      # An integer is required in order to set the log level.
      if len(cmd) < 2:
        self.logger.debug('Insufficient args')
        self.PrintOOBMHelp()
        return
      try:
        self.logger.debug('Log level change request.')
        new_log_level = int(cmd[1])
        self.logger.logger.setLevel(new_log_level)
        self.logger.info('Log level changed to %d.', new_log_level)

        # Forward the request to the interpreter as well.
        self.cmd_pipe.send(item)
      except ValueError:
        # Ignoring the request if an integer was not provided.
        self.PrintOOBMHelp()

    elif cmd[0] == b'timestamp':
      mode = cmd[1].lower()
      self.timestamp_enabled = mode == 'on'
      self.logger.info('%sabling uart timestamps.',
                       'En' if self.timestamp_enabled else 'Dis')

    elif cmd[0] == b'rawdebug':
      mode = cmd[1].lower()
      self.raw_debug = mode == 'on'
      self.logger.info('%sabling per interrupt debug logs.',
                       'En' if self.raw_debug else 'Dis')

    elif cmd[0] == b'interrogate' and len(cmd) >= 2:
      enhanced = False
      mode = cmd[1]
      if len(cmd) >= 3 and cmd[2] == b'enhanced':
        enhanced = True

      # Set the mode if correct.
      if mode in INTERROGATION_MODES:
        self.interrogation_mode = mode
        self.logger.debug('Updated interrogation mode to %s.', mode)

        # Update the assumptions of the EC image.
        self.enhanced_ec = enhanced
        self.logger.debug('Enhanced EC image is now %r', self.enhanced_ec)

        # Send command to interpreter as well.
        self.cmd_pipe.send(b'enhanced ' + str(self.enhanced_ec).encode('ascii'))
      else:
        self.PrintOOBMHelp()

    else:
      self.PrintOOBMHelp()

  def PrintOOBMHelp(self):
    """Prints out the OOBM help."""
    # Print help syntax.
    os.write(self.master_pty, b'\r\n' + b'Known OOBM commands:\r\n')
    os.write(self.master_pty, b'  interrogate <never | always | auto> '
             b'[enhanced]\r\n')
    os.write(self.master_pty, b'  loglevel <int>\r\n')

  def CheckBufferForEnhancedImage(self, data):
    """Adds data to a look buffer and checks to see for enhanced EC image.

    The EC's console task prints a string upon initialization which says that
    "Console is enabled; type HELP for help.".  The enhanced EC images print a
    different string as a part of their init.  This function searches through a
    "look" buffer, scanning for the presence of either of those strings and
    updating the enhanced_ec state accordingly.

    Args:
      data: A string containing the data sent from the interpreter.
    """
    self.look_buffer += data

    # Search the buffer for any of the EC image strings.
    enhanced_match = re.search(ENHANCED_IMAGE_RE, self.look_buffer)
    non_enhanced_match = re.search(NON_ENHANCED_IMAGE_RE, self.look_buffer)

    # Update the state if any matches were found.
    if enhanced_match or non_enhanced_match:
      if enhanced_match:
        self.enhanced_ec = True
      elif non_enhanced_match:
        self.enhanced_ec = False

      # Inform the interpreter of the result.
      self.cmd_pipe.send(b'enhanced ' + str(self.enhanced_ec).encode('ascii'))
      self.logger.debug('Enhanced EC image? %r', self.enhanced_ec)

      # Clear look buffer since a match was found.
      self.look_buffer = b''

    # Move the sliding window.
    self.look_buffer = self.look_buffer[-LOOK_BUFFER_SIZE:]


def CanonicalizeTimeString(timestr):
  """Canonicalize the timestamp string.

  Args:
    timestr: A timestamp string ended with 6 digits msec.

  Returns:
    A string with 3 digits msec and an extra space.
  """
  return timestr[:-3].encode('ascii') + b' '


def IsPrintable(byte):
  """Determines if a byte is printable.

  Args:
    byte: An integer potentially representing a printable character.

  Returns:
    A boolean indicating whether the byte is a printable character.
  """
  return byte >= ord(' ') and byte <= ord('~')


def StartLoop(console, command_active, shutdown_pipe=None):
  """Starts the infinite loop of console processing.

  Args:
    console: A Console object that has been properly initialzed.
    command_active: ctypes data object or multiprocessing.Value indicating if
      servod owns the console, or user owns the console. This prevents input
      collisions.
    shutdown_pipe: A file object for a pipe or equivalent that becomes readable
      (not blocked) to indicate that the loop should exit.  Can be None to never
      exit the loop.
  """
  try:
    console.logger.debug('Console is being served on %s.', console.user_pty)
    console.logger.debug('Console master is on %s.', console.master_pty)
    console.logger.debug('Command interface is being served on %s.',
        console.interface_pty)
    console.logger.debug(console)

    # This checks for HUP to indicate if the user has connected to the pty.
    ep = select.epoll()
    ep.register(console.master_pty, select.EPOLLHUP)

    # This is used instead of "break" to avoid exiting the loop in the middle of
    # an iteration.
    continue_looping = True

    # Used for determining when to print host timestamps
    tm_req = True

    while continue_looping:
      # Check to see if pts is connected to anything
      events = ep.poll(0)
      master_connected = not events

      # Check to see if pipes or the console are ready for reading.
      read_list = [console.interface_pty,
                   console.cmd_pipe, console.dbg_pipe]
      if master_connected:
        read_list.append(console.master_pty)
      if shutdown_pipe is not None:
        read_list.append(shutdown_pipe)

      # Check if any input is ready, or wait for .1 sec and re-poll if
      # a user has connected to the pts.
      select_output = select.select(read_list, [], [], .1)
      if not select_output:
        continue
      ready_for_reading = select_output[0]

      for obj in ready_for_reading:
        if obj is console.master_pty:
          if not command_active.value:
            # Convert to bytes so we can look for non-printable chars such as
            # Ctrl+A, Ctrl+E, etc.
            try:
              line = bytearray(os.read(console.master_pty, CONSOLE_MAX_READ))
              console.logger.debug('Input from user: %s, locked:%s',
                  str(line).strip(), command_active.value)
              for i in line:
                try:
                  # Handle each character as it arrives.
                  console.HandleChar(i)
                except EOFError:
                  console.logger.debug(
                      'ec3po console received EOF from dbg_pipe in HandleChar()'
                      ' while reading console.master_pty')
                  continue_looping = False
                  break
            except OSError:
              console.logger.debug('Ptm read failed, probably user disconnect.')

        elif obj is console.interface_pty:
          if command_active.value:
            # Convert to bytes so we can look for non-printable chars such as
            # Ctrl+A, Ctrl+E, etc.
            line = bytearray(os.read(console.interface_pty, CONSOLE_MAX_READ))
            console.logger.debug('Input from interface: %s, locked:%s',
                str(line).strip(), command_active.value)
            for i in line:
              try:
                # Handle each character as it arrives.
                console.HandleChar(i)
              except EOFError:
                console.logger.debug(
                    'ec3po console received EOF from dbg_pipe in HandleChar()'
                    ' while reading console.interface_pty')
                continue_looping = False
                break

        elif obj is console.cmd_pipe:
          try:
            data = console.cmd_pipe.recv()
          except EOFError:
            console.logger.debug('ec3po console received EOF from cmd_pipe')
            continue_looping = False
          else:
            # Write it to the user console.
            if console.raw_debug:
              console.logger.debug('|CMD|-%s->\'%s\'',
                                   ('u' if master_connected else '') +
                                   ('i' if command_active.value else ''),
                                   data.strip())
            if master_connected:
              os.write(console.master_pty, data)
            if command_active.value:
              os.write(console.interface_pty, data)

        elif obj is console.dbg_pipe:
          try:
            data = console.dbg_pipe.recv()
          except EOFError:
            console.logger.debug('ec3po console received EOF from dbg_pipe')
            continue_looping = False
          else:
            if console.interrogation_mode == b'auto':
              # Search look buffer for enhanced EC image string.
              console.CheckBufferForEnhancedImage(data)
            # Write it to the user console.
            if len(data) > 1 and console.raw_debug:
              console.logger.debug('|DBG|-%s->\'%s\'',
                                   ('u' if master_connected else '') +
                                   ('i' if command_active.value else ''),
                                   data.strip())
            console.LogConsoleOutput(data)
            if master_connected:
              end = len(data) - 1
              if console.timestamp_enabled:
                # A timestamp is required at the beginning of this line
                if tm_req is True:
                  now = datetime.now()
                  tm = CanonicalizeTimeString(now.strftime(HOST_STRFTIME))
                  os.write(console.master_pty, tm)
                  tm_req = False

                # Insert timestamps into the middle where appropriate
                # except if the last character is a newline
                nls_found = data.count(b'\n', 0, end)
                now = datetime.now()
                tm = CanonicalizeTimeString(now.strftime('\n' + HOST_STRFTIME))
                data_tm = data.replace(b'\n', tm, nls_found)
              else:
                data_tm = data

              # timestamp required on next input
              if data[end] == b'\n':
                tm_req = True
              os.write(console.master_pty, data_tm)
            if command_active.value:
              os.write(console.interface_pty, data)

        elif obj is shutdown_pipe:
          console.logger.debug(
              'ec3po console received shutdown pipe unblocked notification')
          continue_looping = False

      while not console.oobm_queue.empty():
        console.logger.debug('OOBM queue ready for reading.')
        console.ProcessOOBMQueue()

  except KeyboardInterrupt:
    pass

  finally:
    ep.unregister(console.master_pty)
    console.dbg_pipe.close()
    console.cmd_pipe.close()
    os.close(console.master_pty)
    os.close(console.interface_pty)
    if shutdown_pipe is not None:
      shutdown_pipe.close()
    console.logger.debug('Exit ec3po console loop for %s', console.user_pty)


def main(argv):
  """Kicks off the EC-3PO interactive console interface and interpreter.

  We create some pipes to communicate with an interpreter, instantiate an
  interpreter, create a PTY pair, and begin serving the console interface.

  Args:
    argv: A list of strings containing the arguments this module was called
      with.
  """
  # Set up argument parser.
  parser = argparse.ArgumentParser(description=('Start interactive EC console '
                                                'and interpreter.'))
  parser.add_argument('ec_uart_pty',
                      help=('The full PTY name that the EC UART'
                            ' is present on. eg: /dev/pts/12'))
  parser.add_argument('--log-level',
                      default='info',
                      help='info, debug, warning, error, or critical')

  # Parse arguments.
  opts = parser.parse_args(argv)

  # Set logging level.
  opts.log_level = opts.log_level.lower()
  if opts.log_level == 'info':
    log_level = logging.INFO
  elif opts.log_level == 'debug':
    log_level = logging.DEBUG
  elif opts.log_level == 'warning':
    log_level = logging.WARNING
  elif opts.log_level == 'error':
    log_level = logging.ERROR
  elif opts.log_level == 'critical':
    log_level = logging.CRITICAL
  else:
    parser.error('Invalid log level. (info, debug, warning, error, critical)')

  # Start logging with a timestamp, module, and log level shown in each log
  # entry.
  logging.basicConfig(level=log_level, format=('%(asctime)s - %(module)s -'
                                               ' %(levelname)s - %(message)s'))

  # Create some pipes to communicate between the interpreter and the console.
  # The command pipe is bidirectional.
  cmd_pipe_interactive, cmd_pipe_interp = threadproc_shim.Pipe()
  # The debug pipe is unidirectional from interpreter to console only.
  dbg_pipe_interactive, dbg_pipe_interp = threadproc_shim.Pipe(duplex=False)

  # Create an interpreter instance.
  itpr = interpreter.Interpreter(opts.ec_uart_pty, cmd_pipe_interp,
                                 dbg_pipe_interp, log_level)

  # Spawn an interpreter process.
  itpr_process = threadproc_shim.ThreadOrProcess(
      target=interpreter.StartLoop, args=(itpr,))
  # Make sure to kill the interpreter when we terminate.
  itpr_process.daemon = True
  # Start the interpreter.
  itpr_process.start()

  # Open a new pseudo-terminal pair
  (master_pty, user_pty) = pty.openpty()
  # Set the permissions to 660.
  os.chmod(os.ttyname(user_pty), (stat.S_IRGRP | stat.S_IWGRP |
                                  stat.S_IRUSR | stat.S_IWUSR))
  # Create a console.
  console = Console(master_pty, os.ttyname(user_pty), cmd_pipe_interactive,
                    dbg_pipe_interactive)
  # Start serving the console.
  v = threadproc_shim.Value(ctypes.c_bool, False)
  StartLoop(console, v)


if __name__ == '__main__':
  main(sys.argv[1:])
