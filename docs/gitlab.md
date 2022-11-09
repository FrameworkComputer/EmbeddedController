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
takes place outside of the chroot environment.

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

## Gitlab runners

The gitlab builds run on a number of [virtual and real machines](https://gitlab.com/groups/zephyr-ec/-/runners)
which are currently at Simon's house, and cloud virtual machines.

### Create a new VM

* Visit https://pantheon.corp.google.com/compute/instances?onCreate=true&project=chromeos-ec-gitlab
  * Click on instance-1
  * Click create similar
* Wait for new instance to be created
* Click on SSH
* Install docker
```
sudo apt-get remove docker docker-engine docker.io containerd runc
sudo apt-get update
sudo apt-get install \
    ca-certificates \
    curl \
    gnupg \
    lsb-release
sudo mkdir -p /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/debian/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/debian \
  $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
sudo apt-get update
sudo apt-get install docker-ce docker-ce-cli containerd.io docker-compose-plugin
sudo docker run hello-world
```
* Install gitlab runner
```
sudo curl -L --output /usr/local/bin/gitlab-runner https://gitlab-runner-downloads.s3.amazonaws.com/latest/binaries/gitlab-runner-linux-amd64
sudo chmod +x /usr/local/bin/gitlab-runner
sudo useradd --comment 'GitLab Runner' --create-home gitlab-runner --shell /bin/bash
sudo gitlab-runner install --user=gitlab-runner --working-directory=/home/gitlab-runner
sudo gitlab-runner start
```
* Register new runner using command from https://gitlab.com/groups/zephyr-ec/-/runners (click on Register a group runner, click on show instructions, click on Linux)
```
sudo gitlab-runner register --url https://gitlab.com/ --registration-token TOKENGOESHERE
Runtime platform                                    arch=amd64 os=linux pid=56156 revision=bbcb5aba version=15.3.0
Running in system-mode.

Enter the GitLab instance URL (for example, https://gitlab.com/):
[https://gitlab.com/]:
Enter the registration token:
[TOKENGOESHERE]:
Enter a description for the runner:
[instance-2]: Cloud runner instance-2
Enter tags for the runner (comma-separated):

Enter optional maintenance note for the runner:

Registering runner... succeeded                     runner=TOKENGOESHERE
Enter an executor: docker, parallels, shell, docker-ssh+machine, custom, docker-ssh, ssh, virtualbox, docker+machine, kubernetes:
docker
Enter the default Docker image (for example, ruby:2.7):
ruby:2.7
Runner registered successfully. Feel free to start it, but if it's running already the config should be automatically reloaded!

Configuration (with the authentication token) was saved in "/etc/gitlab-runner/config.toml"
```

* Install cleanup docker cleanup daily cron
```
( echo "0 3 * * * /usr/bin/docker system prune -f -a --volumes" ; sudo crontab -l -u root ) | sudo crontab -u root -
```
