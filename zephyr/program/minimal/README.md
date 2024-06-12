# Minimal Example Zephyr EC Project

This directory is intended to be an extremely minimal example of a
project.  Should you like, you can use it as a bring up a new program,
or as reference as you require.

If you're bringing up a new variant of a program, you don't need a
whole project directory with a `BUILD.py` and all, and this example is
likely not of use to you.  Check out the [project config
documentation] for instructions on adding a new variant.

[project config documentation]: ../../../docs/zephyr/project_config.md

# Building

To build the `minimal-posix` example, run:

``` shellsession
(chroot) $ zmake build minimal-posix
```

To build the NPCX9 example, run:

``` shellsession
(chroot) $ zmake build minimal-npcx9
```

For the IT8XXX2 example, run:

``` shellsession
(chroot) $ zmake build minimal-it8xxx2
```
