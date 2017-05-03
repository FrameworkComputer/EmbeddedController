# tigertool

tigertool.py is a commandline utility to control the tigertail USB-C mux.
It supports changing the mux status to port A, B, or off.
You can set a serial number to use multiple tigertails at once.

## Usage
Typical usage to set the mux port<br>
```./tigertail.py -m [A|B|off] -s [serialno]```<br>

Reboot the tigertail<br>
```./tigertail.py --reboot```<br>

Set the serial number, when only one tigertail is plugged<br>
```./tigertail.py --setserialno=[serialno]```<br>

Tigertail can support up to 20V 3A on the mux and passes through all
USB-C lines except SBU.
