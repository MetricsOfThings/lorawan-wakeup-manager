#!/usr/bin/env bash
#
# setup-vendor-submodules.sh
#
# Sets up all vendor/ submodules for this repo. This is the one command
# to run after cloning (or at any point later) to get every backend's
# vendor SDK checked out correctly.
#
# Why this script exists (read before "simplifying" it away):
#
#   `vendor/Gecko_SDK` (used by the EFM32G210 backend) is a clone of
#   Silicon Labs' Gecko SDK, a multi-gigabyte, multi-protocol-stack
#   monorepo (Bluetooth, Zigbee, Z-Wave, Thread, Matter, and more, plus
#   git-lfs prebuilt binaries for those stacks). This repo only needs
#   four of its subdirectories (see the sparse-checkout list below).
#   A plain `git submodule update --init --recursive` clones and checks
#   out the *entire* Gecko SDK, which has already exhausted a
#   contributor's disk once (see .superpowers/sdd/task-1-report.md,
#   "What the prior attempt hit"). Git does not persist
#   `sparse-checkout` patterns anywhere in the tracked repo
#   (.gitmodules can't express them) -- they only live in the local
#   `.git/modules/.../info/sparse-checkout` file on whichever machine
#   ran the `sparse-checkout set` command. So every fresh clone needs
#   to redo that scoping, and this script is how.
#
# Safe to re-run any time; every step here is idempotent.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

echo "==> Setting up vendor/CMSIS_5 and vendor/STM32CubeU0 (plain submodules, full checkout)"
# These two are small enough that a normal shallow-ish init is fine; no
# sparse-checkout needed. --depth 1 keeps history small on first clone;
# it's a no-op if the submodule is already initialized.
git submodule update --init --depth 1 -- vendor/CMSIS_5 vendor/STM32CubeU0

echo "==> Setting up vendor/Gecko_SDK (sparse checkout: platform/Device, platform/CMSIS, platform/emlib, platform/common)"
#
# The path list below must stay in sync with the include paths documented
# in platform/efm32g210/CMakeLists.txt, which explains exactly why each
# path is needed (device headers, CMSIS core, emlib, and the
# platform/common headers emlib transitively depends on).
gecko_sdk_dir="vendor/Gecko_SDK"

if [ ! -d "$gecko_sdk_dir/.git" ] && [ ! -f "$gecko_sdk_dir/.git" ]; then
    echo "    Initializing vendor/Gecko_SDK submodule (blobless, shallow, no checkout yet)..."
    git submodule update --init --depth 1 --filter=blob:none --no-checkout -- "$gecko_sdk_dir"
fi

echo "    Configuring sparse-checkout (cone mode)..."
git -C "$gecko_sdk_dir" sparse-checkout init --cone
git -C "$gecko_sdk_dir" sparse-checkout set \
    platform/Device \
    platform/CMSIS \
    platform/emlib \
    platform/common

echo "    Checking out sparse tree..."
git -C "$gecko_sdk_dir" checkout

echo "==> Done. Vendor submodule status:"
git submodule status

echo
echo "==> vendor/Gecko_SDK sparse-checkout scope:"
git -C "$gecko_sdk_dir" sparse-checkout list

echo
echo "==> Disk usage of vendor/:"
du -sh vendor/* 2>/dev/null || true
