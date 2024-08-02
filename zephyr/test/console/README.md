# EC Console tests

## console.output

The `console.output` test suite verifies the following behavior of EC messages
sent to the UART.

* The Zephyr shell sends its output to the UART.

* Legacy EC console command output is sent to the Zephyr shell.  This includes
  `cprintf`, `cprints`, and `cputs` calls with the `enum console_channel` set to
  `CC_COMMAND`.

* Legacy EC calls to `cprintf`, `cprints`, and `cputs` for all other `enum
console_channel` types sends output to the Zephyr logging backend.

* Zephyr logging calls are sent to the UART.  Transitively, legacy EC debug
  output calls are also sent to the UART.

* The legacy EC console command `chan` can filter both legacy EC messages and
  Zephyr logging.

* Legacy EC console command and Zephyr shell command output is never filtered.

## console.restricted

The `console.restricted` test suite validates that legacy EC console commands
tagged with `CMD_FLAG_RESTRICTED` are properly rejected.
