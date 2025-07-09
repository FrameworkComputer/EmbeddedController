# cq-excludes

## Overview

This tool checks the cq settings to make sure that the expected files are
being run through the global CQ. By comparing the files that exist with
the patterns defined in ~/chromiumos/infra/config/build_targets.star.

The problem is that we would like a few files to run the global (slow) CQ,
but the patterns are defined in terms of what should be excluded.

I.e. we would like `src/platform/ec/util/ectool.cc` to trigger the CQ, so we
have a exclude pattern like `src/platform/ec/[^u]*/**`

This tool is written in go, because the code that checks the patterns is
`infra/go/src/infra/cros/internal/buildplan/buildplan.go:findFilesMatchingPatterns`.

## Executing

This should work inside or outside the chroot.

```shell
( cd ~/chromiumos/infra/config && ./regenerate_configs.py -b )
~/chromiumos/src/platform/ec/util/cq-excludes/cq-excludes.sh
```

or if you are experimenting, you can pass the patterns on the command line

```
~/chromiumos/src/platform/ec/util/cq-excludes/cq-excludes.sh \
  'src/platform/ec/[^uz]*/**' 'src/platform/ec/zephyr/*' \
  'src/platform/ec/zephyr/[^z]*/**' 'src/platform/ec/*'
```

## Fixing problems.

If the exclude patterns are not matching the right files, then edit the
_only_build_paths array in `~/chromiumos/infra/config/build_targets.star`, and
then run the `./regenerate_configs.py -b` from the `infra/config` directory.
