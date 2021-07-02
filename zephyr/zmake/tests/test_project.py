# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
import string
import tempfile

import hypothesis
import hypothesis.strategies as st
import pytest

import zmake.modules
import zmake.project

board_names = st.text(alphabet=set(string.ascii_lowercase) | {'_'},
                      min_size=1)
sets_of_board_names = st.lists(st.lists(board_names, unique=True))


class TemporaryProject(tempfile.TemporaryDirectory):
    """A temporary project wrapper.

    Args:
        config: The config dictionary to be used with the project.
    """
    def __init__(self, config):
        self.config = config
        super().__init__()

    def __enter__(self):
        project_path = pathlib.Path(super().__enter__())
        return zmake.project.Project(project_path, config_dict=self.config)


@hypothesis.given(sets_of_board_names)
@hypothesis.settings(deadline=None)
def test_find_dts_overlays(modules):
    """Test the functionality of find_dts_overlays with multiple
    modules, each with sets of board names."""

    # Recursive function to wind up all the temporary directories and
    # call the actual test.
    def setup_modules_and_dispatch(modules, test_fn, module_list=()):
        if modules:
            boards = modules[0]
            with tempfile.TemporaryDirectory() as modpath:
                modpath = pathlib.Path(modpath)
                for board in boards:
                    dts_path = zmake.project.module_dts_overlay_name(
                        modpath, board)
                    dts_path.parent.mkdir(parents=True, exist_ok=True)
                    dts_path.touch()
                setup_modules_and_dispatch(
                    modules[1:], test_fn, module_list=module_list + (modpath,))
        else:
            test_fn(module_list)

    # The actual test case, once temp modules have been setup.
    def testcase(module_paths):
        # Maps board_name→overlay_files
        board_file_mapping = {}
        for modpath, board_list in zip(module_paths, modules):
            for board in board_list:
                file_name = zmake.project.module_dts_overlay_name(
                    modpath, board)
                files = board_file_mapping.get(board, set())
                board_file_mapping[board] = files | {file_name}

        for board, expected_dts_files in board_file_mapping.items():
            with TemporaryProject(
                {'board': board,
                 'toolchain': 'foo',
                 'output-type': 'elf',
                 'supported-zephyr-versions': ['v2.5']}) as project:
                config = project.find_dts_overlays(
                    dict(enumerate(module_paths)))

                actual_dts_files = set(
                    config.cmake_defs.get('DTC_OVERLAY_FILE', '').split(';'))

                assert actual_dts_files == set(map(str, expected_dts_files))

    setup_modules_and_dispatch(modules, testcase)


module_lists = st.lists(st.one_of(*map(st.just, zmake.modules.known_modules)),
                        unique=True)


@hypothesis.given(module_lists)
@hypothesis.settings(deadline=None)
def test_prune_modules(modules):
    """Test the Project.prune_modules method in the usual case (all
    modules available)."""
    module_paths = {
        name: pathlib.Path('/fake/module/path', name)
        for name in zmake.modules.known_modules
    }

    with TemporaryProject(
        {'board': 'native_posix',
         'toolchain': 'coreboot-sdk',
         'output-type': 'elf',
         'supported-zephyr-versions': ['v2.5'],
         'modules': modules}) as project:
        assert set(project.prune_modules(module_paths)) == set(modules)


def test_prune_modules_unavailable():
    """The Project.prune_modules method should raise a KeyError when
    not all modules are available."""

    # Missing 'cmsis'
    module_paths = {
        'hal_stm32': pathlib.Path('/mod/halstm'),
    }

    with TemporaryProject(
        {'board': 'native_posix',
         'toolchain': 'coreboot-sdk',
         'output-type': 'elf',
         'supported-zephyr-versions': ['v2.5'],
         'modules': ['hal_stm32', 'cmsis']}) as project:
        with pytest.raises(KeyError):
            project.prune_modules(module_paths)


def test_find_projects_empty(tmp_path):
    """Test the find_projects method when there are no projects."""
    projects = list(zmake.project.find_projects(tmp_path))
    assert len(projects) == 0


YAML_FILE = """
supported-zephyr-versions:
  - v2.5
toolchain: coreboot-sdk
output-type: npcx
"""


def test_find_projects(tmp_path):
    """Test the find_projects method when there are projects."""
    dir = tmp_path.joinpath('one')
    dir.mkdir()
    dir.joinpath('zmake.yaml').write_text("board: one\n" + YAML_FILE)
    tmp_path.joinpath('two').mkdir()
    dir = tmp_path.joinpath('two/a')
    dir.mkdir()
    dir.joinpath('zmake.yaml').write_text(
        "board: twoa\nis-test: true\n" + YAML_FILE)
    dir = tmp_path.joinpath('two/b')
    dir.mkdir()
    dir.joinpath('zmake.yaml').write_text("board: twob\n" + YAML_FILE)
    projects = list(zmake.project.find_projects(tmp_path))
    projects.sort(key=lambda x: x.project_dir)
    assert len(projects) == 3
    assert projects[0].project_dir == tmp_path.joinpath('one')
    assert projects[1].project_dir == tmp_path.joinpath('two/a')
    assert projects[2].project_dir == tmp_path.joinpath('two/b')
    assert not projects[0].config.is_test
    assert projects[1].config.is_test
    assert not projects[2].config.is_test
