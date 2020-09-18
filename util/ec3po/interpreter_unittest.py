#!/usr/bin/env python
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for the EC-3PO interpreter."""

# Note: This is a py2/3 compatible file.

from __future__ import print_function

# pylint: disable=cros-logging-import
import logging
import mock
import tempfile
import unittest

import six

import interpreter
import threadproc_shim


def GetBuiltins(func):
  if six.PY2:
    return '__builtin__.' + func
  return 'builtins.' + func


class TestEnhancedECBehaviour(unittest.TestCase):
  """Test case to verify all enhanced EC interpretation tasks."""
  def setUp(self):
    """Setup the test harness."""
    # Setup logging with a timestamp, the module, and the log level.
    logging.basicConfig(level=logging.DEBUG,
                        format=('%(asctime)s - %(module)s -'
                                ' %(levelname)s - %(message)s'))

    # Create a tempfile that would represent the EC UART PTY.
    self.tempfile = tempfile.NamedTemporaryFile()

    # Create the pipes that the interpreter will use.
    self.cmd_pipe_user, self.cmd_pipe_itpr = threadproc_shim.Pipe()
    self.dbg_pipe_user, self.dbg_pipe_itpr = threadproc_shim.Pipe(duplex=False)

    # Mock the open() function so we can inspect reads/writes to the EC.
    self.ec_uart_pty = mock.mock_open()

    with mock.patch(GetBuiltins('open'), self.ec_uart_pty):
      # Create an interpreter.
      self.itpr = interpreter.Interpreter(self.tempfile.name,
                                          self.cmd_pipe_itpr,
                                          self.dbg_pipe_itpr,
                                          log_level=logging.DEBUG,
                                          name="EC")

  @mock.patch('interpreter.os')
  def test_HandlingCommandsThatProduceNoOutput(self, mock_os):
    """Verify that the Interpreter correctly handles non-output commands.

    Args:
      mock_os: MagicMock object replacing the 'os' module for this test
        case.
    """
    # The interpreter init should open the EC UART PTY.
    expected_ec_calls = [mock.call(self.tempfile.name, 'ab+')]
    # Have a command come in the command pipe.  The first command will be an
    # interrogation to determine if the EC is enhanced or not.
    self.cmd_pipe_user.send(interpreter.EC_SYN)
    self.itpr.HandleUserData()
    # At this point, the command should be queued up waiting to be sent, so
    # let's actually send it to the EC.
    self.itpr.SendCmdToEC()
    expected_ec_calls.extend([mock.call().write(interpreter.EC_SYN),
                              mock.call().flush()])
    # Now, assume that the EC sends only 1 response back of EC_ACK.
    mock_os.read.side_effect = [interpreter.EC_ACK]
    # When reading the EC, the interpreter will call file.fileno() to pass to
    # os.read().
    expected_ec_calls.append(mock.call().fileno())
    # Simulate the response.
    self.itpr.HandleECData()

    # Now that the interrogation was complete, it's time to send down the real
    # command.
    test_cmd = b'chan save'
    # Send the test command down the pipe.
    self.cmd_pipe_user.send(test_cmd)
    self.itpr.HandleUserData()
    self.itpr.SendCmdToEC()
    # Since the EC image is enhanced, we should have sent a packed command.
    expected_ec_calls.append(mock.call().write(self.itpr.PackCommand(test_cmd)))
    expected_ec_calls.append(mock.call().flush())

    # Now that the first command was sent, we should send another command which
    # produces no output.  The console would send another interrogation.
    self.cmd_pipe_user.send(interpreter.EC_SYN)
    self.itpr.HandleUserData()
    self.itpr.SendCmdToEC()
    expected_ec_calls.extend([mock.call().write(interpreter.EC_SYN),
                              mock.call().flush()])
    # Again, assume that the EC sends only 1 response back of EC_ACK.
    mock_os.read.side_effect = [interpreter.EC_ACK]
    # When reading the EC, the interpreter will call file.fileno() to pass to
    # os.read().
    expected_ec_calls.append(mock.call().fileno())
    # Simulate the response.
    self.itpr.HandleECData()

    # Now send the second test command.
    test_cmd = b'chan 0'
    self.cmd_pipe_user.send(test_cmd)
    self.itpr.HandleUserData()
    self.itpr.SendCmdToEC()
    # Since the EC image is enhanced, we should have sent a packed command.
    expected_ec_calls.append(mock.call().write(self.itpr.PackCommand(test_cmd)))
    expected_ec_calls.append(mock.call().flush())

    # Finally, verify that the appropriate writes were actually sent to the EC.
    self.ec_uart_pty.assert_has_calls(expected_ec_calls)

  @mock.patch('interpreter.os')
  def test_CommandRetryingOnError(self, mock_os):
    """Verify that commands are retried if an error is encountered.

    Args:
      mock_os: MagicMock object replacing the 'os' module for this test
        case.
    """
    # The interpreter init should open the EC UART PTY.
    expected_ec_calls = [mock.call(self.tempfile.name, 'ab+')]
    # Have a command come in the command pipe.  The first command will be an
    # interrogation to determine if the EC is enhanced or not.
    self.cmd_pipe_user.send(interpreter.EC_SYN)
    self.itpr.HandleUserData()
    # At this point, the command should be queued up waiting to be sent, so
    # let's actually send it to the EC.
    self.itpr.SendCmdToEC()
    expected_ec_calls.extend([mock.call().write(interpreter.EC_SYN),
                              mock.call().flush()])
    # Now, assume that the EC sends only 1 response back of EC_ACK.
    mock_os.read.side_effect = [interpreter.EC_ACK]
    # When reading the EC, the interpreter will call file.fileno() to pass to
    # os.read().
    expected_ec_calls.append(mock.call().fileno())
    # Simulate the response.
    self.itpr.HandleECData()

    # Let's send a command that is received on the EC-side with an error.
    test_cmd = b'accelinfo'
    self.cmd_pipe_user.send(test_cmd)
    self.itpr.HandleUserData()
    self.itpr.SendCmdToEC()
    packed_cmd = self.itpr.PackCommand(test_cmd)
    expected_ec_calls.extend([mock.call().write(packed_cmd),
                              mock.call().flush()])
    # Have the EC return the error string twice.
    mock_os.read.side_effect = [b'&&EE', b'&&EE']
    for i in range(2):
      # When reading the EC, the interpreter will call file.fileno() to pass to
      # os.read().
      expected_ec_calls.append(mock.call().fileno())
      # Simulate the response.
      self.itpr.HandleECData()

      # Since an error was received, the EC should attempt to retry the command.
      expected_ec_calls.extend([mock.call().write(packed_cmd),
                                mock.call().flush()])
      # Verify that the retry count was decremented.
      self.assertEqual(interpreter.COMMAND_RETRIES-i-1, self.itpr.cmd_retries,
                       'Unexpected cmd_remaining count.')
      # Actually retry the command.
      self.itpr.SendCmdToEC()

    # Now assume that the last one goes through with no trouble.
    expected_ec_calls.extend([mock.call().write(packed_cmd),
                              mock.call().flush()])
    self.itpr.SendCmdToEC()

    # Verify all the calls.
    self.ec_uart_pty.assert_has_calls(expected_ec_calls)

  def test_PackCommandsForEnhancedEC(self):
    """Verify that the interpreter packs commands for enhanced EC images."""
    # Assume current EC image is enhanced.
    self.itpr.enhanced_ec = True
    # Receive a command from the user.
    test_cmd = b'gettime'
    self.cmd_pipe_user.send(test_cmd)
    # Mock out PackCommand to see if it was called.
    self.itpr.PackCommand = mock.MagicMock()
    # Have the interpreter handle the command.
    self.itpr.HandleUserData()
    # Verify that PackCommand() was called.
    self.itpr.PackCommand.assert_called_once_with(test_cmd)

  def test_DontPackCommandsForNonEnhancedEC(self):
    """Verify the interpreter doesn't pack commands for non-enhanced images."""
    # Assume current EC image is not enhanced.
    self.itpr.enhanced_ec = False
    # Receive a command from the user.
    test_cmd = b'gettime'
    self.cmd_pipe_user.send(test_cmd)
    # Mock out PackCommand to see if it was called.
    self.itpr.PackCommand = mock.MagicMock()
    # Have the interpreter handle the command.
    self.itpr.HandleUserData()
    # Verify that PackCommand() was called.
    self.itpr.PackCommand.assert_not_called()

  @mock.patch('interpreter.os')
  def test_KeepingTrackOfInterrogation(self, mock_os):
    """Verify that the interpreter can track the state of the interrogation.

    Args:
      mock_os: MagicMock object replacing the 'os' module. for this test
        case.
    """
    # Upon init, the interpreter should assume that the current EC image is not
    # enhanced.
    self.assertFalse(self.itpr.enhanced_ec, msg=('State of enhanced_ec upon'
                                                 ' init is not False.'))

    # Assume an interrogation request comes in from the user.
    self.cmd_pipe_user.send(interpreter.EC_SYN)
    self.itpr.HandleUserData()

    # Verify the state is now within an interrogation.
    self.assertTrue(self.itpr.interrogating, 'interrogating should be True')
    # The state of enhanced_ec should not be changed yet because we haven't
    # received a valid response yet.
    self.assertFalse(self.itpr.enhanced_ec, msg=('State of enhanced_ec is '
                                                 'not False.'))

    # Assume that the EC responds with an EC_ACK.
    mock_os.read.side_effect = [interpreter.EC_ACK]
    self.itpr.HandleECData()

    # Now, the interrogation should be complete and we should know that the
    # current EC image is enhanced.
    self.assertFalse(self.itpr.interrogating, msg=('interrogating should be '
                                                   'False'))
    self.assertTrue(self.itpr.enhanced_ec, msg='enhanced_ec sholud be True')

    # Now let's perform another interrogation, but pretend that the EC ignores
    # it.
    self.cmd_pipe_user.send(interpreter.EC_SYN)
    self.itpr.HandleUserData()

    # Verify interrogating state.
    self.assertTrue(self.itpr.interrogating, 'interrogating sholud be True')
    # We should assume that the image is not enhanced until we get the valid
    # response.
    self.assertFalse(self.itpr.enhanced_ec, 'enhanced_ec should be False now.')

    # Let's pretend that we get a random debug print.  This should clear the
    # interrogating flag.
    mock_os.read.side_effect = [b'[1660.593076 HC 0x103]']
    self.itpr.HandleECData()

    # Verify that interrogating flag is cleared and enhanced_ec is still False.
    self.assertFalse(self.itpr.interrogating, 'interrogating should be False.')
    self.assertFalse(self.itpr.enhanced_ec,
                     'enhanced_ec should still be False.')


