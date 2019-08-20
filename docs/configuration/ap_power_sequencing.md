# Configure AP Power Sequencing

This section details the configuration related to managing the system power
states (G3, S5, S3, S0, S0iX, etc). This includes the following tasks:

- Selecting the AP chipset type.
- Configure output GPIOs that enable voltage rails.
- Configure input GPIOs that monitor the voltage rail status (power good
  signals).
- Configure input GPIOs that monitor the AP sleep states.
- Pass through power sequencing signals from the board to the AP, often with
  delays or other sequencing control.

## Config options

The AP chipset options are grouped together in [config.h]. Select exactly one of
the available AP chipset options (e.g. `CONFIG_CHIPSET_APOLLOLAKE`,
`CONFIG_CHIPSET_BRASWELL`, etc). If the AP chipset support is not available,
select `CONFIG_CHIPSET_ECDRIVEN` to enable basic support for handling S3 and S0
power states.

After selecting the chipset, search for additional options that start with
`CONFIG_CHIPSET*` and evaluate whether each option is appropriate to add to
`baseboard.h` or `board.h`.

Finally, evaluate the `CONFIG_POWER_` options for use on your board. In
particular, the `CONFIG_POWER_BUTTON`, and `CONFIG_POWER_COMMON` should be
defined.

The `CONFIG_BRINGUP` option is especially useful option during the initial power
up of a new board. This option is discussed in more detail in the [Testing and
Debugging](#Testing-and-Debugging) section.

## Feature Parameters

None needed in this section.

## GPIOs and Alternate Pins

### EC Outputs to the board

The board should connect the enable signal of one or more voltage rails to the
EC. These enable signals will vary based on the AP type, but are typically
active high signals. For Intel Ice Lake chipsets, this includes enable signals
for the primary 3.3V and primary 5V rails.

```c
GPIO(EN_PP3300_A, PIN(A, 3), GPIO_OUT_LOW)
GPIO(EN_PP5000,   PIN(A, 4), GPIO_OUT_LOW)
```

### EC Outputs to AP

For boards with an x86 AP, the following signals can be connected between the EC
and AP/PCH. Create `GPIO()` entries for any signals used on your board.

- `GPIO_PCH_PWRBTN_L` - Output from the EC that proxies the status of the EC
  input `GPIO_POWER_BUTTON_L` (driven by the H1). Only used when
  `CONFIG_POWER_BUTTON_X86` is defined.
- `GPIO_PCH_RSMRST_L` - Output from the EC that proxies the status of the EC
  input `GPIO_RSMRST_L_PGOOD` (driven by the PMIC or voltage regulators on the
  board).
- `GPIO_PCH_SYS_PWROK` - Output from the EC that indicates when the system power
  is good and the AP can power up.
- `GPIO_PCH_WAKE_L` - Output from the EC, driven low when there is a wake event.

### Power Signal Interrupts

For each power signal defined in the `power_signal_list[]` array, define a
`GPIO_INT()` entry that connects to the `power_signal_interrupt`. The interrupts
are configured to trigger on both rising edge and falling edge.

The example below shows the power signals used with Ice Lake processors.

```c
GPIO_INT(SLP_S0_L, PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_S3_L, PIN(A, 5), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(SLP_S4_L, PIN(D, 4), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_ALL_SYS_PWRGD, PIN(F, 4), GPIO_INT_BOTH,   power_signal_interrupt)
GPIO_INT(PP5000_A_PG_OD, PIN(D, 7), GPIO_INT_BOTH, power_signal_interrupt)
```

See the [GPIO](./gpio.md) documentation for additional details on the GPIO
macros.

## Data structures

- `const struct power_signal_info power_signal_list[]` - This array defines the
  signals from the AP and from the power subsystem on the board that control the
  power state. For some Intel chipsets, including Apollo Lake and Ice Lake, this
  power signal list is already defined by the corresponding chipset file under
  the `./power` directory.

## Tasks

The `CHIPSET` task monitors and handles the power state changes.  This task
should always be enabled with a priority higher than the `CHARGER` task, but
lower than the `HOSTCMD` and `CONSOLE` tasks.

```c
    TASK_NOTEST(CHIPSET, chipset_task, NULL, LARGER_TASK_STACK_SIZE) \
```

The `POWERBTN` and task should be enabled when using x86 based AP chipsets. The
typical priority is higher than the `CONSOLE` task, but lower than the `KEYSCAN`
task.

```c
    TASK_ALWAYS(POWERBTN, power_button_task, NULL, LARGER_TASK_STACK_SIZE) \
```

## Testing and Debugging

During the first power on of prototype devices, it is recommended to enable
`CONFIG_BRINGUP`. This option prevents the EC from automatically powering on the
AP. You can use the EC console commands `gpioget` and `gpioset` to manually
check power good signals and enable power rails in a controlled manner. This
option also enables extra debug to log all power signal transitions to the EC
console. With `CONFIG_BRINGUP` enabled, you can trigger the automatic power
sequencing by running the `powerbtn` from the EC console.

The EC console displays the following text when `CONFIG_BRINGUP` is enabled:

```
WARNING: BRINGUP BUILD
```

Once you manually press the power button, or execute the `powerbtn` command, the
EC console displays both the power state changes and the detected transitions of
all power signals. An example is shown below.

```
> powerbtn
Simulating 200 ms power button press.
[6.790816 power button pressed]
[6.791133 PB pressed]
[6.791410 PB task 1 = pressed]
[6.791755 PB PCH pwrbtn=LOW]
[6.792049 PB task 10 = was-off, wait 199362]
RTC: 0x000067bc (26556.00 s)
[6.792786 power state 5 = G3->S5, in 0x0000]
[6.793190 Set EN_PP3300_A: 1]
[6.793905 SW 0x03]
[6.817627 Set PCH_DSW_PWROK: 1]
[6.818007 Pass thru GPIO_DSW_PWROK: 1]
[6.818351 Set EN_PP5000_A: 1]
RTC: 0x000067bc (26556.00 s)
[6.903830 power state 1 = S5, in 0x0029]
[6.918735 Pass through GPIO_RSMRST_L_PGOOD: 1]
i2c 7 recovery! error code is 13, current state is 0
Simulating power button release.
> [6.991576 power button released]
[6.992009 PB task 10 = was-off]
[6.992376 PB released]
[6.992635 PB task 6 = released]
[6.992958 PB PCH pwrbtn=HIGH]
[6.993256 PB task 0 = idle, wait -1]
[6.993806 PB released]
[6.994149 PB task 6 = released]
[6.994512 PB PCH pwrbtn=HIGH]
[6.994812 PB task 0 = idle, wait -1]
[6.995768 SW 0x01]
3 signal changes:
  6.807298  +0.000000  DSW_PWROK => 1
  6.903417  +0.096119  SLP_SUS_L => 1
  6.908471  +0.005054  PG_EC_RSMRST_ODL => 1
1 signal changes:
  7.909941  +0.000000  SLP_S0_L => 1
[9.026429 Fan 0 stalled!]
RTC: 0x000067bf (26559.00 s)
[9.124643 power state 6 = S5->S3, in 0x003f]
i2c 3 recovery! error code is 13, current state is 0
[9.126543 mux config:2, port:1, res:1]
[9.127109 PD:S5->S3]
RTC: 0x000067bf (26559.00 s)
[9.127985 power state 2 = S3, in 0x003f]
RTC: 0x000067bf (26559.00 s)
[9.128640 power state 7 = S3->S0, in 0x003f]
```

This example shows successful power on of the AP as the AP transitions from the G3
state all the way to the S0 state.

The console messages shown in brackets `[]` include a timestamp. This timestamp
records when the corresponding console message was printed.

The power signal changes are preceded by the message `<N> signal changes:`.
Power signal changes are recorded at interrupt priority into a special buffer
and are not displayed in real time. Instead, printing of the buffer is deferred
until the EC is no longer executing at interrupt priority. This causes the power
signal changes shown on the console to be out of order with respect to the other
EC messages.

The power signal changes include a timestamp to help you correlate when the
actual power signal changed compared to other messages.  From the example above,
the first power signal change recorded is the `DSW_PWROK` signal transitioning
from 0 to 1, and this is recorded at timestamp `6.807298`. Using the regular EC
console timestamp, you can reconstruct the real power sequence to look like the
following:

```
> powerb
Simulating 200 ms power button press.
[6.790816 power button pressed]
[6.791133 PB pressed]
[6.791410 PB task 1 = pressed]
[6.791755 PB PCH pwrbtn=LOW]
[6.792049 PB task 10 = was-off, wait 199362]
RTC: 0x000067bc (26556.00 s)
[6.792786 power state 5 = G3->S5, in 0x0000]
[6.793190 Set EN_PP3300_A: 1]
[6.793905 SW 0x03]
  6.807298  +0.000000  DSW_PWROK => 1             // Manually re-ordered entry
[6.817627 Set PCH_DSW_PWROK: 1]
[6.818007 Pass thru GPIO_DSW_PWROK: 1]
[6.818351 Set EN_PP5000_A: 1]
RTC: 0x000067bc (26556.00 s)
  6.903417  +0.096119  SLP_SUS_L => 1              // Manually re-ordered entry
[6.903830 power state 1 = S5, in 0x0029]
  6.908471  +0.005054  PG_EC_RSMRST_ODL => 1       // Manually re-ordered entry
[6.918735 Pass through GPIO_RSMRST_L_PGOOD: 1]
i2c 7 recovery! error code is 13, current state is 0
Simulating power button release.
> [6.991576 power button released]
[6.992009 PB task 10 = was-off]
[6.992376 PB released]
[6.992635 PB task 6 = released]
[6.992958 PB PCH pwrbtn=HIGH]
[6.993256 PB task 0 = idle, wait -1]
[6.993806 PB released]
[6.994149 PB task 6 = released]
[6.994512 PB PCH pwrbtn=HIGH]
[6.994812 PB task 0 = idle, wait -1]
[6.995768 SW 0x01]
1 signal changes:
  7.909941  +0.000000  SLP_S0_L => 1
[9.026429 Fan 0 stalled!]
RTC: 0x000067bf (26559.00 s)
[9.124643 power state 6 = S5->S3, in 0x003f]
i2c 3 recovery! error code is 13, current state is 0
[9.126543 mux config:2, port:1, res:1]
[9.127109 PD:S5->S3]
RTC: 0x000067bf (26559.00 s)
[9.127985 power state 2 = S3, in 0x003f]
RTC: 0x000067bf (26559.00 s)
[9.128640 power state 7 = S3->S0, in 0x003f]
```


*TODO ([b/147808790](http://issuetracker.google.com/147808790)) Add
documentation specific to each x86 processor type.*

[config.h]: ../new_board_checklist.md#config_h
