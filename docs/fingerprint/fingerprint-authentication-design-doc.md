# Fingerprint Authentication on ChromeOS

Authors: norvez@google.com, vpalatin@google.com

Reviewers: kerrnel@google.com, mnissler@google.com

Last Updated: 2019-01-14

[TOC]

## Objective

### Goals

*   Let users securely unlock their device with just their fingerprint
*   Reuse the same architecture on all future platforms, don’t be tied to a
    specific technology ([Arm TrustZone], [Intel SGX]).
*   Support Android’s [fingerprint authentication framework] so users can for
    example authorise payments in Android apps with their fingerprint. The
    fingerprint implementation needs to comply with Android’s [CDD].

### Non-goals

*   Let users log in with their fingerprint
    *   Users will have to use other authentication methods (e.g. password or
        PIN) to log into their account.
    *   Once logged in, users will be able to unlock the screen with their
        fingerprint

## Background

To unlock their Chromebook users have to enter their password or a PIN.
[Windows] and [macOS] let the user authenticate with their fingerprint for
faster unlocking, we want to bring that capability to ChromeOS.

### Fingerprint matching basics

#### Fingerprint enrollment

When a user wants to register their finger for fingerprint authentication, they
go through the *enrollment* operation. They are asked to touch the sensor
multiple times with different parts of their fingerprint. The
[matching algorithm] uses the images captured during enrollment to build a model
of that fingerprint (known as a *template*).

#### Fingerprint matching

When the user puts their finger on the sensor, an image of the fingerprint is
captured and compared to the fingerprint templates of the enrolled fingerprints
to determine if the fingerprint matches one of the templates.

#### Template update (TU)

When the matching algorithm determines that a fingerprint matches a template
with a high level of certainty, it can (and normally will) use that fingerprint
image to update the template to improve the accuracy of future matching
operations.

### Threat model

There are two main objectives for potential attackers:

*   Large scale collection of biometric data from users by opportunistic
    attackers
    *   This attack is only valuable remotely. In case an attacker has physical
        access to the device they are already able to collect fingerprint data
        left by the user on the device itself without having to attack the
        software.
*   Target a specific user, typically with physical access to the device in
    order to either:
    *   Allow the attacker to enroll their own fingerprint to unlock the device
        at will later on (the “abusive partner” model).
    *   Spoof positive fingerprint matches to let the rest of the system believe
        that a user has successfully identified, for example to break [2FA]
        \("spy" trying to gain access to an organisation’s resources via the
        victim’s computer).

### Privacy and security

*   Biometric data is particularly sensitive, so all operations on fingerprint
    data must happen in a *Secure Biometric Processor* (**SBP**). Attackers must
    not gain access to the user’s fingerprints even if they have exploited the
    software running on the AP.
*   To protect the user’s privacy, fingerprint data must not be accessible
    without the user’s consent, even by Google. Typically it will protected by
    the user’s password.
*   Fingerprint data must not leave the device.
*   For added security, only the specific Chromebook used to enroll the
    fingerprint can use it. Other Chromebooks, even of the same model, must not
    be able to use the enrolled fingerprint.

### Scalability

For Eve, we [considered][Old Design Doc] using SGX as the SBP. However the
complexity of the solution makes that option unattractive, both because of the
amount of dev work required and because of the large resulting attack surface.
It’s also exclusive to Intel, we would have to develop a completely different
architecture for other platforms, which would add more dev work and increase the
attack surface again.

## Overview {#overview}

Devices have a dedicated microcontroller (MCU) running a firmware based on the
[Chromium OS EC] codebase that is used as the *Secure Biometric Processor*
(**SBP**), where all enrollment and matching operations take place. Even if
attackers gained control of the AP, they still would not be able to access the
fingerprint (FP) data since it never leaves the SBP unencrypted.

The SBP controls the sensor directly over a dedicated SPI bus. The SBP is
connected to the host with a different SPI bus, the host has no direct access to
the FP data coming from the sensor.

Enrolled templates for a particular user are stored in the user’s [cryptohome]
but not synced/backed up to the cloud. They are thus encrypted with a key
(`User_Key`) derived from the user’s password, preventing 3rd parties (including
Google) from accessing the fingerprint templates if the user hasn’t entered
their password.

