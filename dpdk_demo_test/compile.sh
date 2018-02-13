#! /bin/bash

. ./setup.sh

make O=$RTE_TARGET $=

echo "==*==*==*==*==*==*==*"

ldd $RTE_TARGET/dpdk_demo
