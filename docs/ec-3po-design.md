# EC-3PO: The EC console interpreter

[TOC]

## Introduction

Today, the Chromium OS Embedded Controller (EC) has a very rich debug console
that is very helpful and has features including command history, editing,
timestamps, channels and much more. However, all of these features currently use
up valuable flash space that a number of our boards desperately need. We
constantly run into this problem where boards are running out of space and
people have to hack out a lot of code just so the image will fit. It's been
occurring with more frequency lately and I imagine it will continue to occur as
we add more features to our EC code base. What we could do instead is move all
of that console functionality out to a separate utility and turn the EC console
into a binary mode which would only speak in host command packets and debug
output packets. EC-3PO would serve as the interpreter translating from the
traditional EC console that we all know and love to host command packets sent
down to the EC and vice versa.

## Benefits

The benefits to be gained are numerous and can all occur without changing
people's existing workflow. The only slight impact might be that we instruct
people to `emerge hdctools` every so often. All people would notice would be
that the EC images would be getting smaller and/or more console features.

### Testing & FAFT

Currently, [FAFT](https://www.chromium.org/for-testers/faft) runs its tests
while trying to parse strings from the EC console. This method can be fairly
fragile as debug output can be interleaved with the console input. A lot of
items could be improved by switching to this host command packet interface.

* Communicating in packets makes testing easier and more robust.
  * When FAFT is running, the EC could be in a binary mode where it only
    communicates in host command packets. These packets are easier to parse,
    create, and filter on.
  * With filtering, you get the added bonus of not having unwanted debug output.
  * It allows us to really test the host command interface which is how the EC
    talks to the AP anyways.
* Better testing of existing host command handlers.
  * By speaking in host command packets, we can reuse the existing host command
    handlers which is nice since we'll be using the same handlers that are used
    to respond to the AP.
* FAFT would no longer have to worry about the console dropping characters.
  * We can add error checking to the interpreter which would automatically retry
    errors. This alleviates FAFT from trying to check if the EC had properly
    received a line of input. (Ctrl+L)

With better and more reliable tests, we can improve the quality of our EC
codebase.

### Space Savings

By moving the console functionality off of the EC, we would be able to shave off
a considerable amount of bytes from the EC images. People wouldn't have to worry
as much about creating a debug console print with the fear of bloating up the
image size. Smaller stack requirements by changing `printf` formatting to only
count bytes while moving common strings off the EC. Additionally, most of these
savings will come for free as it will apply to every EC with a console. We won't
be restricted by the type of chip.

### A richer EC console

* We could do things like on-the-fly console channel filtering.
* Coloring specific channels such as "mark all USB PD messages in green".
* Adding colors in general.
* Adding temporary console commands.
* Longer command history which survives EC reboot
* Searching command history
* Redirecting debug output to log files (which causes no interleaving of command
  and debug output)
* Bang commands (`!foo`)

### Better debuggability

Sometimes, there will be an issue with the EC (or believed to be an EC issue)
such as the keyboard locking up on certain keys or rows. At times like that, it
would be nice to have an EC console to see what's going on. Other times maybe
having a servo connected might make the issue not present itself.

* We could do cool things like having an EC console without having to hook up
  servo.
* Run `ectool` from the chroot using a PTY interface.

## Deployment Strategy

There are many facets to this feature, but here's a deployment strategy which
will gradually take us from the current EC console today, to a future where the
console is completely removed from the EC. The goal will be to make this change
as transparent as possible to developers.

### Phase 1: Insertion

_[[Merged to ToT](https://crrev.com/c/320629) on 2016-02-07]_

Phase 1 will most likely consist of getting EC-3PO in place in between servo and
the EC while not modifying the behavior of the console too much. In this phase,
we can replicate the console interface and achieve the following things.

* Replicate command editing.
* Save command history.
* Add error checking to the console commands.

### Phase 2: Assimilation

Phase 2 will start to introduce the host command packet communication.

* Printing will be done via packets and assembled in EC-3PO.
* Console commands now are sent using the host command packets.
  * This will be incremental as console commands are converted.
* Add debug output filtering and redirection/logging.

### Phase 3: Expansion

Phase 3 will expand the feature set of EC-3PO.

* Add PTY interface to `ectool`.
* Add on-device console without `servod`.
* Colored output.
* Command history search.
* Bang commands (`!foo`)

## High Level Design & Highlights

![Diagram with three boxes. EC-3PO with an incoming PTY communicates with the
Serial Driver over another PTY. The Serial Driver communicates with the EC
UART.](./images/ec-3po-high-level-design.png)

### EC Interface

Each host command is a 16-bit command value. Commands which take parameters or
return response data specify `struct`s for that data. See
[`include/ec_commands.h`](https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/master/include/ec_commands.h)
for the current format of request and replies. Currently, there are no changes
made to the format of the host request and response structures.

On the EC, we essentially need to create a UART host command handler. This
handler will be watching the console input for a byte sequence to switch into
this host command mode. The starting sequence for an incoming host command
packet will be `0xDA`, a byte signifying `EC_COMMAND_PROTOCOL_3`. Once this byte
is seen, the EC will transition to its "host command processing mode" and
receive the host command. It will then process the host command, send the binary
host response, and then transition back into normal mode. Ideally, there should
be some locking of the UART to prevent other writes while this is taking place.

By the end of the assimilation (Phase 2), there would be no "normal mode" and
everything would be in the binary host command mode.

### Host Interface

The host interface is where the bulk of the work will be. This will be
converting from the received host commands to console prints as well as
converting the console inputs to host commands. It will also be responsible for
replicating the console. This includes things like moving back and forth for
command editing.

The interpreter should also open a PTY and `dut-control` should return this PTY
as the `ec_uart_pty`. This is to ensure that the change is as transparent as
possible to developers.

### Features

The following are an explanation of a few of the planned features.

#### Command Error Checking

EC-3PO and the EC can perform error checking when sending the commands to the
EC. This feature would be implemented prior to switching to the binary format.
The interpreter can package the command in the following manner.

* 2 Ampersands
* 2 hex digits representing the length of the command
* 2 hex digits representing the CRC-8 of the command
* 1 Ampersand
* The command itself
* 2 newline characters.

This is robust because no commands currently start with `&`. If the EC does not
see ‘`&&`', then one of the ampersands has been dropped. If the EC doesn't see
an ampersand after 4 hex digits, it either dropped a hex digit or the ampersand.
Two newlines, so dropping one of those is alright. Once the EC gets the command
and newline, it can verify the command string hasn't been corrupted.

In the event that the command string was corrupted, the EC can return an error
string back of `&&EE`. If the translator reads a line with at least one
ampersand followed by one "E", then an error had occurred and the translator can
simply retry the console command. This creates a reliable input to the console,
a great win for FAFT.

#### PTY interface to `ectool`

Once the UART host command handler is functional, we could add the UART as one
of the interfaces to `ectool`. This would allow `ectool` to be run from the
chroot simply by having `ectool` communicate over the PTY. Since `ectool`
communicates using host commands anyways, everything should just work™. The
benefits of this include faster development of `ectool` and host commands as
well as a more robust interface for FAFT.

#### On-device EC console without Servo

Once the transition is complete and the console speaks entirely in host
commands, it's no longer necessary to have the console talk solely over the
UART. EC-3PO, or a variant, could exist as a standalone application that could
be bundled in the system image, just like `ectool`. It would then send and
receive host commands using the same interface that `ectool` uses whether it be
LPC or I2C. This would essentially give us a console without having to hook up
servo.

Note for security reasons, this must be locked down to only allow a subset of
commands and debug output when the system is ready to ship.

#### Replacing `cprintf()`

All prints will need to become packets. In these packets will contain the format
string, but all `cprintf()` has to do is parse to format string to determine how
many bytes the parameters take up. Then, `cprintf()` will send the format string
and the parameters to EC-3PO which will create the proper string using that
information. That makes `cprintf()` on the EC smaller and use less stack space
than it would have used for formatting.

We could also have a table of common format strings which the EC could just
provide an index and the parameters.

## Internal Design

EC-3PO is a Python package which aims to migrate the rich debug console from the
EC itself to the host. It is composed of two modules: console and interpreter.

![Diagram of EC-3PO internal design. Within an outer box labelled EC-3PO are the
Console and Interpreter modules. A bidirectional command pipe links the two, and
a debug pipe goes from the Interpreter to the
Console.](./images/ec-3po-internal-design.png)

### Console module

The console module provides the interactive console interface between the user
and the interpreter. It handles the presentation of the EC console including
editing methods as well as session-persistent command history.

The console runs in an infinite loop listening for activity on three things
using the `select()` system call: the PTY served to the user, the command pipe,
and the debug pipe. The debug pipe is a unidirectional pipe from the interpreter
to the console. From this pipe are debug prints that originate from the attached
EC and the console currently writes these strings as is to the user PTY. The
command pipe is bidirectional and is used between the console and the
interpreter for command traffic. An example transaction would be a host command
request and response.

#### Enhanced EC image negotiation

When the user transmits a character on the PTY, the console begins to scan every
byte and perform the appropriate actions. Since not every EC image will support
these enhanced features, the console must perform an interrogation to determine
what behaviour to take. If the interrogation mode is set to auto, this
negotiation takes place every time the enter key is pressed. The interrogation
is very simple 2 way handshake. The console sends down a byte, `EC_SYN` and
waits a timeout period to receive a byte, `EC_ACK`. This timeout period is 300ms
for non-enhanced EC images and 1 second for enhanced EC images. Enhanced EC
images will try to immediately respond to an `EC_SYN` with an `EC_ACK` to
indicate to EC-3PO that the current EC images is enhanced. The non-enhanced EC
image timeout period is intended to be short because non-enhanced EC images will
never reply to the `EC_SYN`. By keeping this timeout short, we are essentially
inserting a slight pause after each command. However, this timeout is long
enough for an enhanced EC image to send its reply of `EC_ACK`. Once the `EC_ACK`
is received by the console, the console increases the timeout to 1 second for
stability purposes just in case the enhanced EC image takes a bit longer to send
its reply for some reason. This negotiation allows EC-3PO to behave correctly
for both enhanced and non-enhanced EC images.

If a user knows that they are not using an enhanced EC image, they can disable
the interrogation by issuing a OOBM command. This will cause the console to
never initiate a negotiation, eliminating the delay. See the "interrogate"
command in the Out of Band Management section for usage.

#### Enhanced vs. Non-Enhanced EC images

All EC images which don't explicitly enable the new features (or were built
before the features were implemented in the code base) are non-enhanced images.
Non-enhanced EC images will be handling the presentation of the console
including editing methods (and history if enabled). Therefore, the EC-3PO
console and interpreter behave as a simple pipe for this case forwarding
everything straight to the EC to handle. For the enhanced EC images, all console
presentation (including editing methods) is handled locally by the EC-3PO
console. Enhanced EC images will also support all of the other features
discussed in this document.

#### Out of Band Management

The interactive console also has an Out of Band Management (OOBM) interface.
This allows commands to be entered that can alter the behavior of the console
and interpreter during runtime. From the console, one can bring up the OOBM
prompt by pressing ‘%' . These were originally added for debug purposes.

##### Supported Commands

* `loglevel <integer>`
  * Allows setting the effective loglevel of the console and interpreter.
* `interrogate <never | always | auto> [enhanced]`
  * Allows control of when and how often interrogation occurs.

### Interpreter module

The interpreter provides the interpretation layer between the EC UART and the
user. Similar to the console module, the interpreter starts an infinite loop of
servicing the user and the EC by means of a `select()` system call. It receives
commands through its command pipe, formats the commands for the EC, and sends
the command to the EC. It also presents data from the EC to either be displayed
via the interactive console module or some other consumer.

The interpreter also keeps track of whether the EC image it's communicating with
is enhanced or not. This is required so that the interpreter can communicate
correctly with the EC. For enhanced EC images, the interpreter will pack
incoming commands in a particular format. This could be the "packed plaintext"
form or the binary host command format. With the packed plaintext form, the
interpreter also supports command retrying by monitoring the response of the EC
and automatically retrying the command with no input from the user.

### Other users

Since the interpreter communicates using pipes, it's not necessary that the user
use the console module. For example, FAFT could directly connect to the
interpreter and send down commands and receive command responses instead of
having to deal with the PTY and instead just deal with python objects.

