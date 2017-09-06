EC update over USB
==================

chip/g (Cr50) and common code (hammer, servo_micro/v4) update over USB protocols
share a lot in terms of protocol and ideas, but use different code bases.

chip/g EC-side implementation is found in `chip/g/*upgrade*`, and the
userspace tool which provides updates over USB among with supporting other
features and interfaces is found in `extra/usb_updater/gsctool.c`.

Common code uses implementations in `common/*update*.c` and
`include/*update*.h`, and `extra/usb_updater/usb_updater2.c` for the userspace
updater.

Cr50-specific notes
-------------------

The Cr50 firmware image consists of multiple sections, of interest to the
USB updater are the RO and RW code sections, two of each. When firmware update
session is established, the Cr50 device reports locations of backup RW and RO
sections (those not currently used by the device).

Based on this information the updater carves out the appropriate sections from
the full Cr50 firmware binary image and sends them to the device for
programming into flash. Once the new sections are programmed and the device
is restarted, the new RO and RW are used if they pass verification and are
logically newer than the existing sections.

There are two ways to communicate with the Cr50 device: USB and `/dev/tpm0`
(when `gsctool` is running on a chromebook with the Cr50 device). Originally
different protocols were used to communicate over different channels,
starting with version 3 the same protocol is used.

Common-code notes
-----------------

For non-Cr50 or chip/g devices (common code), the layout is a bit different,
as devices usually have a single RO and a single RW, where RO is truly read-only
in production, and verifies RW before jumping to it.

For testing and development, `usb_updater2` is provided, while production code
will use `hammerd` (in `src/platform/hammerd`) to update the device.

Update protocol
---------------

The host (either a local AP or a workstation) is the master of the firmware
update protocol, it sends data to the Cr50 device, which processes it and
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
is transferred to the Cr50 in 1K PDUs. In versions 1 and 2 the address in
the header is the absolute address to place the block to, in version 3 and
later it is the offset into the flash.

Protocol version 5 includes RO and RW key ID information into the first PDU
response. The key ID could be used to tell between prod and dev signing
modes, among other things.

Protocol version 6 does not change the format of the first PDU response,
but it indicates the target's ability to channel TPM vendor commands
through USB connection.

Common-code updater also uses protocol version 6, but has a fairly different
`first_response_pdu` header, indicated by setting `1` in the higher 16-bit for
the protocol version field (`header_type`). The response includes fields
such as maximum PDU size (which is not fixed to 1KB like for Cr50), flash
protection status, version string, and a minimum rollback version.

Details can be found in `include/update_fw.h`.

### State machine (update over USB)

This describes the EC-side state machine for update over USB.

IDLE state:

* If host sends update start PDU (a command without any payload, digest = 0
  and base = 0):

  * Reply with `first_update_pdu` block. Go to OUTSIDE_BLOCK state.

* If host sends a vendor command (see below), execute that, reply, and stay
  in IDLE state. Note that vendor commands are only accepted in IDLE state.

OUTSIDE_BLOCK (preparing to receive start of PDU):

* If no data is received in 5 seconds, go back to IDLE state.
* If host sends `UPDATE_DONE` command (by setting `dest address` to
  `0xb007ab1e`), go back to IDLE state.
* If host sends a valid block start with a valid address, copy the rest
  of the payload and go to INSIDE_BLOCK state.

INSIDE_BLOCK (in a middle of a PDU):

* If no data is received in 5 seconds, go back to IDLE state.
* Copy data to a buffer.

  * If buffer is full (i.e. matches the total expected PDU size), write the
    data and go to OUTSIDE_BLOCK.
  * Else, stay in INSIDE_BLOCK.

### Vendor commands (channeled TPM command, Cr50)

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

The Cr50 device responds to each update PDU with a confirmation which is 4
bytes in size in protocol version 2, and 1 byte in size in all other
versions. Zero value means success, non-zero value is the error code
reported by Cr50.

Again, vendor command responses are subcommand specific.

### Vendor commands (common code)

Vendor commands for command code look very similar to the TPM vendor commands
above, except that we use `UPDATE_EXTRA_CMD` (`b007ab1f`) instead of `EXT_CMD`,
and `Vend. sub.` have a limit set of values (unless otherwise noted, commands
take no parameter, and reply with a single 1-byte status code):

* UPDATE_EXTRA_CMD_IMMEDIATE_RESET (0): Tell EC to reboot immediately.
* UPDATE_EXTRA_CMD_JUMP_TO_RW (1): Tell EC (in RO) to jump to RW, if the
  signature verifies.
* UPDATE_EXTRA_CMD_STAY_IN_RO (2): Tell EC (in RO), to stay in RO, and not
  jump to RW automatically. After this command is sent, a reset is necessary
  for the EC to accept to jump to RW again.
* UPDATE_EXTRA_CMD_UNLOCK_RW (3): Tell EC to unlock RW on next reset.
* UPDATE_EXTRA_CMD_UNLOCK_ROLLBACK (4): Tell EC to unlock ROLLBACK on next reset.
* UPDATE_EXTRA_CMD_INJECT_ENTROPY (5): Inject entropy into the device-specific
  unique identifier (takes at least CONFIG_ROLLBACK_SECRET_SIZE=32 bytes of
  data).
* UPDATE_EXTRA_CMD_PAIR_CHALLENGE (6): Tell EC to answer a X25519 challenge
  for pairing. Takes in a `struct pair_challenge` as data, answers with a
  `struct pair_challenge_response`.
