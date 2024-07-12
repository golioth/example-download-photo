# Golioth Example Download Photo

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
mkdir golioth-example-download-photo
python -m venv golioth-example-download-photo/.venv
source golioth-example-download-photo/.venv/bin/activate
pip install wheel west
```

### Initialize and install

Run one of these two initializations based on your target board:

```
# Initalize for Zephyr
cd ~/golioth-example-download-photo
west init -m git@github.com:golioth/example-download-photo.git --mf west-zephyr.yml .
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

## Building and running this example

### `native_sim`

``` sh
west build -b native_sim example-download-photo
```

or

``` sh
west build -b native_sim/native/64 example-download-photo
```

### `xiao_esp32s3`

``` sh
west build -b xiao_esp32s3 example-download-photo --sysbuild
west flash
```

### `xiao_esp32s3` with `seeed_xiao_round_display`

``` sh
west build -b xiao_esp32s3 example-download-photo  --sysbuild --shield seeed_xiao_round_display
west flash
```
