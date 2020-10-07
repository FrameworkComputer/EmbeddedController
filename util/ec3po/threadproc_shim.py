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

TODO(b/79684405): Stop using multiprocessing.Pipe.  The
multiprocessing.Connection objects it returns serialize and deserialize objects
(via Python pickling), which is necessary for sending them between processes,
but is unnecessary overhead between threads.  This will not be a simple change,
because the ec3po Console and Interpreter classes use the underlying pipe/socket
pairs with select/poll/epoll alongside other file descriptors.  A drop-in
replacement would be non-trivial and add undesirable complexity.  The correct
solution will be to split off the polling of the pipes/queues from this module
into separate threads, so that they can be transitioned to another form of
cross-thread synchronization, e.g. directly waiting on queue.Queue.get() or a
lower-level thread synchronization primitive.

TODO(b/79684405): After this library has been updated to contain
threading-oriented equivalents to its original multiprocessing implementations,
and some reasonable amount of time has elapsed for thread-based ec3po problems
to be discovered, migrate both the platform/ec/ and third_party/hdctools/ sides
of ec3po off of this shim and then delete this file.  IMPORTANT: This should
wait until after completing the TODO above to stop using multiprocessing.Pipe!
"""

# Imports to bring objects into this namespace for users of this module.
from multiprocessing import Pipe
from six.moves.queue import Queue
from threading import Thread as ThreadOrProcess

# True if this module has ec3po using subprocesses, False if using threads.
USING_SUBPROCS = False


def _DoNothing():
  """Do-nothing function for use as a callback with DoIf()."""


def DoIf(subprocs=_DoNothing, threads=_DoNothing):
  """Return a callback or not based on ec3po use of subprocesses or threads.

  Args:
    subprocs: callback that does not require any args - This will be returned
        (not called!) if and only if ec3po is using subprocesses.  This is
        OPTIONAL, the default value is a do-nothing callback that returns None.
    threads: callback that does not require any args - This will be returned
        (not called!) if and only if ec3po is using threads.  This is OPTIONAL,
        the default value is a do-nothing callback that returns None.

  Returns:
    Either the subprocs or threads argument will be returned.
  """
  return subprocs if USING_SUBPROCS else threads


def Value(ctype, *args):
  return ctype(*args)
