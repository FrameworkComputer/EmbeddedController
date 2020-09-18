#!/usr/bin/env python
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for the EC-3PO Console interface."""

# Note: This is a py2/3 compatible file.

from __future__ import print_function

import binascii
# pylint: disable=cros-logging-import
import logging
import mock
import tempfile
import unittest

import six

import console
import interpreter
import threadproc_shim

ESC_STRING = six.int2byte(console.ControlKey.ESC)

class Keys(object):
  """A class that contains the escape sequences for special keys."""
  LEFT_ARROW = [console.ControlKey.ESC, ord('['), ord('D')]
  RIGHT_ARROW = [console.ControlKey.ESC, ord('['), ord('C')]
  UP_ARROW = [console.ControlKey.ESC, ord('['), ord('A')]
  DOWN_ARROW = [console.ControlKey.ESC, ord('['), ord('B')]
  HOME = [console.ControlKey.ESC, ord('['), ord('1'), ord('~')]
  END = [console.ControlKey.ESC, ord('['), ord('8'), ord('~')]
  DEL = [console.ControlKey.ESC, ord('['), ord('3'), ord('~')]

class OutputStream(object):
  """A class that has methods which return common console output."""

  @staticmethod
  def MoveCursorLeft(count):
    """Produces what would be printed to the console if the cursor moved left.

    Args:
      count: An integer representing how many columns to move left.

    Returns:
      string: A string which contains what would be printed to the console if
        the cursor moved left.
    """
    string = ESC_STRING
    string += b'[' + str(count).encode('ascii') + b'D'
    return string

  @staticmethod
  def MoveCursorRight(count):
    """Produces what would be printed to the console if the cursor moved right.

    Args:
      count: An integer representing how many columns to move right.

    Returns:
      string: A string which contains what would be printed to the console if
        the cursor moved right.
    """
    string = ESC_STRING
    string += b'[' + str(count).encode('ascii') + b'C'
    return string

BACKSPACE_STRING = b''
# Move cursor left 1 column.
BACKSPACE_STRING += OutputStream.MoveCursorLeft(1)
# Write a space.
BACKSPACE_STRING += b' '
# Move cursor left 1 column.
BACKSPACE_STRING += OutputStream.MoveCursorLeft(1)

def BytesToByteList(string):
  """Converts a bytes string to list of bytes.

  Args:
    string: A literal bytes to turn into a list of bytes.

  Returns:
    A list of integers representing the byte value of each character in the
      string.
  """
  if six.PY3:
    return [c for c in string]
  return [ord(c) for c in string]

def CheckConsoleOutput(test_case, exp_console_out):
  """Verify what was sent out the console matches what we expect.

  Args:
    test_case: A unittest.TestCase object representing the current unit test.
    exp_console_out: A string representing the console output stream.
  """
  # Read what was sent out the console.
  test_case.tempfile.seek(0)
  console_out = test_case.tempfile.read()

  test_case.assertEqual(exp_console_out, console_out)

def CheckInputBuffer(test_case, exp_input_buffer):
  """Verify that the input buffer contains what we expect.

  Args:
    test_case: A unittest.TestCase object representing the current unit test.
    exp_input_buffer: A string containing the contents of the current input
      buffer.
  """
  test_case.assertEqual(exp_input_buffer, test_case.console.input_buffer,
                        (b'input buffer does not match expected.\n'
                         b'expected: |' + exp_input_buffer + b'|\n'
                         b'got:      |' + test_case.console.input_buffer +
                         b'|\n' + str(test_case.console).encode('ascii')))

def CheckInputBufferPosition(test_case, exp_pos):
  """Verify the input buffer position.

  Args:
    test_case: A unittest.TestCase object representing the current unit test.
    exp_pos: An integer representing the expected input buffer position.
  """
  test_case.assertEqual(exp_pos, test_case.console.input_buffer_pos,
                        'input buffer position is incorrect.\ngot: ' +
                        str(test_case.console.input_buffer_pos) + '\nexp: ' +
                        str(exp_pos) + '\n' + str(test_case.console))

def CheckHistoryBuffer(test_case, exp_history):
  """Verify that the items in the history buffer are what we expect.

  Args:
    test_case: A unittest.TestCase object representing the current unit test.
    exp_history: A list of strings representing the expected contents of the
      history buffer.
  """
  # First, check to see if the length is what we expect.
  test_case.assertEqual(len(exp_history), len(test_case.console.history),
                        ('The number of items in the history is unexpected.\n'
                         'exp: ' + str(len(exp_history)) + '\n'
                         'got: ' + str(len(test_case.console.history)) + '\n'
                         'internal state:\n' + str(test_case.console)))

  # Next, check the actual contents of the history buffer.
  for i in range(len(exp_history)):
    test_case.assertEqual(exp_history[i], test_case.console.history[i],
                          (b'history buffer contents are incorrect.\n'
                           b'exp: ' + exp_history[i] + b'\n'
                           b'got: ' + test_case.console.history[i] + b'\n'
                           b'internal state:\n' +
                           str(test_case.console).encode('ascii')))


class TestConsoleEditingMethods(unittest.TestCase):
  """Test case to verify all console editing methods."""

  def setUp(self):
    """Setup the test harness."""
    # Setup logging with a timestamp, the module, and the log level.
    logging.basicConfig(level=logging.DEBUG,
                        format=('%(asctime)s - %(module)s -'
                                ' %(levelname)s - %(message)s'))

    # Create a temp file and set both the master and slave PTYs to the file to
    # create a loopback.
    self.tempfile = tempfile.TemporaryFile()

    # Create some mock pipes. These won't be used since we'll mock out sends
    # to the interpreter.
    mock_pipe_end_0, mock_pipe_end_1 = threadproc_shim.Pipe()
    self.console = console.Console(self.tempfile.fileno(), self.tempfile,
                                   tempfile.TemporaryFile(),
                                   mock_pipe_end_0, mock_pipe_end_1, "EC")

    # Console editing methods are only valid for enhanced EC images, therefore
    # we have to assume that the "EC" we're talking to is enhanced.  By default,
    # the console believes that the EC it's communicating with is NOT enhanced
    # which is why we have to override it here.
    self.console.enhanced_ec = True
    self.console.CheckForEnhancedECImage = mock.MagicMock(return_value=True)

  def test_EnteringChars(self):
    """Verify that characters are echoed onto the console."""
    test_str = b'abc'
    input_stream = BytesToByteList(test_str)

    # Send the characters in.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Check the input position.
    exp_pos = len(test_str)
    CheckInputBufferPosition(self, exp_pos)

    # Verify that the input buffer is correct.
    expected_buffer = test_str
    CheckInputBuffer(self, expected_buffer)

    # Check console output
    exp_console_out = test_str
    CheckConsoleOutput(self, exp_console_out)

  def test_EnteringDeletingMoreCharsThanEntered(self):
    """Verify that we can press backspace more than we have entered chars."""
    test_str = b'spamspam'
    input_stream = BytesToByteList(test_str)

    # Send the characters in.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Now backspace 1 more than what we sent.
    input_stream = []
    for _ in range(len(test_str) + 1):
      input_stream.append(console.ControlKey.BACKSPACE)

    # Send that sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, verify that input buffer position is 0.
    CheckInputBufferPosition(self, 0)

    # Next, examine the output stream for the correct sequence.
    exp_console_out = test_str
    for _ in range(len(test_str)):
      exp_console_out += BACKSPACE_STRING

    # Now, verify that we got what we expected.
    CheckConsoleOutput(self, exp_console_out)

  def test_EnteringMoreThanCharLimit(self):
    """Verify that we drop characters when the line is too long."""
    test_str = self.console.line_limit * b'o' # All allowed.
    test_str += 5 * b'x' # All should be dropped.
    input_stream = BytesToByteList(test_str)

    # Send the characters in.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, we expect that input buffer position should be equal to the line
    # limit.
    exp_pos = self.console.line_limit
    CheckInputBufferPosition(self, exp_pos)

    # The input buffer should only hold until the line limit.
    exp_buffer = test_str[0:self.console.line_limit]
    CheckInputBuffer(self, exp_buffer)

    # Lastly, check that the extra characters are not printed.
    exp_console_out = exp_buffer
    CheckConsoleOutput(self, exp_console_out)

  def test_ValidKeysOnLongLine(self):
    """Verify that we can still press valid keys if the line is too long."""
    # Fill the line.
    test_str = self.console.line_limit * b'o'
    exp_console_out = test_str
    # Try to fill it even more; these should all be dropped.
    test_str += 5 * b'x'
    input_stream = BytesToByteList(test_str)

    # We should be able to press the following keys:
    # - Backspace
    # - Arrow Keys/CTRL+B/CTRL+F/CTRL+P/CTRL+N
    # - Delete
    # - Home/CTRL+A
    # - End/CTRL+E
    # - Carriage Return

    # Backspace 1 character
    input_stream.append(console.ControlKey.BACKSPACE)
    exp_console_out += BACKSPACE_STRING
    # Refill the line.
    input_stream.extend(BytesToByteList(b'o'))
    exp_console_out += b'o'

    # Left arrow key.
    input_stream.extend(Keys.LEFT_ARROW)
    exp_console_out += OutputStream.MoveCursorLeft(1)

    # Right arrow key.
    input_stream.extend(Keys.RIGHT_ARROW)
    exp_console_out += OutputStream.MoveCursorRight(1)

    # CTRL+B
    input_stream.append(console.ControlKey.CTRL_B)
    exp_console_out += OutputStream.MoveCursorLeft(1)

    # CTRL+F
    input_stream.append(console.ControlKey.CTRL_F)
    exp_console_out += OutputStream.MoveCursorRight(1)

    # Let's press enter now so we can test up and down.
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    exp_console_out += b'\r\n' + self.console.prompt

    # Up arrow key.
    input_stream.extend(Keys.UP_ARROW)
    exp_console_out += test_str[:self.console.line_limit]

    # Down arrow key.
    input_stream.extend(Keys.DOWN_ARROW)
    # Since the line was blank, we have to backspace the entire line.
    exp_console_out += self.console.line_limit * BACKSPACE_STRING

    # CTRL+P
    input_stream.append(console.ControlKey.CTRL_P)
    exp_console_out += test_str[:self.console.line_limit]

    # CTRL+N
    input_stream.append(console.ControlKey.CTRL_N)
    # Since the line was blank, we have to backspace the entire line.
    exp_console_out += self.console.line_limit * BACKSPACE_STRING

    # Press the Up arrow key to reprint the long line.
    input_stream.extend(Keys.UP_ARROW)
    exp_console_out += test_str[:self.console.line_limit]

    # Press the Home key to jump to the beginning of the line.
    input_stream.extend(Keys.HOME)
    exp_console_out += OutputStream.MoveCursorLeft(self.console.line_limit)

    # Press the End key to jump to the end of the line.
    input_stream.extend(Keys.END)
    exp_console_out += OutputStream.MoveCursorRight(self.console.line_limit)

    # Press CTRL+A to jump to the beginning of the line.
    input_stream.append(console.ControlKey.CTRL_A)
    exp_console_out += OutputStream.MoveCursorLeft(self.console.line_limit)

    # Press CTRL+E to jump to the end of the line.
    input_stream.extend(Keys.END)
    exp_console_out += OutputStream.MoveCursorRight(self.console.line_limit)

    # Move left one column so we can delete a character.
    input_stream.extend(Keys.LEFT_ARROW)
    exp_console_out += OutputStream.MoveCursorLeft(1)

    # Press the delete key.
    input_stream.extend(Keys.DEL)
    # This should look like a space, and then move cursor left 1 column since
    # we're at the end of line.
    exp_console_out += b' ' + OutputStream.MoveCursorLeft(1)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify everything happened correctly.
    CheckConsoleOutput(self, exp_console_out)

  def test_BackspaceOnEmptyLine(self):
    """Verify that we can backspace on an empty line with no bad effects."""
    # Send a single backspace.
    test_str = [console.ControlKey.BACKSPACE]

    # Send the characters in.
    for byte in test_str:
      self.console.HandleChar(byte)

    # Check the input position.
    exp_pos = 0
    CheckInputBufferPosition(self, exp_pos)

    # Check that buffer is empty.
    exp_input_buffer = b''
    CheckInputBuffer(self, exp_input_buffer)

    # Check that the console output is empty.
    exp_console_out = b''
    CheckConsoleOutput(self, exp_console_out)

  def test_BackspaceWithinLine(self):
    """Verify that we shift the chars over when backspacing within a line."""
    # Misspell 'help'
    test_str = b'heelp'
    input_stream = BytesToByteList(test_str)
    # Use the arrow key to go back to fix it.
    # Move cursor left 1 column.
    input_stream.extend(2*Keys.LEFT_ARROW)
    # Backspace once to remove the extra 'e'.
    input_stream.append(console.ControlKey.BACKSPACE)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify the input buffer
    exp_input_buffer = b'help'
    CheckInputBuffer(self, exp_input_buffer)

    # Verify the input buffer position. It should be at 2 (cursor over the 'l')
    CheckInputBufferPosition(self, 2)

    # We expect the console output to be the test string, with two moves to the
    # left, another move left, and then the rest of the line followed by a
    # space.
    exp_console_out = test_str
    exp_console_out += 2 * OutputStream.MoveCursorLeft(1)

    # Move cursor left 1 column.
    exp_console_out += OutputStream.MoveCursorLeft(1)
    # Rest of the line and a space. (test_str in this case)
    exp_console_out += b'lp '
    # Reset the cursor 2 + 1 to the left.
    exp_console_out += OutputStream.MoveCursorLeft(3)

    # Verify console output.
    CheckConsoleOutput(self, exp_console_out)

  def test_JumpToBeginningOfLineViaCtrlA(self):
    """Verify that we can jump to the beginning of a line with Ctrl+A."""
    # Enter some chars and press CTRL+A
    test_str = b'abc'
    input_stream = BytesToByteList(test_str) + [console.ControlKey.CTRL_A]

    # Send the characters in.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # We expect to see our test string followed by a move cursor left.
    exp_console_out = test_str
    exp_console_out += OutputStream.MoveCursorLeft(len(test_str))

    # Check to see what whas printed on the console.
    CheckConsoleOutput(self, exp_console_out)

    # Check that the input buffer position is now 0.
    CheckInputBufferPosition(self, 0)

    # Check input buffer still contains our test string.
    CheckInputBuffer(self, test_str)

  def test_JumpToBeginningOfLineViaHomeKey(self):
    """Jump to beginning of line via HOME key."""
    test_str = b'version'
    input_stream = BytesToByteList(test_str)
    input_stream.extend(Keys.HOME)

    # Send out the stream.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, verify that input buffer position is now 0.
    CheckInputBufferPosition(self, 0)

    # Next, verify that the input buffer did not change.
    CheckInputBuffer(self, test_str)

    # Lastly, check that the cursor moved correctly.
    exp_console_out = test_str
    exp_console_out += OutputStream.MoveCursorLeft(len(test_str))
    CheckConsoleOutput(self, exp_console_out)

  def test_JumpToEndOfLineViaEndKey(self):
    """Jump to the end of the line using the END key."""
    test_str = b'version'
    input_stream = BytesToByteList(test_str)
    input_stream += [console.ControlKey.CTRL_A]
    # Now, jump to the end of the line.
    input_stream.extend(Keys.END)

    # Send out the stream.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is correct.  This should be at the
    # end of the test string.
    CheckInputBufferPosition(self, len(test_str))

    # The expected output should be the test string, followed by a jump to the
    # beginning of the line, and lastly a jump to the end of the line.
    exp_console_out = test_str
    exp_console_out += OutputStream.MoveCursorLeft(len(test_str))
    # Now the jump back to the end of the line.
    exp_console_out += OutputStream.MoveCursorRight(len(test_str))

    # Verify console output stream.
    CheckConsoleOutput(self, exp_console_out)

  def test_JumpToEndOfLineViaCtrlE(self):
    """Enter some chars and then try to jump to the end. (Should be a no-op)"""
    test_str = b'sysinfo'
    input_stream = BytesToByteList(test_str)
    input_stream.append(console.ControlKey.CTRL_E)

    # Send out the stream
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position isn't any further than we expect.
    # At this point, the position should be at the end of the test string.
    CheckInputBufferPosition(self, len(test_str))

    # Now, let's try to jump to the beginning and then jump back to the end.
    input_stream = [console.ControlKey.CTRL_A, console.ControlKey.CTRL_E]

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Perform the same verification.
    CheckInputBufferPosition(self, len(test_str))

    # Lastly try to jump again, beyond the end.
    input_stream = [console.ControlKey.CTRL_E]

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Perform the same verification.
    CheckInputBufferPosition(self, len(test_str))

    # We expect to see the test string, a jump to the beginning of the line, and
    # one jump to the end of the line.
    exp_console_out = test_str
    # Jump to beginning.
    exp_console_out += OutputStream.MoveCursorLeft(len(test_str))
    # Jump back to end.
    exp_console_out += OutputStream.MoveCursorRight(len(test_str))

    # Verify the console output.
    CheckConsoleOutput(self, exp_console_out)

  def test_MoveLeftWithArrowKey(self):
    """Move cursor left one column with arrow key."""
    test_str = b'tastyspam'
    input_stream = BytesToByteList(test_str)
    input_stream.extend(Keys.LEFT_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is 1 less than the length.
    CheckInputBufferPosition(self, len(test_str) - 1)

    # Also, verify that the input buffer is not modified.
    CheckInputBuffer(self, test_str)

    # We expect the test string, followed by a one column move left.
    exp_console_out = test_str + OutputStream.MoveCursorLeft(1)

    # Verify console output.
    CheckConsoleOutput(self, exp_console_out)

  def test_MoveLeftWithCtrlB(self):
    """Move cursor back one column with Ctrl+B."""
    test_str = b'tastyspam'
    input_stream = BytesToByteList(test_str)
    input_stream.append(console.ControlKey.CTRL_B)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is 1 less than the length.
    CheckInputBufferPosition(self, len(test_str) - 1)

    # Also, verify that the input buffer is not modified.
    CheckInputBuffer(self, test_str)

    # We expect the test string, followed by a one column move left.
    exp_console_out = test_str + OutputStream.MoveCursorLeft(1)

    # Verify console output.
    CheckConsoleOutput(self, exp_console_out)

  def test_MoveRightWithArrowKey(self):
    """Move cursor one column to the right with the arrow key."""
    test_str = b'version'
    input_stream = BytesToByteList(test_str)
    # Jump to beginning of line.
    input_stream.append(console.ControlKey.CTRL_A)
    # Press right arrow key.
    input_stream.extend(Keys.RIGHT_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is 1.
    CheckInputBufferPosition(self, 1)

    # Also, verify that the input buffer is not modified.
    CheckInputBuffer(self, test_str)

    # We expect the test string, followed by a jump to the beginning of the
    # line, and finally a move right 1.
    exp_console_out = test_str + OutputStream.MoveCursorLeft(len((test_str)))

    # A move right 1 column.
    exp_console_out += OutputStream.MoveCursorRight(1)

    # Verify console output.
    CheckConsoleOutput(self, exp_console_out)

  def test_MoveRightWithCtrlF(self):
    """Move cursor forward one column with Ctrl+F."""
    test_str = b'panicinfo'
    input_stream = BytesToByteList(test_str)
    input_stream.append(console.ControlKey.CTRL_A)
    # Now, move right one column.
    input_stream.append(console.ControlKey.CTRL_F)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is 1.
    CheckInputBufferPosition(self, 1)

    # Also, verify that the input buffer is not modified.
    CheckInputBuffer(self, test_str)

    # We expect the test string, followed by a jump to the beginning of the
    # line, and finally a move right 1.
    exp_console_out = test_str + OutputStream.MoveCursorLeft(len((test_str)))

    # A move right 1 column.
    exp_console_out += OutputStream.MoveCursorRight(1)

    # Verify console output.
    CheckConsoleOutput(self, exp_console_out)

  def test_ImpossibleMoveLeftWithArrowKey(self):
    """Verify that we can't move left at the beginning of the line."""
    # We shouldn't be able to move left if we're at the beginning of the line.
    input_stream = Keys.LEFT_ARROW

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Nothing should have been output.
    exp_console_output = b''
    CheckConsoleOutput(self, exp_console_output)

    # The input buffer position should still be 0.
    CheckInputBufferPosition(self, 0)

    # The input buffer itself should be empty.
    CheckInputBuffer(self, b'')

  def test_ImpossibleMoveRightWithArrowKey(self):
    """Verify that we can't move right at the end of the line."""
    # We shouldn't be able to move right if we're at the end of the line.
    input_stream = Keys.RIGHT_ARROW

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Nothing should have been output.
    exp_console_output = b''
    CheckConsoleOutput(self, exp_console_output)

    # The input buffer position should still be 0.
    CheckInputBufferPosition(self, 0)

    # The input buffer itself should be empty.
    CheckInputBuffer(self, b'')

  def test_KillEntireLine(self):
    """Verify that we can kill an entire line with Ctrl+K."""
    test_str = b'accelinfo on'
    input_stream = BytesToByteList(test_str)
    # Jump to beginning of line and then kill it with Ctrl+K.
    input_stream.extend([console.ControlKey.CTRL_A, console.ControlKey.CTRL_K])

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, we expect that the input buffer is empty.
    CheckInputBuffer(self, b'')

    # The buffer position should be 0.
    CheckInputBufferPosition(self, 0)

    # What we expect to see on the console stream should be the following.  The
    # test string, a jump to the beginning of the line, then jump back to the
    # end of the line and replace the line with spaces.
    exp_console_out = test_str
    # Jump to beginning of line.
    exp_console_out += OutputStream.MoveCursorLeft(len(test_str))
    # Jump to end of line.
    exp_console_out += OutputStream.MoveCursorRight(len(test_str))
    # Replace line with spaces, which looks like backspaces.
    for _ in range(len(test_str)):
      exp_console_out += BACKSPACE_STRING

    # Verify the console output.
    CheckConsoleOutput(self, exp_console_out)

  def test_KillPartialLine(self):
    """Verify that we can kill a portion of a line."""
    test_str = b'accelread 0 1'
    input_stream = BytesToByteList(test_str)
    len_to_kill = 5
    for _ in range(len_to_kill):
      # Move cursor left
      input_stream.extend(Keys.LEFT_ARROW)
    # Now kill
    input_stream.append(console.ControlKey.CTRL_K)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, check that the input buffer was truncated.
    exp_input_buffer = test_str[:-len_to_kill]
    CheckInputBuffer(self, exp_input_buffer)

    # Verify the input buffer position.
    CheckInputBufferPosition(self, len(test_str) - len_to_kill)

    # The console output stream that we expect is the test string followed by a
    # move left of len_to_kill, then a jump to the end of the line and backspace
    # of len_to_kill.
    exp_console_out = test_str
    for _ in range(len_to_kill):
      # Move left 1 column.
      exp_console_out += OutputStream.MoveCursorLeft(1)
    # Then jump to the end of the line
    exp_console_out += OutputStream.MoveCursorRight(len_to_kill)
    # Backspace of len_to_kill
    for _ in range(len_to_kill):
      exp_console_out += BACKSPACE_STRING

    # Verify console output.
    CheckConsoleOutput(self, exp_console_out)

  def test_InsertingCharacters(self):
    """Verify that we can insert characters within the line."""
    test_str = b'accel 0 1' # Here we forgot the 'read' part in 'accelread'
    input_stream = BytesToByteList(test_str)
    # We need to move over to the 'l' and add read.
    insertion_point = test_str.find(b'l') + 1
    for i in range(len(test_str) - insertion_point):
      # Move cursor left.
      input_stream.extend(Keys.LEFT_ARROW)
    # Now, add in 'read'
    added_str = b'read'
    input_stream.extend(BytesToByteList(added_str))

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, verify that the input buffer is correct.
    exp_input_buffer = test_str[:insertion_point] + added_str
    exp_input_buffer += test_str[insertion_point:]
    CheckInputBuffer(self, exp_input_buffer)

    # Verify that the input buffer position is correct.
    exp_input_buffer_pos = insertion_point + len(added_str)
    CheckInputBufferPosition(self, exp_input_buffer_pos)

    # The console output stream that we expect is the test string, followed by
    # move cursor left until the 'l' was found, the added test string while
    # shifting characters around.
    exp_console_out = test_str
    for i in range(len(test_str) - insertion_point):
      # Move cursor left.
      exp_console_out += OutputStream.MoveCursorLeft(1)

    # Now for each character, write the rest of the line will be shifted to the
    # right one column.
    for i in range(len(added_str)):
      # Printed character.
      exp_console_out += added_str[i:i+1]
      # The rest of the line
      exp_console_out += test_str[insertion_point:]
      # Reset the cursor back left
      reset_dist = len(test_str[insertion_point:])
      exp_console_out += OutputStream.MoveCursorLeft(reset_dist)

    # Verify the console output.
    CheckConsoleOutput(self, exp_console_out)

  def test_StoreCommandHistory(self):
    """Verify that entered commands are stored in the history."""
    test_commands = []
    test_commands.append(b'help')
    test_commands.append(b'version')
    test_commands.append(b'accelread 0 1')
    input_stream = []
    for c in test_commands:
      input_stream.extend(BytesToByteList(c))
      input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # We expect to have the test commands in the history buffer.
    exp_history_buf = test_commands
    CheckHistoryBuffer(self, exp_history_buf)

  def test_CycleUpThruCommandHistory(self):
    """Verify that the UP arrow key will print itmes in the history buffer."""
    # Enter some commands.
    test_commands = [b'version', b'accelrange 0', b'battery', b'gettime']
    input_stream = []
    for command in test_commands:
      input_stream.extend(BytesToByteList(command))
      input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Now, hit the UP arrow key to print the previous entries.
    for i in range(len(test_commands)):
      input_stream.extend(Keys.UP_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # The expected output should be test commands with prompts printed in
    # between, followed by line kills with the previous test commands printed.
    exp_console_out = b''
    for i in range(len(test_commands)):
      exp_console_out += test_commands[i] + b'\r\n' + self.console.prompt

    # When we press up, the line should be cleared and print the previous buffer
    # entry.
    for i in range(len(test_commands)-1, 0, -1):
      exp_console_out += test_commands[i]
      # Backspace to the beginning.
      for i in range(len(test_commands[i])):
        exp_console_out += BACKSPACE_STRING

    # The last command should just be printed out with no backspacing.
    exp_console_out += test_commands[0]

    # Now, verify.
    CheckConsoleOutput(self, exp_console_out)

  def test_UpArrowOnEmptyHistory(self):
    """Ensure nothing happens if the history is empty."""
    # Press the up arrow key twice.
    input_stream = 2 * Keys.UP_ARROW

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # We expect nothing to have happened.
    exp_console_out = b''
    exp_input_buffer = b''
    exp_input_buffer_pos = 0
    exp_history_buf = []

    # Verify.
    CheckConsoleOutput(self, exp_console_out)
    CheckInputBufferPosition(self, exp_input_buffer_pos)
    CheckInputBuffer(self, exp_input_buffer)
    CheckHistoryBuffer(self, exp_history_buf)

  def test_UpArrowDoesNotGoOutOfBounds(self):
    """Verify that pressing the up arrow many times won't go out of bounds."""
    # Enter one command.
    test_str = b'help version'
    input_stream = BytesToByteList(test_str)
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    # Then press the up arrow key twice.
    input_stream.extend(2 * Keys.UP_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the history buffer is correct.
    exp_history_buf = [test_str]
    CheckHistoryBuffer(self, exp_history_buf)

    # We expect that the console output should only contain our entered command,
    # a new prompt, and then our command aggain.
    exp_console_out = test_str + b'\r\n' + self.console.prompt
    # Pressing up should reprint the command we entered.
    exp_console_out += test_str

    # Verify.
    CheckConsoleOutput(self, exp_console_out)

  def test_CycleDownThruCommandHistory(self):
    """Verify that we can select entries by hitting the down arrow."""
    # Enter at least 4 commands.
    test_commands = [b'version', b'accelrange 0', b'battery', b'gettime']
    input_stream = []
    for command in test_commands:
      input_stream.extend(BytesToByteList(command))
      input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Now, hit the UP arrow key twice to print the previous two entries.
    for i in range(2):
      input_stream.extend(Keys.UP_ARROW)

    # Now, hit the DOWN arrow key twice to print the newer entries.
    input_stream.extend(2*Keys.DOWN_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # The expected output should be commands that we entered, followed by
    # prompts, then followed by our last two commands in reverse.  Then, we
    # should see the last entry in the list, followed by the saved partial cmd
    # of a blank line.
    exp_console_out = b''
    for i in range(len(test_commands)):
      exp_console_out += test_commands[i] + b'\r\n' + self.console.prompt

    # When we press up, the line should be cleared and print the previous buffer
    # entry.
    for i in range(len(test_commands)-1, 1, -1):
      exp_console_out += test_commands[i]
      # Backspace to the beginning.
      for i in range(len(test_commands[i])):
        exp_console_out += BACKSPACE_STRING

    # When we press down, it should have cleared the last command (which we
    # covered with the previous for loop), and then prints the next command.
    exp_console_out += test_commands[3]
    for i in range(len(test_commands[3])):
      exp_console_out += BACKSPACE_STRING

    # Verify console output.
    CheckConsoleOutput(self, exp_console_out)

    # Verify input buffer.
    exp_input_buffer = b'' # Empty because our partial command was empty.
    exp_input_buffer_pos = len(exp_input_buffer)
    CheckInputBuffer(self, exp_input_buffer)
    CheckInputBufferPosition(self, exp_input_buffer_pos)

  def test_SavingPartialCommandWhenNavigatingHistory(self):
    """Verify that partial commands are saved when navigating history."""
    # Enter a command.
    test_str = b'accelinfo'
    input_stream = BytesToByteList(test_str)
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Enter a partial command.
    partial_cmd = b'ver'
    input_stream.extend(BytesToByteList(partial_cmd))

    # Hit the UP arrow key.
    input_stream.extend(Keys.UP_ARROW)
    # Then, the DOWN arrow key.
    input_stream.extend(Keys.DOWN_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # The expected output should be the command we entered, a prompt, the
    # partial command, clearing of the partial command, the command entered,
    # clearing of the command entered, and then the partial command.
    exp_console_out = test_str + b'\r\n' + self.console.prompt
    exp_console_out += partial_cmd
    for _ in range(len(partial_cmd)):
      exp_console_out += BACKSPACE_STRING
    exp_console_out += test_str
    for _ in range(len(test_str)):
      exp_console_out += BACKSPACE_STRING
    exp_console_out += partial_cmd

    # Verify console output.
    CheckConsoleOutput(self, exp_console_out)

    # Verify input buffer.
    exp_input_buffer = partial_cmd
    exp_input_buffer_pos = len(exp_input_buffer)
    CheckInputBuffer(self, exp_input_buffer)
    CheckInputBufferPosition(self, exp_input_buffer_pos)

  def test_DownArrowOnEmptyHistory(self):
    """Ensure nothing happens if the history is empty."""
    # Then press the up down arrow twice.
    input_stream = 2 * Keys.DOWN_ARROW

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # We expect nothing to have happened.
    exp_console_out = b''
    exp_input_buffer = b''
    exp_input_buffer_pos = 0
    exp_history_buf = []

    # Verify.
    CheckConsoleOutput(self, exp_console_out)
    CheckInputBufferPosition(self, exp_input_buffer_pos)
    CheckInputBuffer(self, exp_input_buffer)
    CheckHistoryBuffer(self, exp_history_buf)

  def test_DeleteCharsUsingDELKey(self):
    """Verify that we can delete characters using the DEL key."""
    test_str = b'version'
    input_stream = BytesToByteList(test_str)

    # Hit the left arrow key 2 times.
    input_stream.extend(2 * Keys.LEFT_ARROW)

    # Press the DEL key.
    input_stream.extend(Keys.DEL)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # The expected output should be the command we entered, 2 individual cursor
    # moves to the left, and then removing a char and shifting everything to the
    # left one column.
    exp_console_out = test_str
    exp_console_out += 2 * OutputStream.MoveCursorLeft(1)

    # Remove the char by shifting everything to the left one, slicing out the
    # remove char.
    exp_console_out += test_str[-1:] + b' '

    # Reset the cursor by moving back 2 columns because of the 'n' and space.
    exp_console_out += OutputStream.MoveCursorLeft(2)

    # Verify console output.
    CheckConsoleOutput(self, exp_console_out)

    # Verify input buffer.  The input buffer should have the char sliced out and
    # be positioned where the char was removed.
    exp_input_buffer = test_str[:-2] + test_str[-1:]
    exp_input_buffer_pos = len(exp_input_buffer) - 1
    CheckInputBuffer(self, exp_input_buffer)
    CheckInputBufferPosition(self, exp_input_buffer_pos)

  def test_RepeatedCommandInHistory(self):
    """Verify that we don't store 2 consecutive identical commands in history"""
    # Enter a few commands.
    test_commands = [b'version', b'accelrange 0', b'battery', b'gettime']
    # Repeat the last command.
    test_commands.append(test_commands[len(test_commands)-1])

    input_stream = []
    for command in test_commands:
      input_stream.extend(BytesToByteList(command))
      input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the history buffer is correct.  The last command, since
    # it was repeated, should not have been added to the history.
    exp_history_buf = test_commands[0:len(test_commands)-1]
    CheckHistoryBuffer(self, exp_history_buf)


class TestConsoleCompatibility(unittest.TestCase):
  """Verify that console can speak to enhanced and non-enhanced EC images."""
  def setUp(self):
    """Setup the test harness."""
    # Setup logging with a timestamp, the module, and the log level.
    logging.basicConfig(level=logging.DEBUG,
                        format=('%(asctime)s - %(module)s -'
                                ' %(levelname)s - %(message)s'))
    # Create a temp file and set both the master and slave PTYs to the file to
    # create a loopback.
    self.tempfile = tempfile.TemporaryFile()

    # Mock out the pipes.
    mock_pipe_end_0, mock_pipe_end_1 = mock.MagicMock(), mock.MagicMock()
    self.console = console.Console(self.tempfile.fileno(), self.tempfile,
                                   tempfile.TemporaryFile(),
                                   mock_pipe_end_0, mock_pipe_end_1, "EC")

  @mock.patch('console.Console.CheckForEnhancedECImage')
  def test_ActAsPassThruInNonEnhancedMode(self, mock_check):
    """Verify we simply pass everything thru to non-enhanced ECs.

    Args:
      mock_check: A MagicMock object replacing the CheckForEnhancedECImage()
        method.
    """
    # Set the interrogation mode to always so that we actually interrogate.
    self.console.interrogation_mode = b'always'

    # Assume EC interrogations indicate that the image is non-enhanced.
    mock_check.return_value = False

    # Press enter, followed by the command, and another enter.
    input_stream = []
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    test_command = b'version'
    input_stream.extend(BytesToByteList(test_command))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Expected calls to send down the pipe would be each character of the test
    # command.
    expected_calls = []
    expected_calls.append(mock.call(
        six.int2byte(console.ControlKey.CARRIAGE_RETURN)))
    for char in test_command:
      if six.PY3:
        expected_calls.append(mock.call(bytes([char])))
      else:
        expected_calls.append(mock.call(char))
    expected_calls.append(mock.call(
        six.int2byte(console.ControlKey.CARRIAGE_RETURN)))

    # Verify that the calls happened.
    self.console.cmd_pipe.send.assert_has_calls(expected_calls)

    # Since we're acting as a pass-thru, the input buffer should be empty and
    # input_buffer_pos is 0.
    CheckInputBuffer(self, b'')
    CheckInputBufferPosition(self, 0)

  @mock.patch('console.Console.CheckForEnhancedECImage')
  def test_TransitionFromNonEnhancedToEnhanced(self, mock_check):
    """Verify that we transition correctly to enhanced mode.

    Args:
      mock_check: A MagicMock object replacing the CheckForEnhancedECImage()
        method.
    """
    # Set the interrogation mode to always so that we actually interrogate.
    self.console.interrogation_mode = b'always'

    # First, assume that the EC interrogations indicate an enhanced EC image.
    mock_check.return_value = True
    # But our current knowledge of the EC image (which was actually the
    # 'previous' EC) was a non-enhanced image.
    self.console.enhanced_ec = False

    test_command = b'sysinfo'
    input_stream = []
    input_stream.extend(BytesToByteList(test_command))

    expected_calls = []
    # All keystrokes to the console should be directed straight through to the
    # EC until we press the enter key.
    for char in test_command:
      if six.PY3:
        expected_calls.append(mock.call(bytes([char])))
      else:
        expected_calls.append(mock.call(char))

    # Press the enter key.
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    # The enter key should not be sent to the pipe since we should negotiate
    # to an enhanced EC image.

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # At this point, we should have negotiated to enhanced.
    self.assertTrue(self.console.enhanced_ec, msg=('Did not negotiate to '
                                                   'enhanced EC image.'))

    # The command would have been dropped however, so verify this...
    CheckInputBuffer(self, b'')
    CheckInputBufferPosition(self, 0)
    # ...and repeat the command.
    input_stream = BytesToByteList(test_command)
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Since we're enhanced now, we should have sent the entire command as one
    # string with no trailing carriage return
    expected_calls.append(mock.call(test_command))

    # Verify all of the calls.
    self.console.cmd_pipe.send.assert_has_calls(expected_calls)

  @mock.patch('console.Console.CheckForEnhancedECImage')
  def test_TransitionFromEnhancedToNonEnhanced(self, mock_check):
    """Verify that we transition correctly to non-enhanced mode.

    Args:
      mock_check: A MagicMock object replacing the CheckForEnhancedECImage()
        method.
    """
    # Set the interrogation mode to always so that we actually interrogate.
    self.console.interrogation_mode = b'always'

    # First, assume that the EC interrogations indicate an non-enhanced EC
    # image.
    mock_check.return_value = False
    # But our current knowledge of the EC image (which was actually the
    # 'previous' EC) was an enhanced image.
    self.console.enhanced_ec = True

    test_command = b'sysinfo'
    input_stream = []
    input_stream.extend(BytesToByteList(test_command))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # But, we will negotiate to non-enhanced however, dropping this command.
    # Verify this.
    self.assertFalse(self.console.enhanced_ec, msg=('Did not negotiate to'
                                                    'non-enhanced EC image.'))
    CheckInputBuffer(self, b'')
    CheckInputBufferPosition(self, 0)

    # The carriage return should have passed through though.
    expected_calls = []
    expected_calls.append(mock.call(
        six.int2byte(console.ControlKey.CARRIAGE_RETURN)))

    # Since the command was dropped, repeat the command.
    input_stream = BytesToByteList(test_command)
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Since we're not enhanced now, we should have sent each character in the
    # entire command separately and a carriage return.
    for char in test_command:
      if six.PY3:
        expected_calls.append(mock.call(bytes([char])))
      else:
        expected_calls.append(mock.call(char))
    expected_calls.append(mock.call(
        six.int2byte(console.ControlKey.CARRIAGE_RETURN)))

    # Verify all of the calls.
    self.console.cmd_pipe.send.assert_has_calls(expected_calls)

  def test_EnhancedCheckIfTimedOut(self):
    """Verify that the check returns false if it times out."""
    # Make the debug pipe "time out".
    self.console.dbg_pipe.poll.return_value = False
    self.assertFalse(self.console.CheckForEnhancedECImage())

  def test_EnhancedCheckIfACKReceived(self):
    """Verify that the check returns true if the ACK is received."""
    # Make the debug pipe return EC_ACK.
    self.console.dbg_pipe.poll.return_value = True
    self.console.dbg_pipe.recv.return_value = interpreter.EC_ACK
    self.assertTrue(self.console.CheckForEnhancedECImage())

  def test_EnhancedCheckIfWrong(self):
    """Verify that the check returns false if byte received is wrong."""
    # Make the debug pipe return the wrong byte.
    self.console.dbg_pipe.poll.return_value = True
    self.console.dbg_pipe.recv.return_value = b'\xff'
    self.assertFalse(self.console.CheckForEnhancedECImage())

  def test_EnhancedCheckUsingBuffer(self):
    """Verify that given reboot output, enhanced EC images are detected."""
    enhanced_output_stream = b"""
--- UART initialized after reboot ---
[Reset cause: reset-pin soft]
[Image: RO, jerry_v1.1.4363-2af8572-dirty 2016-02-23 13:26:20 aaboagye@lithium.mtv.corp.google.com]
[0.001695 KB boot key 0]
[0.001790 Inits done]
[0.001923 not sysjump; forcing AP shutdown]
[0.002047 EC triggered warm reboot]
[0.002155 assert GPIO_PMIC_WARM_RESET_L for 4 ms]
[0.006326 auto_power_on set due to reset_flag 0x22]
[0.006477 Wait for battery stabilized during 1000000]
[0.007368 battery responded with status c0]
[0.009099 hash start 0x00010000 0x0000eb7c]
[0.009307 KB init state: -- -- -- -- -- -- -- -- -- -- -- -- --]
[0.009531 KB wait]
Enhanced Console is enabled (v1.0.0); type HELP for help.
> [0.009782 event set 0x00002000]
[0.009903 hostcmd init 0x2000]
[0.010031 power state 0 = G3, in 0x0000]
[0.010173 power state 4 = G3->S5, in 0x0000]
[0.010324 power state 1 = S5, in 0x0000]
[0.010466 power on 2]
[0.010566 power state 5 = S5->S3, in 0x0000]
[0.037713 event set 0x00000080]
[0.037836 event set 0x00400000]
[0.038675 Battery 89% / 1092h:15 to empty]
[0.224060 hash done 41dac382e3a6e3d2ea5b4d789c1bc46525cae7cc5ff6758f0de8d8369b506f57]
[0.375150 POWER_GOOD seen]
"""
    for line in enhanced_output_stream.split(b'\n'):
      self.console.CheckBufferForEnhancedImage(line)

    # Since the enhanced console string was present in the output, the console
    # should have caught it.
    self.assertTrue(self.console.enhanced_ec)

    # Also should check that the command was sent to the interpreter.
    self.console.cmd_pipe.send.assert_called_once_with(b'enhanced True')

    # Now test the non-enhanced EC image.
    self.console.cmd_pipe.reset_mock()
    non_enhanced_output_stream = b"""
--- UART initialized after reboot ---
[Reset cause: reset-pin soft]
[Image: RO, jerry_v1.1.4363-2af8572-dirty 2016-02-23 13:03:15 aaboagye@lithium.mtv.corp.google.com]
[0.001695 KB boot key 0]
[0.001790 Inits done]
[0.001923 not sysjump; forcing AP shutdown]
[0.002047 EC triggered warm reboot]
[0.002156 assert GPIO_PMIC_WARM_RESET_L for 4 ms]
[0.006326 auto_power_on set due to reset_flag 0x22]
[0.006477 Wait for battery stabilized during 1000000]
[0.007368 battery responded with status c0]
[0.008951 hash start 0x00010000 0x0000ed78]
[0.009159 KB init state: -- -- -- -- -- -- -- -- -- -- -- -- --]
[0.009383 KB wait]
Console is enabled; type HELP for help.
> [0.009602 event set 0x00002000]
[0.009722 hostcmd init 0x2000]
[0.009851 power state 0 = G3, in 0x0000]
[0.009993 power state 4 = G3->S5, in 0x0000]
[0.010144 power state 1 = S5, in 0x0000]
[0.010285 power on 2]
[0.010385 power state 5 = S5->S3, in 0x0000]
"""
    for line in non_enhanced_output_stream.split(b'\n'):
      self.console.CheckBufferForEnhancedImage(line)

    # Since the default console string is present in the output, it should be
    # determined to be non enhanced now.
    self.assertFalse(self.console.enhanced_ec)

    # Check that command was also sent to the interpreter.
    self.console.cmd_pipe.send.assert_called_once_with(b'enhanced False')


class TestOOBMConsoleCommands(unittest.TestCase):
  """Verify that OOBM console commands work correctly."""
  def setUp(self):
    """Setup the test harness."""
    # Setup logging with a timestamp, the module, and the log level.
    logging.basicConfig(level=logging.DEBUG,
                        format=('%(asctime)s - %(module)s -'
                                ' %(levelname)s - %(message)s'))
    # Create a temp file and set both the master and slave PTYs to the file to
    # create a loopback.
    self.tempfile = tempfile.TemporaryFile()

    # Mock out the pipes.
    mock_pipe_end_0, mock_pipe_end_1 = mock.MagicMock(), mock.MagicMock()
    self.console = console.Console(self.tempfile.fileno(), self.tempfile,
                                   tempfile.TemporaryFile(),
                                   mock_pipe_end_0, mock_pipe_end_1, "EC")
    self.console.oobm_queue = mock.MagicMock()

  @mock.patch('console.Console.CheckForEnhancedECImage')
  def test_InterrogateCommand(self, mock_check):
    """Verify that 'interrogate' command works as expected.

    Args:
      mock_check: A MagicMock object replacing the CheckForEnhancedECIMage()
        method.
    """
    input_stream = []
    expected_calls = []
    mock_check.side_effect = [False]

    # 'interrogate never' should disable the interrogation from happening at
    # all.
    cmd = b'interrogate never'
    # Enter the OOBM prompt.
    input_stream.extend(BytesToByteList(b'%'))
    # Type the command
    input_stream.extend(BytesToByteList(cmd))
    # Press enter.
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    input_stream = []

    # The OOBM queue should have been called with the command being put.
    expected_calls.append(mock.call.put(cmd))
    self.console.oobm_queue.assert_has_calls(expected_calls)

    # Process the OOBM queue.
    self.console.oobm_queue.get.side_effect = [cmd]
    self.console.ProcessOOBMQueue()

    # Type out a few commands.
    input_stream.extend(BytesToByteList(b'version'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    input_stream.extend(BytesToByteList(b'flashinfo'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    input_stream.extend(BytesToByteList(b'sysinfo'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # The Check function should NOT have been called at all.
    mock_check.assert_not_called()

    # The EC image should be assumed to be not enhanced.
    self.assertFalse(self.console.enhanced_ec, 'The image should be assumed to'
                     ' be NOT enhanced.')

    # Reset the mocks.
    mock_check.reset_mock()
    self.console.oobm_queue.reset_mock()

    # 'interrogate auto' should not interrogate at all.  It should only be
    # scanning the output stream for the 'console is enabled' strings.
    cmd = b'interrogate auto'
    # Enter the OOBM prompt.
    input_stream.extend(BytesToByteList(b'%'))
    # Type the command
    input_stream.extend(BytesToByteList(cmd))
    # Press enter.
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    input_stream = []
    expected_calls = []

    # The OOBM queue should have been called with the command being put.
    expected_calls.append(mock.call.put(cmd))
    self.console.oobm_queue.assert_has_calls(expected_calls)

    # Process the OOBM queue.
    self.console.oobm_queue.get.side_effect = [cmd]
    self.console.ProcessOOBMQueue()

    # Type out a few commands.
    input_stream.extend(BytesToByteList(b'version'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    input_stream.extend(BytesToByteList(b'flashinfo'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    input_stream.extend(BytesToByteList(b'sysinfo'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # The Check function should NOT have been called at all.
    mock_check.assert_not_called()

    # The EC image should be assumed to be not enhanced.
    self.assertFalse(self.console.enhanced_ec, 'The image should be assumed to'
                     ' be NOT enhanced.')

    # Reset the mocks.
    mock_check.reset_mock()
    self.console.oobm_queue.reset_mock()

    # 'interrogate always' should, like its name implies, interrogate always
    # after each press of the enter key.  This was the former way of doing
    # interrogation.
    cmd = b'interrogate always'
    # Enter the OOBM prompt.
    input_stream.extend(BytesToByteList(b'%'))
    # Type the command
    input_stream.extend(BytesToByteList(cmd))
    # Press enter.
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    input_stream = []
    expected_calls = []

    # The OOBM queue should have been called with the command being put.
    expected_calls.append(mock.call.put(cmd))
    self.console.oobm_queue.assert_has_calls(expected_calls)

    # Process the OOBM queue.
    self.console.oobm_queue.get.side_effect = [cmd]
    self.console.ProcessOOBMQueue()

    # The Check method should be called 3 times here.
    mock_check.side_effect = [False, False, False]

    # Type out a few commands.
    input_stream.extend(BytesToByteList(b'help list'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    input_stream.extend(BytesToByteList(b'taskinfo'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    input_stream.extend(BytesToByteList(b'hibdelay'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # The Check method should have been called 3 times here.
    expected_calls = [mock.call(), mock.call(), mock.call()]
    mock_check.assert_has_calls(expected_calls)

    # The EC image should be assumed to be not enhanced.
    self.assertFalse(self.console.enhanced_ec, 'The image should be assumed to'
                     ' be NOT enhanced.')

    # Now, let's try to assume that the image is enhanced while still disabling
    # interrogation.
    mock_check.reset_mock()
    self.console.oobm_queue.reset_mock()
    input_stream = []
    cmd = b'interrogate never enhanced'
    # Enter the OOBM prompt.
    input_stream.extend(BytesToByteList(b'%'))
    # Type the command
    input_stream.extend(BytesToByteList(cmd))
    # Press enter.
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    input_stream = []
    expected_calls = []

    # The OOBM queue should have been called with the command being put.
    expected_calls.append(mock.call.put(cmd))
    self.console.oobm_queue.assert_has_calls(expected_calls)

    # Process the OOBM queue.
    self.console.oobm_queue.get.side_effect = [cmd]
    self.console.ProcessOOBMQueue()

    # Type out a few commands.
    input_stream.extend(BytesToByteList(b'chgstate'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    input_stream.extend(BytesToByteList(b'hash'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    input_stream.extend(BytesToByteList(b'sysjump rw'))
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # The check method should have never been called.
    mock_check.assert_not_called()

    # The EC image should be assumed to be enhanced.
    self.assertTrue(self.console.enhanced_ec, 'The image should be'
                    ' assumed to be enhanced.')


if __name__ == '__main__':
  unittest.main()
