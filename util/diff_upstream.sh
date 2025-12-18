#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

SRC_DIR="$(realpath "$( dirname "${BASH_SOURCE[0]}" )/../../..")"
THIRD_PARTY_DIR="${SRC_DIR}/third_party"
echo "3p root directory: ${THIRD_PARTY_DIR}"

# This should include all the third_party modules that zmake can see.
# And also any repos that copybot copies from indirectly, i.e.
# zephyrproject-rtos/cmsis -> zephyr/cmsis -> zephyrproject/modules/hal/cmsis
declare -A repos=(
  # chre-main-public-copybot-downstream.ini
  ['android/platform/system/chre']='https://android.googlesource.com/platform/system/chre main'
  # chre-main-copybot-downstream.ini
  ['android/platform/system/chre_internal']='https://chrome-internal.googlesource.com/chromeos/third_party/chre upstream/main'
  # pigweed-main-copybot-downstream.ini
  ['pigweed']='https://pigweed.googlesource.com/pigweed/pigweed main'
  # zephyr-cmsis_6-copybot-downstream.ini
  ['zephyr/cmsis_6']='https://github.com/zephyrproject-rtos/CMSIS_6.git main'
  # zephyr-main-copybot-downstream.ini
  ['zephyr/main']='https://github.com/zephyrproject-rtos/zephyr.git main'
  # zephyr-nanopb-copybot-downstream.ini
  ['zephyr/nanopb']='https://github.com/zephyrproject-rtos/nanopb.git zephyr'
  # zephyr-picolibc-copybot-downstream.ini
  ['zephyr/picolibc']='https://github.com/zephyrproject-rtos/picolibc.git main'
  # zephyr-project-cmsis-copybot-downstream.ini
  ['zephyrproject/modules/hal/cmsis']='https://github.com/zephyrproject-rtos/cmsis.git master'
  # zephyr-project-egis_module-copybot-downstream.ini
  ['zephyrproject/modules/hal/egis_module']='https://github.com/EgisMCU/egis_module.git main'
  # zephyr-project-hal_egis-copybot-downstream.ini
  ['zephyrproject/modules/hal/egis']='https://github.com/EgisMCU/hal_egis.git main'
  # zephyr-project-intel-copybot-downstream.ini
  ['zephyrproject/modules/hal/intel']='https://github.com/zephyrproject-rtos/hal_intel.git main'
  # zephyr-project-stm32-copybot-downstream.ini
  ['zephyrproject/modules/hal/stm32']='https://github.com/zephyrproject-rtos/hal_stm32.git main'
)

# All expected diffs (FROMPULLs)
declare -A expected_diffs=(
  # b/389761200 4919881523df2ef5dae9cacebdeb3e72936ec793
  #   Revert "drivers: watchdog: stm32 iwdg: explicit single channel"
  # b/460502081 69c7fccd5e707b3f016d0c4efd536791371ecb15
  #   FROMPULL: kernel: Add Kconfig option to disable LTO for kernel sources
  # b/460504453 1797ed0688a1b226d9ea861bbb3675c1bd7bb650
  #   FROMPULL: soc: it8xxx2: Select KERNEL_NO_LTO only when LTO is enabled
  ['zephyr/main']="\
    4919881523df2ef5dae9cacebdeb3e72936ec793 \
    69c7fccd5e707b3f016d0c4efd536791371ecb15 \
    1797ed0688a1b226d9ea861bbb3675c1bd7bb650"
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
    # cmsis has some commits out of order
    c3bd2094f92d574377f7af2aec147ae181aa5f8e)
      upstream_commit=4b96cbb174678dcd3ca86e11e1f24bc5f8726da0
      ;;
    # nanopb has some commits out of order
    0aa6f11bc7563989da85774a0decaecd3b304d6a)
      upstream_commit=65cbefb4695bc7af1cb733ced99618afb3586b20
      ;;
    # picolibc has a commit out of order
    b25f4a47784d2c24695977c903fe114565ae2bc6)
      upstream_commit=1c73900b79dbc02b80d09f5d637382249158e1ec
      ;;
    # zephyrproject/modules/hal/intel switched upstream repos here
    8c6772bb56997da40e3f624192334de22ee5e5a8)
      upstream_commit=82a33b2de29523d9ce572b3d0110a808665cd3ff
      ;;
    # cmsis switched upstream repos here
    818dedc88d61e87ffde020d245d007a548204d80)
      upstream_commit=512cc7e895e8491696b61f7ba8066b4a182569b8
      ;;
    # hal_stm32 switched upstream repos here
    2a535edbfb51d2524578a1b8f8342e9644ac0864)
      upstream_commit=9d05ebdff47b5071fa092de243a1244e7c27f518
      ;;
    9e5f90b27e929ff9803abf3841eb3a452dc4830f)
      upstream_commit=0e9e07d8eb89107aa57ad25a12ba1ed4112c53ab
      ;;
    495cbd601502e07c8d39873df8d791100b4a7e38)
      upstream_commit=58a89e7894dd90be8fab467f9504afa4533b0aa0
      ;;
    dfe251554b26412dd683ee26474925d7132218ac)
      upstream_commit=458e6f8ae3d
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
      zephyr/picolibc)
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
