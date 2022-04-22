# TPM Seed for Fingerprint MCU

Authors: pmalani@google.com, norvez@google.com

Reviewers: semenzato@google.com, apronin@google.com, mnissler@google.com and
others

Last Updated: 2018-11-01

[TOC]

## Objective

Increase security for Fingerprint (FP) templates by using a TPM-sourced seed in
addition to internal FPMCU entropy while encrypting FP templates. The
TPM-sourced seed will be derived from the system key which is loaded from the
TPM during boot in mount-encrypted.

## Background

Fingerprint authorization in ChromeOS, relies on encrypted FP templates which
are stored in each user’s mount directory. These templates are created and
encrypted by the FPMCU during FP enrollment, before being sent back to the AP
(Application Processor). When the user logs in, these templates are sent to the
FPMCU where they get decrypted and loaded.

The encryption is performed in the FPMCU using entropy which is internal to the
MCU and never leaves the MCU. That way, even if the templates are somehow
obtained by and attacker from the user mount directory, they cannot be
decrypted, since the attacker will not have access to the MCU entropy. This
entropy gets reset on every powerwash/recovery.

The complete design doc is [Fingerprint Authentication on ChromeOS].

## Requirements and Scale

The solution proposed should exhibit the following attributes:

*   Strengthens security of FP templates.
*   Does not compromise the security of other sub-systems.
*   Works fast and doesn’t affect time to boot, or reduce boot-time stability.

## Design Ideas

In addition to FPMCU entropy, we include a TPM-sourced seed (derived from the
system key) while performing template encryption. The TPM system key gets
regenerated during powerwash/recovery, so in the event that the FP templates are
accessed due to a runtime exploit, a power-wash / recovery from the user will
ensure:

*   The raw templates cannot be decrypted, since the TPM-seed would have been
    lost irrevocably.
*   Since a new TPM-seed is generated (since a new system key is created), old
    templates can’t be re-used, even if the attacker could somehow gain access
    to the FP MCU entropy.

The overall design consists of two components:

*   Generating a TPM-seed and sending it to the Biometric sensor managers.
*   The Bio sensor managers sending the seed to the FPMCU and programming it
    into the encryption / decryption operations of FP templates.

### TPM seed generation {#seed-generation}

![TPM Seed Diagram]

The TPM seed generation would proceed as follows:

1.  During mount-encrypted execution, after the `System_key` is loaded, the
    TPM-backed system key will be HMAC-ed with a simple salt (the string
    `biod`):

    ```
    TPM_Seed = HMAC_SHA256(System_key, "biod")
    ```

2.  The resulting 256-bit seed (called `TPM_Seed`) will be maintained in a
    `brillo::SecureBlob`.

3.  The `TPM_seed` will be saved into a tmpfs file
    (`/run/biod_crypto_init/seed`) for consumption by `bio_crypto_init`. This
    file's ownership will be set up such that only user/group `biod` can access
    it.

4.  `bio_crypto_init` (the binary which sends the seed to the FPMCU) will be
    spawned after mount-encrypted completes. This is ensured by setting the
    `bio_crypto_init` upstart rules to depend on `starting boot-services`

5.  On the `bio_crypto_init` side, the `TPM_seed` will be retrieved from the
    tmpfs file and forwarded to the FP MCU via the various BiometricManagers.
    Immediately after reading from the tmpfs file, `bio_crypto_init` will nuke
    (write all 0’s and then delete) the tmpfs file.

6.  The upstart rules of biod will be modified such that it will start after
    `bio_crypto_init` stops (this modification can be made in the `.conf` file
    of biod)

#### IPC Mechanism {#ipc}

(For a discussion of various alternatives, please see the
[Alternatives Considered] section)

The IPC mechanism selected should have the following features:

*   Allow to quickly pass the `TPM_seed` between mount-encrypted and
    `bio_crypto_init`.
*   Minimize the presence of extra/asynchronously deleted copies of the
    `TPM_seed` buffer in kernel and memory. This is crucial to minimize the risk
    of access to this seed.

The currently proposed method of passing the `TPM_seed` is to use a **file in
tmpfs**. The sequence would be as follows:

*   mount-encrypted will write the `TPM_Seed` to a file in `/run`
    (`/run/bio_crypto_init/seed`). `/run` is a tmpfs created by the OS for use
    by various system services.
