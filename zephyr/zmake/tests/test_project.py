# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test zmake project module."""

import pathlib
import string
import tempfile

import hypothesis  # pylint:disable=import-error
import hypothesis.strategies as st  # pylint:disable=import-error
import pytest  # pylint:disable=import-error
import zmake.modules
import zmake.output_packers
import zmake.project

board_names = st.text(alphabet=set(string.ascii_lowercase) | {"_"}, min_size=1)
sets_of_board_names = st.lists(st.lists(board_names, unique=True))


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
                        modpath, board
                    )
                    dts_path.parent.mkdir(parents=True, exist_ok=True)
                    dts_path.touch()
                setup_modules_and_dispatch(
                    modules[1:], test_fn, module_list=module_list + (modpath,)
                )
        else:
            test_fn(module_list)

    # The actual test case, once temp modules have been setup.
    def testcase(module_paths):
        # Maps board_name→overlay_files
        board_file_mapping = {}
        for modpath, board_list in zip(module_paths, modules):
            for board in board_list:
                file_name = zmake.project.module_dts_overlay_name(
                    modpath, board
                )
                files = board_file_mapping.get(board, set())
                board_file_mapping[board] = files | {file_name}

        for board, expected_dts_files in board_file_mapping.items():
            project = zmake.project.Project(
                zmake.project.ProjectConfig(
                    project_name=board,
                    zephyr_board=board,
                    output_packer=zmake.output_packers.ElfPacker,
                    supported_toolchains=["llvm"],
                    project_dir=pathlib.Path("/fakebuild"),
                )
            )
            config = project.find_dts_overlays(dict(enumerate(module_paths)))
            actual_dts_files = set(
                config.cmake_defs.get("DTC_OVERLAY_FILE", "").split(";")
            )

            assert actual_dts_files == set(map(str, expected_dts_files))

    setup_modules_and_dispatch(modules, testcase)


module_lists = st.lists(
    st.one_of(*map(st.just, zmake.modules.known_modules)), unique=True
)


@hypothesis.given(module_lists)
@hypothesis.settings(deadline=None)
def test_prune_modules(modules):
    """Test the Project.prune_modules method in the usual case (all
    modules available)."""
    module_paths = {
        name: pathlib.Path("/fake/module/path", name)
        for name in zmake.modules.known_modules
    }

    project = zmake.project.Project(
        zmake.project.ProjectConfig(
            project_name="prunetest",
            zephyr_board="native_posix",
            output_packer=zmake.output_packers.ElfPacker,
            supported_toolchains=["coreboot-sdk"],
            project_dir=pathlib.Path("/fake"),
            modules=modules,
        ),
    )
    assert set(project.prune_modules(module_paths)) == set(modules)


def test_prune_modules_unavailable():
    """The Project.prune_modules method should raise a KeyError when
    not all modules are available."""

    # Missing 'cmsis'
    module_paths = {
        "hal_stm32": pathlib.Path("/mod/halstm"),
    }

    project = zmake.project.Project(
        zmake.project.ProjectConfig(
            project_name="prunetest",
            zephyr_board="native_posix",
            output_packer=zmake.output_packers.ElfPacker,
            supported_toolchains=["coreboot-sdk"],
            project_dir=pathlib.Path("/fake"),
            modules=["hal_stm32", "cmsis"],
        ),
    )
    with pytest.raises(KeyError):
        project.prune_modules(module_paths)


def test_find_projects_empty(tmp_path):
    """Test the find_projects method when there are no projects."""
    projects = list(zmake.project.find_projects(tmp_path))
    assert len(projects) == 0


CONFIG_FILE_1 = """
register_raw_project(project_name="one", zephyr_board="one")
register_host_test(test_name="two")
register_npcx_project(project_name="three", zephyr_board="three")
register_binman_project(project_name="four", zephyr_board="four")
"""

CONFIG_FILE_2 = """
register_raw_project(
    project_name="five",
    zephyr_board="foo",
    dts_overlays=[here / "gpio.dts"],
)
"""


