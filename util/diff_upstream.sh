#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

SRC_DIR="$(realpath "$( dirname "${BASH_SOURCE[0]}" )/../../..")"
THIRD_PARTY_DIR="${SRC_DIR}/third_party"
echo "3p root directory: ${THIRD_PARTY_DIR}"

# This should include all the third_party modules that zmake can see.
# And also any repos that copybot copies from indirectly, i.e.
# zephyrproject-rtos/cmsis_6 -> zephyrproject/modules/hal/cmsis_6
declare -A repos=(
  # config/chre/main-public.ini
  ['android/platform/system/chre']='https://android.googlesource.com/platform/system/chre main'
  # config/chre/main.ini
  ['android/platform/system/chre_internal']='https://chrome-internal.googlesource.com/chromeos/third_party/chre upstream/main'
  # config/pigweed/main.ini
  ['pigweed']='https://pigweed.googlesource.com/pigweed/pigweed main'
  # config/zephyr/main.ini
  ['zephyrproject/zephyr']='https://github.com/zephyrproject-rtos/zephyr.git main'
  # config/zephyr/project-cmsis_6.ini
  ['zephyrproject/modules/hal/cmsis_6']='https://github.com/zephyrproject-rtos/CMSIS_6.git main'
  # config/zephyr/project-egis_module.ini
  ['zephyrproject/modules/hal/egis_module']='https://github.com/EgisMCU/egis_module.git main'
  # config/zephyr/project-hal_egis.ini
  ['zephyrproject/modules/hal/egis']='https://github.com/EgisMCU/hal_egis.git main'
  # config/zephyr/project-intel.ini
  ['zephyrproject/modules/hal/intel']='https://github.com/zephyrproject-rtos/hal_intel.git main'
  # config/zephyr/project-stm32.ini
  ['zephyrproject/modules/hal/stm32']='https://github.com/zephyrproject-rtos/hal_stm32.git main'
  # config/zephyr/project-chre.ini
  ['zephyrproject/modules/lib/chre']='https://github.com/zephyrproject-rtos/chre.git zephyr'
  # config/zephyr/project-nanopb.ini
  ['zephyrproject/modules/lib/nanopb']='https://github.com/zephyrproject-rtos/nanopb.git zephyr'
  # config/zephyr/project-picolibc.ini
  ['zephyrproject/modules/lib/picolibc']='https://github.com/zephyrproject-rtos/picolibc.git main'
)

# All expected diffs (FROMPULLs)
declare -A expected_diffs=(
  # b/389761200 17452ff89d458b4201bc1ce2debbb30703f45c0d
  #   Revert "drivers: watchdog: stm32 iwdg: explicit single channel"
  ['zephyrproject/zephyr']="\
    17452ff89d458b4201bc1ce2debbb30703f45c0d \
    "
)

function die() {
  echo "$@"
  exit 1
}

all_repos=("${!repos[@]}")
if [ "$#" -gt 0 ]; then
  all_repos=( "$@" )
