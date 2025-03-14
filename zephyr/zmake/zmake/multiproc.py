# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Zmake multiprocessing utility module.

This module is used to aid in zmake's multiprocessing. It contains tools
available to log output from multiple processes on the fly. This means that a
process does not need to finish before the output is available to the developer
on the screen.
"""

import collections
import io
import logging
import os
import select
import threading
from typing import Any, ClassVar, Dict, List


class LogWriter:
    """Contains information about a file descriptor that is producing output

    There is typically one of these for each file descriptor that a process is
    writing to while running (stdout and stderr).

    Properties:
        _logger: The logger object to use.
        _log_level: The logging level to use.
        _override_func: A function used to override the log level. The
            function will be called once per line prior to logging and will be
            passed the arguments of the line and the default log level.
        _written_at_level: dict:
            key: log_level
            value: True if output was written at that level
        _job_id: The name to prepend to logged lines
        _file_descriptor: The file descriptor being logged.
    """

    # A local pipe use to signal the look that a new file descriptor was added and
    # should be included in the select statement.
    _logging_interrupt_pipe = []

    # A condition variable used to synchronize logging operations.
    _logging_cv = threading.Condition()
    # A map of file descriptors to their LogWriter
    _logging_map: ClassVar[Dict[io.TextIOBase, "LogWriter"]] = {}
    # The thread that handles the reading from pipes and writing to log.
    _logging_thread = None

    # Should we log job names or not
    _log_job_names = False

    @classmethod
    def reset(cls):
        """Reset this module to its starting state (useful for tests)"""
        with LogWriter._logging_cv:
            LogWriter._logging_map.clear()
            if len(LogWriter._logging_interrupt_pipe) > 1:
                os.write(LogWriter._logging_interrupt_pipe[1], b"x")
            else:
                cls._logging_interrupt_pipe = os.pipe()
            LogWriter._logging_thread = None
            LogWriter._logging_cv.notify_all()

    @classmethod
    def set_job_name_logging(cls, enable: bool):
        """Controls if log messages are prepended with their job names"""
        cls._log_job_names = enable

    def __init__(
        self,
        logger,
        log_level,
        log_level_override_func,
        job_id,
        file_descriptor,
        tee_output=None,
    ):
        self._logger = logger
        self._log_level = log_level
        self._override_func = log_level_override_func
        # A map whether output was printed at each logging level
        self._written_at_level = collections.defaultdict(lambda: False)
        self._job_id = job_id
        self._file_descriptor = file_descriptor
        self._tee_output = tee_output

    def log_line(self, line):
        """Log a line of output

        If the log-level override function requests a change in log level, that
        causes self._log_level to be updated accordingly.

        Args:
            line: Text line to log
        """
        if self._tee_output:
            self._tee_output.write(line)
            self._tee_output.write("\n")
        if self._override_func:
            # Get the new log level and update the default. The reason we
            # want to update the default is that if we hit an error, all
            # future logging should be moved to the new logging level. This
            # greatly simplifies the logic that is needed to update the log
            # level.
            self._log_level = self._override_func(line, self._log_level)
        if self._job_id and LogWriter._log_job_names:
            self._logger.log(self._log_level, "[%s] %s", self._job_id, line)
        else:
            self._logger.log(self._log_level, line)
        self._written_at_level[self._log_level] = True

    def has_written(self, log_level):
        """Check if output was written at a certain log level

        Args:
            log_level: log level to check

        Returns:
            True if any output was written at that log level, False if not
        """
        return self._written_at_level[log_level]

    def wait(self):
        """Wait for this LogWriter to finish.

        This method will block execution until all the logs have been flushed out.
        """
        with LogWriter._logging_cv:
            LogWriter._logging_cv.wait_for(
                lambda: self._file_descriptor not in LogWriter._logging_map
            )
        if self._tee_output:
            self._tee_output.close()
            self._tee_output = None

    @classmethod
    def _log_fd(cls, file_descriptor: io.TextIOBase):
        """Log information from a single file descriptor.

        This function is BLOCKING. It will read from the given file descriptor until
        either the end of line is read or EOF. Once EOF is read it will remove the
        file descriptor from _logging_map so it will no longer be used.
        Additionally, in some cases, the file descriptor will be closed (caused by
        a call to Popen.wait()). In these cases, the file descriptor will also be
        removed from the map as it is no longer valid.
        """
        with LogWriter._logging_cv:
            if file_descriptor in LogWriter._logging_map:
                writer = LogWriter._logging_map[file_descriptor]
                if file_descriptor.closed:
                    del LogWriter._logging_map[file_descriptor]
                    LogWriter._logging_cv.notify_all()
                    return
                line = file_descriptor.readline()
                if not line:
                    # EOF
                    del LogWriter._logging_map[file_descriptor]
                    LogWriter._logging_cv.notify_all()
                    return
                line = line.rstrip("\n")
                if line:
                    writer.log_line(line)

    @classmethod
    def _prune_logging_fds(cls):
        """Prune the current file descriptors under _logging_map.

        This function will iterate over the logging map and check for closed file
        descriptors. Every closed file descriptor will be removed.
        """
        with LogWriter._logging_cv:
            remove = [
                file_descriptor
                for file_descriptor in LogWriter._logging_map
                if file_descriptor.closed
            ]
            for file_descriptor in remove:
                del LogWriter._logging_map[file_descriptor]
            if remove:
                LogWriter._logging_cv.notify_all()

    @classmethod
    def _logging_loop(cls):
        """The primary logging thread loop.

        This is the entry point of the logging thread. It will listen for (1) any
        new data on the output file descriptors that were added via log_output() and
        (2) any new file descriptors being added by log_output(). Once a file
        descriptor is ready to be read, this function will call _log_fd to perform
        the actual read and logging.
        """
        while True:
            with LogWriter._logging_cv:
                LogWriter._logging_cv.wait_for(lambda: LogWriter._logging_map)
                keys: List[Any] = list(LogWriter._logging_map.keys())
                keys.append(LogWriter._logging_interrupt_pipe[0])
            try:
                fds, _, _ = select.select(keys, [], [])
            except ValueError:
                # One of the file descriptors must be closed, prune them and try
                # again.
                LogWriter._prune_logging_fds()
                continue
            if LogWriter._logging_interrupt_pipe[0] in fds:
                # We got a sentinel byte sent by log_output(), this is a signal used to
                # break out of the blocking select.select call to tell us that the
                # file descriptor set has changed. We just need to read the byte and
                # remove this descriptor from the list. If we actually have data
                # that should be read it will be read in the for loop below.
                os.read(LogWriter._logging_interrupt_pipe[0], 1)
                fds.remove(LogWriter._logging_interrupt_pipe[0])
            for file in fds:
                LogWriter._log_fd(file)

    @classmethod
    def log_output(
        cls,
        logger,
        log_level,
        file_descriptor,
        log_level_override_func=None,
        job_id=None,
        tee_output=None,
    ):
        """Log the output from the given file descriptor.

        Args:
            logger: The logger object to use.
            log_level: The logging level to use.
            file_descriptor: The file descriptor to read from.
            log_level_override_func: A function used to override the log level. The
            function will be called once per line prior to logging and will be
            passed the arguments of the line and the default log level.

        Returns:
            LogWriter object for the resulting output
        """
        with LogWriter._logging_cv:
            if (
                LogWriter._logging_thread is None
                or not LogWriter._logging_thread.is_alive()
            ):
                # First pass or thread must have died, create a new one.
                LogWriter._logging_thread = threading.Thread(
                    target=LogWriter._logging_loop, daemon=True
                )
                LogWriter._logging_thread.start()

            writer = LogWriter(
                logger,
                log_level,
                log_level_override_func,
                job_id,
                file_descriptor,
                tee_output=tee_output,
            )
            LogWriter._logging_map[file_descriptor] = writer
            # Write a sentinel byte to the pipe to break the select so we can add the
            # new fd.
            os.write(LogWriter._logging_interrupt_pipe[1], b"x")
            # Notify the condition so we can run the select on the current fds.
            LogWriter._logging_cv.notify_all()
        return writer

    @classmethod
    def wait_for_log_end(cls):
        """Wait for all the logs to be printed.

        This method will block execution until all the logs have been flushed out.
        """
        with LogWriter._logging_cv:
            LogWriter._logging_cv.wait_for(lambda: not LogWriter._logging_map)


class ThreadWithResult(threading.Thread):
    """Subclass to create a thread and return the result from the target"""

    def __init__(
        self,
        group=None,
        target=None,
        name=None,
        args=(),
        kwargs=None,
        *,
        daemon=None
    ):
        threading.Thread.__init__(
            self,
            group=group,
            target=target,
            name=name,
            args=args,
            kwargs=kwargs,
            daemon=daemon,
        )
        self._return = None

    def run(self):
        if self._target is not None:
            self._return = self._target(*self._args, **self._kwargs)

    def join(self, timeout=None):
        threading.Thread.join(self, timeout=timeout)
        return self._return


class Executor:
    """Parallel executor helper class.

    This class is used to run multiple functions in parallel. The functions MUST
    return an integer result code (or throw an exception). This class will start
    a thread per operation and wait() for all the threads to resolve.

    Attributes:
        lock: The condition variable used to synchronize across threads.
        threads: A list of threading.Thread objects currently under this
         Executor.
        results: A list of result codes returned by each of the functions called
         by this Executor.
    """

    def __init__(self):
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

        Returns:
            A join function which will wait until this task is finished.
        """
        with self.lock:
            thread = ThreadWithResult(
                target=lambda: self._run_fn(func), daemon=True
            )
            thread.start()
            self.threads.append(thread)
            return thread.join

    def wait(self):
        """Wait for a result to be available.

        This function waits for the executor to resolve (i.e., all
        threads have finished).

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
        caught and 1 will be used as the assumed result value.

        Args:
            func: The function to run.
        """
        try:
            result = func()
        except Exception as e:  # pylint: disable=broad-except
            self.logger.exception(e)
            result = 1
        with self.lock:
            self.results.append(result)
            self.lock.notify_all()

        return result

    @property
    def _is_finished(self):
        """Whether or not the Executor is considered to be done.

        Returns:
            True if the Executor is considered done.
        """
        if len(self.threads) == len(self.results):
            return True
        return False

    @property
    def _result(self):
        """The result code of the Executor.

        Note that _is_finished must be True for this to have any meaning.

        Returns:
            An int representing the result value of the underlying functions.
        """
        return next((result for result in self.results if result), 0)
