# EC-3PO

[TOC]

## What is EC-3PO?

EC-3PO is the console interpreter that will one day replace the EC console that
we have today.  EC-3PO aims to migrate our rich debug console from the EC itself
to the host.  This allows us to maintain our rich debug console without
impacting our EC image sizes while also allowing us to add new features.

For more information, see [the design doc](./ec-3po-design.md).

## How do I use EC-3PO?

If you're using `servod` to connect to your EC, chances are you're already using
it. EC-3PO was grafted into `servod` on Feb 7th 2016. If you're not running
`servod`, you can run EC-3PO manually by running `console.py` in the
`util/ec3po` directory from the EC checkout. You will need to provide the PTY
that you get from elsewhere though.

To obtain the EC console PTY, inside the chroot run:

```shell
$ dut-control ec_uart_pty
```

**NOTE: It's important to use `dut-control` to query the PTY instead of just
eyeballing the `servod` output.** The former PTY (now known as
`raw_ec_uart_pty`) will be sending raw binary data. Trying to use that console
with an enhanced EC image will definitely fail and you won't be able to
send/receive any commands.

Then use your favorite serial terminal program to connect to the PTY. Since
`servod` is run as root, you'll need to run your serial terminal program as root
as well using `sudo`. This is because the permissions have changed from 666 to
660.

EC-3PO has been tested with `minicom`, `screen`, `socat`, and `cu`. However, if
you're using `cu` you'll have to do the following to get it to work because
apparently, `cu` wants group write permissions. On Ubuntu at least, the PTY is
created with the `tty` group. If on your machine it's not, then just replace
`tty` with whatever group it's created with.

1. Create a `tty` group if you don't have one already.
1. Add root to the `tty` group.
1. Rerun `cu` with `sudo` and it should work now.

## Why does the console seem "laggier" than before?

This is because there's a ~300ms delay after entering each console command. This
is due to the interrogation that the console interpreter performs to determine
if the EC image it's currently talking to is enhanced or not. Debug prints
coming from the EC should be the same speed. Since most people aren't currently
using the enhanced EC images, you can go ahead and run this command if the 300ms
delay is unbearable.

To disable the delay:

1. Open the EC console.
1. Press `%`
1. Enter `interrogate never`
1. Then press enter.

\**For `socat` users, due to the line buffered nature, you'll have to just enter
`%interrogate never`. Notice the lack of the space character between `%` and the
command.*

The interrogation delay should now be gone and you can have your 300ms/cmd back.

## How do I try out this "enhanced" EC image you speak of?

You simply add this to your board.h file.

```c
#define CONFIG_EXPERIMENTAL_CONSOLE
```

## I can't open the EC console

Make sure you try with `sudo`. If you're using `cu`, make sure root is a member
of the group of the created PTY.
