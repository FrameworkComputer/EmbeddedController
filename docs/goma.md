# Ludicrous Speed Compilation with Goma

[TOC]

*** note
At the moment, Goma is only available for Googlers.
***

All commands should be performed in the chroot.

## Login to Goma

First, login to Goma:

``` shellsession
$ goma_auth login
```

You'll be prompted to accept an agreement, and copy a URL into your
browser for login.  Be sure to use your @google.com account.

The login info should persist across reboots, so you'll only need to
do this step once (or rarely, if you get logged out).

## Start Goma

Start Goma's `compiler_proxy` daemon.  It will run in the background.

``` shellsession
$ goma_ctl ensure_start
```

You'll need to do this each time you start your machine.

## Compiling (Zephyr EC)

Run `zmake` as you normally do, and just add the `--goma` flag.  For
example:

``` shellsession
$ zmake --goma testall
```

jrosenth@ observed on a ThinkStation P920 that a clean `testall` with
`--goma` takes only 1 minute 55 seconds, versus 8 minutes and 20
seconds without, so it's worth setting up!

## Compiling (Legacy EC)

The `Makefile` doesn't yet support Goma, but this is planned for the
future.  [b/214323409](https://issuetracker.google.com/214323409)
tracks the progress.
