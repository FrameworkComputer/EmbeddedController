#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Launch an EC image in renode.

DEFAULT_BOARD="bloonchipper"
DEFAULT_PROJECT="ec"

SCRIPT_NAME="$(basename "${BASH_SOURCE[0]}")"
SCRIPT_PATH="$(dirname "${BASH_SOURCE[0]}")"

usage() {
  echo "Usage: ${SCRIPT_NAME} [[board-name [project-name]]"
  echo ""
  echo "Launch an EC image in renode. This can be the actual firmware"
  echo "image or an on-board test image."
  echo ""
  echo "Environment Variables:"
  echo "  BOARD overrides board-name argument."
  echo "  PROJECT overrides project-name argument."
  echo ""
  echo "Args:"
  echo "  board-name is the name of the EC board [${DEFAULT_BOARD}]"
  echo "  project-name is the name of the EC project [${DEFAULT_PROJECT}]"
  echo "    This is normally ec for the main firmware image and the test"
  echo "    name for an on-board test image."
  echo ""
  echo "Examples:"
  echo "  ${SCRIPT_NAME}"
  echo "  ${SCRIPT_NAME} bloonchipper abort"
  echo "  BOARD=dartmonkey ${SCRIPT_NAME}"
  echo "  PROJECT=always_memset ${SCRIPT_NAME}"
}

# Print the command and then run it.
msg-run() {
    echo -e "\E[1;32m> $*\E[m"
    "$@"
}

main() {
  local board="${1:-${BOARD:-${DEFAULT_BOARD}}}"
  local project="${2:-${PROJECT:-${DEFAULT_PROJECT}}}"

  for arg; do
    case "${arg}" in
      --help|-h)
        usage
        return 0
        ;;
    esac
  done

  # Since we are going to cd later, we need to capture the absolute path.
  local ec_dir
  ec_dir="$(realpath "${SCRIPT_PATH}/..")"
  local out="${ec_dir}/build/${board}"

  if [[ "${project}" != "ec" ]]; then
    out+="/${project}"
  fi

  local bin="${out}/${project}.bin"
  local elf_ro="${out}/RO/${project}.RO.elf"
  local elf_rw="${out}/RW/${project}.RW.elf"

  # We need to run the "include @util/renode/${board}.resc", which is
  # relative to ec root.
  cd "${ec_dir}" || return 1

  EXECUTE=( )
  EXECUTE+=( "\$bin='${bin}';" )
  EXECUTE+=( "\$elf_ro='${elf_ro}';" )
  EXECUTE+=( "\$elf_rw='${elf_rw}';" )
  EXECUTE+=( "include @util/renode/${board}.resc;" )
  # Change logLevel from WARNING to ERROR, since the console is flooded
  # with WARNINGs.
  EXECUTE+=( "logLevel 3;" )
  # https://renode.readthedocs.io/en/latest/debugging/gdb.html
  # (gdb) target remote :3333
  EXECUTE+=( "machine StartGdbServer 3333;" )
  EXECUTE+=( "start;" )

  CMD=( renode )
  CMD+=( --console )
  CMD+=( --execute "${EXECUTE[*]}" )

  msg-run exec "${CMD[@]}"
}

main "$@"
