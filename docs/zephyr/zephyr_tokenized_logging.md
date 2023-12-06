# Zephyr EC Tokenized Logging

[TOC]

## Introduction

Tokenized logging is a feature that reduces your binary's image size by
converting log format strings into 32 bit token identifiers. These format
strings and tokens are saved off into a token database used for detokenizing when
viewing the logs. Zephyr EC leverages [Pigweeds Tokenizer](https://pigweed.dev/pw_tokenizer/)
module to accomplish tokenizing and detokenizing of logs.

Tokenized EC logs should be transparent to the developer in most scenarios.
Detokenizing occurs before the log is outputted to console or saved to
`cros_ec.log` on the DUT.

## Enabling Tokenization in EC

Enable Kconfig `CONFIG_PLATFORM_EC_LOG_TOKENIZED` and its dependencies for the
board you want to enable tokenized logging.  Additionally, make sure `picolibc`
and `pigweed` modules are added to your board and don't forget to regenerate
`all_targets.generated.bzl`.

*Note: Tokenized logging is only supported for Zephyr EC*

Example: https://crrev.com/c/5182026.
```
register_brox_project(
    project_name="brox",
    modules=["picolibc", "ec", "pigweed"],
)
```

## Token Database
The token database contains a mapping of the 32 bit hashed token ids to the log
format strings they represent.  Upon receiving a tokenized log message from EC,
the detokenizing process references this database to decode the message back to
its original string.

There are three types of databases available when using tokenization in EC.
 1. **Board Specific** - This database is only compatible with the board its
 built against.
 2. **Unified** - This database is a merged database of all board specific
 databases of a build, this database is compatible with all boards in the
 current build.
 3. **Historical** - This database contains a history of all tokens.  This will
 work with all boards built at any time.  This is the database deployed on
 shipping DUTs and is the database used with released firmware images.  See
 Historical Database section for more details.

## Workflows

### Local EC Images

1. Building EC with Tokenization
    1. Enable Tokenization as defined above.
    2. Kick off your compile as [normal](zephyr_build.md), the token database is
       generated as part of your build.
2. Token Database Locations
    * ZMake
      * **Board Specific**: `build/zephyr/${BOARD}/output/database.bin`
      * **Unified** `build/tokens.bin`

    * Portage/Ebuild
      * **Board Specific**: `"${root_build_dir}/${project}"/output/database.bin`
      * **Unified**: `"${root_build_dir}/tokens.bin`
3. Start `servod` (see servod details)
4. Flash EC - specify `--dut_ip` argument to scp the token database to DUT.
   Enter the DUT's root password when prompted.
  ```
  ./util/flash_ec --board=markarth --zephyr --dut_ip=${DUT_IP}
  ```

### Released EC images

Nothing to do! The token database is preloaded on DUT and servod docker images.
Viewing logs should not require any additional steps!

## Servod Details

Servod accepts a `token_db` argument to the path of the token database. The
default path is `/usr/share/cros_ec/tokens/historical.bin`.  Servod uses
Pigweeds auto updating detokenizer, this monitors the files for changes and
reloads the database when changes occur. This allows you to leave servod running
while rebuilding an EC image/token database.

Upon startup, tokenization is defaulted on or off by the servod overlay
configuration file. You can change the default by modifying the
`use_cros_ec_tokens` control name.
Example: https://crrev.com/c/5202485.
```
  <control>
    <name>uses_cros_ec_tokens</name>
    <doc>CrOS EC logging is tokenized</doc>
    <params drv="echo" value="always" interface="servo"/>
  </control>
```

### Inside Chroot
You can launch servod within chroot with the following command specifying the
path to the token database.
```
sudo servod -b ${BOARD} --token_db=/mnt/host/source/src/platform/ec/build/tokens.bin
```

### Using Docker
*As of 12/6/2023 servod docker is under dogfood so the following may change.*

The docker image supports fetching the historical database from GCS upon
start-up when provided the `--fetch-token-db` argument. This database will
automatically be used to detokenize logs.

*TODO(b/320527595) change argument default to true.*
```
start-servod -b ${BOARD} --channel=release -- --fetch-token-db
```

If you want to use a locally built token database, this will require mounting
your path to the token database to the docker image.
See [servod outside chroot](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/main/docs/servod_outside_chroot.md#i-want-to-flash-firmware-how-do-i-do-that) for details.

You can mount your `ec/build` path to the docker image and specify the token
database to use with the following command.
```
start-servod -b ${BOARD} --channel=release --mount=<your_path>/ec/build:/tmp/cros_ec -n flashing_servod -- --token_db=/tmp/cros_ec/tokens.bin
```

Once connected to ec_uart_pty - you can specify the token database using above
```
%tokens on /tmp/firmware_to_flash/tokens.bin
```

The docker image should be prepopulated with a token database at
`/usr/share/cros_ec/tokens/historical.bin`

### Using EC Console with Tokenization
Once servod is running you can toggle the detokenizer algorithm on or off. This
can be done by connecting to the `ec_uart_pty` and running one of the following
commands.

```
%tokens on
%tokens on <path to token database>
%tokens off
```

Make sure to add the `%` character in the command, this is a special indicator
to EC3PO for OOBM commands.
Using `%tokens on` with no path reloads the last path specified.  On start-up
this will be `/usr/share/cros_ec/tokens/historical.bin`.
*Note: Path to token database is based on where servod is launched and running!*

Viewing logs on a tokenized EC image with tokenization turned off will look like
the following.
```
23-12-06 14:44:37.794 ec:~> pd 0 state
pd 0 state
23-12-06 14:44:39.816 `o7eqFQAGBkVuYWJsZQNTTksDREZQ~`8RegCQA=~`P2J9PQxBdHRhY2hlZC5TTkuEwAQ=~`UpI03wxQRV9TTktfUmVhZHmCCA==~`ubjWdA==~`dwIKAA==~ec:~>
```
A failure to decode will dump its base64 tokenized message as well. You'll
notice above the base64 message is encapsulated with a prefix of
`` ` ``  (backtick) and suffix `~`.

*Note - depending on the terminal emulator used you may need to force a `\n`
character at the end of your command.  Some terminal emulators add `"\r\n"` when
enter is pressed. To force a "\n" when using socat press `<ctrl+v> <enter>` then
send it off with another press of enter. So the command will look something like
`%tokens off<ctrl+v><enter><enter>` using socat.*


## On DUT EC Logs
### [Timberslide](https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/timberslide)

Timberslide expects the database to be located at one of these locations, and
will use the first found in this order.
1. `/usr/local/cros_ec/tokens.bin`
2. `/usr/share/cros_ec/tokens.bin`

The first path can be updated via secure copy, (same method used in flash_ec)
```
scp tokens.bin root@${DUT_IP}:/usr/local/cros_ec/tokens.bin
```

The second path is on a read-only partitiion. `cros deploy` can be used to
manually update this path. Run following commands
```
cros workon start chromeos-base/chromeos-zephyr -b ${BOARD}
cros_sdk cros_workon_make --board=${BOARD} chromeos-base/chromeos-zephyr
cros deploy ${DUT_IP} chromeos-base/chromeos-zephyr
```

## Recovering from failures

A failure can occur when an outdated database is used with an EC image. Pigweed
provides a [Detokenizing CLI
tool](https://pigweed.dev/pw_tokenizer/detokenization.html#detokenizing-cli-tool)
to help with debugging detokenizing failures.

First, you'll need to setup your pigweed root directory.  This typically lives
in the following location.

```
$ export PW_ROOT=~/chromiumos/src/third_party/pigweed/
```

Using the tokenized message above, the below command detokenizes it to the
following:

```
$ python3 ${PW_ROOT}/pw_tokenizer/py/pw_tokenizer/detokenize.py base64 -i failed.txt -p "\`" build/tokens.bin | sed "s/~//g"
Port C0 CC3, Enable - Role: SNK-DFP TC State: Attached.SNK, Flags: 0x9002 PE State: PE_SNK_Ready, Flags: 0x0201 SPR
ec:>
```

The `sed "s/~//g"` is used to strip the token suffix from the output.

You can also fetch the `cros_ec.log` from DUT and detokenize the log as well.
```
$ scp root@${DUT_IP}:/var/log/cros_ec.log ./cros_ec.log
```
A snippet from cros_ec.log may look like the following.
```
2024-01-11T19:58:46.532000Z [9536.808900 HC 0x0137]
2024-01-11T19:58:46.532000Z `jqTWxQdOdXZvdG9uCG5wY3g5bTNmCDAwMTYwMjA3
2024-01-11T19:58:46.532000Z Board:	2
2024-01-11T19:58:46.532000Z RO:	markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z RW:	markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z Build:	markarth-0.0.0-8c88717 2024-01-11 10:15:35
2024-01-11T19:58:46.532000Z 		asemjonovs@asemjonovs TOK
2024-01-11T19:58:46.532000Z Reset flags: 0x00000020 (soft)
```
Notice the log has detokenized logs as well as a failure to detokenize.
Running the following
```
$ python3 ${PW_ROOT}/pw_tokenizer/py/pw_tokenizer/detokenize.py base64 -i ./cros_ec.log -p "\`" build/tokens.bin > cros_ec_detokenized.log
```

Fixes the log
```
2024-01-11T19:58:46.532000Z [9536.808900 HC 0x0137]
2024-01-11T19:58:46.532000Z Chip:	Nuvoton npcx9m3f 00160207
2024-01-11T19:58:46.532000Z Board:	2
2024-01-11T19:58:46.532000Z RO:	markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z RW:	markarth-0.0.0-8c88717
2024-01-11T19:58:46.532000Z Build:	markarth-0.0.0-8c88717 2024-01-11 10:15:35
2024-01-11T19:58:46.532000Z 		asemjonovs@asemjonovs TOK
2024-01-11T19:58:46.532000Z Reset flags: 0x00000020 (soft)
```

## Historical Database Management

The historical token database is the database to support all boards and
its entire history of log format strings used over time.  This database should
handle all boards no matter when it was released.
This lives at `https://storage.googleapis.com/chromeos-localmirror/cros_ec/tokens/historical.bin`

Database management is handled in `recipes/build_firmware_historical_db.py`.  The
[firmware-zephyr-token-db-uploader](https://ci.chromium.org/ui/p/chromeos/builders/informational/firmware-zephyr-token-db-uploader) builder will update the database on a daily basis.
Race conditions between builders are handled using [request-preconditions](https://cloud.google.com/storage/docs/request-preconditions).
This allows multiple builders (such as firmware branches) to run the same recipe
to fetch, merge, and upload the database to GCS.

## Token Collisions

TODO(b/287267896)
Upon CQ submission, LUCI will identify when collisions occur and notify the
developer to alter their log statement.
