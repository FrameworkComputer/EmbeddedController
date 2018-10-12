# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This is a shim library for the ec3po transition from subprocesses to threads.

This is necessary because ec3po is split between the platform/ec/ and
third_party/hdctools/ repositories, so the transition cannot happen atomically
in one change.  See http://b/79684405 #39.

This contains only the multiprocessing objects or threading-oriented equivalents
that are actually in use by ec3po.  There is no need for further functionality,
because this shim will be deleted after the migration is complete.

TODO(b/79684405): After both platform/ec/ and third_party/hdctools/ sides of
ec3po have been updated to use this library, replace the multiprocessing
implementations with threading-oriented equivalents.

TODO(b/79684405): After this library has been updated to contain
threading-oriented equivalents to its original multiprocessing implementations,
and some reasonable amount of time has elapsed for thread-based ec3po problems
to be discovered, migrate both the platform/ec/ and third_party/hdctools/ sides
of ec3po off of this shim and then delete this file.
"""

from multiprocessing import Pipe
from multiprocessing import Process as ThreadOrProcess
from multiprocessing import Queue
from multiprocessing import Value
