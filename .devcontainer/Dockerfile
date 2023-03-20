FROM mcr.microsoft.com/devcontainers/base:jammy

RUN apt update
RUN apt install -y gcc-arm-none-eabi libftdi1-dev build-essential pkg-config gawk gcc-10
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 10