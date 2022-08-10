# Zephyr Testing Resources

This is a compilation of resources for developers participating in the Zephyr
EC Fix-it Week, running from 15 - 19 Aug 2022.

Please note: many of the links in this document will only be accessible to
Googlers.

[TOC]

## Introductory materials

 * [Fix-it week Training Presentation](https://goto.google.com/cros-ec-fixit-week-presentation) -
   by Yuval Peress (Start here - Googlers only)
   * Ask questions during the live presentation using the
     [Dory](https://goto.google.com/cros-ec-fixit-week-dory) (Google only)
 * Sample CLs for your reference:
   * [Writing a new emulator](https://crrev.com/c/2903206)
   * [Writing a console command test ](https://crrev.com/c/3594484)
   * [Writing a host command test](https://crrev.com/c/3530114)
   * [Using test fixtures and local FFF mocks](https://crrev.com/c/3607055)
   * [Defining global FFF mocks](https://crrev.com/c/3252365)
   * [Resetting global FFF mocks](https://crrev.com/c/3500299)

## Finding Work to do

We have assembled a [hotlist](http://b/hotlists/4300616) of low-coverage areas
in the codebase. Please remember to assign bugs to yourself to avoid duplicate
work being performed and do not take bugs until you are ready to actively work
on it.

We also encourage you to check our coverage reports to identify files needing
additional test coverage:

 * [Gitlab coverage reports](https://gitlab.com/zephyr-ec/ec/-/jobs/artifacts/main/file/build/all_builds_filtered_rpt/index.html?job=merged_coverage)
 * [Internal Code Search](https://goto.google.com/cros-ec-fixit-week-cs)
   (enable the coverage layer - Googlers only)

## Submitting tests for review

The fastest way to have your code reviewed is to add
`zephyr-test-eng@google.com` to your CL. This will randomly assign a
member of the Zephyr EC Testing team to your CL. The team will be monitoring
Gerrit extra closely during Fix-it week to streamline reviews. Please do _not_
send CLs directly to individuals or to the wider Zephyr reviewers group.

## Getting Help

Questions on writing, running, and debugging tests should be asked in [YAQS with
the zephyr-rtos-test topic](https://goto.google.com/cros-ec-fixit-week-yaqs).
Part of our goal during Fix-it Week is to assemble a knowledge base of testing
information. Your questions and the resulting dialogue will be very beneficial
to future developers, so please ask away! (Googlers only)
