#!/usr/bin/python2
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Unit tests for the EC-3PO Console interface."""
from __future__ import print_function
import binascii
import mock
import multiprocessing
import tempfile
import unittest

import console

ESC_STRING = chr(console.ControlKey.ESC)

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
    string += '[' + str(count) + 'D'
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
    string += '[' + str(count) + 'C'
    return string

BACKSPACE_STRING = ''
# Move cursor left 1 column.
BACKSPACE_STRING += OutputStream.MoveCursorLeft(1)
# Write a space.
BACKSPACE_STRING += ' '
# Move cursor left 1 column.
BACKSPACE_STRING += OutputStream.MoveCursorLeft(1)

class TestConsoleEditingMethods(unittest.TestCase):
  """Test case to verify all console editing methods."""

  def setUp(self):
    """Setup the test harness."""
    # Create a temp file and set both the master and slave PTYs to the file to
    # create a loopback.
    self.tempfile = tempfile.TemporaryFile()

    # Create some dummy pipes.  These won't be used since we'll mock out sends
    # to the interpreter.
    dummy_pipe_end_0, dummy_pipe_end_1 = multiprocessing.Pipe()
    self.console = console.Console(self.tempfile.fileno(), self.tempfile,
                                   dummy_pipe_end_0, dummy_pipe_end_1)

    # Mock out sends to the interpreter.
    multiprocessing.Pipe.send = mock.MagicMock()

  def StringToByteList(self, string):
    """Converts a string to list of bytes.

    Args:
      string: A literal string to turn into a list of bytes.

    Returns:
      A list of integers representing the byte value of each character in the
        string.
    """
    return [ord(c) for c in string]

  def BadConsoleOutput(self, expected, got):
    """Format the console output into readable text.

    Args:
      expected: A list of bytes representing the expected output console
        stream.
      got: A list of byte representing the actual output console stream.

    Returns:
      string: A formatted string which shows the expected console output stream
        and the actual console output stream.
    """
    esc_state = 0
    string = 'Incorrect console output stream.\n'
    string += 'exp: |'
    count = 0
    for char in expected:
      if esc_state != 0:
        if esc_state == console.EscState.ESC_START:
          if char == '[':
            esc_state = console.EscState.ESC_BRACKET
        elif esc_state == console.EscState.ESC_BRACKET:
          if char == 'D':
            string += '[cursor left ' + str(count) + ' cols]'
            esc_state = 0
          elif char == 'C':
            string += '[cursor right ' + str(count) + ' cols]'
            esc_state = 0
          else:
            count = int(char)
      # Print if it's printable.
      elif console.IsPrintable(ord(char)):
        string += char
      else:
        # It might be a sequence of some type.
        if ord(char) == console.ControlKey.ESC:
          # Need to look at the following sequence.
          esc_state = console.EscState.ESC_START
        else:
          string += '{' + binascii.hexlify(char) + '}'

    string += '|\n\ngot: |'
    for char in got:
      if esc_state != 0:
        if esc_state == console.EscState.ESC_START:
          if char == '[':
            esc_state = console.EscState.ESC_BRACKET
        elif esc_state == console.EscState.ESC_BRACKET:
          if char == 'D':
            string += '[cursor left ' + str(count) + ' cols]'
            esc_state = 0
          elif char == 'C':
            string += '[cursor right ' + str(count) + ' cols]'
            esc_state = 0
          else:
            count = int(char)
      # Print if it's printable.
      elif console.IsPrintable(ord(char)):
        string += char
      else:
        # It might be a sequence of some type.
        if ord(char) == console.ControlKey.ESC:
          # Need to look at the following sequence.
          esc_state = console.EscState.ESC_START
        else:
          string += '{' + binascii.hexlify(char) + '}'
    string += '|\n\n'

    # TODO(aaboagye): It would be nice to replace all those move left 1, ' ',
    # move left 1, with backspace.

    return string

  def CheckConsoleOutput(self, exp_console_out):
    """Verify what was sent out the console matches what we expect.

    Args:
      exp_console_out: A string representing the console output stream.
    """
    # Read what was sent out the console.
    self.tempfile.seek(0)
    console_out = self.tempfile.read()

    self.assertEqual(exp_console_out,
                     console_out,
                     (self.BadConsoleOutput(exp_console_out, console_out)
                      + str(self.console)))

  def CheckInputBuffer(self, exp_input_buffer):
    """Verify that the input buffer contains what we expect.

    Args:
      exp_input_buffer: A string containing the contents of the current input
        buffer.
    """
    self.assertEqual(exp_input_buffer, self.console.input_buffer,
                     ('input buffer does not match expected.\n'
                      'expected: |' + exp_input_buffer + '|\n'
                      'got:      |' + self.console.input_buffer + '|\n' +
                      str(self.console)))

  def CheckInputBufferPosition(self, exp_pos):
    """Verify the input buffer position.

    Args:
      exp_pos: An integer representing the expected input buffer position.
    """
    self.assertEqual(exp_pos, self.console.input_buffer_pos,
                     'input buffer position is incorrect.\ngot: ' +
                     str(self.console.input_buffer_pos) + '\nexp: ' +
                     str(exp_pos) + '\n' + str(self.console))

  def CheckHistoryBuffer(self, exp_history):
    """Verify that the items in the history buffer are what we expect.

    Args:
      exp_history: A list of strings representing the expected contents of the
        history buffer.
    """
    # First, check to see if the length is what we expect.
    self.assertEqual(len(exp_history), len(self.console.history),
                     ('The number of items in the history is unexpected.\n'
                      'exp: ' + str(len(exp_history)) + '\n'
                      'got: ' + str(len(self.console.history)) + '\n'
                      'internal state:\n' + str(self.console)))

    # Next, check the actual contents of the history buffer.
    for i in range(len(exp_history)):
      self.assertEqual(exp_history[i], self.console.history[i],
                       ('history buffer contents are incorrect.\n'
                        'exp: ' + exp_history[i] + '\n'
                        'got: ' + self.console.history[i] + '\n'
                        'internal state:\n' + str(self.console)))

  def test_EnteringChars(self):
    """Verify that characters are echoed onto the console."""
    test_str = 'abc'
    input_stream = self.StringToByteList(test_str)

    # Send the characters in.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Check the input position.
    exp_pos = len(test_str)
    self.CheckInputBufferPosition(exp_pos)

    # Verify that the input buffer is correct.
    expected_buffer = test_str
    self.CheckInputBuffer(expected_buffer)

    # Check console output
    exp_console_out = test_str
    self.CheckConsoleOutput(exp_console_out)

  def test_EnteringDeletingMoreCharsThanEntered(self):
    """Verify that we can press backspace more than we have entered chars."""
    test_str = 'spamspam'
    input_stream = self.StringToByteList(test_str)

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
    self.CheckInputBufferPosition(0)

    # Next, examine the output stream for the correct sequence.
    exp_console_out = test_str
    for _ in range(len(test_str)):
      exp_console_out += BACKSPACE_STRING

    # Now, verify that we got what we expected.
    self.CheckConsoleOutput(exp_console_out)

  def test_EnteringMoreThanCharLimit(self):
    """Verify that we drop characters when the line is too long."""
    test_str = self.console.line_limit * 'o' # All allowed.
    test_str += 5 * 'x' # All should be dropped.
    input_stream = self.StringToByteList(test_str)

    # Send the characters in.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, we expect that input buffer position should be equal to the line
    # limit.
    exp_pos = self.console.line_limit
    self.CheckInputBufferPosition(exp_pos)

    # The input buffer should only hold until the line limit.
    exp_buffer = test_str[0:self.console.line_limit]
    self.CheckInputBuffer(exp_buffer)

    # Lastly, check that the extra characters are not printed.
    exp_console_out = exp_buffer
    self.CheckConsoleOutput(exp_console_out)

  def test_ValidKeysOnLongLine(self):
    """Verify that we can still press valid keys if the line is too long."""
    # Fill the line.
    test_str = self.console.line_limit * 'o'
    exp_console_out = test_str
    # Try to fill it even more; these should all be dropped.
    test_str += 5 * 'x'
    input_stream = self.StringToByteList(test_str)

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
    input_stream.extend(self.StringToByteList('o'))
    exp_console_out += 'o'

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
    exp_console_out += '\r\n' + self.console.prompt

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
    exp_console_out += ' ' + OutputStream.MoveCursorLeft(1)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify everything happened correctly.
    self.CheckConsoleOutput(exp_console_out)

  def test_BackspaceOnEmptyLine(self):
    """Verify that we can backspace on an empty line with no bad effects."""
    # Send a single backspace.
    test_str = [console.ControlKey.BACKSPACE]

    # Send the characters in.
    for byte in test_str:
      self.console.HandleChar(byte)

    # Check the input position.
    exp_pos = 0
    self.CheckInputBufferPosition(exp_pos)

    # Check that buffer is empty.
    exp_input_buffer = ''
    self.CheckInputBuffer(exp_input_buffer)

    # Check that the console output is empty.
    exp_console_out = ''
    self.CheckConsoleOutput(exp_console_out)

  def test_BackspaceWithinLine(self):
    """Verify that we shift the chars over when backspacing within a line."""
    # Misspell 'help'
    test_str = 'heelp'
    input_stream = self.StringToByteList(test_str)
    # Use the arrow key to go back to fix it.
    # Move cursor left 1 column.
    input_stream.extend(2*Keys.LEFT_ARROW)
    # Backspace once to remove the extra 'e'.
    input_stream.append(console.ControlKey.BACKSPACE)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify the input buffer
    exp_input_buffer = 'help'
    self.CheckInputBuffer(exp_input_buffer)

    # Verify the input buffer position. It should be at 2 (cursor over the 'l')
    self.CheckInputBufferPosition(2)

    # We expect the console output to be the test string, with two moves to the
    # left, another move left, and then the rest of the line followed by a
    # space.
    exp_console_out = test_str
    exp_console_out += 2 * OutputStream.MoveCursorLeft(1)

    # Move cursor left 1 column.
    exp_console_out += OutputStream.MoveCursorLeft(1)
    # Rest of the line and a space. (test_str in this case)
    exp_console_out += 'lp '
    # Reset the cursor 2 + 1 to the left.
    exp_console_out += OutputStream.MoveCursorLeft(3)

    # Verify console output.
    self.CheckConsoleOutput(exp_console_out)

  def test_JumpToBeginningOfLineViaCtrlA(self):
    """Verify that we can jump to the beginning of a line with Ctrl+A."""
    # Enter some chars and press CTRL+A
    test_str = 'abc'
    input_stream = self.StringToByteList(test_str) + [console.ControlKey.CTRL_A]

    # Send the characters in.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # We expect to see our test string followed by a move cursor left.
    exp_console_out = test_str
    exp_console_out += OutputStream.MoveCursorLeft(len(test_str))

    # Check to see what whas printed on the console.
    self.CheckConsoleOutput(exp_console_out)

    # Check that the input buffer position is now 0.
    self.CheckInputBufferPosition(0)

    # Check input buffer still contains our test string.
    self.CheckInputBuffer(test_str)

  def test_JumpToBeginningOfLineViaHomeKey(self):
    """Jump to beginning of line via HOME key."""
    test_str = 'version'
    input_stream = self.StringToByteList(test_str)
    input_stream.extend(Keys.HOME)

    # Send out the stream.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, verify that input buffer position is now 0.
    self.CheckInputBufferPosition(0)

    # Next, verify that the input buffer did not change.
    self.CheckInputBuffer(test_str)

    # Lastly, check that the cursor moved correctly.
    exp_console_out = test_str
    exp_console_out += OutputStream.MoveCursorLeft(len(test_str))
    self.CheckConsoleOutput(exp_console_out)

  def test_JumpToEndOfLineViaEndKey(self):
    """Jump to the end of the line using the END key."""
    test_str = 'version'
    input_stream = self.StringToByteList(test_str)
    input_stream += [console.ControlKey.CTRL_A]
    # Now, jump to the end of the line.
    input_stream.extend(Keys.END)

    # Send out the stream.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is correct.  This should be at the
    # end of the test string.
    self.CheckInputBufferPosition(len(test_str))

    # The expected output should be the test string, followed by a jump to the
    # beginning of the line, and lastly a jump to the end of the line.
    exp_console_out = test_str
    exp_console_out += OutputStream.MoveCursorLeft(len(test_str))
    # Now the jump back to the end of the line.
    exp_console_out += OutputStream.MoveCursorRight(len(test_str))

    # Verify console output stream.
    self.CheckConsoleOutput(exp_console_out)

  def test_JumpToEndOfLineViaCtrlE(self):
    """Enter some chars and then try to jump to the end. (Should be a no-op)"""
    test_str = 'sysinfo'
    input_stream = self.StringToByteList(test_str)
    input_stream.append(console.ControlKey.CTRL_E)

    # Send out the stream
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position isn't any further than we expect.
    # At this point, the position should be at the end of the test string.
    self.CheckInputBufferPosition(len(test_str))

    # Now, let's try to jump to the beginning and then jump back to the end.
    input_stream = [console.ControlKey.CTRL_A, console.ControlKey.CTRL_E]

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Perform the same verification.
    self.CheckInputBufferPosition(len(test_str))

    # Lastly try to jump again, beyond the end.
    input_stream = [console.ControlKey.CTRL_E]

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Perform the same verification.
    self.CheckInputBufferPosition(len(test_str))

    # We expect to see the test string, a jump to the begining of the line, and
    # one jump to the end of the line.
    exp_console_out = test_str
    # Jump to beginning.
    exp_console_out += OutputStream.MoveCursorLeft(len(test_str))
    # Jump back to end.
    exp_console_out += OutputStream.MoveCursorRight(len(test_str))

    # Verify the console output.
    self.CheckConsoleOutput(exp_console_out)

  def test_MoveLeftWithArrowKey(self):
    """Move cursor left one column with arrow key."""
    test_str = 'tastyspam'
    input_stream = self.StringToByteList(test_str)
    input_stream.extend(Keys.LEFT_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is 1 less than the length.
    self.CheckInputBufferPosition(len(test_str) - 1)

    # Also, verify that the input buffer is not modified.
    self.CheckInputBuffer(test_str)

    # We expect the test string, followed by a one column move left.
    exp_console_out = test_str + OutputStream.MoveCursorLeft(1)

    # Verify console output.
    self.CheckConsoleOutput(exp_console_out)

  def test_MoveLeftWithCtrlB(self):
    """Move cursor back one column with Ctrl+B."""
    test_str = 'tastyspam'
    input_stream = self.StringToByteList(test_str)
    input_stream.append(console.ControlKey.CTRL_B)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is 1 less than the length.
    self.CheckInputBufferPosition(len(test_str) - 1)

    # Also, verify that the input buffer is not modified.
    self.CheckInputBuffer(test_str)

    # We expect the test string, followed by a one column move left.
    exp_console_out = test_str + OutputStream.MoveCursorLeft(1)

    # Verify console output.
    self.CheckConsoleOutput(exp_console_out)

  def test_MoveRightWithArrowKey(self):
    """Move cursor one column to the right with the arrow key."""
    test_str = 'version'
    input_stream = self.StringToByteList(test_str)
    # Jump to beginning of line.
    input_stream.append(console.ControlKey.CTRL_A)
    # Press right arrow key.
    input_stream.extend(Keys.RIGHT_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is 1.
    self.CheckInputBufferPosition(1)

    # Also, verify that the input buffer is not modified.
    self.CheckInputBuffer(test_str)

    # We expect the test string, followed by a jump to the beginning of the
    # line, and finally a move right 1.
    exp_console_out = test_str + OutputStream.MoveCursorLeft(len((test_str)))

    # A move right 1 column.
    exp_console_out += OutputStream.MoveCursorRight(1)

    # Verify console output.
    self.CheckConsoleOutput(exp_console_out)

  def test_MoveRightWithCtrlF(self):
    """Move cursor forward one column with Ctrl+F."""
    test_str = 'panicinfo'
    input_stream = self.StringToByteList(test_str)
    input_stream.append(console.ControlKey.CTRL_A)
    # Now, move right one column.
    input_stream.append(console.ControlKey.CTRL_F)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the input buffer position is 1.
    self.CheckInputBufferPosition(1)

    # Also, verify that the input buffer is not modified.
    self.CheckInputBuffer(test_str)

    # We expect the test string, followed by a jump to the beginning of the
    # line, and finally a move right 1.
    exp_console_out = test_str + OutputStream.MoveCursorLeft(len((test_str)))

    # A move right 1 column.
    exp_console_out += OutputStream.MoveCursorRight(1)

    # Verify console output.
    self.CheckConsoleOutput(exp_console_out)

  def test_ImpossibleMoveLeftWithArrowKey(self):
    """Verify that we can't move left at the beginning of the line."""
    # We shouldn't be able to move left if we're at the beginning of the line.
    input_stream = Keys.LEFT_ARROW

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Nothing should have been output.
    exp_console_output = ''
    self.CheckConsoleOutput(exp_console_output)

    # The input buffer position should still be 0.
    self.CheckInputBufferPosition(0)

    # The input buffer itself should be empty.
    self.CheckInputBuffer('')

  def test_ImpossibleMoveRightWithArrowKey(self):
    """Verify that we can't move right at the end of the line."""
    # We shouldn't be able to move right if we're at the end of the line.
    input_stream = Keys.RIGHT_ARROW

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Nothing should have been output.
    exp_console_output = ''
    self.CheckConsoleOutput(exp_console_output)

    # The input buffer position should still be 0.
    self.CheckInputBufferPosition(0)

    # The input buffer itself should be empty.
    self.CheckInputBuffer('')

  def test_KillEntireLine(self):
    """Verify that we can kill an entire line with Ctrl+K."""
    test_str = 'accelinfo on'
    input_stream = self.StringToByteList(test_str)
    # Jump to beginning of line and then kill it with Ctrl+K.
    input_stream.extend([console.ControlKey.CTRL_A, console.ControlKey.CTRL_K])

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, we expect that the input buffer is empty.
    self.CheckInputBuffer('')

    # The buffer position should be 0.
    self.CheckInputBufferPosition(0)

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
    self.CheckConsoleOutput(exp_console_out)

  def test_KillPartialLine(self):
    """Verify that we can kill a portion of a line."""
    test_str = 'accelread 0 1'
    input_stream = self.StringToByteList(test_str)
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
    self.CheckInputBuffer(exp_input_buffer)

    # Verify the input buffer position.
    self.CheckInputBufferPosition(len(test_str) - len_to_kill)

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
    self.CheckConsoleOutput(exp_console_out)

  def test_InsertingCharacters(self):
    """Verify that we can insert charcters within the line."""
    test_str = 'accel 0 1' # Here we forgot the 'read' part in 'accelread'
    input_stream = self.StringToByteList(test_str)
    # We need to move over to the 'l' and add read.
    insertion_point = test_str.find('l') + 1
    for i in range(len(test_str) - insertion_point):
      # Move cursor left.
      input_stream.extend(Keys.LEFT_ARROW)
    # Now, add in 'read'
    added_str = 'read'
    input_stream.extend(self.StringToByteList(added_str))

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # First, verify that the input buffer is correct.
    exp_input_buffer = test_str[:insertion_point] + added_str
    exp_input_buffer += test_str[insertion_point:]
    self.CheckInputBuffer(exp_input_buffer)

    # Verify that the input buffer position is correct.
    exp_input_buffer_pos = insertion_point + len(added_str)
    self.CheckInputBufferPosition(exp_input_buffer_pos)

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
      exp_console_out += added_str[i]
      # The rest of the line
      exp_console_out += test_str[insertion_point:]
      # Reset the cursor back left
      reset_dist = len(test_str[insertion_point:])
      exp_console_out += OutputStream.MoveCursorLeft(reset_dist)

    # Verify the console output.
    self.CheckConsoleOutput(exp_console_out)

  def test_StoreCommandHistory(self):
    """Verify that entered commands are stored in the history."""
    test_commands = []
    test_commands.append('help')
    test_commands.append('version')
    test_commands.append('accelread 0 1')
    input_stream = []
    for c in test_commands:
      input_stream.extend(self.StringToByteList(c))
      input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # We expect to have the test commands in the history buffer.
    exp_history_buf = test_commands
    self.CheckHistoryBuffer(exp_history_buf)

  def test_CycleUpThruCommandHistory(self):
    """Verify that the UP arrow key will print itmes in the history buffer."""
    # Enter some commands.
    test_commands = ['version', 'accelrange 0', 'battery', 'gettime']
    input_stream = []
    for command in test_commands:
      input_stream.extend(self.StringToByteList(command))
      input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Now, hit the UP arrow key to print the previous entries.
    for i in range(len(test_commands)):
      input_stream.extend(Keys.UP_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # The expected output should be test commands with prompts printed in
    # between, followed by line kills with the previous test commands printed.
    exp_console_out = ''
    for i in range(len(test_commands)):
      exp_console_out += test_commands[i] + '\r\n' + self.console.prompt

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
    self.CheckConsoleOutput(exp_console_out)

  def test_UpArrowOnEmptyHistory(self):
    """Ensure nothing happens if the history is empty."""
    # Press the up arrow key twice.
    input_stream = 2 * Keys.UP_ARROW

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # We expect nothing to have happened.
    exp_console_out = ''
    exp_input_buffer = ''
    exp_input_buffer_pos = 0
    exp_history_buf = []

    # Verify.
    self.CheckConsoleOutput(exp_console_out)
    self.CheckInputBufferPosition(exp_input_buffer_pos)
    self.CheckInputBuffer(exp_input_buffer)
    self.CheckHistoryBuffer(exp_history_buf)

  def test_UpArrowDoesNotGoOutOfBounds(self):
    """Verify that pressing the up arrow many times won't go out of bounds."""
    # Enter one command.
    test_str = 'help version'
    input_stream = self.StringToByteList(test_str)
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)
    # Then press the up arrow key twice.
    input_stream.extend(2 * Keys.UP_ARROW)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the history buffer is correct.
    exp_history_buf = [test_str]
    self.CheckHistoryBuffer(exp_history_buf)

    # We expect that the console output should only contain our entered command,
    # a new prompt, and then our command aggain.
    exp_console_out = test_str + '\r\n' + self.console.prompt
    # Pressing up should reprint the command we entered.
    exp_console_out += test_str

    # Verify.
    self.CheckConsoleOutput(exp_console_out)

  def test_CycleDownThruCommandHistory(self):
    """Verify that we can select entries by hitting the down arrow."""
    # Enter at least 4 commands.
    test_commands = ['version', 'accelrange 0', 'battery', 'gettime']
    input_stream = []
    for command in test_commands:
      input_stream.extend(self.StringToByteList(command))
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
    exp_console_out = ''
    for i in range(len(test_commands)):
      exp_console_out += test_commands[i] + '\r\n' + self.console.prompt

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
    self.CheckConsoleOutput(exp_console_out)

    # Verify input buffer.
    exp_input_buffer = '' # Empty because our partial command was empty.
    exp_input_buffer_pos = len(exp_input_buffer)
    self.CheckInputBuffer(exp_input_buffer)
    self.CheckInputBufferPosition(exp_input_buffer_pos)

  def test_SavingPartialCommandWhenNavigatingHistory(self):
    """Verify that partial commands are saved when navigating history."""
    # Enter a command.
    test_str = 'accelinfo'
    input_stream = self.StringToByteList(test_str)
    input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Enter a partial command.
    partial_cmd = 'ver'
    input_stream.extend(self.StringToByteList(partial_cmd))

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
    exp_console_out = test_str + '\r\n' + self.console.prompt
    exp_console_out += partial_cmd
    for _ in range(len(partial_cmd)):
      exp_console_out += BACKSPACE_STRING
    exp_console_out += test_str
    for _ in range(len(test_str)):
      exp_console_out += BACKSPACE_STRING
    exp_console_out += partial_cmd

    # Verify console output.
    self.CheckConsoleOutput(exp_console_out)

    # Verify input buffer.
    exp_input_buffer = partial_cmd
    exp_input_buffer_pos = len(exp_input_buffer)
    self.CheckInputBuffer(exp_input_buffer)
    self.CheckInputBufferPosition(exp_input_buffer_pos)

  def test_DownArrowOnEmptyHistory(self):
    """Ensure nothing happens if the history is empty."""
    # Then press the up down arrow twice.
    input_stream = 2 * Keys.DOWN_ARROW

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # We expect nothing to have happened.
    exp_console_out = ''
    exp_input_buffer = ''
    exp_input_buffer_pos = 0
    exp_history_buf = []

    # Verify.
    self.CheckConsoleOutput(exp_console_out)
    self.CheckInputBufferPosition(exp_input_buffer_pos)
    self.CheckInputBuffer(exp_input_buffer)
    self.CheckHistoryBuffer(exp_history_buf)

  def test_DeleteCharsUsingDELKey(self):
    """Verify that we can delete characters using the DEL key."""
    test_str = 'version'
    input_stream = self.StringToByteList(test_str)

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
    exp_console_out += test_str[-1:] + ' '

    # Reset the cursor by moving back 2 columns because of the 'n' and space.
    exp_console_out += OutputStream.MoveCursorLeft(2)

    # Verify console output.
    self.CheckConsoleOutput(exp_console_out)

    # Verify input buffer.  The input buffer should have the char sliced out and
    # be positioned where the char was removed.
    exp_input_buffer = test_str[:-2] + test_str[-1:]
    exp_input_buffer_pos = len(exp_input_buffer) - 1
    self.CheckInputBuffer(exp_input_buffer)
    self.CheckInputBufferPosition(exp_input_buffer_pos)

  def test_RepeatedCommandInHistory(self):
    """Verify that we don't store 2 consecutive identical commands in history"""
    # Enter a few commands.
    test_commands = ['version', 'accelrange 0', 'battery', 'gettime']
    # Repeat the last command.
    test_commands.append(test_commands[len(test_commands)-1])

    input_stream = []
    for command in test_commands:
      input_stream.extend(self.StringToByteList(command))
      input_stream.append(console.ControlKey.CARRIAGE_RETURN)

    # Send the sequence out.
    for byte in input_stream:
      self.console.HandleChar(byte)

    # Verify that the history buffer is correct.  The last command, since
    # it was repeated, should not have been added to the history.
    exp_history_buf = test_commands[0:len(test_commands)-1]
    self.CheckHistoryBuffer(exp_history_buf)


if __name__ == '__main__':
  unittest.main()
