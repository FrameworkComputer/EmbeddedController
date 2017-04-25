EC update over USB
==================

CR50-specific notes
-------------------

The CR50 firmware image consists of multiple sections, of interest to the
USB updater are the RO and RW code sections, two of each. When firmware update
session is established, the CR50 device reports locations of backup RW and RO
sections (those not currently used by the device).

Based on this information the updater carves out the appropriate sections from
the full CR50 firmware binary image and sends them to the device for
programming into flash. Once the new sections are programmed and the device
is restarted, the new RO and RW are used if they pass verification and are
logically newer than the existing sections.

There are two ways to communicate with the CR50 device: USB and `/dev/tpm0`
(when `usb_updater` is running on a chromebook with the CR50 device). Originally
different protocols were used to communicate over different channels,
starting with version 3 the same protocol is used.

Update protocol
---------------

The host (either a local AP or a workstation) is the master of the firmware
update protocol, it sends data to the CR50 device, which processes it and
responds.

The encapsulation format is different between the `/dev/tpm0` and USB cases:

      4 bytes      4 bytes         4 bytes               variable size
    +-----------+--------------+---------------+----------~~--------------+
    + total size| block digest |  dest address |           data           |
    +-----------+--------------+---------------+----------~~--------------+
     \           \                                                       /
      \           \                                                     /
       \           +----- FW update PDU sent over /dev/tpm0 -----------+
        \                                                             /
         +--------- USB frame, requires total size field ------------+

The update protocol data units (PDUs) are passed over `/dev/tpm0`, the
encapsulation includes integrity verification and destination address of
the data (more of this later). `/dev/tpm0` transactions pretty much do not
have size limits, whereas the USB data is sent in chunks of the size
determined when the USB connection is set up. This is why USB requires an
additional encapsulation into frames to communicate the PDU size to the
client side so that the PDU can be reassembled before passing to the
programming function.

In general, the protocol consists of two phases: connection establishment
and actual image transfer.

The very first PDU of the transfer session is used to establish the
connection. The first PDU does not have any data, and the `dest address`
field is set to zero. Receiving such a PDU signals the programming function
that the host intends to transfer a new image.

The response to the first PDU varies depending on the protocol version.

Note that protocol versions before 5 are described here for completeness,
but are not supported any more.

Version 1 is used over `/dev/tpm0`. The response is either 4 or 1 bytes in
size. The 4 byte response is the *base address* of the backup RW section,
and there is no support for RO updates. The one byte response is an error
indication, possibly reporting flash erase failure, command format error, etc.

Version 2 is used over USB. The response is 8 bytes in size. The first four
bytes are either the *base address* of the backup RW section (still no RO
updates), or an error code, the same as in Version 1. The second 4 bytes
are the protocol version number (set to 2).

All versions above 2 behave the same over `/dev/tpm0` and USB.

Version 3 response is 16 bytes in size. The first 4 bytes are the error code
the second 4 bytes are the protocol version (set to 3) and then 4 byte
*offset* of the RO section followed by the 4 byte *offset* of the RW section.

Version 4 response in addition to version 3 provides header revision fields
for active RO and RW images running on the target.

Once the connection is established, the image to be programmed into flash
is transferred to the CR50 in 1K PDUs. In versions 1 and 2 the address in
the header is the absolute address to place the block to, in version 3 and
later it is the offset into the flash.

Protocol version 5 includes RO and RW key ID information into the first PDU
response. The key ID could be used to tell between prod and dev signing
modes, among other things.

Protocol version 6 does not change the format of the first PDU response,
but it indicates the target's ability to channel TPM vendor commands
through USB connection.

### Vendor commands

When channeling TPM vendor commands the USB frame looks as follows:

      4 bytes      4 bytes         4 bytes       2 bytes      variable size
    +-----------+--------------+---------------+-----------+------~~~-------+
    + total size| block digest |    EXT_CMD    | vend. sub.|      data      |
    +-----------+--------------+---------------+-----------+------~~~-------+

Where `Vend. sub` is the vendor subcommand, and data field is subcommand
dependent. The target tells between update PDUs and encapsulated vendor
subcommands by looking at the `EXT_CMD` value - it is set to `0xbaccd00a` and
as such is guaranteed not to be a valid update PDU destination address.

These commands cannot exceed the USB packet size (typically 64 bytes), as
no reassembly is performed for such frames.

The vendor command response size is not fixed, it is subcommand dependent.

The CR50 device responds to each update PDU with a confirmation which is 4
bytes in size in protocol version 2, and 1 byte in size in all other
versions. Zero value means success, non-zero value is the error code
reported by CR50.

Again, vendor command responses are subcommand specific.
