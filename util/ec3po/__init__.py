# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The EC console interpreter.

EC-3PO is a console interpreter which migrates the rich debug console from the
EC itself to the host.  This allows for a rich debug console without impacting
EC image sizes while also allowing the development of new console features.

The package consists of two modules: console and interpreter.  The console
module provides the interactive console interface between the user and the
interpreter.  It handles the presentation of the EC console including editing
methods as well as session-persistent command history.

The interpreter module provides the interpretation layer between the EC UART and
the user.  The user does not necessarily have to be the interactive console, but
could be something like autotest.  The interpreter is also responsible for the
automatic command retrying if the EC drops a character in a command.  This is a
stopgap until all commands are communicated via host commands.
"""

import console
import interpreter
import threadproc_shim