On top of that, enrolled templates are also encrypted by a device-specific
`HW_Key`. `HW_Key` is derived from a secret that has been randomly generated by
the SBP, which prevents decrypting the templates on another device.

### Architecture

![Fingerprint Architecture]

### Typical workflows

#### FP enrollment

1.  User starts the enrollment flow from the Settings UI.
1.  SBP starts the enrollment operation.
1.  SBP captures a number of FP images (exact number depends on the sensor,
    typically 3-4 to 10-12) and builds the template in the SBP’s volatile memory
1.  SBP encrypts the template with `HW_Key` and sends the encrypted template to
    the AP.
1.  AP encrypts the template with `User_Key` and saves it to non-volatile
    storage.
1.  User goes back to step 1 to enroll another finger. A user can typically
    enroll 3 to 5 fingers, depending on how many templates the SBP can hold in
    its internal volatile storage at the same time.

#### User login

1.  User logs in by typing their password.
1.  FP templates of that user go through the first level of decryption, with
    `User_Key`.
1.  FP templates are uploaded to the SBP.
1.  FP templates go through the second level of decryption in the SBP, with
    `HW_Key`.
1.  Deciphered FP templates are kept in the SBP’s volatile memory, ready to use
    for matching operations.

#### Screen unlocking operation

1.  User touches the sensor with their finger.
1.  SBP verifies that the FP image matches one of the user’s templates.
1.  SBP wakes up the AP and sends a “FP matched” message to the AP
1.  The AP unlocks the screen.
1.  Matcher updates the template in the SBP’s volatile memory.
1.  SBP encrypts the updated template with `HW_Key` and sends the encrypted
    template to the AP.
1.  AP encrypts the template with `User_Key` and saves it to non-volatile
    storage.

## Detailed Design {#detailed-design}

### FP template encryption {#template-encryption}

FP templates are encrypted "twice". First, the templates are encrypted by the
SBP with a hardware-bound key that is unique to this SBP and that only the SBP
knows. On top of that, the AP also encrypts the FP templates with a key bound to
the user password.

#### User-bound encryption

The FP templates are stored in a "[cryptohome daemon store folder]" which is
encrypted by [cryptohome] with a key tied to the user password. We plan to
replace this post-launch with a mechanism similar to
[Authentication-Time User Secrets]. Separate design doc to come.

#### Hardware-bound encryption

FP templates are AES-encrypted with `HW_Key`. `HW_Key` is bound to this specific
SBP so encrypted templates can only be deciphered by this specific SBP. To
ensure that a powerwash/recovery/WP toggle/.../ makes the encryption key
impossible to recover, `HW_Key` also depends on a secret held by the [TPM].

We use an AEAD cipher (AES-GCM) to detect if the encrypted templates have been
tampered with by an attacker controlling the AP.

##### SBP secret generation

The SBP generates a new 256-bit random number `SBP_Src_Key` every time the user
goes through recovery or powerwashes the device. The [clobber-state] script
sends a command to the SBP to make it immediately regenerate a new `SBP_Src_Key`
immediately after requesting a TPM clear.

`SBP_Src_Key` is stored by the SBP’s internal Flash and never shared with the
AP.

##### TPM-held Secret

To avoid potential bugs where `SBP_Src_Key` would not always be made
unrecoverable in some corner cases of recovery or powerwash, we make the
encryption key `HW_Key` depend on a secret that is held by the TPM and deleted
every time the TPM is cleared, for example if someone attempts to do a
"[ccd open]" to disable the hardware WP.

The following is a summary of the mechanism, see the specific design doc
[TPM Seed for Fingerprint MCU] for details.

The TPM already holds a "[system key]" `Cros_Sys_Key` in NVRAM space that is
used to derive the encryption key of the stateful partition. That "system key"
can only be read once per boot, typically by [mount_encrypted].

We modify mount_encrypted so that right after reading the seed, it derives a key
`TPM_Seed`:

```
TPM_Seed = HMAC-SHA256(Cros_Sys_Key, "biod")
```

