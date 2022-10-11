# Zephyr Testing

[TOC]

Before starting, developers should read the following:
- [Zephyr Test Framework](https://docs.zephyrproject.org/latest/develop/test/ztest.html)
- [Zephyr Test Runner (Twister)](https://docs.zephyrproject.org/latest/develop/test/twister.html)

## Getting help

Get your tests reviewed by adding [zephyr-test-eng@google.com](mailto:zephyr-test-eng@google.com)
as a reviewer on your CL.

Ask a question:
- If you're a googler, please post using our YAQS label [zephyr-rtos-test](http://yaqs/eng/t/zephyr-rtos-test)
- External contributors should email to [zephyr-test-eng@google.com](mailto:zephyr-test-eng@google.com)
  or ask on the public Zephyr discord [#testing](https://discord.com/channels/720317445772017664/733037944922964069)
  channel for generic Zephyr questions.

## Where to add tests?

Tests currently reside in [zephyr/test](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/test/).
When adding a new test it's possible to either create a new directory which
includes a `testcase.yaml` or add a new entry to an existing `testcase.yaml` if
the test is a slight variation of an existing test.

### How to decide where your tests should go?

If you're adding a new compilational unit, check your dependencies. Is this unit
tied to an existing Kconfig? If so, it's possible that another test is already
enabling it and linking your `.c` file. It's probably worth your time to simply
add a new test suite to that existing test binary, or at the very least,
creating a variant of the binary by adding a new entry in the `testcase.yaml`
file.

If this doesn't work, it's possible that you might need to create a new test
from scratch. First, decide if this will be an integration test or a unit test.
Integration tests will build the full system (see below) and thus are sometimes
easier to set up (since they look and feel like an application). For the same
reason that makes them easier to set up, the lack of mocks can make things more
difficult when trying to force execution down a specific code path.

In the case of integration tests, any test under `zephyr/test` will serve as a
good example. Alternatively, an example of a unit test can be found under
[common/spi/flash_reg](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/common/spi/flash_reg/).

## The structure of the `testcase.yaml`

The top level `testcase.yaml` uses the `test:` block to define the attributes
for each test case. The optional `common:` block uses the same attributes as the
test cases, but the attributes are applied to all test cases in the file. See
[here](https://docs.zephyrproject.org/latest/develop/test/twister.html#test-cases)
for more details.

Some common attributes include:
- `extra_configs` which is a list of Kconfigs to add to the test.
- `extra_args` which is a string containing additional arguments to pass to
  CMake

## Integration tests

Integration tests build the full EC. They require devicetree and all Kconfigs to
be set appropriately. To build an integration test, simply use the following
CMakeLists.txt template:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HITS $ENV{ZEPHYR_BASE})
project(<your_test_project_name>)
target_sources(app
  PRIVATE
    src/...
)

# Add global FFF declaration
add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
```

Then, you'll need a default `prj.conf` file to set the default configs. Keep in
mind that this file will be automatically included in all the tests unless it is
overridden by the test's `extra_args` so you only want to put things that will
apply to all tests here. It will look like:

```
CONFIG_ZTEST=y
CONFIG_ZTEST_NEW_API=y

# Enable the shim of the legacy CrosEC (You’ll need both)
CONFIG_CROS_EC=y
CONFIG_PLATFORM_EC=y
```

## Mocking

We're using [FFF](http://github.com/meekrosoft/fff) for our mocking framework.
For most use cases these are the things to remember:
1. Reset your fakes in your test suite's `before` function using `RESET_FAKE()`.
   If multiple suites need to reset fakes, consider using a
   [test rule](https://docs.zephyrproject.org/latest/develop/test/ztest.html#test-rules).
2. You'll need *1* instance of `DEFINE_FFF_GLOBALS;` in your binary. This is
   done in the sample above via including `test_utils` into your binary as a
   subdirectory.
3. Use C++ for better `custom_fake` handling by setting the following before
    including `fff.h`:
    ```c
    #define CUSTOM_FFF_FUNCTION_TEMPLATE(RETURN, FUNCNAME, ...) \
        std::function<RETURN(__VA_ARGS__)> FUNCNAME
    ```
    This will enable the following:
    ```c
    ZTEST_F(suite, test_x)
    {
      my_function_fake.custom_fake = [fixture]() {
        /* Capturing lambda has access to 'fixture' */
      };
      ...
    }
    ```


## Running twister

Run all tests under a specific directory:

```shell
platform/ec$ ./twister -T path/to/my/tests
```

Run a specific test:
```shell
platform/ec$ ./twister -s path/to/my/tests/my.test.case
```

Run all tests with coverage (get more info on code coverage at
[Zephyr ztest code coverage](../code_coverage.md#Zephyr_ztest_code_coverage):
```shell
platform/ec$ ./twister -p native_posix -p unit_testing --coverage
```

Get more info on twister:
```shell
platform/ec$ ./twister --help
```

Other useful flags:
- `-i` Print inline logs to STDOUT on error (as well as log file)
- `-n` No clean (incremental builds)
- `-c` Clobber, don't create a new `twister-out/` directory
- `-v` Verbose logging (can use `-vv` or `-vvv` for more logging)
- `-b` Build only

## Using assumptions

The `zassume_*` API is used to minimize failures while allowing us to find out
exactly what's going wrong. When writing a test, only assert on code you're
testing. Any dependencies should use the `zassume_*` API. If the assumption
fails, your test will be marked as skipped and Twister will report an error.
Generally speaking, if an assumption fails, either the test wasn't set up
correctly, or there should be another test that's testing the dependency.

### Example: when to use an assumption

In a given project layout we might have several components (A, B, C, and D). In
this scenario, components B, C, and D all depend on A to function. In each test
for B, C, and D we'll include the following:

```c
static void test_suite_before(void *f)
{
  struct my_suite_fixture *fixture = f;

  zassume_ok(f->a->init(), "Failed to initialize A, see test suite 'a_init'");
}
```

The above will call A's init function and assume that it returned 0 (status OK).
If this assumption fails, then B, C, and D will all be marked as skipped along
with a log message telling us to look at the test suite 'a_init'.

Key takeaways:
1. If it's code that you depend on (module/library/logic), use assume. It's not
   what you're testing, you shouldn't raise false failures.
2. Document why/where you believe that the logic should have been tested. If we
   end up skipping this test, we should know where the tests that should have
   caught the error belong.

## Debugging

If the test targets `native_posix` or `unit_testins` platforms, you can run it
through your choice of debugger (lldb is provided in the chroot). Additionally,
it's possible to run only a subset of the tests via:

```shell
$ ./twister-out/native_posix/zephyr/test/.../zephyr/zephyr.exe -list
# List of all tests in the binary in the format of <suite_name:test_name>
$ ./twister-out/native_posix/zephyr/test/.../zephyr/zephyr.exe -test=suite0:*,suite1:test0
# Runs all tests under suite0 and test0 from suite1
$ lldb ./twister-out/unit_testing/.../testbinary
# Starts lldb for the test binary
```

## Deflaking

zTest allows deflaking tests via shuffling. To enable this feature, simply turn
on `CONFIG_ZTEST_SHUFFLE=y`. Your test binary's test suites and test order will
be random as well as run multiple times. To fine-tune the execution:
- Set `CONFIG_ZTEST_SHUFFLE_SUITE_REPEAT_COUNT` to an `int` controlling how many
  times the test suites will be shuffled and run.
- Set `CONFIG_ZTEST_SHUFFLE_TEST_REPEAT_COUNT` to an `int` controlling how many
  times the individual tests will be shuffled and run (per suite run).

Note that the total number of times that tests will run is the result of
multiplying those two Kconfig values.
