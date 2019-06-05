# Common Mocks
This directory holds mock implementations for use in fuzzers and tests.

Each mock is given some friendly build name, like ROLLBACK or FP_SENSOR.
This name is defined in [common/mock/build.mk](build.mk) and referenced
from unit tests and fuzzers' `.mocklist` file.

## Creating a new mock

* Add the mock source to [common/mock](/common/mock) and the
  optional header file to [include/mock](/include/mock).
  Header files are only necessary if you want to expose additional
  mock control functions/variables.
* Add an new entry in [common/mock/build.mk](build.mk)
  that is conditioned on your mock's name.

If a unit test or fuzzer requests this mock, the build system will
set the variable `HAS_MOCK_<BUILD_NAME>` to `y` at build time.
This variable is used to conditionally include the the mock source
in [common/mock/build.mk](build.mk).

Example line from [common/mock/build.mk](build.mk):

```make
# Mocks
mock-$(HAS_MOCK_ROLLBACK) += mock/rollback_mock.o
```

## Using a mock
Unit tests and fuzzers can request a particular mock by adding an entry to
their `.mocklist` file. The mocklist file is similar to a `.tasklist`
file, where it is named according to the test/fuzz's name followed by
`.mocklist`, like `fpsensor.mocklist`.
The mocklist file is optional, so you may need to create one.

Example `.mocklist`:

```c
/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

 #define CONFIG_TEST_MOCK_LIST \
	MOCK(ROLLBACK)         \
	MOCK(FPSENSOR)
```

If you need additional mock control functionality, you may need to include
the mock's header file, which is prepended with `mock/`.

For example, to control the return values of the rollback mock:

```c
#include "mock/rollback_mock.h"

void somefunc() {
	mock_ctrl_rollback.get_secret_fail = true;
}
```