`TPM_Seed` is then uploaded to the SBP where it will part of the
[Input Key Material (IKM)] and immediately cleared from the AP’s memory, while
the attack surface is very small (e.g. no network connections, stateful
partition not yet mounted) to prevent attackers from accessing it.

##### `HW_Key` derivation {#hw-key-derivation}

The `HW_Key` 128-bit AES key for every FP template on the device is derived from
the SBP’s secret and the TPM’s secret to ensure uniqueness. Therefore, even two
identical devices would have different encryption keys. The user ID is also used
as an input for key derivation, so 2 users on the same device won’t share
encryption keys either. Summing up, the key used to encrypt a template depends
on:

*   Device-bound `TPM_Seed`, randomly generated on recovery/powerwash
*   SBP-specific `SBP_Src_Key`, randomly generated on recovery/powerwash
*   User ID on the device
*   Encryption salt, randomly generated before every encryption

###### Salt for key derivation

Every time we update a template, we generate a new random 128-bit salt.

The salt is not required to be secret, so we store `User_Salt` in cleartext next
to the user’s encrypted FP templates on the disk.

On user login, biod sends the salt and the encrypted FP templates to the SBP.
biod also sends the User ID to the SBP. The SBP derives the AES key using [HKDF]
with HMAC-SHA256:

```
HW_Key = HKDF(HMAC-SHA256, SBP_Src_Key, TPM_Seed, User_Salt, User_ID)
```

