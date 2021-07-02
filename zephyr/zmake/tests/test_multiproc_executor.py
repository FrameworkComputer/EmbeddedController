# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import threading

import zmake.multiproc


def test_single_function_executor_success():
    executor = zmake.multiproc.Executor()
    executor.append(lambda: 0)
    assert executor.wait() == 0


def test_single_function_executor_fail():
    executor = zmake.multiproc.Executor()
    executor.append(lambda: -2)
    assert executor.wait() == -2


def test_single_function_executor_raise():
    executor = zmake.multiproc.Executor()
    executor.append(lambda: 1 / 0)
    assert executor.wait() != 0


def _lock_step(cv, predicate, step, return_value=0):
    with cv:
        cv.wait_for(predicate=lambda: step[0] == predicate)
        step[0] += 1
        cv.notify_all()
    return return_value


def test_two_function_executor_wait_for_both():
    cv = threading.Condition()
    step = [0]
    executor = zmake.multiproc.Executor()
    executor.append(lambda: _lock_step(cv=cv, predicate=0, step=step))
    executor.append(lambda: _lock_step(cv=cv, predicate=1, step=step))
    assert executor.wait() == 0
    assert step[0] == 2


def test_two_function_executor_one_fails():
    cv = threading.Condition()
    step = [0]
    executor = zmake.multiproc.Executor()
    executor.append(lambda: _lock_step(cv=cv, predicate=0, step=step, return_value=-1))
    executor.append(lambda: _lock_step(cv=cv, predicate=1, step=step))
    assert executor.wait() == -1
    assert step[0] == 2
