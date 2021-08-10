# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import subprocess

import zmake.project
import zmake.version as version


def _git_init(repo):
    """Create a new git repository."""
    repo.mkdir()
    subprocess.run(
        ["git", "-c", "init.defaultBranch=main", "-C", repo, "init"], check=True
    )


def _git_add(repo, path, contents="example!\n"):
    """Write contents and stage a file."""
    path.write_text(contents)
    subprocess.run(["git", "-C", repo, "add", path], check=True)


def _git_commit(repo, message="message!"):
    env = {
        "GIT_AUTHOR_NAME": "Alyssa P. Hacker",
        "GIT_AUTHOR_EMAIL": "aphacker@example.org",
        "GIT_AUTHOR_DATE": "Thu, 07 Apr 2005 22:13:13 +0200",
        "GIT_COMMITTER_NAME": "Ben Bitdiddle",
        "GIT_COMMITTER_EMAIL": "bitdiddle@example.org",
        "GIT_COMMITTER_DATE": "Tue, 30 Aug 2005 10:50:30 -0700",
    }
    subprocess.run(["git", "-C", repo, "commit", "-m", message], check=True, env=env)


def _setup_example_repos(tmp_path):
    """Setup temporary project, zephyr base, and module repos.

    Args:
        tmp_path: Directory to set up files in.

    Returns:
        A 3-tuple of project, zephyr_base, modules_dict.
    """
    project_path = tmp_path / "prj"
    project_path.mkdir()

    project = zmake.project.Project(
        project_path,
        config_dict={
            "board": "foo",
            "toolchain": "bar",
            "output-type": "raw",
            "supported-zephyr-versions": ["v2.6"],
        },
    )
    # Has one commit.
    zephyr_base = tmp_path / "zephyr_base"
    _git_init(zephyr_base)
    _git_add(
        zephyr_base,
        zephyr_base / "VERSION",
        "VERSION_MAJOR=2\nVERSION_MINOR=6\nPATCHLEVEL=99\n",
    )
    _git_commit(zephyr_base, "Added version file")

    # Has one commit.
    mod1 = tmp_path / "mod1"
    _git_init(mod1)
    _git_add(mod1, mod1 / "file1")
    _git_commit(mod1)

    # Has two commits.
    mod2 = tmp_path / "mod2"
    _git_init(mod2)
    _git_add(mod2, mod2 / "file2")
    _git_commit(mod2)
    _git_add(mod2, mod2 / "file3")
    _git_commit(mod2)

    return project, zephyr_base, {"mod1": mod1, "mod2": mod2}


def test_version_string(tmp_path):
    project, zephyr_base, modules = _setup_example_repos(tmp_path)
    assert (
        version.get_version_string(project, zephyr_base, modules)
        == "prj_v2.6.4-mod1:02fd7a,mod2:b5991f,os:377d26"
    )


def test_version_string_static(tmp_path):
    project, zephyr_base, modules = _setup_example_repos(tmp_path)
    assert (
        version.get_version_string(project, zephyr_base, modules, static=True)
        == "prj_v2.6.0-STATIC"
    )
