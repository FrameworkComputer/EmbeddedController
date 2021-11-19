# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import pathlib

import pytest

import zmake.output_packers
import zmake.project as project
import zmake.toolchains as toolchains


@pytest.fixture
def mockfs(monkeypatch, tmp_path):
    """Setup a fake fs root for pathlib objects at tmp_path/mockfs."""
    mockfs_dir = pathlib.PosixPath(tmp_path / "mockfs")
    mockfs_dir.mkdir()

    class FakePath(pathlib.Path):
        def __new__(cls, *args, **kwargs):
            parts = pathlib.PosixPath(*args).relative_to("/").parts
            # Make sure we don't double up our mocked directory.
            mock_dir_parts = mockfs_dir.relative_to("/").parts
            if parts[: len(mock_dir_parts)] == mock_dir_parts:
                return pathlib.PosixPath(*args)
            return pathlib.PosixPath("/", *mock_dir_parts, *parts)

    monkeypatch.setattr(pathlib, "Path", FakePath)
    return mockfs_dir


@pytest.fixture
def coreboot_sdk_exists(mockfs):
    coreboot_sdk_dir = mockfs / "opt" / "coreboot-sdk"
    coreboot_sdk_dir.mkdir(parents=True)


@pytest.fixture
def llvm_exists(mockfs):
    llvm_file = mockfs / "usr" / "bin" / "x86_64-pc-linux-gnu-clang"
    llvm_file.parent.mkdir(parents=True)
    llvm_file.write_text("")


@pytest.fixture
def host_toolchain_exists(mockfs, monkeypatch):
    monkeypatch.setattr(os, "environ", {})

    gcc_file = mockfs / "usr" / "bin" / "gcc"
    gcc_file.parent.mkdir(parents=True)
    gcc_file.write_text("")


@pytest.fixture
def zephyr_exists(mockfs):
    zephyr_sdk_version_file = mockfs / "opt" / "zephyr-sdk" / "sdk_version"
    zephyr_sdk_version_file.parent.mkdir(parents=True)
    zephyr_sdk_version_file.write_text("")


@pytest.fixture
def fake_project(tmp_path):
    return project.Project(
        project.ProjectConfig(
            project_name="foo",
            zephyr_board="foo",
            supported_toolchains=[
                "coreboot-sdk",
                "host",
                "llvm",
                "zephyr",
            ],
            output_packer=zmake.output_packers.RawBinPacker,
            project_dir=tmp_path,
        ),
    )


module_paths = {
    "ec": pathlib.Path("/mnt/host/source/src/platform/ec"),
}


def test_coreboot_sdk(fake_project, coreboot_sdk_exists):
    tc = fake_project.get_toolchain(module_paths)
    assert isinstance(tc, toolchains.CorebootSdkToolchain)

    config = tc.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "coreboot-sdk",
        "TOOLCHAIN_ROOT": "/mnt/host/source/src/platform/ec/zephyr",
    }


def test_llvm(fake_project, llvm_exists):
    tc = fake_project.get_toolchain(module_paths)
    assert isinstance(tc, toolchains.LlvmToolchain)

    config = tc.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "llvm",
        "TOOLCHAIN_ROOT": "/mnt/host/source/src/platform/ec/zephyr",
    }


def test_zephyr(fake_project, zephyr_exists):
    tc = fake_project.get_toolchain(module_paths)
    assert isinstance(tc, toolchains.ZephyrToolchain)

    config = tc.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "zephyr",
        "ZEPHYR_SDK_INSTALL_DIR": str(pathlib.Path("/opt/zephyr-sdk")),
    }
    assert config.environ_defs == {
        "ZEPHYR_SDK_INSTALL_DIR": str(pathlib.Path("/opt/zephyr-sdk")),
    }


def test_zephyr_from_env(mockfs, monkeypatch, fake_project):
    zephyr_sdk_path = mockfs / "zsdk"
    zephyr_sdk_path.mkdir()

    environ = {"ZEPHYR_SDK_INSTALL_DIR": str(zephyr_sdk_path)}
    monkeypatch.setattr(os, "environ", environ)

    tc = fake_project.get_toolchain(module_paths)
    assert isinstance(tc, toolchains.ZephyrToolchain)

    config = tc.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "zephyr",
        "ZEPHYR_SDK_INSTALL_DIR": str(zephyr_sdk_path),
    }
    assert config.environ_defs == {
        "ZEPHYR_SDK_INSTALL_DIR": str(zephyr_sdk_path),
    }


def test_host_toolchain(fake_project, host_toolchain_exists):
    tc = fake_project.get_toolchain(module_paths)
    assert isinstance(tc, toolchains.HostToolchain)

    config = tc.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "host",
    }


def test_toolchain_override(mockfs, fake_project):
    tc = fake_project.get_toolchain(module_paths, override="foo")
    config = tc.get_build_config()
    assert isinstance(tc, toolchains.GenericToolchain)
    assert config.cmake_defs == {"ZEPHYR_TOOLCHAIN_VARIANT": "foo"}
