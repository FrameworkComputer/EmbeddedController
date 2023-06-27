# Parallel Uprevs for platform/ec

## Overview

Since the platform/ec repo doesn't use the normal ebuild based CQ some of the
ebuild files don't use the normal automatic uprev. Instead the go/pupr tool
creates new CLs to uprev the chromeos-base/ec-utils and send them through the
slow CQ running all the ebuilds that depend on it.

## Reviewer responsibilities

For the most part you can ignore these CLs. You don't need to add CR+2 nor CQ+2.
However if you see that the CL has failed the CQ several times, you should
check and see if there is a real bug that is preventing the CL from submitting.

If there is a pupr cl that is known bad, i.e. there is a breakage in the code
and pupr needs to pick a different cl, then please Abandon that cl, don't
CR-2 it.  Pupr will keep adding CQ+2 to the cl over and over again until it
submits, even if it never will.

## Failure implications

If the CL fails to merge, then the ectool binary will not be built with the
latest code.