At that point, the SBP [authenticates and deciphers](#aead) the FP templates.
The SBP then generates a new 128-bit salt `User_Salt_New` randomly and derives a
new AES key:

```
HW_Key_New = HKDF(HMAC-SHA256, SBP_Src_Key, TPM_Seed, User_Salt_New, User_ID)
```

Updated FP templates are then encrypted with `HW_Key_New` before being stored on
the host, along with the new salt `User_Salt_New`.

*Note*: The SBP has a unique serial number hwID that could also be used as an
additional input to the KDF (though it never changes). The entropy is pretty low
and though not easily accessible an attacker who had stolen the device could
gain access to it. After consulting with the security team, using the hwID was
deemed unnecessary since it wasn’t adding real entropy.

##### AEAD (AES-GCM) Encryption {#aead}

To encrypt the FP templates with `HW_Key` we use BoringSSL’s implementation of
AES-GCM128.

###### Initialisation Vector

The encryption operations are done by the R/W firmware that doesn’t have write
access to the Flash, so it can’t keep track of IVs that could have already been
used during previous boots since it has no way to persist state. Instead, the
SBP will generate a random 96-bit IV every time it needs to encrypt a template
with `HW_Key` before sending it back to the host for storage. This only happens
every time a user successfully matches their finger, which assuming 1 match
every second for 10 years would result in 3600\*24\*365\*10 < 350,000,000, so
the risk of reusing an IV is acceptable. To ensure that a compromised host could
not try to generate too many messages to find collisions, the SBP rate-limits
the number of encryption operations to 1 per second.

The IV will be stored on the host with the salt, the encrypted templates and the
16-byte tag for authentication.

###### Authentication Tag

To authenticate the encrypted templates, we use a 128-bit tag that we store in
clear text with the encrypted template.

Authentication of the encrypted templates prevents attackers from generating
random templates to try to attack directly the matching libraries rather than
the AES-GCM128 implementation. It also prevents attackers from trying to pass
their own template instead of the user’s FP template.

###### Encryption Flowchart

Encryption of the FP template in the SBP before the ciphered data is sent to the
AP for storage.

![Encryption Flowchart]

###### Decryption Flowchart

Decryption of the ciphered FP template coming from the AP when the user logs in.

![Decryption Flowchart]

#### FP template disk format

Encrypted templates are stored in a “[cryptohome daemon store folder]” that is
only mounted/decrypted when the user has logged in. The templates are stored as
JSON files with the following fields:

```json
{
  "biomanager": “CrosFpBiometricsManager” string
  "version": integer describing the version of the file format. Set to 1 at launch
  "data": Base64-encoded string containing the `HW_Key`-encrypted template
  "label": user-configurable human-readable string listed in the UI
  "record_id": UUID of that template generated at enrollment time
}
```

##### `HW_Key`-encrypted template format

The content of the "data" field is the encrypted template that can be deciphered
by the SBP.

Field Name | Field description                                                     | Field size (bytes) | Field offset (bytes)
---------- | --------------------------------------------------------------------- | ------------------ | --------------------
Version    | Number describing the version of the file format. Set to 3 at launch. | 2                  | 0
Reserved   | Reserved bytes, set to 0                                              | 2                  | 2
Nonce      | Randomly-generated IV                                                 | 12                 | 4
Salt       | Randomly-generated salt                                               | 16                 | 16
Tag        | AES-GCM Authentication Tag                                            | 16                 | 32
Template   | Encrypted template                                                    | 47552              | 48

When the user logs in, the cryptohome daemon store folder of that user is
mounted and the JSON files become available to biod. For every enrolled finger,
biod sends the `HW_Key`-encrypted template to the SBP. The SBP
[derives `HW_Key`](#hw-key-derivation) for that template and deciphers the
template.

### Protection of the SBP

To access the unencrypted data and/or `HW_Key`, attackers have 3 main options:

*   Temporarily gain read or even execution access in the SBP through a firmware
    bug
    *   Would allow an attacker to gain access to the clear text FP data and/or
        the encryption key
    *   Mitigation strategy in [Prevent RW exploits](#prevent-rw-exploits)
*   Turn a temporary compromise of the SBP’s firmware into a permanent exploit
    by replacing the SBP’s firmware with a firmware controlled by the attacker
    *   Would allow an attacker to gain access to the clear text FP data and/or
        the encryption key
    *   Would allow an attacker to spoof positive FP matches, defeating 2FA
    *   Mitigation in [Verified firmware](#verified-firmware)
*   Use physical access and control of WP to load a compromised firmware to the
    SBP
    *   Mitigation in [Control WP/BOOT0](#control-wp-boot0)

#### Verified firmware {#verified-firmware}

To verify the integrity of the firmware we use a mechanism similar to the one
used to protect the EC in detachable keyboards as described in
[Detachable Base Verified Boot].

The SBP has a minimalistic RO firmware that contains the public part of an
RSA-3072 exponent 3 key pair. The corresponding private key is only accessible
by the ChromeOS signers and is used to sign SBP firmwares. On boot the RO
firmware verifies the signature of the RW firmware. If the RW signature is
valid, the RO firmware protects itself by setting the WP bit of the Flash then
jumps to RW.

##### Anti-rollback

On top of verifying the signature of the RW firmware, the RO firmware must
verify that the RW firmware is not an outdated version with known
vulnerabilities. This is required to prevent attackers from loading valid but
vulnerable RW firmwares. This is achieved with an anti-rollback mechanism as
described in
[Detachable Base Verified Boot][Detachable Base Verified Boot Anti-Rollback].

###### Nocturne-specific anti-rollback

On nocturne, the SBP is an STM32H7 MCU, with 128K Flash blocks. We still need 2
pingpong RB blocks to prevent data loss, so the Flash map looks like this:

Name                | Size
------------------- | -------
RO firmware         | 128 KB
Blank               | 640 KB
RB1 + `SBP_Src_Key` | 128 KB
RB2 + `SBP_Src_Key` | 128 KB
RW firmware         | 1024 KB

The Nocturne SBP uses the same Flash block for the anti-rollback mechanism and
`SBP_Src_Key`. Most of the anti-rollback mechanism is identical to the one
described in
[Detachable Base Verified Boot][Detachable Base Verified Boot Anti-Rollback],
and the key is similar to the entropy/secret stored for
[Detachable Base Swap Detection].

The rollback minimum version is updated whenever RO has verified RW signature,
and the RW rollback version is larger than what is stored in the RB block.

When re-keying is desired, `SBP_Src_Key` is updated by doing the following
operation:

```
SHA256(SBP_Src_Key || entropy)
```

where `entropy` is generated from STM32H7 True Random Number Generator (see
[RM0433] Chapter 33 for details). Since there are 2 rollback blocks, and we
ping-pong between them, re-keying should involve updating `SBP_Src_Key` twice,
so that both blocks are erased, and no remnant of the previous key is left over.

#### Prevent RW exploits {#prevent-rw-exploits}

Even non-persistent exploits in the RW firmware would be problematic if the
attacker was able to read the content of the memory or the Flash, e.g. via a
buffer overflow, since they could gain access to the clear text FP data and/or
the encryption key. If the attacker was also able to execute code in RW, they
would be able to spoof positive FP matches.

##### Attack through host command interface {#attack-host-command}

The AP can send a number of commands to the SBP, for example to wait for a match
or to update the RW firmware. In case of a vulnerability in the protocol an
attacker with (potentially remote) access to the AP<->SBP SPI bus could send bad
specially crafted commands to the SBP and potentially gain read, write or even
execute permissions in the SBP.

###### Mitigation strategies

*   Limit the size of the API exposed by the SBP to the AP
*   Fuzz the host command interface

##### Attack through crafted templates uploaded to the SBP {#template-attack}

The AP partially deciphers (with `User_Key`) the templates stored on the disk
then sends the `HW_Key`-encrypted templates to the SBP where they will be
deciphered and then passed to the matching algorithm. An attacker could submit a
carefully crafted template to the SBP that would exploit holes in the closed
source matching algorithm library.

###### Mitigation strategies

We use AEAD to decipher and authenticate the templates received from the AP,
they are not passed directly to the matching library. Bad templates will be
intercepted by the decryption code.

##### RAM noexec

Even if an attacker gained some level of access to the SBP, the RAM is not
executable so it would be hard for the attacker to execute compromised code, for
example to spoof successful authentication and break 2FA or to attempt to turn
into a persistent compromission of the SBP by writing a new compromised firmware
to Flash.

#### Control WP/BOOT0 {#control-wp-boot0}

The BOOT0 pin of the MCU is gated by the WP controlled by Haven. Since toggling
the WP bit from Haven requires physical access to the device, remote attackers
can’t toggle the BOOT0 pin to make the MCU start in bootloader mode and
read/write the Flash from the AP.

However, with physical access (> 5 minutes) an attacker could disable the WP
signal from Haven and toggle the BOOT0 pin to start the MCU in bootloader mode.

##### Flash protected with RDP Level 1

We will set the Flash in [Global Read-out Protection (RDP) mode Level 1]. This
means that attackers with physical access who would manage to start the MCU in
bootloader mode would not be able to read `SBP_Src_Key` from the Flash.
Attackers would still be able to read the content of the RAM and registers but
at that point the MCU would just have rebooted and the RAM would be empty.

If the attacker attempted to write their own code to the Flash (for example to
replace RO), RDP Level 1 would only allow that after a complete erasure of the
Flash that would wipe `SBP_Src_Key`, preventing the user from decrypting FP
templates.

*Note*: An attacker with that level of access could in theory replace the RO
firmware with their own firmware. This would however have wiped enrolled
fingers, giving the user an indication that their device might have been
tampered with. This wouldn’t give access to existing FP templates or images to
the attacker, only future enrollments.

##### RMA

To ensure that a device is clean after e.g. refurbishing, the RMA procedure
would require that the operator disabled the WP bit from Haven and toggled BOOT0
to switch to bootloader mode. After that a known good RO and RW firmware can be
written to the Flash and the operator will reenable the WP bit from Haven.

## Security Considerations

### Security boundaries

#### Chrome to system services

Biod and Chrome communicate over D-Bus (defined [here][biod D-Bus API]).

*   Chrome lets biod know when the user has signed in, so biod can load the
    templates to the [SBP](#overview).
*   Biod lets Chrome know when the SBP has detected a positive or negative match
    so Chrome can unlock the screen.
*   Chrome tells biod to start/end enrolling a finger.
*   Chrome tells biod to start/end authentication (matching) mode.

#### Kernel to firmware

The SBP uses the `cros_ec` interface, same as the EC. There are additional
SBP-specific host commands that the AP can send to the SBP, see
[Attack through host command interface](#attack-host-command).

### Privileges

#### Sandboxing

Biod uses Minijail ([upstart script][biod upstart script]) for [sandboxing], and
has a [seccomp filter].

### Untrusted input

Encrypted templates are read from the stateful partition where they could be
corrupted or tampered with. Biod itself doesn’t parse that input -it’s still
encrypted by the SBP- and merely marshalls the data around to and from the SBP.
To ensure the integrity of the input, we use [AEAD] with an
[implementation][AEAD implementation] based on BoringSSL.

The encrypted templates are wrapped inside JSON files that could be corrupted or
tampered with. Biod does parse and interpret some fields of those JSON files.
That input is [fuzzed].

### Sensitive data

The SBP handles biometric data, see the [Detailed Design](#detailed-design)
section that describes how we keep that data protected from attackers.

### Attack surface

#### Libraries

*   Biod uses libbrillo and libchrome
*   The SBP firmware is based on the cros_ec code already used in the EC. Two
    significant additions:
    *   Parts of BoringSSL (AES and AES-GCM) ported to cros_ec
    *   3rd-party proprietary blob used for matching, see
        [Closed source blobs in the SBP](#closed-source-blobs).

#### Remote attacks

Neither biod nor the SBP are exposed directly to remote attackers. Since biod
communicates with Chrome over D-Bus, and attacker who had compromised Chrome
could start sending D-Bus commands to biod.

#### Closed source blobs in the SBP {#closed-source-blobs}

The enrollment/matching and image capture libraries are provided by a 3rd-party
vendor in binary form. That proprietary blob went through a security audit by a
3rd party auditor (see the auditor’s [report][Security Audit Report]).

On top of the security audit of the 3rd-party proprietary code, we limit the
attack surface of those libraries by not directly exposing them to user input.
Data (e.g. FP templates) that is fed to those libraries isn’t directly coming
from untrusted user input, it is sanitized by the opensource glue logic and
wrappers. For example, we use AEAD to ensure that the encrypted data that is
deciphered before being passed to the 3rd-party libraries has been generated by
the SBP itself. For more details, see section
[Attack through crafted templates uploaded to the SBP](#template-attack).

### Implementation robustness

#### biod (userspace daemon)

##### Multi-threading/multi-process

biod uses `base::MessageLoopForIO`, no custom multi-thread or multi-process
implementation.

##### State machine implementation

biod has 3 main states:

*   Idle
*   Waiting for a match: controlled by the [AuthSession] object
*   Enrolling a new fingerprint: controlled by the [EnrollSession] object.

#### cros_fp (SBP firmware)

##### Multi-threading/multi-process

We use the [primitives][EC primitives] of the Chromium OS EC: tasks, hooks, and
deferred functions.

##### Memory allocation

Most buffers (e.g. for FP images and templates) are [statically allocated]. The
vendor libraries do require some dynamic memory allocation, we provide
[wrappers functions] that use the [malloc/free memory module for Chrome EC].

##### State machine implementation

There is one main [state machine] that configures the matching/enrollment code
to be ready for a match or to enroll a finger.

### Cryptography

See detailed discussion in the ["FP template encryption"](#template-encryption)
section.

### Metrics {#metrics}

Metrics related to security that we’re collecting through UMA:

*   `Ash.Login.Lock.AuthMethod.Used.ClamShellMode` to know if FP is used to
    authenticate
*   `Ash.Login.Lock.AuthMethod.Used.TabletMode` to know if FP is used to
    authenticate
*   `Fingerprint.Unlock.AuthSuccessful` tracks whether FP authentication was
    successful or not
*   `Fingerprint.Unlock.AttemptsCountBeforeSuccess` tracks how many attempts it
    takes for users to unlock with their fingerprint
*   `Fingerprint.UnlockEnabled` tracks whether FP unlocking is enabled or not
*   `Fingerprint.Unlock.EnrolledFingerCount` reports the number of fingers that
    users have enrolled

Complete list of metrics collected via UMA:
[New UKM collection review - CrOS FP Unlock]

### Potential attacks

#### Enroll a rogue fingerprint

An attacker with physical access to the device could enroll their own
fingerprint under the victim’s account and use it to unlock the device at-will
in the future.

*   Enrollment UI requires the user password before telling biod to start an
    enrollment session, so the attacker would need some form of exploit to
    bypass Chrome and trigger the enrollment. We plan to replace this
    post-launch with a mechanism similar to [Authentication-Time User Secrets].
    Separate design doc to come.
*   Even if it’s not a persistent exploit, a rogue enrolled fingerprint would
    persist.
*   The victim’s fingerprint data would still be secure.
*   The enrollment UI shows how many fingers are enrolled.

## Privacy Considerations

### Fingerprint data is kept locally on the device

The raw fingerprint images themselves never leave the SBP. The fingerprint
templates are kept on the local storage (encrypted both with the `HW_Key` and
the `User_Key`) of the device and not synced to the cloud, encrypted or not.

### Fingerprint data decryption requires the user password

The fingerprint templates are stored in a "[cryptohome daemon store folder]"
which is only mounted when the user logs in. To do so, they must have entered
their password.

### FP matching is not used for login, only unlocking

Before using their fingerprint to unlock the device the user must have logged
in, typically with the Google Account password.

### Lock screen will display a FP icon if enabled

If a user has enabled FP unlocking, a FP icon will be associated to that user on
the lock screen. This potentially lets others know that a user has enabled FP
unlocking. This seems reasonable when the small resulting decrease in privacy is
weighed against the fact that adding an icon greatly improves UX.

### Metrics collection

We collect anonymous metrics through [UMA], see section [Metrics](#metrics) for
details.

### Logs

Biod, the SBP, and Chrome have logs related to the fingerprint process.
[Privacy fields for Fingerprints] lists the log entries and their privacy
implications. Full [PDD is here].

#### Biod

The log files are in `/var/log/biod/`.

#### SBP

The log file is `/var/log/cros_fp.log`.

<!-- Links -->

[2FA]: https://en.wikipedia.org/wiki/Multi-factor_authentication
[AEAD implementation]: https://chromium.googlesource.com/chromiumos/platform/ec/+/aed008f87c3c880edecf7608ab24eaa4bee1bc46/common/fpsensor.c#574
[AEAD]: https://en.wikipedia.org/wiki/Authenticated_encryption
[Arm TrustZone]: https://www.arm.com/products/security-on-arm/trustzone
[Authentication-Time User Secrets]: http://go/authentication-time-user-secrets
[AuthSession]: https://chromium.googlesource.com/chromiumos/platform2/+/eae39a9ad1239f8fbfa8164255578b306ff6ba5c/biod/biometrics_manager.h#96
[biod D-Bus API]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/system_api/dbus/biod/
[biod upstart script]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/biod/init/biod.conf
[ccd open]: https://chromium.googlesource.com/chromiumos/platform/ec/+/cr50_stab/docs/case_closed_debugging_cr50.md#Open-CCD
[CDD]: https://source.android.com/compatibility/android-cdd#7_3_10_fingerprint_sensor
[Chromium OS EC]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/README.md
[clobber-state]: https://chromium.googlesource.com/chromiumos/platform2/+/962ab1bc481db0cf504b5449eb3a3d5008ea7601/init/clobber_state.cc#475
[cryptohome daemon store folder]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/sandboxing.md#securely-mounting-cryptohome-daemon-store-folders
[cryptohome]: https://www.chromium.org/chromium-os/chromiumos-design-docs/protecting-cached-user-data
[Detachable Base Swap Detection]: https://docs.google.com/document/d/1WYdkkSAL_RHVc5mUXnAvBBfAeM7Wj3ABa1dbeTdvm74/edit#heading=h.g74ijelumqop
[Detachable Base Verified Boot Anti-Rollback]: http://go/detachable-base-vboot#heading=h.fimcm174ok3
[Detachable Base Verified Boot]: http://go/detachable-base-vboot#heading=h.dolfbdpggye6
[EC primitives]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/README.md#Software-Features
[EnrollSession]: https://chromium.googlesource.com/chromiumos/platform2/+/eae39a9ad1239f8fbfa8164255578b306ff6ba5c/biod/biometrics_manager.h#92
[fingerprint authentication framework]: https://developer.android.com/about/versions/marshmallow/android-6.0.html#fingerprint-authentication
[fuzzed]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/biod/biod_storage_fuzzer.cc
[Global Read-out Protection (RDP) mode Level 1]: https://www.st.com/content/ccc/resource/technical/document/application_note/b4/14/62/81/18/57/48/05/DM00075930.pdf/files/DM00075930.pdf/jcr:content/translations/en.DM00075930.pdf
[HKDF]: https://tools.ietf.org/html/rfc5869
[Input Key Material (IKM)]: https://en.wikipedia.org/wiki/HKDF
[Intel SGX]: https://software.intel.com/en-us/sgx
[macOS]: https://support.apple.com/en-us/HT207054
[malloc/free memory module for Chrome EC]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/common/shmalloc.c
[matching algorithm]: https://en.wikipedia.org/wiki/Fingerprint#Algorithms
[mount_encrypted]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/cryptohome/mount_encrypted
[New UKM collection review - CrOS FP Unlock]: https://docs.google.com/document/d/1qjDCMcBcrhSeg_uwyEIRsXHKmzUTJahcg6a4YVhkuLo
[Old Design Doc]: https://docs.google.com/document/d/1MdPRmCDkVg1HO9DdbvPT5fDZS2ICg5ys9_ok_K95EEU
[PDD is here]: http://go/cros-fingerprint-pdd
[Privacy fields for Fingerprints]: https://docs.google.com/spreadsheets/d/1jLfnuhfbrImpoxuj92OkAxS_GGrm7QkpQhsUQCkO9ec
[Privacy fields for Fingerprints]: https://docs.google.com/spreadsheets/d/1jLfnuhfbrImpoxuj92OkAxS_GGrm7QkpQhsUQCkO9ec/
[RM0433]: https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/c9/a3/76/fa/55/46/45/fa/DM00314099/files/DM00314099.pdf/jcr:content/translations/en.DM00314099.pdf
[sandboxing]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/sandboxing.md
[seccomp filter]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/biod/init/seccomp/biod-seccomp-amd64.policy
[Security Audit Report]: https://drive.google.com/file/d/0B1HHKpeDpzYnMDdocGxwWUhpckpWM0hMU0tPa2ZjdEFnLU53/view?usp=sharing&resourcekey=0-utAJosm8Lwvx9TOz7F4i7w
[state machine]: https://chromium.googlesource.com/chromiumos/platform/ec/+/90d177e3f0ae729bea7e24934a3c6ef9f2520d45/common/fpsensor.c#252
[statically allocated]: https://chromium.googlesource.com/chromiumos/platform/ec/+/90d177e3f0ae729bea7e24934a3c6ef9f2520d45/common/fpsensor.c#57
[system key]: https://chromium.googlesource.com/chromiumos/platform2/+/23b79133514ac2cd986bce21c398fb6658bda248/cryptohome/mount_encrypted/encryption_key.h#125
[UMA]: http://go/uma
[Windows]: https://www.microsoft.com/en-us/windows/windows-hello
[wrappers functions]: https://chrome-internal.googlesource.com/chromeos/platform/ec-private/+/9ebb3f10c611afff695f679aaeed1a35551a116b/fpc_sensor_pal.c#52
[TPM Seed for Fingerprint MCU]: ../fingerprint/fingerprint-tpm-seed.md
[TPM]: https://www.chromium.org/developers/design-documents/tpm-usage/

<!-- Images -->

<!-- If you make changes to the docs below make sure to regenerate the PNGs by
     appending "export/png" to the Google Drive link. -->

<!-- https://docs.google.com/drawings/d/1-JUWTF7sUTND29BfhDvIudzX_S6g-iwoxG1InPedmVw -->

[Decryption Flowchart]: ../images/cros_fingerprint_decryption_flowchart.png

<!-- https://drive.google.com/open?id=1uUprgLsTUZZ2G2QWRYcRn6zBAh6ejvJagVRD7eZQv-k -->

[Encryption Flowchart]: ../images/cros_fingerprint_encryption_flowchart.png

<!-- https://docs.google.com/drawings/d/1DFEdxfDXEtYY3LNOOJFAxVw2A7rKouH98tnb1yiXLAA -->

[Fingerprint Architecture]: ../images/cros_fingerprint_architecture_diagram.png
