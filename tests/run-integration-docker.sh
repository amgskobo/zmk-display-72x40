#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Run the integration suite in an isolated Docker workspace. The argument
# names the ZMK variant: upstream (the default) or dya.
# ZMK_TEST_WORKSPACE_VOLUME names a Docker volume that keeps the west
# workspace between runs, so that a rerun updates it rather than fetching it.

set -euo pipefail

# Git Bash rewrites arguments that look like POSIX paths before Docker sees
# them, which turns the container's /src into a path under the Git install.
export MSYS_NO_PATHCONV=1

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
image="${ZMK_TEST_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
workspace=()
evidence=()
credentials=()

# Only an explicitly selected DYA dependency key is exposed to this container.
if [ -n "${ZMK_TEST_LED_DEPLOY_KEY:-}" ]; then
    if [ "${1:-upstream}" != dya ]; then
        echo "LED deploy-key access is supported only for dya" >&2
        exit 2
    fi
    case "$ZMK_TEST_LED_DEPLOY_KEY" in
        /*|[A-Za-z]:/*) ;;
        *) echo "ZMK_TEST_LED_DEPLOY_KEY must be absolute" >&2; exit 2 ;;
    esac
    if [ ! -f "$ZMK_TEST_LED_DEPLOY_KEY" ] || [ -L "$ZMK_TEST_LED_DEPLOY_KEY" ]; then
        echo "LED deploy key must be an existing regular file" >&2
        exit 2
    fi
    credentials=(
        --volume "$ZMK_TEST_LED_DEPLOY_KEY:/run/credentials/led-key:ro"
        --volume "$repo_root/tests/integration/github-known-hosts:/run/credentials/known_hosts:ro"
        --env 'GIT_SSH_COMMAND=ssh -F /dev/null -i /run/credentials/led-key -o BatchMode=yes -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=/run/credentials/known_hosts'
        --env GIT_TERMINAL_PROMPT=0
        --env GIT_CONFIG_COUNT=1
        --env GIT_CONFIG_KEY_0=url.ssh://git@github.com/amgskobo/zmk-led-patterns.insteadOf
        --env GIT_CONFIG_VALUE_0=https://github.com/amgskobo/zmk-led-patterns
    )
fi

if [ -n "${ZMK_TEST_OUTPUT:-}" ]; then
    case "$ZMK_TEST_OUTPUT" in
        /*|[A-Za-z]:/*) ;;
        *) echo "ZMK_TEST_OUTPUT must be absolute" >&2; exit 2 ;;
    esac
    if [ -e "$ZMK_TEST_OUTPUT" ] || [ -L "$ZMK_TEST_OUTPUT" ]; then
        echo "evidence output already exists: $ZMK_TEST_OUTPUT" >&2
        exit 2
    fi
    mkdir -p -- "$(dirname -- "$ZMK_TEST_OUTPUT")"
    mkdir -- "$ZMK_TEST_OUTPUT"
    evidence=(--volume "$ZMK_TEST_OUTPUT:/results" --env ZMK_TEST_OUTPUT=/results/build)
fi

if [ -n "${ZMK_TEST_WORKSPACE_VOLUME:-}" ]; then
    workspace=(--volume "$ZMK_TEST_WORKSPACE_VOLUME:/workspace" --env ZMK_TEST_WORKSPACE=/workspace)
fi

command=(docker run --rm \
    --volume "$repo_root:/src:ro" \
    ${workspace[@]+"${workspace[@]}"} \
    ${evidence[@]+"${evidence[@]}"} \
    ${credentials[@]+"${credentials[@]}"} \
    "$image" \
    /bin/bash -lc \
    'apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends pkg-config libsdl2-dev >/dev/null && exec /bin/bash /src/tests/integration/run.sh "$@"' \
    bash "$@")
if [ -n "${ZMK_TEST_OUTPUT:-}" ]; then
    printf '%q ' "${command[@]}" >"$ZMK_TEST_OUTPUT/docker-command.txt"
    printf '\n' >>"$ZMK_TEST_OUTPUT/docker-command.txt"
fi
exec "${command[@]}"