*   `bio_crypto_init` will read the `TPM_Seed` from the known tmpfs file.
*   As soon as `bio_crypto_init` reads the `TPM_Seed`, it will first overwrite
    (`/run/biod_crypto_init/seed`) with all 0s, and immediately after will
    delete `/run/biod_crypto_init/seed`.
*   `bio_crypto_init` can then instantiate its BiometricManager classes and send
    the data to the FP MCU. This way, even if the sending of data fails, there
    will not be any stray copy of the `TPM_seed` in a process’s memory, or in
    tmpfs.

##### Advantages

*   No/minimal buffering of copies of `TPM_Seed` in kernel.
*   No need to create and pass FDs between mount-encrypted and
    `bio_crypto_init`.

##### Disadvantages

*   If `bio_crypto_init` crashes / fails to start, the tmpfs file remains in the
    system, i.e cleanup of tmpfs is reliant on `bio_crypto_init`.

### Programming TPM_Seed into MCU

#### Entropy addition v/s programming TPM Seed

When a device boots up for the first time after going through
recovery/powerwash, biod will force an "Add Entropy" step. This involves:

*   rebooting the FP MCU to RO
*   Performing an entropy addition step
*   Rebooting the FP MCU to RW
*   Verifying that the entropy addition has taken place (by checking the block
    ID of the rollback info on the MCU).

Unfortunately, since the `TPM_Seed` will be stored in MCU RAM, the reboot of the
FPMCU will lead to the `TPM_Seed` being lost until the next boot. In the absence
of a `TPM_Seed`, all FPMCU operations will fail (until the next boot). There is
no opportunity to reprogram the `TPM_Seed`, because that must take place during
mount-encrypted, which must necessarily run before `biod` starts.

There are two proposals to work around this issue. The one eventually selected
has been included here, and the other alternative has been placed in the
[Alternatives Considered] section.

##### Make bio_crypto_init solely set the TPM_Seed (don't perform entropy_add)

In this method, `bio_crypto_init` will not perform any reboot on the MCU, and
solely program the `TPM_Seed`. This would mean that if a device was to boot for
a first time without having done any previous powerwash/recovery, the first boot
would not have FP functionality. FP functionality would be regained on all
subsequent boot (since the entropy would have been added/initialized by then).

The downside of this approach is a poor user experience.

The benefit is a simple implementation of the `bio_crypto_init` tool, which will
consequently also take less time to execute (booting to RO/RW are time consuming
operations).

In practice all devices leaving the factory floor would have `bio_wash
--factory_init` done on them during finalisation to initialise the entropy, and
so this shouldn't affect a large majority of end users.

### Signaling biod to start

In order to avoid races which might occur because both `bio_crypto_init` and
`biod` will try to access the `BiometricManagers`' hardware. We need to ensure
that `biod` only starts after `bio_crypto_init` ends.

To accomplish this, `biod.conf` will be modified to include a dependency on
`bio_crypto_init` to start the daemon. So, the relevant portion of `biod.conf`
will now be:

```
start on started system-services and started uinput and stopped bio_crypto_init
```

### Formula to calculate IKM used for encryption in MCU

In the FPMCU we will use the concatenation of `TPM_Seed` and [`SBP_Src_Key`] as
Input Key Material (IKM) to derive an encryption key. Combined with a random
salt, the pseudo random key (PRK) would be derived as:

```
PRK = HMAC_SHA256(Random_Salt, SBP_Src_Key || TPM_Seed)
```

## Alternatives Considered {#alt-considered}

A few alternatives are being considered for the IPC Mechanism

#### pipe/socketpair

##### Disadvantages

*   The data written to pipes is buffered in internal kernel buffers till it is
    read out from the other end of the pipe/socketpair. In the case of a
    `bio_crypto_init` crashing, this will leave a copy in the internal kernel
    buffers. Question: How long before those internal buffers get cleared in the
    case of the pipe not being read from?

#### Anonymous file (memfd_create) / Anonymous mmap

##### Disadvantages

*   Question: When all references to the anonymous file are dropped, are the
    contents of the anonymous file re-allocated, overwritten, or is the
    corresponding inode simply destroyed (and the data blocks still stick around
    and are reallocated lazily ?)

There was also another alternative considered for the sequence of programming
the TPM seed and initializing the FPMCU: make `bio_crypto_init` add entropy and
then set TPM.

## Security Considerations

### Security boundaries

