# Zephyr EC PDC Tracing

[TOC]

## Overview

PDC (PD controller) tracing can be enabled `CONFIG_USBC_PDC_TRACE_MSG`.
When enabled, the EC console `pdc trace` command or ectool equivalent
can be used to enable, disable and dump PDC messages.

These PDC messages are nominally standard UCSI messages, but in practice
are vendor specific. `pdc trace` can be used to examine messages
exchanges between the EC and the PDC chip.

## Examples

`pdc trace all` enables tracing between the EC and all PDC chips.
`pdc trace 0` enables tracing between the EC and all PDC chip #0.
`pdc trace off` disables tracing (the default).

`pdc trace` dumps the accumulated PDC messages. The output looks like:

```
SEQ:0004 PORT:1 OUT {
bytes 7: 08 05 9a 00 0a 01 03
}
```

Where `SEQ:wxyz` is the 16-bit message sequence number. This is added by
the tracing framework to help the consumer detect dropped messages.

`PORT:x` is the PDC port number associated with the messages.

`OUT` the direction of the message. Out means from the EC to the PDC.

`bytes n: ...` the number of bytes in the message followed by the actual
bytes.

## Implementation

PDC messages are captured by hooks in the PDC receive and transmit
routines. These are buffered in a fixed-size (e.g. 1 KiB, as determined
by `CONFIG_USBC_PDC_TRACE_MSG_FIFO_SIZE`) circular buffer. When the
buffer is full, no more messages are are accepted, effectively shutting
down tracing.

PDC messages are stored as `struct pdc_trace_msg_entry` (defined in
`ec_commands.h`) in the FIFO since that is the information needed by
the consumers of these messages.

Captured PDC messages are retrieved, and thereby consumed, using one of
two mechanisms:

1. EC console: `pdc trace`:

   * `pdc trace 1` enable tracing on port #1
   * `pdc trace on` enable tracing on all ports
   * `pdc trace off` disable tracing
   * `pdc trace` report accumulated messages

   Note: in all cases, accumulated messages are reported. The idea is
   that after the command executes, the FIFO is empty or contains fresh
   data.

2. AP terminal: `ectool pdctrace` with options:

   * `-h`        Usage help.
   * `-p <PORT>` Enable tracing on port <PORT>. By default, tracing is
                 requested all ports.
                 Port `on` or `all` enables tracing on all PDC ports.
                 Port `off` or `none` disables tracing.
   * `-s`        Report trace messages on stdout. This is the default
                 only when no other destinations are specified.
   * `-w <FILE>` Write trace messages to PCAP file.

   `ectool pdctrace -p none` disables tracing and exits.
   `ectool pdctrace` continues running, performing the requested operation
   until interrupted with `^C`.
