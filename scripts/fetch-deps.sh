#!/bin/bash
# Fetch third-party sources that are not stored in this repo.
set -e
cd "$(dirname "$0")/.."
mkdir -p third_party
if [ ! -d third_party/lvgl ]; then
    git clone -q --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git third_party/lvgl
    rm -rf third_party/lvgl/.git
fi
# Pretendard (SIL OFL 1.1) is only needed to regenerate src/ui/fonts/*.c
if [ ! -d third_party/fonts/p ]; then
    mkdir -p third_party/fonts
    curl -sL -o third_party/fonts/pretendard.zip https://github.com/orioncactus/pretendard/releases/download/v1.3.9/Pretendard-1.3.9.zip
    unzip -o -q third_party/fonts/pretendard.zip 'public/static/Pretendard-*.otf' 'LICENSE*' -d third_party/fonts/p
fi
echo ok
