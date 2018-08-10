# Sweetberry USB power monitoring

This tool allows high speed monitoring of power rails via a special USB
endpoint. Currently this is implemented for the Sweetberry board.

To use on a board, you'll need two config files, one describing the board, a
`.board` file, and one describing the particular rails you want to monitor in
this session, a `.scenario` file.

## Converting from servo_ina configs

- Method 1 (not limited to chroot)

  Many configs can be found for the servo_ina_board in `hdctools/servo/data/`.
  Sweetberry is plug compatible with servo_ina headers, and config files can be
  converted with the following tool:

  ```
  ./convert_servo_ina.py <board>_r0_loc.py
  ```

  This will generate `<board>_r0_loc.board` and `<board>_r0_loc.scenario`
  locally, which can be used with `powerlog.py`.

- Method 2 (recommended for Chrome OS developers, requires chroot)

  If you are using `powerlog.py` within the chroot, copy `<board>_r0_loc.py` to
  `src/third_party/hdctools/servo/data`, then add this line to file:

  ```python
  config_type = 'sweetberry'
  ```

  And run command in chroot:

  ```
  (Anywhere in chroot, just ONCE) cros_workon --host start dev-util/hdctools
  ```

  Then every time you make a change to `<board>_r0_loc.py`, run:

  ```
  (Anywhere in chroot) sudo emerge dev-util/hdctools
  ```

  The command will install the corresponding `.board` and `.scenario` file in
  the chroot. To use `powerlog.py` use the command:

  ```
  (Anywhere in chroot) powerlog -b <board>_r0_loc.board -c <board>_r0_loc.scenario
  ```

  There is no need to specify the absolute path to the `.board` and `.scenario`
  file, once they are installed into the chroot. If there is any changes to
  `<board>_r0_loc.py`, you need to `sudo emerge dev-util/hdctools` again.

## Board files

Board files contain a list of rails, supporting 48 channels each on up to two
Sweetberries. For each rail you must specify a name, sense resistor value, and
channel number. You can optionally list expected voltage and net name.
The format is as follows, in json:

example.board:

```json
[
{ "name": "railname",
  "rs": <sense resistor value in ohms>,
  "sweetberry": <"A" for main Sweetberry, "B" for a secondary Sweetberry>,
  "channel": <0-47 according to board schematic>,
  "v": <optional expected bus voltage in volts>,
  "net": <optional schematic net name>
},
{...}
]
```

## Scenario files

Scenario files contain the set of rails to monitor in this session. The file
format is simply a list of rail names from the board file.

Optionally, you can specify the type of measurement, from the set of
`"POWER"`, `"BUSV"`, `"CURRENT"`, `"SHUNTV"`. If not specified, the default is
power.

example.scenario:

```json
[
"railname",
"another_railname",
["railname", "BUSV"],
["railname", "CURRENT"],
...
]
```

## Output

`powerlog.py` will output a csv formatted log to stdout, at timing intervals
specified on the command line. Currently values below `-t 10000` do not work
reliably but further updates should allow faster updating.

An example run of:

```
./powerlog.py -b board/marlin/marlin.board -c board/marlin/marlin_short.scenario -t 100000
```

Will result in:
```
ts:32976us, VBAT uW, VDD_MEM uW, VDD_CORE uW, VDD_GFX uW, VDD_1V8_PANEL uW
0.033004, 12207.03, 4882.81, 9155.27, 2441.41, 0.00
0.066008, 12207.03, 3662.11, 9155.27, 2441.41, 0.00
0.099012, 12207.03, 3662.11, 9155.27, 2441.41, 0.00
...
```

The output format is as follows:

- `ts:32976us`

  Timestamps either zero based or synced to system clock, in seconds. The column
  header indicates the selected sampling interval. Since the INA231 has specific
  hardware defines sampling options, this will be the closest supported option
  lower than the requested `-t` value on the command line.

- `VBAT uW`

  Microwatt reading from this rail, generated on the INA by integrating the
  voltage/amperage on the sense resistor over the sampling time, and multiplying
  by the sampled bus voltage.

- `... uW`

  Further microwatt entry columns for each rail specified in your scenario file.

- `... xX`

  Measurement in uW, mW, mV, uA, uV as per config.

## Calculate stats and store data and stats

When appropriate flag is set, powerlog.py is capable of calculating statistics
and storing statistics and raw data.

- Example 1

  ```
  ./powerlog.py -b board/eve_dvt2_loc/eve_dvt2_loc.board -c board/eve_dvt2_loc/eve_dvt2_loc.scenario --save_stats [<directory>]
  ```

  If `<directory>` is specified, this will save stats as:
  `<directory>/sweetberry<timestemp>/summary.txt`.
  If `<directory>` does not exist, it will be created.

  If `<directory>` is not specified but the flag is set, this will save stats
  under the directory which `powerlog.py` is in:
  `<directory of powerlog.py>/sweetberry<timestemp>/summary.txt`.

  If `--save_stats` flag is not set, stats will not be saved.

- Example 2

  ```
  ./powerlog.py -b board/eve_dvt2_loc/eve_dvt2_loc.board -c board/eve_dvt2_loc/eve_dvt2_loc.scenario --save_raw_data [<directory>]
  ```

  If `<directory>` is specified, this will save raw data in:
  `<directory>/sweetberry<timestemp>/raw_data/`.
  If `<directory>` does not exist, it will be created.

  If `<directory>` is not specified but the flag is set, this will save raw data
  under the directory which `powerlog.py` is in:
  `<directory of powerlog.py>/sweetberry<timestemp>/raw_data/`.

  If `--save_raw_data` flag is not set, raw data will not be saved.

- Example 3:

  ```
  ./powerlog.py -b board/eve_dvt2_loc/eve_dvt2_loc.board -c board/eve_dvt2_loc/eve_dvt2_loc.scenario --save_stats_json [<directory>]
  ```

  If `<directory>` is specified, this will save MEANS in json as:
  `<directory>/sweetberry<timestemp>/summary.json`.
  If `<directory>` does not exist, it will be created.

  If `<directory>` is not specified but the flag is set, this will save MEANS in
  json under the directory which `powerlog.py` is in:
  `<directory of powerlog.py>/sweetberry<timestemp>/summary.json`.

  If `--save_stats` flag is not set, stats will not be saved.

  `--save_stats_json` is designed for `power_telemetry_logger` for easy reading
  and writing.

## Making developer changes to `powerlog.py`

`powerlog.py` is installed in chroot, and the developer can import `powerlog` or
use `powerlog` directly anywhere within chroot. Anytime the developer makes a
change to `powerlog.py`, the developer needs to re-install `powerlog.py` so that
anything that imports `powerlog` does not break. The following is how the
developer installs `powerlog.py` during development.

Run command in chroot:

```
(Anywhere in chroot, just ONCE) cros_workon --host start chromeos-base/ec-devutils
(Anywhere in chroot, every time powerlog.py is changed) sudo emerge chromeos-base/ec-devutils
```
