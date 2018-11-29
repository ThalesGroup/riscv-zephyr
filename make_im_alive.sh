#!/bin/bash

export ZEPHYR_GCC_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk

. ./zephyr-env.sh

set -e
set -u

cd samples/im_alive
mkdir -p build/am_ft_devkit
cd build/am_ft_devkit

cmake -DBOARD=am_ft_devkit ../..
make clean
make
