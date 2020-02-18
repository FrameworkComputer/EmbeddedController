# Code Reviews

The `platform/ec` repository makes use of a code review system that tries to
evenly distribute code reviews among available reviewers.

## How to request a review

Add `cros-ec-reviewers@google.com` to the reviewer line in Gerrit. A background
job will come around and replace the `cros-ec-reviewers@google.com` address with
the next available reviewer in the EC reviewer rotation. This typically takes on
the order of minutes.

Optionally, you can click the [FIND OWNERS] button in the UI, and select
`cros-ec-reviewers@google.com`.

## When to use review system

If you are modifying code in `common/`, `chip/`, or `core/`, feel free to use
the `cros-ec-reviewers@google.com` system. It is **never** a requirement to use
`cros-ec-reviewers@google.com`. You can always request a review from a specific
person.

## Responsibilities of reviewers

If the selected reviewer is unfamiliar with code in a CL, then that reviewer
should at least ensure that EC style and paradigms are being followed. Once EC
styles and paradigms are being followed, then the reviewer can give a +1 and add
the appropriate domain expert for that section of code.

## How can I join the rotation?

Add your name to the [list of reviewers][1].

[1]: http://google3/chrome/crosinfra/gwsq/ec_reviewers
