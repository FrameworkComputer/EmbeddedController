# Code Reviews

The `platform/ec` repository makes use of a code review system that tries to
evenly distribute code reviews among available reviewers.

[TOC]

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

## Code review guidelines
### Goals for guidelines

* Minimize review latency.
* Minimize duplication of effort among reviewers.

### Guidelines for all code reviews

* Keep each CL to one logical change.
  * Where there are several layers of dependencies, break the patch up into
    multiple CLs.
  * Avoid significant scope increases in subsequent patchsets; when reviewers
    ask for significant additions, consider adding them in a follow-up CL
    instead of uploading a new patchset.
  * Refactor in a separate CL from functional changes; this also applies to
    unrelated style and typo fixes.
  * See Google small CL [guidelines][5] for more tips.
  * If you are about to send out more than 400 lines of non-trivial changes, and
    you haven’t written a design doc, consider whether you need one and who
    should review it.
* Prefer to discuss significant design issues in bugs or design docs, before
  code review.
  * Associate a bug with a code review (`BUG=...`) when context is relevant.
* Indicate clearly when a CL is not yet ready for review and when it is; don’t
  start the review until you’ve stopped making changes.
  * Use `repo upload --no-email` to avoid sending emails for CLs not ready for
    review.
  * Use Gerrit comments as needed to clarify that a CL is now ready for review.
  * Recommended: Leave “WIP” in the CL title until it is ready to review.
  * Optional: Mark the CL as “Work in Progress” in Gerrit (or use `repo upload
    --wip`) until it is ready to review.
  * Optional: Mark the CL as Verified+1 when it is ready for review.
  * Optional: Mark the CL as Verified-1 _until_ it is ready for review.
* Make sure that `make buildall` succeeds after each individual change; this
  facilitates bisecting.
* Assign reviewers with a specific scope for each reviewer.
  * Ideally one person will be qualified to review the entire CL; pick that
    person.
  * If multiple domain experts are required, indicate in the review request
    which parts each of them should review; don’t add a reviewer without a
    scope.
  * If someone needs to be aware that a change is happening, add them as CC; do
    this conservatively.
  * TODO: Use the attention set to clarify whose turn it is to respond, when
    that feature becomes available.
* Consider multi-stage review: Get LGTM within a certain scope before moving to
  the next reviewer.
  * This may help reduce the number of patchsets and duplicated review effort.
  * It is probably most helpful when multiple reviewers should look at the same
    files.
  * Prioritize reviewers who may want fundamental changes, e.g. a component
    maintainer or domain expert should look at it before a language expert.
* Keep related changes in a single relation chain if practical.
  * If not, consider using [topics][2] in Gerrit to group related CLs.
* Try to make visible updates actionable for reviewers.
  * Don’t post a comment saying that you’ve done something before the patchset
    that does it.
  * Don’t post comments that say you’ll do something in the next patchset; just
    wait for that patchset.
  * If practical, don’t upload a new patchset until you can usefully respond to
    all unresolved comments.
  * Use `repo upload --no-email` for patchsets that address comments before the
    responses to those comments.
  * Try to minimize rebases in the middle of a review; if practical, don’t
    rebase until just before submitting.
* Try to make review comments maximally actionable for authors, who may be in
  different timezones or may be managing a relation chain with multiple
  reviewers.
  * Prefer to provide feedback on an entire CL in one shot, so that the author
    could plausibly respond to all of it and need no further review.
* Resolve outstanding comments before submitting.
  * Don’t mark a comment as resolved until the question has been answered,
    request completed, concern assuaged, etc.
  * Typically, a reviewer should not CR+2 a CL until the reviewer’s comments
    have been resolved to their satisfaction. To reduce latency, a reviewer may
    CR+2 a CL with the stated assumption that the author will address
    outstanding minor comments; in this case, the author should resolve them
    before submitting.
* Abandon changes that you don’t intend to keep working on. (`repo abandon`
  doesn’t do it on Gerrit.)
  * This should probably include CLs that, after review, you want to
    substantially rewrite or don’t expect to submit for a long time.

### Guidelines for partner code reviews

* Complete partner-internal code review (using Gerrit) before assigning to
  Google reviewers.
* Send CLs to `cros-ec-reviewers@google.com` for review by default.
  * Or send to specific Googlers with domain expertise, consistent with above
    guidelines.

### Guidelines for extended partner efforts

* Discuss at the beginning of the effort which Googlers should review or be CCed
  on CLs.
  * Optionally create a [GWSQ][4] specific to the effort.

## Reference

* [Coreboot Gerrit Guidelines][3]
* [Google small CL guidelines][5]

[1]: http://google3/chrome/crosinfra/gwsq/ec_reviewers
[2]: https://gerrit-review.googlesource.com/Documentation/intro-user.html#topics
[3]: https://doc.coreboot.org/getting_started/gerrit_guidelines.html
[4]: https://g3doc.corp.google.com/gws/tools/gwsq/v3/g3doc/gerrit.md
[5]: https://google.github.io/eng-practices/review/developer/small-cls.html
