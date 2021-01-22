# Porting EC unit tests to Ztest

[TOC]

This HOWTO shows the process for porting the EC's `base32` unit test to
Zephyr's Ztest framework. All of the work is done in `src/platform/ec`.

See [Test Framework - Zephyr Project Documentation](https://docs.zephyrproject.org/1.12.0/subsystems/test/ztest.html#quick-start-unit-testing) for details about Zephyr's Ztest framework.


For examples of porting an EC unit test to the Ztest API, see:
* [base32](https://crrev.com/c/2492527) and [improvements](https://crrev.com/c/2634401)
* [accel_cal](https://crrev.com/c/2645198)

## Determine source files being tested

Determine which C files the unit test requires by finding the test in
`test/test_config.h`:
```
#ifdef TEST_BASE32
#define CONFIG_BASE32
#endif
```
Locate the `CONFIG` item(s) in `common/build.mk`:
```
common-$(CONFIG_BASE32)+=base32.o
```
So for the `base32` test, we only need to shim `common/base32.c`.

Add the C files to `zephyr/shim/CMakeLists.txt`, in the "Shimmed modules"
section:

```
# Shimmed modules
zephyr_sources_ifdef(CONFIG_PLATFORM_EC "${PLATFORM_EC}/common/base32.c")
```

Refer to [zephyr: shim in base32.c](https://crrev.com/c/2468631).

## Create test directory

Create a new directory for the unit test in `zephyr/test/base32`.

Create `zephyr/test/base32/prj.conf` with these contents:
```
CONFIG_ZTEST=y
CONFIG_PLATFORM_EC=y
```

Create `zephyr/test/base32/CMakeLists.txt` with these contents:
```
cmake_minimum_required(VERSION 3.13.1)
project(base32)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

target_sources(app PRIVATE ${PLATFORM_EC}/test/base32.c)
```

## Modify test source code

### Test cases
In the unit test, replace `run_test` with `TEST_MAIN()`. This will allow both
platform/ec tests and Ztests to share the same entry point.

Change `RUN_TEST` to `ztest_unit_test` and add the `ztest_test_suite` wrapper
plus the call to `ztest_run_test_suite`.

```
/*
 * Define the test cases to run. We need to do this twice, once in the format
 * that Ztest uses, and again in the format the the EC test framework uses.
 * If you add a test to one of them, make sure to add it to the other.
 */
TEST_MAIN()
{
	ztest_test_suite(test_base32_lib,
			 ztest_unit_test(test_crc5),
			 ztest_unit_test(test_encode),
			 ztest_unit_test(test_decode));
	ztest_run_test_suite(test_base32_lib);
}
```

Each function that is called by `ztest_unit_test` needs to be declared using
`DECLARE_EC_TEST`. Keep the `return EC_SUCCESS;` at the end
of the test function. Note that for the EC build, `TEST_MAIN` will call
`test_reset` before running the test cases, and `test_print_result` after.

### Assert macros
Change the `TEST_ASSERT` macros to `zassert` macros. There are plans to
automate this process, but for now, it's a manual process involving some
intelligent find-and-replace.

* `TEST_ASSERT(n)` to `zassert_true(n, NULL)`
* `TEST_EQ(a, b, fmt)` to `zassert_equal(a, b, fmt ## ", " ## fmt, a, b)`
  * e.g. `TEST_EQ(a, b, "%d")` becomes `zassert_equal(a, b, "%d, %d", a, b)`
* `TEST_NE(a, b, fmt)` to `zassert_not_equal(a, b, fmt ## ", " ## fmt, a, b)`
* `TEST_LT(a, b, fmt)` to `zassert_true(a < b, fmt ## ", " ## fmt, a, b)`
* `TEST_LE(a, b, fmt)` to `zassert_true(a <= b, fmt ## ", " ## fmt, a, b)`
* `TEST_GT(a, b, fmt)` to `zassert_true(a > b, fmt ## ", " ## fmt, a, b)`
* `TEST_GE(a, b, fmt)` tp `zassert_true(a >= b, fmt ## ", " ## fmt, a, b)`
* `TEST_BITS_SET(a, bits)` to `zassert_true(a & (int)bits == (int)bits, "%u, %u", a & (int)bits, (int)bits)`
* `TEST_BITS_CLEARED(a, bits)` to `zassert_true(a & (int)bits == 0, "%u, 0", a & (int)bits)`
* `TEST_ASSERT_ARRAY_EQ(s, d, n)` to `zassert_mem_equal(s, d, b, NULL)`
* `TEST_CHECK(n)` to `zassert_true(n, NULL)`
* `TEST_NEAR(a, b, epsilon, fmt)` to `zassert_within(a, b, epsilon, fmt, a)`
  * Currently, every usage of `TEST_NEAR` involves floating point values
* `TEST_ASSERT_ABS_LESS(n, t)` to `zassert_true(abs(n) < t, "%d, %d", n, t)`
  * Currently, every usage of `TEST_ASSERT_ANS_LESS` involves signed integers.

There isn't a good replacement for `TEST_ASSERT_MEMSET(d, c, n)`, but it is
only used in two tests, `printf.c` and `utils.c`. If you need this test,
you'll need to code up a loop over the `n` bytes starting at `d`, and
`zassert_equal` that each byte is equal to `c`.

Also note that some tests use constructs like `TEST_ASSERT(var == const)`,
which would have been better write as `TEST_EQ(var, const)`. These should be
rewritten to use `zassert_equal`.

Refer to
[test: Allow EC unit test to use Ztest API](https://crrev.com/c/2492527) for
the changes to the base32.c source code.

### Tasklist

For any test that has a corresponding `${TESTNAME}.tasklist`, add the file
`shimmed_test_tasks.h` in the zephyr test directory, and in that file,
`#include` the tasklist file. See [accel_cal](https://crrev.com/c/2645198)
for an example.

Add `CONFIG_HAS_TEST_TASKS=y` to the `prj.conf` file, as well as the appropriate
`CONFIG_PLATFORM_EC` defines to include or exclude code that the unit under
test uses.

## Build and run

Use `zmake` to build and run the test:
```
(cr) $ zmake -l DEBUG configure --test -B build/ztest/base32 zephyr/test/base32
...
UART_0 connected to pseudotty: /dev/pts/1
*** Booting Zephyr OS build zephyr-v2.4.0-1-g63b2330a85cd  ***
Running test suite test_base32_lib
===================================================================
START - test_crc5
 PASS - test_crc5
===================================================================
START - test_encode
 PASS - test_encode
===================================================================
START - test_decode
 PASS - test_decode
===================================================================
Test suite test_base32_lib succeeded
===================================================================
PROJECT EXECUTION SUCCESSFUL
(cr) $
```

