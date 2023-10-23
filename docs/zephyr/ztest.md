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
- External contributors should:
    - email to [zephyr-test-eng@google.com](mailto:zephyr-test-eng@google.com)
    - or [sign up to the Public Zephyr discord](https://discord.com/invite/Ck7jw53nU2), and then visit the [#testing](https://discord.com/channels/720317445772017664/733037944922964069)
  channel for questions.

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
- `extra_configs` - a list of Kconfigs to add to the test.
- `extra_conf_files` - specifies a YAML list of additional Kconfig files to
  to apply to the build. Replaces the `CONF_FILE` field in extra_args.
- `extra_overlay_confs` - specifies a list of overlay Kconfig files. Replaces
  the `OVERLAY_CONFIG` field in extra_args.
- `extra_dtc_overlay_files` - specifies a list of additional device tree files
  to apply to the build. Replaces the `DTC_OVERLAY_FILE` field in extra_args.

`extra_args` is a string field that allows injecting free-form CMake variables
into the build. It is rarely needed and the practice of specifying `CONF_FILE`,
`OVERLAY_CONFIG`, or `DTC_OVERLAY_FILE` here is deprecated. Please use the
above fields for these functions, as they are much more readable.
## Integration tests

Integration tests build the full EC. They require devicetree and all Kconfigs to
be set appropriately. To build an integration test, simply use the following
CMakeLists.txt template:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
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

### Run all tests under a specific directory

```shell
platform/ec$ ./twister -T path/to/my/tests
```

### Run a specific test
```shell
platform/ec$ ./twister -s <test dir>/<my.test.scenario>
```

For example:
```shell
platform/ec$ ./twister -s drivers/drivers.default
```

Explanation of this string: `drivers/` is the directory under `zephyr/test/`
that contains the requested test, and `drivers.default` is the specific test
scenario specified in that directory's `testcase.yaml` file.

### Run all tests with coverage

You can find more info on code coverage at
[Zephyr ztest code coverage](../code_coverage.md#Zephyr_ztest_code_coverage).

```shell
platform/ec$ ./twister -p native_posix -p unit_testing --coverage
```

### Get more info on twister
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

The `zassume*` API is used to skip tests when certain preconditions are not
met. Please don't use it. In our tests we shouldn't ever need to skip tests
since we control all dependencies. If for some reason you actually need to skip
a test use `ztest_test_skip()` since that will indicate that you intended to
skip and didn't use assume by mistake when you meant to use assert.

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
