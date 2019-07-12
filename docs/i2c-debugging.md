I²C Debugging Tips
==================

The EC codebase has functionality to help you debug I²C errors without
pulling out the scope. Some of the debug functionality is disabled by
default to save space, but can be enabled with the `CONFIG_I2C_DEBUG`
option.

Tracing
-------

You can use the `i2ctrace` command to monitor (ranges of) addresses:

    i2ctrace [list
            | disable <id>
            | enable <port> <address>
            | enable <port> <address-low> <address-high>]

For example:

    > i2ctrace enable 0 0x10 0x30
    > i2ctrace enable 0 0x40 0x50
    > i2ctrace list
    id port address
    -- ---- -------
    0     0 0x10 to 0x30
    1     0 0x40 to 0x50
    ... debug spam may follow ...
    i2c: write 0:0x50 A8
    i2c: read 0:0x50 00 02 00 06 00 44
    ...
    > i2ctrace disable 1
    > i2ctrace list
    id port address
    -- ---- -------
    0     0 0x10 to 0x30

A maximum of 8 debug entries are supported at a single time.

Note that `i2ctrace enable` will merge debug entries when possible:

    > i2ctrace enable 0 0x10 0x30
    > i2ctrace enable 0 0x40 0x50
    > i2ctrace enable 0 0x31 0x3f
    > i2ctrace list
    id port address
    -- ---- -------
    0     0 0x10 to 0x50
