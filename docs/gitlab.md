# Gitlab CI

The Zephyr EC Test Team uses external Gitlab CI jobs to generate code coverage
reports. These CI jobs are defined in the `.gitlab-ci.yml` file in
`platform/ec`.

[TOC]

## Running CI jobs locally

**Note: as of Gitlab 16.0, the `gitlab-runner exec` subcommand is deprecated.**
There is currently not a supported mechanism for running Gitlab CI pipelines
locally.

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
