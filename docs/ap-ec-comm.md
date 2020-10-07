# Application Processor to EC communication

[TOC]

## Overview

The Application Processor (sometimes called the host) communicates with the EC
by issuing *host commands*, which are identified by a command ID and version
number, and then reading a response. When a host command is issued through
`ectool`, two or three software components are involved:

* `ectool`, the user-space binary,
* normally the `cros-ec` Kernel driver, and
* the code on the EC itself. This can be thought of as two parts:
  * a chip-specific driver for the appropriate transport, and
  * the generic host command handling code (mostly in the [host command task]).

We'll go into detail of each of these, as well as the traffic on the wire, in
the following sections.

### `ectool`

`ectool` contains wrapper functions for the host commands exposed by the EC,
providing a CLI. They call one of the transport-specific `ec_command`
implementations in the `util/comm-*.c` files to send and receive from the EC.

### EC kernel driver

In most cases, `ectool` communicates via the [`cros-ec` Kernel driver], rather
than directly from userspace. It sends raw commands to the Kernel driver, which
sends them on to the EC, bypassing a lot of the other Kernel driver
functionality.

There are other CrOS EC-related Kernel drivers, which use host commands to act
as adapters to existing Linux APIs. For example, sensors from the EC are mapped
to the Linux [Industrial I/O] system.

### On the wire

Now we come to the protocol itself. All transactions take this general form:

* Host writes the request packet, consisting of:
  * a transport-specific header;
  * a `struct ec_host_request` containing the command ID, data length, and a
    checksum; and
  * zero or more bytes of parameters for the command, the format of which
    depends on the command.
* Host reads the response to its request, consisting of:
  * a transport-specific header;
  * a `struct ec_host_response` containing the result code, data length, and a
    checksum; and
  * zero or more bytes of response from the command, again with a
    command-specific format.

### On the EC

The host packet is received on the EC by some chip-specific code which checks
its transport-specific header, then passes it on to the common host command code,
starting at `host_packet_receive`. The common code validates the packet and
then sends it on to the handler function (annotated with the
`DECLARE_HOST_COMMAND` macro), which runs in the `HOSTCMD` task. The handler can
set a response by modifying its arguments struct, which is sent back to the host
via the chip-specific code.

While this is happening, the EC needs to indicate to the host that it is busy
processing and not yet ready to give a response. How it does this depends on the
transport method used (see [Transport-specific details] below).

## Versions

There are two different concepts of "version" involved in host commands: version
of the overarching protocol, and versions of individual commands.

### Protocol versions

There have been three protocol versions so far, and this document describes
version 3. Version 1 was superseded by 2 before it shipped, so no devices use
it anymore. Version 2 is generally deprecated, but you might still encounter it
occasionally.

Which version is in use can be determined using the `EC_CMD_GET_PROTOCOL_INFO`
command. This was only introduced in version 3, however, so if errors,
`EC_CMD_HELLO` should be sent in version 2. If the hello command succeeds, the
EC speaks version 2.

### Command versions

Individual commands also have versions, independent of the protocol version
they're being called with. Different versions of a command may have different
parameter or response formats. `EC_CMD_GET_CMD_VERSIONS` returns the versions of
the given command supported by the EC. These version numbers start at 0.

## Transport-specific details

Although the command and response formats are the same across all transports,
some details of how they are transmitted differ, which may be of interest when
implementing the EC side of the protocol on a new chip.

### I<sup>2</sup>C

I<sup>2</sup>C is very flexible with its timing, so when the EC receives a
packet from the host, it should stretch the clock, holding it low until it is
ready for the host to read the response.

If the host tries to read more bytes than were in the response, the EC should
respond with an obvious filler byte (such as 0xEC). For example, if a command
that normally returns 50 bytes errors, its response will only be 8 bytes (the
size of the response struct). The host will probably try to read 50 bytes
anyway, so the EC should send the 8 bytes of the struct followed by 42 copies of
the filler byte.

### SPI

The SPI bus is similar to I<sup>2</sup>C, but with two major exceptions. First,
there's a minimum speed on the SPI bus. If slave devices don't respond quickly
enough, the master will assume they're broken and give up. Second, every
transaction is bidirectional. When bits are being clocked from master to slave
on the MOSI line, the master will simultaneously read bits in the other
direction on the MISO line.

Hardware devices can usually handle this, and often some hardware-based flow
control used to "stretch" the transaction by a bit or byte if the slave device
needs a little extra time to respond to the master's demands.

When exchanging messages with the EC on the SPI bus, the EC's host commands are
communicated using our own software flow-control scheme, because most of the
embedded controllers either aren't fast enough or don't have any support for
hardware flow-control.

It works like this: When the AP sends a byte to the EC, if the EC doesn't have a
response queued up in advance, a default byte is returned. The EC
preconfigures that default response byte to indicate its status (ready, busy,
waiting for more input, etc.). Once the AP has sent a complete command message,
it continues clocking bytes to the EC (which the EC ignores) and just looks at
the response byte that comes back. Once the EC has parsed the AP's command and
is ready to reply, it sends a "start of frame" byte, followed by the actual
response. The AP continues to read and ignore bytes from the EC until it sees
the start of frame byte, and then it knows that the EC's response is starting
with the next byte.

Once the response packet has been read, any additional reads should return
`EC_SPI_PAST_END`.

### LPC or eSPI

The EC should set `EC_LPC_STATUS_PROCESSING` in its command status register
after receiving a host packet and before it has a response ready.


[`cros-ec` Kernel driver]: https://chromium.googlesource.com/chromiumos/third_party/kernel/+/refs/heads/chromeos-4.19/drivers/mfd/cros_ec_dev.c
[Industrial I/O]: https://www.kernel.org/doc/html/v4.14/driver-api/iio/index.html
[host command task]: https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/master/common/host_command.c
[Transport-specific details]: #Transport_specific-details
