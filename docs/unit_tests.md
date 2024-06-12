# Unit Tests

Provides an overview of how to write and run the unit tests in the EC codebase.

[TOC]

## Running Unit Tests {#running}

The unit tests run on the host machine using the [`host` board].

List available unit tests:

```bash
(chroot) ~/trunk/src/platform/ec $ make print-host-tests
```

Build and run a specific unit test (the `host_command` test in this example):

```bash
(chroot) ~/trunk/src/platform/ec $ make run-host_command
```

Build and run all unit tests:

```bash
(chroot) ~/trunk/src/platform/ec $ make runhosttests -j
```

## Debugging Unit Tests

You need the host version of gdb:

```bash
(chroot) sudo emerge -j sys-devel/gdb
```

Then run gdb on the specific test you want to debug (the `host_command` test in this example):

```
(chroot) gdb build/host/host_command/host_command.exe
handle SIGUSR1 noprint nostop
break test_hostcmd_ok
run
```

## Writing Unit Tests

Unit tests live in the [`test`] subdirectory of the CrOS EC codebase.

All new unit tests should be written to use the Zephyr Ztest
[API](https://docs.zephyrproject.org/latest/guides/test/ztest.html). If you are
making significant changes to an existing test, you should also look at porting
the test from the EC test API to the Ztest API.

Using the Ztest API makes the unit tests suitable for submitting upstream to the
Zephyr project, and reduces the porting work when the EC transitions to the
Zephyr RTOS.

### File headers

Include [`test_util.h`] and any other required includes. In this example, the
function being tested is defined in the test, but a real unit test would include
the header file for the module that defines `some_function`.

`test/my_test.c`:

```c
#include <stdbool.h>
#include "test_util.h"

static bool some_function(void)
{
    return true;
}
```

[`test_util.h`] includes `ztest.h` if `CONFIG_ZEPHYR` is defined, or defines a
mapping from the `zassert` macros to the EC `TEST_ASSERT` macros if
`CONFIG_ZEPHYR` is not defined.

### Test cases

Define the test cases. Use the `EC_TEST_RETURN` return type on these functions.

```c
/* Write a function with the following signature: */
test_static EC_TEST_RETURN test_my_function(void)
{
    /* Run some code */
    bool condition = some_function();

    /* Check that the expected condition is correct. */
    zassert_true(condition);

    return EC_SUCCESS;
}
```

`test/my_test.c`:

```c
/* Write a function with the following signature: */
test_static EC_TEST_RETURN test_my_function(void)
{
    /* Run some code */
    bool condition = some_function();

    /* Check that the expected condition is correct. */
    TEST_EQ(condition, true, "%d");

    return EC_SUCCESS;
}
```

The only difference between those two versions of `test/my_test.c` is the
assertion: `c zassert_true(condition);` versus `c TEST_EQ(condition, true,
"%d");`

### Specify the test cases to run

The EC test API enumerates the test cases using `RUN_TEST` in the `run_test`
function, while the Ztest API enumerates the test cases using `ztest_unit_test`
inside another macro for the test suite, inside of `test_main`.

`test/my_test.c`:

```c
#ifdef CONFIG_ZEPHYR
void test_main(void)
{
    ztest_test_suite(test_my_unit,
             ztest_unit_test(test_my_function));
    ztest_run_test_suite(test_my_unit);
}
#else
/* The test framework will call the function named "run_test" */
void run_test(int argc, char **argv)
{
    /* Each unit test can be run using the RUN_TEST macro: */
    RUN_TEST(test_my_function);

    /* Report the results of all the tests at the end. */
    test_print_result();
}
#endif /* CONFIG_ZEPHYR */
```

### Task List

EC unit tests can run additional tasks besides the main test thread. The EC unit
test implementation provides a phtreads-based implementation of the EC task API.
We do not yet support running additional tasks in Ztest-based tests.

In the [`test`] subdirectory, create a `tasklist` file for your test that lists
the tasks that should run as part of the test:

`test/my_test.tasklist`:

```c
/*
 * No test task in this case, but you can use `TASK_TEST` macro to specify one.
 */
#define CONFIG_TEST_TASK_LIST
```

### Makefile

Add the test to the `Makefile` so that it can build as an EC unit test:

`test/build.mk`:

```Makefile
test-list-host += my_test
```

and

```Makefile
my_test-y=my_test.o
```

Make sure you test shows up in the "host" tests:

```bash
(chroot) $ make print-host-tests | grep my_test
host-my_test
run-my_test
```
### Test Config File

Add any test-specific configurations to the [test_config.h](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/HEAD:src/platform/ec/test/test_config.h) file:

```c
#ifdef TEST_<my_test>
/*
 * Add test-specific configurations here.
 */
#endif
```

### Build and Run

Build and run the test as an EC unit test:

```bash
(chroot) $ make run-my_test
```

For building the test as a Zephyr Ztest unit test, follow the instructions in
[Porting EC unit tests to Ztest](./zephyr/ztest.md) to build the unit test for Zephyr's
"native_sim" host-based target.

<!-- mdformat off(b/139308852) -->
*** note
**TIP**: Unit tests should be independent from each other as much as possible.
This keeps the test (and any system state) simple to reason about and also
allows running unit tests in parallel. You can use the
[`before_test` hook][`test_util.h`] to reset the state before each test is run.
***
<!-- mdformat on -->

## Mocks

We do not yet support mocks for Zephyr Ztest-based tests. [Mocks][`mock`] enable
you to simulate behavior for parts of the system that you're not directly
testing. They can also be useful for testing specific edge cases that are hard
to exercise during normal use (e.g., error conditions).

See the [Mock README] for details.

### Mock Time

When writing unit tests that rely on a clock, it's best not to rely on a real
hardware clock. It's very difficult to enforce exact timing with a real clock,
which leads to test flakiness (and developers ignoring tests since they're flaky
). Instead, use the [Mock Timer] to adjust the time during the test.

[`mock`]: /include/mock
[Mock Timer]: /include/mock/timer_mock.h
[`test`]: /test
[`host` board]: /board/host/
[`test_util.h`]: /include/test_util.h
[Mock README]: /common/mock/README.md
