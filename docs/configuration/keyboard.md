## Configure Keyboard

## Config options

Keyboard options start with `CONFIG_KEYBOARD*`. Evaluate whether each option is
appropriate to add to `baseboard.h` or `board.h`.

Your board should select only one of these options to configure the protocol
used to send keyboard events to the AP.

-   `CONFIG_KEYBOARD_PROTOCOL_8042` - Systems with an x86 AP use the 8042
    protocol.
-   `CONFIG_KEYBOARD_PROTOCOL_MKBP` - Systems without an x86 AP (e.g. ARM)
    typically use the MKBP protocol.

## Feature Parameters

-   `CONFIG_KEYBOARD_KSO_BASE <pin>` - Evaluate whether this parameter is
    required by your board.

## GPIOs and Alternate Pins

Define `ALTERNATE()` pin entries for all keyboard matrix signals, to connect the
signals to the keyboard controller of the EC chipset.

Note that KSO_02 is purposely not configured for for alternate mode. See the
[H1 Special Requirements](#H1-Special-Requirements) below for details.

```c
/* Example Keyboard pin setup */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
ALTERNATE(PIN_MASK(3, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT) /* KSI_00-01 */
ALTERNATE(PIN_MASK(2, 0xFC), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT) /* KSI_02-07 */
ALTERNATE(PIN_MASK(2, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_00-01 */
ALTERNATE(PIN_MASK(1, 0x7F), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_03-09 */
ALTERNATE(PIN_MASK(0, 0xF0), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_10-13 */
ALTERNATE(PIN_MASK(8, 0x04), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_14    */
```

See the [GPIO](./gpio.md) documentation for additional details on the GPIO
macros.

## Data structures

-   `struct keyboard_scan_config keyscan_config` - This can be used to customize
    the keyboard scanner (e.g. scan frequency, debounce duration, etc.).

## Tasks

The `KEYSCAN` task monitors the keyboard matrix for new key presses and is
required by this feature. The priority is set as one of the highest priority
tasks in the system, typically only below the `PD_Cn` and `PD_INT_Cn` tasks.

```c
    TASK_NOTEST(KEYSCAN, keyboard_scan_task, NULL, TASK_STACK_SIZE) \
```

The `KEYPROTO` task handles sending and receiving 8042 protocol messages from
the AP and is required when `CONFIG_KEYBOARD_PROTOCOL_8042` is used. The typical
priority is lower than the `HOSTCMD` task.

```c
    TASK_NOTEST(KEYPROTO, keyboard_protocol_task, NULL, TASK_STACK_SIZE) \
```

## Additional Notes

-   If you're including keyboard support, you should also define
    `CONFIG_CMD_KEYBOARD` to enable keyboard debug commands from the EC console.
-   `CONFIG_KEYBOARD_PROTOCOL_MKBP` automatically enables `CONFIG_MKBP_EVENT`.
-   Boards that enable `CONFIG_KEYBOARD_PROTOCOL_8042` will often also define
    `CONFIG_MKBP_EVENT` for sensor events. In this case only motion sensor data
    is reported using the MKBP protocol, keyboard events are provided using the
    8042 protocol. Refer to [Configuring Sensors](./motion_sensors.md) for more
    information.

### H1 Special Requirements

On Boards that use the H1 secure microcontroller, one KSI (keyboard scan input)
signal and one KSO (keyboard scan output) signal are routed through the H1
microcontroller. There are additional GPIO and configuration options that must
be enabled in this case. - The KSO_02/COL2 signal is always inverted. Explicitly
configure the GPIO to default low. `c GPIO(KBD_KSO2, PIN(1, 7), GPIO_OUT_LOW) /*
KSO_02 inverted */` - Add the define `CONFIG_KEYBOARD_COL2_INVERTED` to
`baseboard.h` or `board.h`.
