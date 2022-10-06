# i2c-pseudo driver

Usually I2C adapters are implemented in a kernel driver.  It is also possible to
implement an adapter in userspace, through the /dev/i2c-pseudo-controller
interface.  Load module i2c-pseudo for this.

Use cases for this module include:

* Using local I2C device drivers, particularly i2c-dev, with I2C busses on
  remote systems.  For example, interacting with a Device Under Test (DUT)
  connected to a Linux host through a debug interface, or interacting with a
  remote host over a network.

* Implementing I2C device driver tests that are impractical with the i2c-stub
  module.  For example, when simulating an I2C device where its driver might
  issue a sequence of reads and writes without interruption, and the value at a
  certain address must change during the sequence.

This is not intended to replace kernel drivers for actual I2C busses on the
local host machine.

## Details

Each time /dev/i2c-pseudo-controller is opened, and the correct initialization
command is written to it (ADAPTER_START), a new I2C adapter is created.  The
adapter will live until its file descriptor is closed.  Multiple pseudo adapters
can co-exist simultaneously, controlled by the same or different userspace
processes.  When an I2C device driver sends an I2C message to a pseudo adapter,
the message becomes readable from its file descriptor.  If a reply is written
before the adapter timeout expires, that reply will be sent back to the I2C
device driver.

Reads and writes are buffered inside i2c-pseudo such that userspace controllers
may split them up into arbitrarily small chunks.  Multiple commands, or portions
of multiple commands, may be read or written together.

Blocking I/O is the default.  Non-blocking I/O is supported as well, enabled by
O_NONBLOCK.  Polling is supported, with or without non-blocking I/O.  A special
command (ADAPTER_SHUTDOWN) is available to unblock any pollers or blocked
reads or writes, as a convenience for a multi-threaded or multi-process program
that wants to exit.

It is safe to access a single controller fd from multiple threads or processes
concurrently, though it is up to the controller to ensure proper ordering, and
to ensure that writes for different commands do not get interleaved.  However,
it is recommended (not required) that controller implementations have only one
reader thread and one writer thread, which may or may not be the same thread.
Avoiding multiple readers and multiple writers greatly simplifies controller
implementation, and there is likely no performance benefit to be gained from
concurrent reads or concurrent writes due to how i2c-pseudo serializes them
internally.  After all, on a real I2C bus only one I2C message can be active at
a time.

Commands are newline-terminated, both those read from the controller device, and
those written to it.

## Read Commands

The commands that may be read from a pseudo controller device are:


---

Read Command

:   `I2C_ADAPTER_NUM <num>`

Example

:   `"I2C_ADAPTER_NUM 5\\n"`

Details


---

Read Command

:   `I2C_PSEUDO_ID <num>`

Example

:   `"I2C_PSEUDO_ID 98\\n"`

Details


---

Read Command

:   `I2C_BEGIN_XFER`

Example

:   `"I2C_BEGIN_XFER\\n"`

Details


---

Read Command

:   `I2C_XFER_REQ <xfer_id> <msg_id> <addr> <flags> <data_len> [<write_byte>[:...]]`

Example

:   `"I2C_XFER_REQ 3 0 0x0070 0x0000 2 AB:9F\\n"`

Example

:   `"I2C_XFER_REQ 3 1 0x0070 0x0001 4\\n"`

Details


---

Read Command

:   `I2C_COMMIT_XFER`

Example

:   `"I2C_COMMIT_XFER\\n"`

Details

## Write Commands

The commands that may be written to a pseudo controller device are:

Write Command

:   `SET_ADAPTER_NAME_SUFFIX <suffix>`

Example

:   `"SET_ADAPTER_NAME_SUFFIX My Adapter\\n"`

Details


---

Write Command

:   `SET_ADAPTER_TIMEOUT_MS <ms>`

Example

:   `"SET_ADAPTER_TIMEOUT_MS 2000\\n"`

Details


---

Write Command

:   `ADAPTER_START`

Example

:   `"ADAPTER_START\\n"`

Details


---

Write Command

:   `GET_ADAPTER_NUM`

Example

:   `"GET_ADAPTER_NUM\\n"`

Details


---

Write Command

:   `GET_PSEUDO_ID`

Example

:   `"GET_PSEUDO_ID\\n"`

Details


---

Write Command

:   `I2C_XFER_REPLY <xfer_id> <msg_id> <addr> <flags> <errno> [<read_byte>[:...]]`

Example

:   `"I2C_XFER_REPLY 3 0 0x0070 0x0000 0\\n"`

Example

:   `"I2C_XFER_REPLY 3 1 0x0070 0x0001 0 0B:29:02:D9\\n"`

Details


---

Write Command

:   `ADAPTER_SHUTDOWN`

Example

:   `"ADAPTER_SHUTDOWN\\n"`

Details

## Example userspace controller code

In C, a simple exchange between i2c-pseudo and userspace might look like the
example below.  Note that for brevity this lacks any error checking and
handling, which a real pseudo controller implementation should have.

```
int fd;
char buf[1<<12];

fd = open("/dev/i2c-pseudo-controller", O_RDWR);
/* Create the I2C adapter. */
dprintf(fd, "ADAPTER_START\n");

/*
 * Pretend this I2C adapter number is 5, and the first I2C xfer sent to it was
 * from this command (using its i2c-dev interface):
 * $ i2cset -y 5 0x70 0xC2
 *
 * Then this read would place the following into *buf:
 * "I2C_BEGIN_XFER\n"
 * "I2C_XFER_REQ 0 0 0x0070 0x0000 1 C2\n"
 * "I2C_COMMIT_XFER\n"
 */
read(fd, buf, sizeof(buf));

/* This reply would allow the i2cset command above to exit successfully. */
dprintf(fd, "I2C_XFER_REPLY 0 0 0x0070 0x0000 0\n");

/*
 * Now pretend the next I2C xfer sent to this adapter was from:
 * $ i2cget -y 5 0x70 0xAB
 *
 * Then this read would place the following into *buf:
 * "I2C_BEGIN_XFER\n"
 * "I2C_XFER_REQ 1 0 0x0070 0x0000 1 AB\n"
 * "I2C_XFER_REQ 1 1 0x0070 0x0001 1\n'"
 * "I2C_COMMIT_XFER\n"
 */
read(fd, buf, sizeof(buf));

/*
 * These replies would allow the i2cget command above to print the following to
 * stdout and exit successfully:
 * 0x0b
 *
 * Note that it is also valid to write these together in one write().
 */
dprintf(fd, "I2C_XFER_REPLY 1 0 0x0070 0x0000 0\n");
dprintf(fd, "I2C_XFER_REPLY 1 1 0x0070 0x0001 0 0B\n");

/* Destroy the I2C adapter. */
close(fd);
```
