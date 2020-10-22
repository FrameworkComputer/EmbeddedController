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

## Writing Unit Tests

Unit tests live in the [`test`] subdirectory of the CrOS EC codebase.

Existing EC unit tests will use the EC Test API, including test-related macros
(e.g., `TEST_EQ`, `TEST_NE`) and functions defined in [`test_util.h`]. Note
the `EC_TEST_RETURN` return type on the functions that are test cases.

`test/my_test.c`:

```c
#include <stdbool.h>
#include "test_util.h"

static bool some_function(void)
{
    return true;
}

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

New unit tests or significant changes to existing tests should use the Zephyr
Ztest [API](https://docs.zephyrproject.org/latest/guides/test/ztest.html).

`test/my_test.c`:

```c
#include <stdbool.h>
#include "test_util.h"

static bool some_function(void)
{
    return true;
}

/* Write a function with the following signature: */
test_static EC_TEST_RETURN test_my_function(void)
{
    /* Run some code */
    bool condition = some_function();

    /* Check that the expected condition is correct. */
    zassert_true(condition, NULL);

    return EC_SUCCESS;
}
```

Note that the only difference between those two versions of `test/my_test.c`
is the assertion:
```c
    TEST_EQ(condition, true, "%d");
```
versus
```c
    zassert_true(condition, NULL);
```

Currently, these tests using the Ztest API are still built with the EC test
framework. [`test_util.h`] defines a mapping from the `zassert` macros to the
EC `TEST_ASSERT` macros when `CONFIG_ZEPHYR` is not `#define`'d.

Even though the tests are currently compiled only to the EC test framework,
developers should still target the Ztest API for new unit tests. Future work
will support building directly with the Ztest API. This makes the unit tests
suitable for submitting upstream to the Zephyr project, and reduces the
porting work when the EC transitions to the Zephyr RTOS. Similarly, when
a development makes significant modifications to an existing unit test, they
should consider porting the test to the Ztest API as part of the modifications.

See [chromium:2492527](https://crrev.com/c/2492527) for a simple example of
porting an EC unit test to the Ztest API.

`test/my_test.c`:

The EC test API enumerates the test cases using `RUN_TEST` in the `run_test`
function, while the Ztest API enumerates the test cases using `ztest_unit_test`
inside another macro for the test suite, inside of `test_main`.

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

In the [`test`] subdirectory, create a `tasklist` file for your test that lists
the tasks that should run as part of the test:

`test/my_test.tasklist`:

```c
/*
 * No test task in this case, but you can use `TASK_TEST` macro to specify one.
 */
#define CONFIG_TEST_TASK_LIST
```

Add the test to the `Makefile`:

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

Build and run the test:

```bash
(chroot) $ make run-my_test
```

*** note
**TIP**: Unit tests should be independent from each other as much as possible.
This keeps the test (and any system state) simple to reason about and also
allows running unit tests in parallel. You can use the
[`before_test` hook][`test_util.h`] to reset the state before each test is run.
***

## Mocks

[Mocks][`mock`] enable you to simulate behavior for parts of the system that
you're not directly testing. They can also be useful for testing specific edge
cases that are hard to exercise during normal use (e.g., error conditions).

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