*   A new minijailed process (`bio_crypto_init`) is run when `starting
    boot-services` is signaled.
*   An IPC takes place between mount-encrypted and `bio_crypto_init` via a tmpfs
    file. The reading and deletion of the tmpfs file is detailed in the
    [IPC Mechanism] section.

### Privileges

*   `bio_crypto_init` runs minijail-ed and runs with user/group `biod`. Only the
    files required for its functioning (i.e., the tmpfs file `/run/`, the
    devnode to access the FPMCU, log directory inside
    `/var/log/bio_crypto_init`) are mounted and visible inside the sandbox. See
    the [minijail0 arguments] for a full explanation.

### Untrusted input

*   The only input is the `System_key` which is retrieved from the TPM anyway
    during mount-encrypted execution. Thus, no additional or new input is being
    fed to the feature.
*   Additionally, the derived TPM-seed is saved in a tmpfs file which has a
    user/group ownership as `biod` so only users `root` or `biod` can access the
    file. Since `bio_crypto_init` runs only during `starting boot-services` and
    the process along with the conf file ensures that the file is deleted after
    execution, there is no additional threat of the `/tmp` file being corrupted.

### Sensitive data

*   The feature involves the storage of a `TPM_Seed` derived from the
    `System_key` from TPM, in a file on tmpfs (the file is zeroed and deleted
    once read by `bio_crypto_init`).

### Attack surface

*   In the event of the contents of the tmp file being read, the `TPM_Seed`
    would not be of much use to the attacker, since the use of `HMAC_SHA256`
    means the attacker would still not have access to the system key (brute
    force trial of HMAC 256 would be required to guess the system salt required
    to produce the TPM-seed).
*   In the unlikely event of the contents of the tmp file being modified before
    they are programmed into the FPMCU, FP unlock would fail (since the
    encrypted templates would not longer be decrypted correctly, since the FPMCU
    encryption key would have changed). The FP templates encryption key is a
    combination of both the `TPM_seed` as well as the internal `SBP_Src_Key`
    combined with a random salt, and since only the encrypted templates are
    stored on the rootfs, the templates would simply be rendered useless. A
    powerwash/recovery can help restore functionality of FP unlock, but new
    templates would have to be registered.
*   This code should not be accessible to remote attackers.

### Implementation robustness

*   `bio_crypto_init` uses two processes. A child process is spawned by
    `bio_crypto_init` and the FPMCU programming is done on the child process.
    The parent process waits for the child process to complete, or kills the
    process if it exceeds a timeout limit. This ensures that the process doesn't
    hang indefinitely.
*   The feature uses tmpfs (`/run/bio_crypto_init/seed`) as an IPC mechanism to
    transfer the `TPM_Seed` between mount-encrypted and `bio_crypto_init`.
    Please see the [Alternatives Considered] and [Design Ideas] section
    regarding rationale behind choosing tmpfs vis a vis socketpair/pipe.

### Cryptography

*   `HMAC_SHA256` is used to derived `TPM_Seed` from the `System_key` as
    described in section [TPM seed generation].

*   `HMAC_SHA256` is also used to derive the FPMCU’s encryption key. This is the
    same as it was earlier; the only change is that source key has been updated
    to also include the `TPM_Seed`.

## Privacy Considerations

This implementation should not have any adverse implications on Privacy (over
and above existing functionality on ChromeOS). This provides security hardening
for the fingerprint templates to prevent their retrieval and mis-use.

[Fingerprint Authentication on ChromeOS]: ../fingerprint/fingerprint-authentication-design-doc.md
[`SBP_Src_Key`]: ../fingerprint/fingerprint-authentication-design-doc.md#sbp-secret-generation
[IPC Mechanism]: #ipc
[minijail0 arguments]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/biod/init/bio_crypto_init.conf;l=36;drc=1fcefaa166e868069ad1b81091333ff75e0657f6
[Design Ideas]: #design-ideas
[TPM seed generation]: #seed-generation
[Alternatives Considered]: #alt-considered

<!-- Images -->

<!-- If you make changes to the docs below make sure to regenerate the PNGs by
     appending "export/png" to the Google Drive link. -->

<!-- https://docs.google.com/drawings/d/1d0ocdnEjsO26c3usP1FwgTZ7VwEr-4ydnC0WMhOnbLY -->

[TPM Seed Diagram]: ../images/cros_fingerprint_tpm_seed.png
