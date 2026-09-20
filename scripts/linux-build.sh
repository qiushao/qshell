#!/bin/bash

SCRIPT_DIR=$(cd $(dirname ${BASH_SOURCE[0]});pwd)
cd ${SCRIPT_DIR}/..

# 优先使用传入参数，否则从 git tag 获取，最后使用默认值
if [ -n "$1" ]; then
    VERSION="$1"
elif git describe --tags --abbrev=0 &>/dev/null; then
    VERSION=$(git describe --tags --abbrev=0 | sed 's/^[vV]//')
else
    VERSION="1.0.0"
fi

echo "Building version: ${VERSION}"

rm -rf build
cmake -B build -S . -DAPP_VERSION="${VERSION}" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
cmake --build build --target package
