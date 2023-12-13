# Implementing I2C adapters in userspace

<!-- SPDX-License-Identifier: GPL-2.0 -->
The `i2c-pseudo` module provides `/dev/i2c-pseudo` for implementing
I2C adapters in userspace.

Use cases for userspace I2C adapters include:

* Using standard Linux I2C interfaces, particularly `/dev/i2c-%d` from
  `i2c-dev` module, to control I2C on remote systems under test managed by
  userspace software.

* Mocking the behavior of I2C devices for regression testing Linux drivers and
  software that use I2C.

Supported I2C adapter functionality:

* `I2C_FUNC_I2C`

* `I2C_FUNC_10BIT_ADDR`

* `I2C_FUNC_PROTOCOL_MANGLING`

* `I2C_FUNC_SMBUS_EMUL`

Native SMBUS is not currently supported, only SMBUS emulation. The UAPI could be
extended with new commands to support native SMBUS in the future.

## Basic usage

The UAPI is based around ioctl() commands. Each open `/dev/i2c-pseudo`
file descriptor has its own independent state and I2C adapter device.

Open `/dev/i2c-pseudo` and call ioctl `I2CP_IOCTL_START` to create a new
I2C adapter on the system. The adapter will live until its file descriptor is
closed. Multiple pseudo adapters can co-exist simultaneously, controlled by the
same or different userspace processes.

When an I2C device driver sends an I2C transfer request to a pseudo adapter, the
transfer becomes available via ioctl `I2CP_IOCTL_XFER_REQ`. Issue a reply
via ioctl `I2CP_IOCTL_XFER_REPLY` before the adapter timeout expires and the
reply will be sent back to the I2C device driver. Polling and non-blocking modes
are supported.

## ioctl() commands

This is a short summary of each ioctl command. See
[include/uapi/linux/i2c-pseudo.h](../../include/uapi/linux/i2c-pseudo.h) for a
detailed description of arguments, usage, and behavior.

For all commands, any non-negative return value indicates success, and any
negative return value indicates an error with `errno` set.

`I2CP_IOCTL_START`

> Set I2C adapter attributes and start the adapter.

`I2CP_IOCTL_XFER_REQ`

> Get an I2C transfer request.

> If there is no transfer waiting to be requested this will block indefinitely
> for the next one, unless `O_NONBLOCK` is set, in which case this will fail
> with `EWOULDBLOCK` or `EAGAIN`.

> This will never *successfully* return the same transfer more than once.
> Certain errors may be corrected and retried before the pending transfer times
> out.

`I2CP_IOCTL_XFER_REPLY`

> Reply to an I2C transfer request.

> The reply indicates success or failure, as well as the number of I2C messages
> that were transferred out of the total requested.

> All I2C reads that the reply says were transferred must have their buffers
> filled with the requested data, regardless of whether the reply indicates
> overall success or failure.

> This will never successfully return for the same I2C transfer more than once.

`I2CP_IOCTL_GET_COUNTERS`

> Get transfer counters, including transfers which failed from timeout or
> other reasons before userspace requested or replied to them.

> This provides visibility into problems keeping up with transfer requests.

> Note that a successful `I2CP_IOCTL_XFER_REPLY` is always a success from the
> perspective of these counters, even if the reply indicated a failure.
> Userspace can track its own failure reasons without kernel assistance,
> and likely with more detail than just the error code provided to the kernel.

`I2CP_IOCTL_SHUTDOWN`

> Permanently unblock all userspace I/O and refuse further I2C transfers.

> `I2CP_IOCTL_XFER_REQ`, `I2CP_IOCTL_XFER_REPLY`, and I2C transfers will
> fail with `ESHUTDOWN`, and polling will indicate `POLLHUP`.

> This does not remove the actual I2C adapter device, that only happens when the
> file descriptor is closed.

> Use of this is *optional* for userspace convenience to unblock other threads
> or processes before closing the fd, to avoid undefined or undesirable behavior
> around closing an fd when other threads may be blocked on it. Programs may of
> course avoid that using other means of synchronization, or by virtue of being
> single-threaded.