fi
for repo in "${all_repos[@]}"; do
  read -ra upstream <<<"${repos[${repo}]}"
  upstream_repo="${upstream[0]}"
  upstream_branch="${upstream[1]}"

  cd "${THIRD_PARTY_DIR}/${repo}" || die "${THIRD_PARTY_DIR}/${repo} not found"
  repo start nodiffs . 2>/dev/null || die "repo start failed"
  git pull --quiet || die "git pull of ${repo} failed"
  upstream_commit="$(git log . | sed -e '/^\s*GitOrigin-RevId:/!d' \
    -e 's/.*: //' -e 's/)$//' | head -1)"
  if [ "${upstream_commit}" == "" ]; then
    upstream_commit="${upstream_branch}"
  fi
  case "${upstream_commit}" in
    # picolibc has a commit out of order
    b25f4a47784d2c24695977c903fe114565ae2bc6)
      upstream_commit=1c73900b79dbc02b80d09f5d637382249158e1ec
      ;;
    # zephyrproject/modules/hal/intel switched upstream repos here
    8c6772bb56997da40e3f624192334de22ee5e5a8)
      upstream_commit=82a33b2de29523d9ce572b3d0110a808665cd3ff
      ;;
    # hal_stm32 switched upstream repos here
    2a535edbfb51d2524578a1b8f8342e9644ac0864)
      upstream_commit=9d05ebdff47b5071fa092de243a1244e7c27f518
      ;;
    # chre
    9e5f90b27e929ff9803abf3841eb3a452dc4830f)
      upstream_commit=0e9e07d8eb89107aa57ad25a12ba1ed4112c53ab
      ;;
    # pigweed
    495cbd601502e07c8d39873df8d791100b4a7e38)
      upstream_commit=58a89e7894dd90be8fab467f9504afa4533b0aa0
      ;;
    # zephyr/main
    dfe251554b26412dd683ee26474925d7132218ac)
      upstream_commit=458e6f8ae3d
      ;;
    # nanopb upstream switch
    54a8f364e39bf21e2c5fd3ee7e36557f8b2f6da5)
      upstream_commit=65cbefb4695bc7af1cb733ced99618afb3586b20
      ;;
    # picolibc upstream switch
    e16b6e6e69dcceaa778f8eeb68c8e1f70e271aa9)
      upstream_commit=01254932e8e81085817ed61fd858648584ffe37c
      ;;
    # cmsis_6 upstream switch
    a04b38d91cda4ff3064d67349006f23b2ba8273b)
      upstream_commit=30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74
      ;;
  esac
  echo "==============================="
  echo -n "Diffing ${repo} vs "
  echo "${upstream_repo}@${upstream_branch} (${upstream_commit})"

  upstream_dir=$(mktemp -d)
  function cleanup {
    rm -rf "${upstream_dir}"
  }
  trap cleanup EXIT SIGINT
  git clone --quiet --no-checkout "${upstream_repo}" "${upstream_dir}"
	git -C "${upstream_dir}" checkout --quiet "${upstream_commit}"

  # Apply known diffs
  read -ra expected_commits <<<"${expected_diffs[${repo}]}"
  for frompull_commit in "${expected_commits[@]}" ; do
    echo "Applying known diff ${frompull_commit}"
    git format-patch -1 "${frompull_commit}" --stdout --relative | \
      git -C "${upstream_dir}" am --3way || die "Failed to apply known diff"
  done

  log_output=$(git -C "${upstream_dir}" --no-pager log --no-decorate \
    --format='%h %s %cr' "${upstream_commit}..origin/${upstream_branch}") \
    || die "git log failed"
  if [ "${log_output}" != "" ]; then
    echo "Copybot not yet merged commits:"
    echo "${log_output}"
    echo "---------"
  fi

  ( git ls-files . ; git -C "${upstream_dir}" ls-files ) | sort -u | \
  while IFS= read -r  file; do
    # Per repo exceptions
    case "${repo}" in
      pigweed)
        case "${file}" in
          cloudbuild_pigweed.yaml)
            continue
            ;;
        esac
        ;;
      zephyr/picolibc|zephyrproject/modules/lib/picolibc)
        case "${file}" in
          # These should be upstreamed after we have multilib
          scripts/*-coreboot-*)
            continue
            ;;
        esac
        ;;
    esac
    # Exceptions that apply to all repos
    case "${file}" in
      .gitkeep)
        continue
        ;;
      # There can be a top-level OWNERS, and it shouldn't be compared
      OWNERS)
        continue
        ;;
      # Other OWNERS files should not exist at all downstream
      */OWNERS)
        if ! [ -f "${file}" ] ; then
          continue
        else
          echo "Downstream/${file} should not exist, but does!"
        fi
        ;;
      DIR_METADATA|PRESUBMIT.cfg|.vpython3)
        # Skip these files if they are only downstream
        if ! [ -f "${upstream_dir}/${file}" ] ; then
          continue
        fi
        ;;
    esac
    if ! diff_output=$(diff --no-dereference -u  -L "Upstream/${file}" \
      -L "Downstream/${file}" "${upstream_dir}/${file}" "${file}" \
      2>&1 ); then
      echo diff --no-dereference -u "Upstream/${file}" "Downstream/${file}"
      echo "${diff_output}"
    fi
  done

  cleanup
  trap - EXIT SIGINT
done

exit 0
