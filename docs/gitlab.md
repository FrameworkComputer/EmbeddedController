# Gitlab CI

The Zephyr EC Test Team uses external Gitlab CI jobs to generate code coverage
reports. These CI jobs are defined in the `.gitlab-ci.yml` file in
`platform/ec`.

[TOC]

## Running CI jobs locally

For development purposes, it is possible to run the CI jobs on a local machine
using Docker and `gitlab-runner`.

Note: not all features of Gitlab CI are available when running builds locally.
For example, the local runner cannot build dependencies specified in the
`needs:` sections. (But you can run jobs individually). More details can be
found in the [`gitlab-runner` docs]
(https://docs.gitlab.com/runner/commands/#limitations-of-gitlab-runner-exec).

### Installation

First, you must [install Docker](https://docs.docker.com/get-docker/) on your
system. This is out of the scope of this guide, but there are many resources
on the Internet describing how to do this. Docker allows the CI jobs to run in a
controlled environment using containers, ensuring that the jobs run consistently
and without needing to pollute your own system with the many dependencies.

Next, install the Gitlab Runner. This application is able interpret the
`.gitlab-ci.yml` file, spawn Docker containers to run jobs, and report back
results and artifacts. Usually, runners are deployed in fleets and configured to
register with a Gitlab server, which can push CI jobs to individual runners to
execute. However, it is also possible to use the runner in a purely local mode
using `gitlab-runner exec`.

Full instructions are available on the
[Gitlab website](https://docs.gitlab.com/runner/install/), but the fastest way
to get it for Debian-based systems is to download and install the package
directly:

```
wget https://gitlab-runner-downloads.s3.amazonaws.com/latest/deb/gitlab-runner_amd64.deb
sudo dpkg -i gitlab-runner_amd64.deb
```

### Running a Job

Once Docker and the Gitlab Runner are installed, invoke it as follows. This
takes place outside of the

```
(outside)
mkdir ~/gitlab-runner-output  # Do once

cd ~/chromiumos/src/platform/ec
sudo gitlab-runner exec docker \
    --docker-volumes "$HOME/chromiumos/:$HOME/chromiumos/" \
    --docker-volumes "$HOME/gitlab-runner-output:/builds" \
    <name of job>
```

Please note:
  * `$HOME/chromiumos` should be adjusted to wherever your tree is checked out
    to. The reason it is necessary to mount the entire source tree to the
    Docker container (as opposed to just `platform/ec`, which is done
    automatically) is because the runner needs access to the Git database in
    order to clone the source code. Because the `platform/ec` repository is
    governed by the `repo` tool, the Git database is actually located at
    `$HOME/chromiumos/.repo/projects/src/platform/ec.git`. (The `.git` directory
    in `platform/ec` is merely a symlink)
  * The second mount causes the runner's work directory to be backed by a
    directory on your own system so that you can examine the artifacts after the
    job finishes and the container is stopped.
  * `<name of job>` is one of the jobs defined in `.gitlab-ci.yml`, such as
    `twister_coverage`.
  * You may see error messages like `ERROR: Could not create cache adapter`.
    These appear to be benign, although getting the cache to work might improve
    subsequent build times. This may be investigated at a later date.

### Accessing Artifacts

If you used the command as shown above, all of the build artifacts and source,
as checked out by the Gitlab runner, should be under `~/gitlab-runner-output`.
This will persist after the container exits but also get overwritten again on
the next run.