## Blocking and non-blocking modes

Blocking mode is the default. Set `O_NONBLOCK` for non-blocking mode.

In blocking mode `I2CP_IOCTL_XFER_REQ` will wait indefinitely for a new
I2C transfer request if there is not one waiting to be requested. It can be
interrupted by a signal or `I2CP_IOCTL_SHUTDOWN`. It may restart automatically
after a signal handler that was established with `SA_RESTART`.

In non-blocking mode `I2CP_IOCTL_XFER_REQ` will fail with
`EWOULDBLOCK` or `EAGAIN` if no transfer is waiting to be requested.

Other commands never wait indefinitely for transfer requests and are unaffected
by `O_NONBLOCK`.

## Polling

Polling is supported, in blocking or non-blocking mode.

`POLLIN`

> An I2C transfer is available for `I2CP_IOCTL_XFER_REQ`.

`POLLOUT`

> An I2C transfer is waiting for `I2CP_IOCTL_XFER_REPLY`.

> This is always the case immediately after successful `I2CP_IOCTL_XFER_REQ`,
> so polling for this is unnecessary, it is safe and recommended to call
> `I2CP_IOCTL_XFER_REPLY` as soon as a reply is ready.

`POLLHUP`

> `I2CP_IOCTL_SHUTDOWN` has been called.

While polling is fully functional in blocking mode, polling cannot be used to
avoid blocking. If a pending I2C transaction request times out between receiving
`POLLIN` and issuing `I2CP_IOCTL_XFER_REQ`, the latter will wait for the
next transfer request unless `O_NONBLOCK` is set.

## Example userspace I2C adapter

See [samples/i2c-pseudo/i2c-adapter-example.c](../../samples/i2c-pseudo/i2c-adapter-example.c) for a simple program that
starts an I2C adapter and prints the I2C transfers it receives, with I2C reads
filled by reading from `stdin`.

Sample usage:

```
$ sudo modprobe i2c-pseudo
$ cd samples/i2c-pseudo
$ make i2c-adapter-example
$ ./i2c-adapter-example < /dev/urandom
adapter_num=13
```

Use a different terminal to issue I2C transfers to its I2C adapter number:

```
$ sudo modprobe i2c-dev
$ i2ctransfer -y 13 w2@0x20 0x03 0x5a w3@0x77 0x2b+
$ i2ctransfer -y 13 w2@0x20 0x03 0x5a r5@0x75
$ i2ctransfer -y 13 w5@0x70 0xc2 0xff=
$ i2ctransfer -y 13 w3@0x1e 0x1a+ r2 r2
```

With the data read from `/dev/urandom` the full exchange might look like this
on the `i2c-transfer` side:

```
$ i2ctransfer -y 13 w2@0x20 0x03 0x5a w3@0x77 0x2b+
$ i2ctransfer -y 13 w2@0x20 0x03 0x5a r5@0x75
0x7f 0x3c 0xf1 0x30 0x46
$ i2ctransfer -y 13 w5@0x70 0xc2 0xff=
$ i2ctransfer -y 13 w3@0x1e 0x1a+ r2 r2
0x3e 0xe4
0x58 0xe9
```

And like this on the `i2c-adapter-example` side:

```
$ ./i2c-adapter-example < /dev/urandom
adapter_num=13

begin transaction
addr=0x20 flags=0x200 len=2 write=[0x03 0x5a]
addr=0x77 flags=0x200 len=3 write=[0x2b 0x2c 0x2d]
end transaction

begin transaction
addr=0x20 flags=0x200 len=2 write=[0x03 0x5a]
addr=0x75 flags=0x201 len=5 read=[0x7f 0x3c 0xf1 0x30 0x46]
end transaction

begin transaction
addr=0x70 flags=0x200 len=5 write=[0xc2 0xff 0xff 0xff 0xff]
end transaction

begin transaction
addr=0x1e flags=0x200 len=3 write=[0x1a 0x1b 0x1c]
addr=0x1e flags=0x201 len=2 read=[0x3e 0xe4]
addr=0x1e flags=0x201 len=2 read=[0x58 0xe9]
end transaction
```
