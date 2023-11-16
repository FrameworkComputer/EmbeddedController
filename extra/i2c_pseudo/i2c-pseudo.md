# i2c-pseudo driver

While I2C adapters are usually implemented in a kernel driver, they can also be
implemented in userspace through the `/dev/i2c-pseudo` interface. Load module
`i2c-pseudo` for this.

Use cases for userspace I2C adapters include:

* Using standard Linux I2C interfaces, particularly `/dev/i2c-%d`, to control
  I2C on remote systems under test managed by userspace software.

* Mocking the behavior of I2C devices for regression testing Linux drivers and
  software that use I2C.

## Details

Open `/dev/i2c-pseudo` and call ioctl `I2CP_IOCTL_START` to create a new
I2C adapter on the system. The adapter will live until its file descriptor is
closed. Multiple pseudo adapters can co-exist simultaneously, controlled by the
same or different userspace processes.

When an I2C device driver sends an I2C transaction to a pseudo adapter, the
transaction becomes available via ioctl `I2CP_IOCTL_XFER_REQ`. If a reply is
issued via ioctl `I2CP_IOCTL_XFER_REPLY` before the adapter timeout expires,
that reply will be sent back to the I2C device driver.

By default `I2CP_IOCTL_XFER_REQ` will block indefinitely for an
I2C transaction request to come in from an I2C device driver. Use `O_NONBLOCK`
for non-blocking behavior. Other ioctl commands are unaffected by `O_NONBLOCK`
and never block indefinitely.

Polling is supported, in blocking or non-blocking mode. `EPOLLIN` indicates an
I2C transaction is available for `I2CP_IOCTL_XFER_REQ`. `EPOLLOUT` indicates
`I2CP_IOCTL_XFER_REPLY` will not block, which is always the case and so is
never worth polling for. `EPOLLHUP` indicates `I2CP_IOCTL_SHUTDOWN` has been
called.

While polling is fully functional in blocking mode, it cannot be used to avoid
blocking. If a pending I2C transaction request times out between receiving
`EPOLLIN` and issuing `I2CP_IOCTL_XFER_REQ` the latter can block.
`O_NONBLOCK` is needed to avoid this.

The ioctl `I2CP_IOCTL_SHUTDOWN` will unblock any pollers or blocked
`I2CP_IOCTL_XFER_REQ`, as a convenience for a multi-threaded or multi-process
program that wants to exit. Use of `I2CP_IOCTL_SHUTDOWN` is purely optional.

See `include/uapi/linux/i2c-pseudo.h` for a detailed description of ioctl
args, usage, and behavior.

## Example userspace I2C adapter

`samples/i2c-pseudo/i2c-adapter-example.c` is a simple program that starts an
I2C adapter and prints the I2C transfers it receives, with I2C reads filled by
reading from stdin.

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
