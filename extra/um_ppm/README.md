# Usermode PPM implementation

Usermode UCSI PPM implementation for evaluation and testing usage. Try using
this with the `ucsi_um_test` kernel driver.

## Setup

For usermode implementations, we use libi2c and libgpiod to provide an smbus
implementation. You will need the right libraries to build:

```
sudo apt-get install libi2c-dev libgpiod-dev
```

# Architecture

```
┌─────────┐
│OPM      │
│kernel or│
│cli      │
└────┬────┘
     │
     │
     ▼          ┌───────────┐
 ┌────────┐     │ PD Driver │
 │        ├────►│           │
 │  PPM   │     ├───────────┤
 │        │     │           │
 └────────┘     │ I2C Driver│
                └───────────┘
```

The usermode ppm implementation consists of the PPM task, the PD driver and the
I2C driver backing it. The OPM will either be a CLI (for locally triggering some
functionality) or the `ucsi_um_test` kernel module.
