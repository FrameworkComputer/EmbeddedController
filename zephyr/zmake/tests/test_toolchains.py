# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for toolchains.py."""

import os
import pathlib

# pylint: disable=import-error
import pytest
from zmake import project
from zmake import toolchains
import zmake.output_packers


# pylint:disable=redefined-outer-name,unused-argument


@pytest.fixture
def mockfs(monkeypatch, tmp_path: pathlib.Path):
    """Setup a fake fs root for pathlib objects at tmp_path/mockfs."""
    mockfs_dir = tmp_path / "mockfs"
    mockfs_dir.mkdir()

    def map_path(path):
        try:
            rel_path = path.relative_to(mockfs_dir)
            fake = mockfs_dir.joinpath(rel_path)
            return fake
        except ValueError:
            fake = mockfs_dir.joinpath(*path.parts[1:])
            return fake

    real_stat = pathlib.Path.stat

    def stat(path):
        return real_stat(map_path(path))

    monkeypatch.setattr(pathlib.Path, "stat", stat)
    return mockfs_dir


@pytest.fixture
def coreboot_sdk_exists(mockfs):
    """Provide a mock coreboot-sdk."""
    coreboot_sdk_dir = mockfs / "opt" / "coreboot-sdk"
    coreboot_sdk_dir.mkdir(parents=True)


@pytest.fixture
def llvm_exists(mockfs):
    """Provide a mock llvm."""
    llvm_file = mockfs / "usr" / "bin" / "x86_64-pc-linux-gnu-clang"
    llvm_file.parent.mkdir(parents=True)
    llvm_file.write_text("")


@pytest.fixture
def host_toolchain_exists(mockfs, monkeypatch):
    """Provide a mock host toolchain."""
    monkeypatch.setattr(os, "environ", {})

    gcc_file = mockfs / "usr" / "bin" / "gcc"
    gcc_file.parent.mkdir(parents=True)
    gcc_file.write_text("")


@pytest.fixture
def zephyr_exists(mockfs):
    """Provide a mock zephyr sdk."""
    zephyr_sdk_version_file = mockfs / "opt" / "zephyr-sdk" / "sdk_version"
    zephyr_sdk_version_file.parent.mkdir(parents=True)
    zephyr_sdk_version_file.write_text("")


@pytest.fixture
def no_environ(monkeypatch):
    """Clear all environment variables."""
    environ = {}
    monkeypatch.setattr(os, "environ", environ)


@pytest.fixture
def fake_project(tmp_path):
    """Create a project that can be used in all the tests."""
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


def test_coreboot_sdk(fake_project: project.Project, coreboot_sdk_exists):
    """Test that the corebook sdk can be found."""
    chain = fake_project.get_toolchain(module_paths)
    assert isinstance(chain, toolchains.CorebootSdkToolchain)

    config = chain.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "coreboot-sdk",
        "TOOLCHAIN_ROOT": "/mnt/host/source/src/platform/ec/zephyr",
        "COREBOOT_SDK_ROOT": "/opt/coreboot-sdk",
    }


def test_llvm(fake_project, llvm_exists):
    """Test that llvm can be found."""
    chain = fake_project.get_toolchain(module_paths)
    assert isinstance(chain, toolchains.LlvmToolchain)

    config = chain.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "llvm",
        "TOOLCHAIN_ROOT": "/mnt/host/source/src/platform/ec/zephyr",
    }


def test_zephyr(fake_project: project.Project, zephyr_exists, no_environ):
    """Test that the zephyr sdk can be found in a standard location."""
    chain = fake_project.get_toolchain(module_paths)
    assert isinstance(chain, toolchains.ZephyrToolchain)

    config = chain.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "zephyr",
        "ZEPHYR_SDK_INSTALL_DIR": str(pathlib.Path("/opt/zephyr-sdk")),
    }


def test_zephyr_from_env(mockfs, monkeypatch, fake_project):
    """Test that the zephyr sdk can be found from env variable."""
    zephyr_sdk_path = mockfs / "zsdk"
    zephyr_sdk_path.mkdir()

    environ = {"ZEPHYR_SDK_INSTALL_DIR": str(zephyr_sdk_path)}
    monkeypatch.setattr(os, "environ", environ)

    chain = fake_project.get_toolchain(module_paths)
    assert isinstance(chain, toolchains.ZephyrToolchain)

    config = chain.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "zephyr",
        "ZEPHYR_SDK_INSTALL_DIR": str(zephyr_sdk_path),
    }


def test_host_toolchain(fake_project, host_toolchain_exists):
    """Test that the host toolchain can be found."""
    chain = fake_project.get_toolchain(module_paths)
    assert isinstance(chain, toolchains.HostToolchain)

    config = chain.get_build_config()
    assert config.cmake_defs == {
        "ZEPHYR_TOOLCHAIN_VARIANT": "host",
    }


def test_toolchain_override(mockfs, fake_project):
    """Test that the toolchain can be overridden."""
    chain = fake_project.get_toolchain(module_paths, override="foo")
    config = chain.get_build_config()
    assert isinstance(chain, toolchains.GenericToolchain)
    assert config.cmake_defs == {"ZEPHYR_TOOLCHAIN_VARIANT": "foo"}


def test_generic_toolchain():
    """Verify that GenericToolchain.probe() returns False."""
    chain = toolchains.GenericToolchain("name")
    assert not chain.probe()


def test_no_toolchains(fake_project: project.Project, mockfs, no_environ):
    """Check for error when there are no toolchains."""
    with pytest.raises(
        OSError, match=r"No supported toolchains could be found on your system"
    ):
        fake_project.get_toolchain(module_paths)


def test_override_without_sdk(
    fake_project: project.Project, mockfs, no_environ
):
    """Check for error override is set to zephyr, but it can't be found."""
    chain = fake_project.get_toolchain(module_paths, override="zephyr")

    with pytest.raises(
        RuntimeError, match=r"No installed Zephyr SDK was found"
    ):
        chain.get_build_config()