class TestUARTDisconnection(unittest.TestCase):
  """Test case to verify interpreter disconnection/reconnection."""
  def setUp(self):
    """Setup the test harness."""
    # Setup logging with a timestamp, the module, and the log level.
    logging.basicConfig(level=logging.DEBUG,
                        format=('%(asctime)s - %(module)s -'
                                ' %(levelname)s - %(message)s'))

    # Create a tempfile that would represent the EC UART PTY.
    self.tempfile = tempfile.NamedTemporaryFile()

    # Create the pipes that the interpreter will use.
    self.cmd_pipe_user, self.cmd_pipe_itpr = threadproc_shim.Pipe()
    self.dbg_pipe_user, self.dbg_pipe_itpr = threadproc_shim.Pipe(duplex=False)

    # Mock the open() function so we can inspect reads/writes to the EC.
    self.ec_uart_pty = mock.mock_open()

    with mock.patch(GetBuiltins('open'), self.ec_uart_pty):
      # Create an interpreter.
      self.itpr = interpreter.Interpreter(self.tempfile.name,
                                          self.cmd_pipe_itpr,
                                          self.dbg_pipe_itpr,
                                          log_level=logging.DEBUG,
                                          name="EC")

    # First, check that interpreter is initialized to connected.
    self.assertTrue(self.itpr.connected, ('The interpreter should be'
                                          ' initialized in a connected state'))

  def test_DisconnectStopsECTraffic(self):
    """Verify that when in disconnected state, no debug prints are sent."""
    # Let's send a disconnect command through the command pipe.
    self.cmd_pipe_user.send(b'disconnect')
    self.itpr.HandleUserData()

    # Verify interpreter is disconnected from EC.
    self.assertFalse(self.itpr.connected, ('The interpreter should be'
                                           'disconnected.'))
    # Verify that the EC UART is no longer a member of the inputs.  The
    # interpreter will never pull data from the EC if it's not a member of the
    # inputs list.
    self.assertFalse(self.itpr.ec_uart_pty in self.itpr.inputs)

  def test_CommandsDroppedWhenDisconnected(self):
    """Verify that when in disconnected state, commands are dropped."""
    # Send a command, followed by 'disconnect'.
    self.cmd_pipe_user.send(b'taskinfo')
    self.itpr.HandleUserData()
    self.cmd_pipe_user.send(b'disconnect')
    self.itpr.HandleUserData()

    # Verify interpreter is disconnected from EC.
    self.assertFalse(self.itpr.connected, ('The interpreter should be'
                                           'disconnected.'))
    # Verify that the EC UART is no longer a member of the inputs nor outputs.
    self.assertFalse(self.itpr.ec_uart_pty in self.itpr.inputs)
    self.assertFalse(self.itpr.ec_uart_pty in self.itpr.outputs)

    # Have the user send a few more commands in the disconnected state.
    command = 'help\n'
    for char in command:
      self.cmd_pipe_user.send(char.encode('utf-8'))
      self.itpr.HandleUserData()

    # The command queue should be empty.
    self.assertEqual(0, self.itpr.ec_cmd_queue.qsize())

    # Now send the reconnect command.
    self.cmd_pipe_user.send(b'reconnect')

    with mock.patch(GetBuiltins('open'), mock.mock_open()):
      self.itpr.HandleUserData()

    # Verify interpreter is connected.
    self.assertTrue(self.itpr.connected)
    # Verify that EC UART is a member of the inputs.
    self.assertTrue(self.itpr.ec_uart_pty in self.itpr.inputs)
    # Since no command was sent after reconnection, verify that the EC UART is
    # not a member of the outputs.
    self.assertFalse(self.itpr.ec_uart_pty in self.itpr.outputs)

  def test_ReconnectAllowsECTraffic(self):
    """Verify that when connected, EC UART traffic is allowed."""
    # Let's send a disconnect command through the command pipe.
    self.cmd_pipe_user.send(b'disconnect')
    self.itpr.HandleUserData()

    # Verify interpreter is disconnected.
    self.assertFalse(self.itpr.connected, ('The interpreter should be'
                                           'disconnected.'))
    # Verify that the EC UART is no longer a member of the inputs nor outputs.
    self.assertFalse(self.itpr.ec_uart_pty in self.itpr.inputs)
    self.assertFalse(self.itpr.ec_uart_pty in self.itpr.outputs)

    # Issue reconnect command through the command pipe.
    self.cmd_pipe_user.send(b'reconnect')

    with mock.patch(GetBuiltins('open'), mock.mock_open()):
      self.itpr.HandleUserData()

    # Verify interpreter is connected.
    self.assertTrue(self.itpr.connected, ('The interpreter should be'
                                          'connected.'))
    # Verify that the EC UART is now a member of the inputs.
    self.assertTrue(self.itpr.ec_uart_pty in self.itpr.inputs)
    # Since we have issued no commands during the disconnected state, no
    # commands are pending and therefore the PTY should not be added to the
    # outputs.
    self.assertFalse(self.itpr.ec_uart_pty in self.itpr.outputs)


if __name__ == '__main__':
  unittest.main()
