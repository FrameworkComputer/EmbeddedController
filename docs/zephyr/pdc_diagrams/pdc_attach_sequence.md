# PDC Attach Sequence

[Return to PDC documentation](../pdc.md)

A generic call flow for when a device is plugged into PDC port to illustrate
the communication between PDC, DRIVER, and Power Management modules.

*Note: Between step 9 and 13, the connector status is NOT valid yet, as the driver
 has not populated the structure until step 13.  Power Mgmt is not informed of this
 until step 15.*
![PDC Attach Sequence](pdc_attach_sequence.png)
