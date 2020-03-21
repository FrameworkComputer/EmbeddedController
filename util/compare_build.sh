#!/bin/bash

# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Tool to compare two commits and make sure that the resulting build output is
# exactly the same.
#
# The board parameter is a space separated list containing any valid board
# or special board group listed below. Items added to the list can be prefixed
# with a + or - to enforce that it is added or removed from the active set of
# boards. Boards are added or removed in the order in which they appear.
# * all       - All boards that are built by the "buildall" target
# * fp        - All relevant boards for fingerprint
# * stm32     - All boards that use an STM32 chip
# * npcx      - "
# * mchp      - "
# * ish       - "
# * it83xx    - "
# * lm4       - "
# * mec1322   - "
# * max32660  - "
# * mt_scp    - "
#
# Example: --boards "+all -stm32"

# Cr50 doesn't have reproducible builds.
# The following fails:
# git commit --allow-empty -m "Test" &&
# ./util/compare_build.sh --boards cr50 --ref1 HEAD --ref2 HEAD^
#
# Note to Developers
# Although this script is a good proving ground for new testing techniques,
# care should be taken to offload functionality to other core components.

. /usr/share/misc/shflags

DEFINE_string 'boards' "nocturne_fp" 'Boards to build (all, fp, stm32, hatch)' \
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

# Usage: assoc-add-keys <associate_array_name> [item1 [item2...]]
assoc-add-keys() {
  local -n arr="${1}"
  shift

  for key in "${@}"; do
    arr["${key}"]="${key}"
  done
}

# Usage: assoc-rm-keys <associate_array_name> [item1 [item2...]
assoc-rm-keys() {
  local -n arr="${1}"
  shift

  for key in "${@}"; do
    unset arr["${key}"]
  done
}

# Usage: make-print-boards
#
# Cache the make print-boards output
make-print-boards() {
  local file="${TMP_DIR}/make-print-boards-cache"
  if [[ ! -f "${file}" ]]; then
    # This command take about 1 second to run
    make print-boards >"${file}"
  fi
  cat "${file}"
}

# Usage: boards-with CHIP
boards-with() {
  local pattern="${1}"

  for b in $(make-print-boards); do
    grep -E -q "${pattern}" "board/${b}/build.mk" && echo "${b}"
  done
}


##########################################################################
# Argument Parsing and Parameter Setup                                   #
##########################################################################

TMP_DIR="$(mktemp -d -t compare_build.XXXX)"

# Process commandline flags.
FLAGS "${@}" || exit 1
eval set -- "${FLAGS_ARGV}"

set -e

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

# Expansion targets
#
# Get all CHIP variants in use:
# grep -E 'CHIP[[:space:]]*\:' board/*/build.mk \
#   | sed 's/.*:=[[:space:]]*//' | sort -u
declare -A BOARDS_EXPANSIONS=(
  # make-print-boards already filters out the skipped boards
  [all]="$(make-print-boards)"
  [fp]="dartmonkey bloonchipper nucleo-dartmonkey nucleo-h743zi"
  [stm32]="$(boards-with 'CHIP[[:space:]:=]*stm32')"
  [npcx]="$(boards-with 'CHIP[[:space:]:=]*npcx')"
  [mchp]="$(boards-with 'CHIP[[:space:]:=]*mchp')"
  [ish]="$(boards-with 'CHIP[[:space:]:=]*ish')"
  [it83xx]="$(boards-with 'CHIP[[:space:]:=]*it83xx')"
  [lm4]="$(boards-with 'CHIP[[:space:]:=]*lm4')"
  [mec1322]="$(boards-with 'CHIP[[:space:]:=]*mec1322')"
  [max32660]="$(boards-with 'CHIP[[:space:]:=]*max32660')"
  [mt_scp]="$(boards-with 'CHIP[[:space:]:=]*mt_scp')"
)

mapfile -t BOARDS_VALID_RAW < <(basename -a board/*)
declare -A BOARDS_VALID=( )
assoc-add-keys BOARDS_VALID "${!BOARDS_EXPANSIONS[@]}" "${BOARDS_VALID_RAW[@]}"

declare -A BOARDS=( )

# Parse boards selection
for b in ${FLAGS_boards}; do
  name="$(sed -E 's/^(-|\+)//' <<<"${b}")"
  # Check for a valid board
  if [[ "${BOARDS_VALID[${name}]}" != "${name}" ]]; then
    echo "# Error - Board '${name}' does not exist" >&2
    exit 1
  fi
  # Check for expansion target
  if [[ -n "${BOARDS_EXPANSIONS[${name}]}" ]]; then
    name="${BOARDS_EXPANSIONS[${name}]}"
  fi
  read -r -a name_arr <<< "${name}"

  # Process addition or deletion
  case "${b}" in
    -*)
      assoc-rm-keys BOARDS "${name_arr[@]}"
      ;;
    +*|*)
      assoc-add-keys BOARDS "${name_arr[@]}"
      ;;
  esac
done

if [[ ${#BOARDS[@]} -eq 0 ]]; then
  echo "# Error - No boards selected" >&2
  exit 1
fi
echo "# Board Selection:"
printf "%s\n" "${BOARDS[@]}" | sort | column

##########################################################################
# Runtime                                                                #
##########################################################################

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
