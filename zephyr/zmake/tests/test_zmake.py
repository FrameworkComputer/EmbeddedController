# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Do a run of 'zmake build' and check the output"""

import logging
import os
import pathlib
import tempfile
import unittest.mock as mock
from unittest.mock import patch
from testfixtures import LogCapture

import zmake.jobserver
import zmake.project
import zmake.zmake as zm

OUR_PATH = os.path.dirname(os.path.realpath(__file__))


class FakeProject:
    """A fake project which requests two builds and does no packing"""
    # pylint: disable=too-few-public-methods
    def __init__(self):
        self.packer = mock.Mock()
        self.packer.pack_firmware = mock.Mock(return_value=[])

    @staticmethod
    def iter_builds():
        """Yield the two builds that zmake normally does"""
        yield 'build-ro', None
        yield 'build-rw', None


class FakeJobserver(zmake.jobserver.GNUMakeJobServer):
    """A fake jobserver which just runs 'cat' on the provided files"""

    def __init__(self, fnames):
        """Start up a jobserver with two jobs

        Args:
            fnames: List of the two filenames to supply, one for each job
        """
        super().__init__()
        self.jobserver = zmake.jobserver.GNUMakeJobServer(jobs=2)
        self.fnames = fnames

    def get_job(self):
        """Fake implementation of get_job(), which returns a real JobHandle()"""
        return zmake.jobserver.JobHandle(mock.Mock())

    # pylint: disable=arguments-differ
    def popen(self, _cmd, *args, **kwargs):
        """Ignores the provided command and just runs 'cat' instead"""
        new_cmd = ['cat', self.fnames.pop()]
        return self.jobserver.popen(new_cmd, *args, **kwargs)


def do_test_with_log_level(log_level, samples=None):
    """Test filtering using a particular log level

    Args:
        log_level: Level to use
        samples: List of sample files to use (in the 'files' subdir), None to
            use the standard ones

    Returns:
        tuple:
            - List of log strings obtained from the run
            - Temporary directory used for build
    """
    fnames = [os.path.join(OUR_PATH, 'files', f)
              for f in samples or ['sample_ro.txt', 'sample_rw.txt']]
    zmk = zm.Zmake(jobserver=FakeJobserver(fnames))

    with LogCapture(level=log_level) as cap:
        with tempfile.TemporaryDirectory() as tmpname:
            with patch.object(zmake.project, 'Project',
                              return_value=FakeProject()):
                zmk.build(pathlib.Path(tmpname))
    recs = [rec.getMessage() for rec in cap.records]
    return recs, tmpname


def test_filter_normal():
    """Test filtering of a normal build (with no errors)"""
    recs, _ = do_test_with_log_level(logging.ERROR)
    assert not recs


def test_filter_info():
    """Test what appears on the INFO level"""
    recs, tmpname = do_test_with_log_level(logging.INFO)
    # This produces an easy-to-read diff if there is a difference
    assert set(recs) == {
        'Building %s:build-ro: /usr/bin/ninja -C %s/build-build-ro' %
        (tmpname, tmpname),
        'Building %s:build-rw: /usr/bin/ninja -C %s/build-build-rw' %
        (tmpname, tmpname),
        'Building /tmp/z/vol:ro: /usr/bin/ninja -C /tmp/z/vol/build-ro',
        'Building /tmp/z/vol:rw: /usr/bin/ninja -C /tmp/z/vol/build-rw',
        'FLASH:      241868 B       512 KB     46.13%',
        'IDT_LIST:          0 GB         2 KB      0.00%',
        'Memory region         Used Size  Region Size  %age Used',
        'Running /usr/bin/ninja -C /tmp/z/vol/build-ro',
        'Running /usr/bin/ninja -C /tmp/z/vol/build-rw',
        'SRAM:       48632 B        62 KB     76.60%',
        }


def test_filter_debug():
    """Test what appears on the DEBUG level"""
    recs, _ = do_test_with_log_level(logging.DEBUG)
    # The RO version has three extra lines: the SUCCESS asterisks
    # Both versions add the first 'Building' line above, with the temp dir
    expect = 321 + 318 + 2
    assert len(recs) == expect


def test_filter_error():
    """Test that devicetree errors appear"""
    recs, tmpname = do_test_with_log_level(logging.ERROR,
                                           ['sample_err.txt'] * 2)
    uniq = set(recs)
    assert len(uniq) == 1
    assert "devicetree error: 'adc' is marked as required" in list(uniq)[0]
