# EC Host Command API

## Overview

The EC provices a control channel on an external interface using SPI,
LPC, or I2C bus. The low level details of the protocol used on this
interface is captured in [AP-EC-COMM](ap-ec-comm.md).

The entity connected to the other side of this control channel is
typically the "AP" (application processor) - the main processor of a
system running a full-blown operating system.

## `ec_commands.h`

The EC implements hundreds of "host commands" (operations) to support
the AP. These are organized as an opcode and data structures defining
the input and output data for each opcode. `include/ec_commands.h`
defines all of the available opcodes and associated data structures.

For example, the `HELLO` host command is defined as:

`#define EC_CMD_HELLO 0x0001`

It takes a `struct ec_params_hello` as input and returns a
`struct ec_response_hello`.

The vast majority of host commands follow this simple naming pattern,
but exceptions have crept in over time, generally unintentionally, as
there is no mechanism to validate that this pattern has been followed.
Also, implementers must be careful to use the intended combination of
host commands and data types as there is no mechanism to validate the
correct combination has been used.

## `ec_cmd_api.h`

`ec_cmd_api.h` extends `ec_commands.h` by providing a function based
host command API. The `HELLO` host command from the previous example can
be invoked using a function provided by `ec_cmd_api.h`:

```
int ec_cmd_hello(CROS_EC_COMMAND_INFO *h,
                 const struct ec_params_hello *p,
                 struct ec_response_hello *r)
```

where `CROS_EC_COMMAND_INFO` is defined by the application for its house-keeping.
It is intended to be a connection handle or other state information needed by
the caller to communicate with the EC.

The application also needs to provide an implementation of
`cros_ec_command` that performs the actual communication with the EC.

```
int CROS_EC_COMMAND(CROS_EC_COMMAND_INFO *h,
                    int command, int version,
                    const void *outdata, int outsize,
                    void *indata, int insize)
```

## Maintaining `ec_cmd_api.h`

`ec_cmd_api.h` consists of 2 sections corresponding to the different
methods used to determine the host command API function signature:

1. This section consists of functions that do not follow a simple
   pattern and need to be specified explicitly.

2. This section consists of functions that can be generated with the
   help of template macros.
