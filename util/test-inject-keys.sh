#!/bin/bash
#
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Regression test for inject-keys.py.  Works by creating a "fake" ectool
# and comparing expected ectool commands with expected ones.

TMPX=/tmp/inject-key-test$$_x
TMPY=/tmp/inject-key-test$$_y

cleanup() {
  rm -f ./ectool "${TMPX}" "${TMPY}"
}

fail() {
  echo "$@"
  exit 1
}

trap cleanup SIGINT

PATH=.:${PATH}

if [ -e ectool ]; then
  if [ "$(cat ectool)" != $'#! /bin/bash\necho $*' ]; then
    echo "./ectool exists, please remove it to run this script"
    exit 1
  fi
fi

echo "#! /bin/bash" > ectool
echo 'echo $*' >> ectool
chmod a+x ectool

# tests that should fail

# bad args
./inject-keys.py        >& /dev/null && fail "undetected zero args"
./inject-keys.py -k     >& /dev/null && fail "undetected mismatched args (1)"
./inject-keys.py -k a b >& /dev/null && fail "undetected mismatched args (2)"
./inject-keys.py -z a   >& /dev/null && fail "undetected bad flag"

# bad key
./inject-keys.py -p foobar >& /dev/null && fail "undetected bad key"

# tests that should succeed with the expected output

# simple string
./inject-keys.py -s abcd > "${TMPX}"

cat > "${TMPY}" <<EOF
kbpress 4 1 1
kbpress 4 1 0
kbpress 0 3 1
kbpress 0 3 0
kbpress 5 2 1
kbpress 5 2 0
kbpress 4 2 1
kbpress 4 2 0
EOF

cmp "${TMPX}" "${TMPY}" || fail "${TMPX} and ${TMPY} differ"

# string with shifted characters
./inject-keys.py -s A@%Bx > "${TMPX}"

cat > "${TMPY}" <<EOF
kbpress 5 7 1
kbpress 4 1 1
kbpress 4 1 0
kbpress 5 7 0
kbpress 5 7 1
kbpress 6 4 1
kbpress 6 4 0
kbpress 5 7 0
kbpress 5 7 1
kbpress 3 3 1
kbpress 3 3 0
kbpress 5 7 0
kbpress 5 7 1
kbpress 0 3 1
kbpress 0 3 0
kbpress 5 7 0
kbpress 5 4 1
kbpress 5 4 0
EOF

cmp "${TMPX}" "${TMPY}" || fail "${TMPX} and ${TMPY} differ"

# keystroke injection
./inject-keys.py -k enter > "${TMPX}"

cat > "${TMPY}" <<EOF
kbpress 4 11 1
kbpress 4 11 0
EOF

cmp "${TMPX}" "${TMPY}" || fail "${TMPX} and ${TMPY} differ"

# key event injection
./inject-keys.py -p enter > "${TMPX}"

cat > "${TMPY}" <<EOF
kbpress 4 11 1
EOF

cmp "${TMPX}" "${TMPY}" || fail "${TMPX} and ${TMPY} differ"

cleanup
