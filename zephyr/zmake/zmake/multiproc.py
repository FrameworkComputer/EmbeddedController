# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import logging
import os
import select
import threading

"""Zmake multiprocessing utility module.

This module is used to aid in zmake's multiprocessing. It contains tools
available to log output from multiple processes on the fly. This means that a
process does not need to finish before the output is available to the developer
on the screen.
"""

# A local pipe use to signal the look that a new file descriptor was added and
# should be included in the select statement.
_logging_interrupt_pipe = os.pipe()
# A condition variable used to synchronize logging operations.
_logging_cv = threading.Condition()
# A map of file descriptors to their logger/logging level tuple.
_logging_map = {}


def _log_fd(fd):
    """Log information from a single file descriptor.

    This function is BLOCKING. It will read from the given file descriptor until
    either the end of line is read or EOF. Once EOF is read it will remove the
    file descriptor from _logging_map so it will no longer be used.
    Additionally, in some cases, the file descriptor will be closed (caused by
    a call to Popen.wait()). In these cases, the file descriptor will also be
    removed from the map as it is no longer valid.
    """
    with _logging_cv:
        logger, log_level = _logging_map[fd]
        if fd.closed:
            del _logging_map[fd]
            _logging_cv.notify_all()
            return
        line = fd.readline()
        if not line:
            # EOF
            del _logging_map[fd]
            _logging_cv.notify_all()
            return
        line = line.strip()
        if line:
            logger.log(log_level, line)


def _prune_logging_fds():
    """Prune the current file descriptors under _logging_map.

    This function will iterate over the logging map and check for closed file
    descriptors. Every closed file descriptor will be removed.
    """
    with _logging_cv:
        remove = [fd for fd in _logging_map.keys() if fd.closed]
        for fd in remove:
            del _logging_map[fd]
        if remove:
            _logging_cv.notify_all()


def _logging_loop():
    """The primary logging thread loop.

    This is the entry point of the logging thread. It will listen for (1) any
    new data on the output file descriptors that were added via log_output and
    (2) any new file descriptors being added by log_output. Once a file
    descriptor is ready to be read, this function will call _log_fd to perform
    the actual read and logging.
    """
    while True:
        with _logging_cv:
            _logging_cv.wait_for(lambda: _logging_map)
            keys = list(_logging_map.keys()) + [_logging_interrupt_pipe[0]]
        try:
            fds, _, _ = select.select(keys, [], [])
        except ValueError:
            # One of the file descriptors must be closed, prune them and try
            # again.
            _prune_logging_fds()
            continue
        if _logging_interrupt_pipe[0] in fds:
            # We got a dummy byte sent by log_output, this is a signal used to
            # break out of the blocking select.select call to tell us that the
            # file descriptor set has changed. We just need to read the byte and
            # remove this descriptor from the list. If we actually have data
            # that should be read it will be read in the for loop below.
            os.read(_logging_interrupt_pipe[0], 1)
            fds.remove(_logging_interrupt_pipe[0])
        for fd in fds:
            _log_fd(fd)


_logging_thread = threading.Thread(target=_logging_loop, daemon=True)


def log_output(logger, log_level, file_descriptor):
    """Log the output from the given file descriptor.

    Args:
        logger: The logger object to use.
        log_level: The logging level to use.
        file_descriptor: The file descriptor to read from.
    """
    with _logging_cv:
        if not _logging_thread.is_alive():
            _logging_thread.start()
        _logging_map[file_descriptor] = (logger, log_level)
        # Write a dummy byte to the pipe to break the select so we can add the
        # new fd.
        os.write(_logging_interrupt_pipe[1], b'x')
        # Notify the condition so we can run the select on the current fds.
        _logging_cv.notify_all()


def wait_for_log_end():
    """Wait for all the logs to be printed.

    This method will block execution until all the logs have been flushed out.
    """
    with _logging_cv:
        _logging_cv.wait_for(lambda: not _logging_map)


class Executor:
    """Parallel executor helper class.

    This class is used to run multiple functions in parallel. The functions MUST
    return an integer result code (or throw an exception). This class will start
    a thread per operation and wait() for all the threads to resolve. If
    fail_fast is set to True, then not all threads must return before wait()
    returns. Instead either ALL threads must return 0 OR any thread must return
    a non zero result (or throw an exception).

    Attributes:
        fail_fast: Whether or not the first function's error code should
         terminate the executor.
        lock: The condition variable used to synchronize across threads.
        threads: A list of threading.Thread objects currently under this
         Executor.
        results: A list of result codes returned by each of the functions called
         by this Executor.
    """
    def __init__(self, fail_fast):
        self.fail_fast = fail_fast
        self.lock = threading.Condition()
        self.threads = []
        self.results = []
        self.logger = logging.getLogger(self.__class__.__name__)

    def append(self, func):
        """Append the given function to the wait list.

        Once added, the function's return value will be used to determine the
        Executor's final result value. The function must return an int result
        code or throw an exception. For example: If two functions were added
        to the Executor, they will both be run in parallel and their results
        will determine whether or not the Executor succeeded. If both functions
        returned 0, then the Executor's wait function will also return 0.

        Args:
            func: A function which returns an int result code or throws an
             exception.
        """
        with self.lock:
            thread = threading.Thread(target=lambda: self._run_fn(func),
                                      daemon=True)
            thread.start()
            self.threads.append(thread)

    def wait(self):
        """Wait for a result to be available.

        This function waits for the executor to resolve. Being resolved depends on
        the initial fail_fast setting.
        - If fail_fast is True then the executor is resolved as soon as any thread
          throws an exception or returns a non-zero result. Or, all the threads
          returned a zero result code.
        - If fail_fast is False, then all the threads must have returned a result
          code or have thrown.

        Returns:
            An integer result code of either the first failed function or 0 if
            they all succeeded.
        """
        with self.lock:
            self.lock.wait_for(predicate=lambda: self._is_finished)
            return self._result

    def _run_fn(self, func):
        """Entry point to each running thread.

        This function will run the function provided in the append() function.
        The result value of the function will be used to determine the
        Executor's result value. If the function throws any exception it will be
        caught and -1 will be used as the assumed result value.

        Args:
            func: The function to run.
        """
        try:
            result = func()
        except Exception as ex:
            self.logger.exception(ex)
            result = -1
        with self.lock:
            self.results.append(result)
            self.lock.notify_all()

    @property
    def _is_finished(self):
        """Whether or not the Executor is considered to be done.

        Returns:
            True if the Executor is considered done.
        """
        if len(self.threads) == len(self.results):
            return True
        return self.fail_fast and any([result for result in self.results])

    @property
    def _result(self):
        """The result code of the Executor.

        Note that _is_finished must be True for this to have any meaning.

        Returns:
            An int representing the result value of the underlying functions.
        """
        return next((result for result in self.results if result), 0)
