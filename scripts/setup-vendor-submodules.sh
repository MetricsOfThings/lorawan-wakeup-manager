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

echo "==> Setting up vendor/CMSIS_5 and vendor/STM32CubeU0 (plain submodules, full checkout, recursive)"
# These two are small enough that a normal shallow-ish init is fine; no
# sparse-checkout needed. --depth 1 keeps history small on first clone;
# it's a no-op if the submodule is already initialized.
#
# --recursive matters here: vendor/STM32CubeU0 has its own nested
# submodules (Drivers/STM32U0xx_HAL_Driver, Drivers/CMSIS/Device/ST/STM32U0xx,
# plus a few unused BSP ones), and platform/stm32u031/CMakeLists.txt compiles
# sources directly out of the first two. Without --recursive those nested
# dirs stay empty and the stm32u031 build fails with missing headers/sources.
# This is safe (unlike Gecko_SDK below): CMSIS_5 and STM32CubeU0 don't carry
# multi-gigabyte payloads in their submodule trees.
git submodule update --init --recursive --depth 1 -- vendor/CMSIS_5 vendor/STM32CubeU0

echo "==> Setting up vendor/STM32CubeC0 (plain submodule, full checkout, recursive)"
# Same pattern and same rationale as vendor/STM32CubeU0 above: a full
# --recursive clone was measured at ~360 MB (Task 1 of the STM32C011
# backend, see .superpowers/sdd/task-1-report.md), comparable to
# STM32CubeU0's ~322 MB and nowhere near Gecko_SDK's multi-gigabyte
# territory, so no sparse-checkout scoping is needed here.
#
# --recursive matters here too: vendor/STM32CubeC0 has its own nested
# submodules (Drivers/STM32C0xx_HAL_Driver, Drivers/CMSIS/Device/ST/STM32C0xx,
# plus unused BSP/Middlewares ones), and platform/stm32c011/CMakeLists.txt
# includes headers out of the first two (Task 1's smoke-test target; later
# tasks compile sources from them too). Without --recursive those nested
# dirs stay empty and the stm32c011 build fails with missing
# headers/sources.
git submodule update --init --recursive --depth 1 -- vendor/STM32CubeC0

echo "==> Setting up vendor/Gecko_SDK (sparse checkout: platform/Device, platform/CMSIS, platform/emlib, platform/common)"
#
# The path list below must stay in sync with the include paths documented
# in platform/efm32g210/CMakeLists.txt, which explains exactly why each
# path is needed (device headers, CMSIS core, emlib, and the
# platform/common headers emlib transitively depends on).
gecko_sdk_dir="vendor/Gecko_SDK"

# The exact commit this repo has pinned vendor/Gecko_SDK to -- read from
# the superproject's own gitlink, not trusted from whatever a fresh
# clone's default branch happens to be at checkout time. Read this
# BEFORE any clone/fetch below touches the submodule.
gecko_sdk_pinned_sha="$(git rev-parse "HEAD:${gecko_sdk_dir}")"

# NOTE: we deliberately do NOT use `git submodule update --init` here.
# `git submodule update` only accepts `--checkout|--rebase|--merge` as its
# checkout-mode selector; `--no-checkout` is not a valid flag for that
# subcommand (it belongs to `git clone`/`git switch`, not `submodule
# update`), so that invocation fails a usage check on every released git
# version once `set -euo pipefail` is in effect. Instead we register the
# submodule and do the blobless, no-checkout clone ourselves, then formally
# absorb it into the superproject's submodule bookkeeping.
git submodule init -- "$gecko_sdk_dir"

if [ ! -d "$gecko_sdk_dir/.git" ] && [ ! -f "$gecko_sdk_dir/.git" ]; then
    echo "    Initializing vendor/Gecko_SDK submodule (blobless, shallow, no checkout yet)..."
    gecko_sdk_url="$(git config -f .gitmodules --get "submodule.${gecko_sdk_dir}.url")"
    git clone --filter=blob:none --no-checkout --depth 1 --single-branch \
        "$gecko_sdk_url" "$gecko_sdk_dir"
    git submodule absorbgitdirs -- "$gecko_sdk_dir"
fi

echo "    Configuring sparse-checkout (cone mode)..."
git -C "$gecko_sdk_dir" sparse-checkout init --cone
git -C "$gecko_sdk_dir" sparse-checkout set \
    platform/Device \
    platform/CMSIS \
    platform/emlib \
    platform/common

# Pin to the exact commit recorded in the superproject rather than
# whatever ref the initial clone's default branch happened to resolve
# to -- a bare `git clone --no-checkout` (or a later `checkout` with no
# argument) tracks the remote's CURRENT default-branch tip, which drifts
# over time and would silently hand out a different, unverified SDK
# snapshot than the one this repo actually vendors. `--depth 1` here
# works because most Git hosts (including GitHub) support fetching an
# arbitrary reachable commit SHA directly, not just branch/tag tips.
echo "    Fetching and checking out the pinned commit (${gecko_sdk_pinned_sha})..."
git -C "$gecko_sdk_dir" fetch --depth 1 origin "$gecko_sdk_pinned_sha"
git -C "$gecko_sdk_dir" checkout "$gecko_sdk_pinned_sha"

echo "==> Done. Vendor submodule status:"
git submodule status

echo
echo "==> vendor/Gecko_SDK sparse-checkout scope:"
git -C "$gecko_sdk_dir" sparse-checkout list

echo
echo "==> Disk usage of vendor/:"
du -sh vendor/* 2>/dev/null || true
