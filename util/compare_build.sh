#!/bin/bash

# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Tool to compare two commits and make sure that the resulting build output is
# exactly the same.

. /usr/share/misc/shflags

DEFINE_string 'boards' "nocturne_fp" 'Board to build (\"all\" for all boards)' \
              'b'
DEFINE_string 'ref1' "HEAD" 'Git reference (commit, branch, etc)'
DEFINE_string 'ref2' "HEAD^" 'Git reference (commit, branch, etc)'
DEFINE_boolean 'keep' "${FLAGS_FALSE}" \
               'Remove the temp directory after comparison.' 'k'
# Integer type can still be passed blank ("")
DEFINE_integer 'jobs' "-1" 'Number of jobs to pass to make' 'j'
# When compiling both refs for all boards, mem usage was larger than 32GB.
# If you don't have more than 32GB, you probably don't want to build both
# refs at the same time. Use the -o flag.
DEFINE_boolean 'oneref' "${FLAGS_FALSE}" \
               'Build only one set of boards at a time. This limits mem.' 'o'

# Process commandline flags.
FLAGS "${@}" || exit 1
eval set -- "${FLAGS_ARGV}"

set -e

BOARDS_TO_SKIP="$(grep -E '^skip_boards =' Makefile.rules)"
BOARDS_TO_SKIP="${BOARDS_TO_SKIP//skip_boards = /}"
# Cr50 doesn't have reproducible builds.
# The following fails:
# git commit --allow-empty -m "Test" &&
# ./util/compare_build.sh --board cr50 --ref1 HEAD --ref2 HEAD^
BOARDS_TO_SKIP+=" cr50"

# Can specify any valid git ref (e.g., commits or branches).
# We need the long sha for fetching changes
OLD_REF="$(git rev-parse "${FLAGS_ref1}")"
NEW_REF="$(git rev-parse "${FLAGS_ref2}")"

MAKE_FLAGS=( )
# Specify -j 1 for sequential
if (( FLAGS_jobs > 0 )); then
  MAKE_FLAGS+=( "-j" "${FLAGS_jobs}" )
else
  MAKE_FLAGS+=( "-j" )
fi

BOARDS=( "${FLAGS_boards}" )

if [[ "${FLAGS_boards}" == "all" ]]; then
  BOARDS=( )
  echo "Skipping boards: ${BOARDS_TO_SKIP}"
  for b in $(make print-boards); do
    skipped=0
    for skip in ${BOARDS_TO_SKIP}; do
      if [[ "${skip}" == "${b}" ]]; then
        skipped=1
        break
      fi
    done
    if [[ ${skipped} == 0 ]]; then
      BOARDS+=( "${b}" )
    fi
  done
fi

echo "BOARDS: ${BOARDS[*]}"

TMP_DIR="$(mktemp -d -t compare_build.XXXX)"

# We want make to initiate the builds for ref1 and ref2 so that a
# single jobserver manages the process.
# We should do the build comparison in the Makefile to allow for easier
# debugging when --keep is enabled.
echo "# Preparing Makefile"
cat > "${TMP_DIR}/Makefile" <<HEREDOC
ORIGIN ?= $(realpath .)
CRYPTOC_DIR ?= $(realpath ../../third_party/cryptoc)
BOARDS ?= ${BOARDS[*]}

.PHONY: all
all: build-${OLD_REF} build-${NEW_REF}

ec-%:
	git clone --quiet --no-checkout \$(ORIGIN) \$@
	git -C \$@ checkout --quiet \$(@:ec-%=%)

build-%: ec-%
	\$(MAKE) --no-print-directory -C \$(@:build-%=ec-%)                   \\
		STATIC_VERSION=1                                              \\
		CRYPTOCLIB=\$(CRYPTOC_DIR)                                    \\
		\$(addprefix proj-,\$(BOARDS))
	@printf "  MKDIR   %s\n" "\$@"
	@mkdir -p \$@
	@for b in \$(BOARDS); do	                                      \\
		printf "  CP -l   '%s' to '%s'\n"                             \\
			"\$(@:build-%=ec-%)/build/\$\$b/ec.bin"               \\
                        "\$@/\$\$b-ec.bin";                                   \\
		cp -l \$(@:build-%=ec-%)/build/\$\$b/ec.bin \$@/\$\$b-ec.bin; \\
	done

# So that make doesn't try to remove them
ec-${OLD_REF}:
ec-${NEW_REF}:
HEREDOC


build() {
  echo make --no-print-directory -C "${TMP_DIR}" "${MAKE_FLAGS[@]}"  "$@"
  make --no-print-directory -C "${TMP_DIR}" "${MAKE_FLAGS[@]}"  "$@"
  return $?
}

echo "# Launching build. Cover your eyes."
result=0
if [[ "${FLAGS_oneref}" == "${FLAGS_FALSE}" ]]; then
  build "build-${OLD_REF}" "build-${NEW_REF}" || result=$?
else
  build "build-${OLD_REF}" && build "build-${NEW_REF}" || result=$?
fi
if [[ ${result} -ne 0 ]]; then
  echo >&2
  echo "# Failed to make one or more of the refs." >&2
  exit 1
fi
echo

echo "# Comparing Files"
echo
if diff "${TMP_DIR}/build-"{"${OLD_REF}","${NEW_REF}"}; then
  echo "# Verdict: MATCH"
  result=0
else
  echo "# Verdict: FAILURE"
  result=1
fi
echo

# Do keep in mind that temp directory take a few GB if all boards are built.
if [[ "${FLAGS_keep}" == "${FLAGS_TRUE}" ]]; then
  echo "# Keeping temp directory around for your inspection."
  echo "# ${TMP_DIR}"
else
  echo "# Removing temp directory"
  rm -rf "${TMP_DIR}"
fi

exit "${result}"
