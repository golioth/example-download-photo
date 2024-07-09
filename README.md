# Golioth Example Template

## Overview

Barebones starting point for building Golioth examples using the
"manifest repository" approach that places the example in an `app`
folder that is a sibling to the `deps` folder where all dependencies are
installed.

## Supported Boards

| Vendor    | Model                      | Zephyr name          |
| --------- | -------------------------- | -------------------- |
| Espressif | ESP32-DevkitC              | esp32_devkitc_wrover |
| Nordic    | nRF9160 DK                 | nrf9160dk_nrf9160_ns |
| NXP       | i.MX RT1024 Evaluation Kit | mimxrt1024_evk       |

## Local Setup

> :important: Do not clone this repo using git. Zephyr's ``west`` meta
> tool should be used to set up your local workspace.

### Install the Python virtual environment

```
cd ~
mkdir golioth-example-template
python -m venv golioth-example-template/.venv
source golioth-example-template/.venv/bin/activate
pip install wheel west
```

### Initialize and install

Run one of these two initializations based on your target board:

```
# Initalize for Zephyr
cd ~/golioth-example-template
west init -m git@github.com:golioth/example-template.git --mf west-zephyr.yml .

# Initalize for NCS (Nordic boards only)
cd ~/golioth-example-template
west init -m git@github.com:golioth/example-template.git --mf west-ncs.yml .

```

Fetch dependencies and configure the build environment

```
west update
west zephyr-export
pip install -r deps/zephyr/scripts/requirements.txt
```

## Building the application

Prior to building, update ``VERSION`` file to reflect the firmware
version number you want to assign to this build.

Then run the following commands to build and program the firmware based
on the device you are using.

### Zephyr build commands

```
west build -p -b <my_board_name> --sysbuild app
west flash
```

### NCS build commands

```
west build -p -b nrf9160dk_nrf9160_ns app
west flash
```

### Configure Authentication credential

Configure PSK-ID and PSK using the device shell based on your Golioth
credentials and reboot:

```
   uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
   uart:~$ settings set golioth/psk <my-psk>
   uart:~$ kernel reboot cold
```

## Features

This example implements the following Golioth services:

* Runtime credentials
* Backend Logging
* Device Settings
* OTA Firmware Update

## Switch Between Zephyr and NCS

After initializing your local repository, you may switch between
building for Zephyr boards and building for NCS boards by using the
following commands:

### Switch to NCS Build

```
west config manifest.file west-ncs.yml
west update
```

### Switch to Zephyr Build

```
west config manifest.file west-zephyr.yml
west update
```

## Using this template to start a new project

Fork this template to create your own Reference Design. After checking
out your fork, use the following approach to pull in future changes:

* Setup

  * Create a `template` remote based on the Reference Design Template
    repository

* Merge in template changes

  * Fetch template changes and tags
  * Merge template release tag into your `main` (or other branch)
  * Resolve merge conflicts (if any) and commit to your repository

```
# Setup
git remote add template https://github.com/golioth/example-template.git
git fetch template --tags

# Merge in template changes
git fetch template --tags
git checkout your_local_branch
git merge exampletemplate_v0.1.0

# Resolve merge conflicts if necessary
git add resolved_files
git commit
```
