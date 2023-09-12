# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test jobserver functionality."""

from asyncio import subprocess
import logging
import os
import threading

# pylint: disable=import-error
import pytest
import zmake.jobserver


def _do_test_jobserver(
    jobs, commandline_jobs=0, use_client=False, open_pipe=True
):
    """Test a jobserver configured with a specified number of jobs."""

    effective_jobs = jobs
    pipe = None
    if use_client:
        makeflags = f" -j{jobs}"
        if jobs > 1:
            pipe = os.pipe()
            # GNU make puts one less job in the pipe than allowed, since you
            # get one job "for free" without contacting the jobserver. Or
            # another way of looking at is that make consumes one slot for you
            # before calling your job just in case you aren't jobserver aware.
            os.write(pipe[1], b"A" * (jobs - 1))
            os.set_inheritable(pipe[0], True)
            os.set_inheritable(pipe[1], True)
            makeflags += f" --jobserver-auth={pipe[0]},{pipe[1]}"
            if not open_pipe:
                os.close(pipe[0])
                os.close(pipe[1])
                effective_jobs = 1
        jobserver = zmake.jobserver.GNUMakeJobClient.from_environ(
            env={"MAKEFLAGS": makeflags}, jobs=commandline_jobs
        )
    else:
        jobserver = zmake.jobserver.GNUMakeJobServer(jobs=jobs)
        pipe = jobserver._inheritable_pipe  # pylint:disable=protected-access
        if jobs == 1:
            makeflags = " -j1"
        else:
            makeflags = f" -j{jobs} --jobserver-auth={pipe[0]},{pipe[1]}"

    lock = threading.Condition()
    started_threads = 0
    ended_threads = 0
    active_threads = 0
    please_exit = threading.Semaphore(0)
    thread_count = jobs + 5
    if commandline_jobs:
        effective_jobs = commandline_jobs

    def _my_thread():
        nonlocal started_threads
        nonlocal active_threads
        nonlocal ended_threads
        nonlocal pipe

        with lock:
            started_threads += 1
            lock.notify_all()
        with jobserver.get_job():
            with lock:
                active_threads += 1
                lock.notify_all()
            proc = jobserver.popen(
                [
                    "sh",
                    "-c",
                    'echo "MAKEFLAGS=${MAKEFLAGS}"; ls /proc/self/fd',
                ],
                stdout=subprocess.PIPE,
                universal_newlines=True,
            )
            proc.wait()
            output = proc.stdout.readlines()
            assert output[0] == f"MAKEFLAGS={makeflags}\n"
            if pipe:
                if effective_jobs > 1:
                    assert f"{pipe[0]}\n" in output
                    assert f"{pipe[1]}\n" in output
                else:
                    assert f"{pipe[0]}\n" not in output
                    assert f"{pipe[1]}\n" not in output

            please_exit.acquire()  # pylint:disable=consider-using-with
            with lock:
                active_threads -= 1
                ended_threads += 1
                lock.notify_all()

    logging.debug("Starting %s threads", thread_count)
    for _ in range(thread_count):
        threading.Thread(target=_my_thread, daemon=True).start()

    with lock:
        lock.wait_for(
            lambda: started_threads == thread_count
            and active_threads == effective_jobs,
            10,
        )
        logging.debug("Asserting %s active_threads", effective_jobs)
        assert started_threads == thread_count
        assert active_threads == effective_jobs
        assert ended_threads == 0

    logging.debug("Ending %s threads", 5)
    for _ in range(5):
        please_exit.release()

    with lock:
        lock.wait_for(
            lambda: active_threads == effective_jobs and ended_threads == 5, 10
        )
        logging.debug("Asserting %s active_threads", effective_jobs)
        assert started_threads == thread_count
        assert active_threads == effective_jobs
        assert ended_threads == 5

    logging.debug("Ending %s threads", thread_count - 5)
    for _ in range(thread_count - 5):
        please_exit.release()

    with lock:
        lock.wait_for(lambda: ended_threads == thread_count, 10)
        logging.debug("Asserting %s active_threads", 0)
        assert started_threads == thread_count
        assert active_threads == 0
        assert ended_threads == thread_count


def test_jobserver_10():
    """Test a jobserver configured with 10 jobs."""
    _do_test_jobserver(10)


def test_jobserver_2():
    """Test a jobserver configured with 2 jobs."""
    _do_test_jobserver(2)


def test_jobserver_1():
    """Test a jobserver configured with 1 job."""
    _do_test_jobserver(1)


def test_jobclient_10():
    """Test a jobclient configured with 10 jobs."""
    _do_test_jobserver(10, use_client=True)


def test_jobclient_2():
    """Test a jobserver configured with 2 jobs."""
    _do_test_jobserver(2, use_client=True)


def test_jobclient_1():
    """Test a jobserver configured with 1 job."""
    _do_test_jobserver(1, use_client=True)


def test_jobclient_10_j1():
    """Test a jobclient configured with 10 jobs but zmake -j1 was called."""
    _do_test_jobserver(10, commandline_jobs=1, use_client=True)


def test_jobclient_1_j1():
    """Test a jobserver configured with 1 job but zmake -j1 was called."""
    _do_test_jobserver(1, commandline_jobs=1, use_client=True)


def test_jobclient_missing():
    """Test a jobclient with no MAKEFLAGS."""
    jobserver = zmake.jobserver.GNUMakeJobClient.from_environ(env={})
    assert jobserver is None


def test_jobclient_dryrun():
    """Test a jobclient make dryrun in MAKEFLAGS."""
    with pytest.raises(SystemExit) as pytest_wrapped_e:
        zmake.jobserver.GNUMakeJobClient.from_environ(
            env={"MAKEFLAGS": "n -j1"}
        )
    assert pytest_wrapped_e.type == SystemExit
    assert pytest_wrapped_e.value.code == 0


def test_jobclient_10_no_pipes():
    """Test a jobclient configured with 10 jobs but the file descriptors are missing."""
    _do_test_jobserver(10, use_client=True, open_pipe=False)


def test_jobclient_1_no_pipes():
    """Test a jobclient configured with 1 job but the file descriptors are missing."""
    _do_test_jobserver(1, use_client=True, open_pipe=False)
