#!/usr/bin/env bash
set -euo pipefail

# Usage: bash packaging/build-arm.sh /absolute/output/directory
# A pinned source archive may be placed in that directory before publishing.
recipe_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
output_dir=${1:?Usage: bash packaging/build-arm.sh OUTPUT_DIRECTORY}
mkdir -p -- "$output_dir"
output_dir=$(realpath -- "$output_dir")
image=omarchy-qt-arm64:latest

if ! docker image inspect "$image" >/dev/null 2>&1; then
    printf 'Missing Docker image or Docker access: %s\nSee AGENTS.md for setup.\n' "$image" >&2
    exit 1
fi

docker run --rm --platform linux/arm64 \
    --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=$recipe_dir/PKGBUILD,dst=/recipe/PKGBUILD,readonly" \
    --mount "type=bind,src=$output_dir,dst=/work" \
    --workdir /work \
    -e SRCDEST=/work -e PKGDEST=/work \
    "$image" /usr/bin/bash -euc '
        test "$(uname -m)" = aarch64
        mkdir -p arm-build
        cp /recipe/PKGBUILD arm-build/PKGBUILD
        cd arm-build
        printf "source /etc/makepkg.conf\nPKGEXT=\047.pkg.tar.zst\047\n" > makepkg.conf
        makepkg --config "$PWD/makepkg.conf" --cleanbuild --force
    '