def test_find_projects(tmp_path):
    """Test the find_projects method when there are projects."""
    cf1_dir = tmp_path / "cf1"
    cf1_dir.mkdir()
    (cf1_dir / "BUILD.py").write_text(CONFIG_FILE_1)

    cf2_bb_dir = tmp_path / "cf2_bb"
    cf2_bb_dir.mkdir()
    cf2_dir = cf2_bb_dir / "cf2"
    cf2_dir.mkdir()
    (cf2_dir / "BUILD.py").write_text(CONFIG_FILE_2)

    projects = zmake.project.find_projects(tmp_path)
    assert len(projects) == 5
    assert projects["one"].config.project_dir == cf1_dir
    assert not projects["one"].config.is_test

    assert projects["test-two"].config.project_dir == cf1_dir
    assert projects["test-two"].config.zephyr_board == "native_posix"
    assert projects["test-two"].config.is_test

    assert projects["three"].config.project_dir == cf1_dir
    assert not projects["three"].config.is_test
    assert projects["three"].config.zephyr_board == "three"

    assert projects["four"].config.project_dir == cf1_dir
    assert not projects["four"].config.is_test
    assert projects["four"].config.zephyr_board == "four"

    assert projects["five"].config.project_dir == cf2_dir
    assert not projects["five"].config.is_test
    assert projects["five"].config.zephyr_board == "foo"


def test_find_projects_name_conflict(tmp_path):
    """When two projects define the same name, that should be an error."""
    cf1_dir = tmp_path / "cf1"
    cf1_dir.mkdir()
    (cf1_dir / "BUILD.py").write_text(CONFIG_FILE_2)

    cf2_dir = tmp_path / "cf2"
    cf2_dir.mkdir()
    (cf2_dir / "BUILD.py").write_text(CONFIG_FILE_2)

    with pytest.raises(KeyError):
        zmake.project.find_projects(tmp_path)


def test_register_variant(tmp_path):
    """Test registering a variant."""
    (tmp_path / "BUILD.py").write_text(
        """
some = register_raw_project(
    project_name="some",
    zephyr_board="foo",
    dts_overlays=[here / "gpio.dts"],
)

some_variant = some.variant(project_name="some-variant", zephyr_board="bar")
another = some_variant.variant(
    project_name="another",
    dts_overlays=[here / "another.dts"],
)
    """
    )
    projects = zmake.project.find_projects(tmp_path)
    assert projects["some"].config.zephyr_board == "foo"
    assert projects["some-variant"].config.zephyr_board == "bar"
    assert projects["another"].config.zephyr_board == "bar"
    assert projects["another"].config.dts_overlays == [
        tmp_path / "gpio.dts",
        tmp_path / "another.dts",
    ]


@pytest.mark.parametrize(
    ("actual_files", "config_files", "expected_files"),
    [
        (["prj_link.conf"], [], []),
        (["prj.conf"], [], ["prj.conf"]),
        (
            ["prj.conf", "cfg.conf"],
            ["prj.conf", "cfg.conf"],
            ["prj.conf", "cfg.conf"],
        ),
        (
            ["prj.conf", "prj_samus.conf", "prj_link.conf"],
            ["prj_link.conf"],
            ["prj.conf", "prj_link.conf"],
        ),
    ],
)
def test_kconfig_files(tmp_path, actual_files, config_files, expected_files):
    """Test for setting kconfig_files property."""
    for name in actual_files:
        (tmp_path / name).write_text("")

    project = zmake.project.Project(
        zmake.project.ProjectConfig(
            project_name="samus",
            zephyr_board="lm4",
            output_packer=zmake.output_packers.RawBinPacker,
            supported_toolchains=["coreboot-sdk"],
            project_dir=tmp_path,
            kconfig_files=[tmp_path / name for name in config_files],
        ),
    )

    builds = list(project.iter_builds())
    assert len(builds) == 1

    _, config = builds[0]
    assert sorted(f.name for f in config.kconfig_files) == sorted(
        expected_files
    )
