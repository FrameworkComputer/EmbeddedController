#!/usr/bin/env bash

sudo apt-get install -y gcc-arm-none-eabi libftdi1-dev build-essential pkg-config
make BOARD=hx20 CROSS_COMPILE=arm-none-eabi-
