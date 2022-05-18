## LED behavior on Intel RVP

There are two LEDs on RVP, they represent battery and charger status
respectively.

LED         | Description
------------|------------------------
CHARGER_LED | Represent charger state
BATTERY_LED | Represent battery state

LEDs on RVP emit a single color (green). Rather than just using the on and off
state of the LED, PWM is used to blink the LED to represent multiple states and
the below table represents the multiple LED states.

LED State      | Description
---------------|------------------------------
LED_ON         | Switch On using gpio/pwmduty
LED_OFF        | Switch Off using gpio/pwmduty
LED_FLASH_SLOW | Flashing with 2 sec period
LED_FLASH_FAST | Flashing with 250ms period

### LED Behavior : Charger

CHARGER_LED is dedicated to represent Charger status and the below table
represents the LED states for the Charger.

Charger Status       | LED States
---------------------|---------------
Charging             | LED_ON
Discharging          | LED_FLASH_SLOW
Charging error       | LED_FLASH_FAST
No Charger Connected | LED_OFF

### LED Behavior : Battery

BATTERY_LED is dedicated to represent Battery status and the below table
represents the LED states for the Battery.

Battery Status              | LED States
----------------------------|---------------
Battery Low (<10%)          | LED_FLASH_FAST
Battery Normal (10% to 90%) | LED_FLASH_SLOW
Battery Full (>90%)         | LED_ON
Battery Not Present         | LED_OFF
