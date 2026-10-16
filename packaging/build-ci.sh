#!/usr/bin/env bash
set -euo pipefail

# Runs as an unprivileged user inside the architecture's Arch Linux container.
expected_arch=${1:?Usage: build-ci.sh ARCH}
[[ "$(uname -m)" == "$expected_arch" ]]
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
cp /out/PKGBUILD /out/vigilant-*.tar.gz "$build_dir/"
cd "$build_dir"
printf "source /etc/makepkg.conf\nPKGEXT='.pkg.tar.zst'\n" > makepkg.conf
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software PKGDEST=/out makepkg --config "$PWD/makepkg.conf" --cleanbuild --force --noconfirm
source PKGBUILD
package="/out/$pkgname-$pkgver-$pkgrel-$expected_arch.pkg.tar.zst"
bsdtar -xOf "$package" .PKGINFO | grep -Fx "arch = $expected_arch"
mkdir unpacked
bsdtar -xf "$package" -C unpacked
readelf -h unpacked/usr/bin/vigilant
case "$expected_arch" in
  x86_64) readelf -h unpacked/usr/bin/vigilant | grep -F 'Advanced Micro Devices X86-64' ;;
  aarch64) readelf -h unpacked/usr/bin/vigilant | grep -F 'AArch64' ;;
  *) exit 1 ;;
esac
mkdir -p runtime
chmod 700 runtime
export XDG_RUNTIME_DIR="$PWD/runtime"
export XDG_DATA_HOME="$PWD/test-data"
export XDG_CONFIG_HOME="$PWD/test-config"
set +e
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  dbus-run-session -- timeout 8s unpacked/usr/bin/vigilant > smoke.log 2>&1
smoke_status=$?
set -e
cat smoke.log
[[ "$smoke_status" == 124 ]] || { echo "Startup smoke test failed: $smoke_status" >&2; exit 1; }
cd /out
sha256sum "$pkgname-$pkgver.tar.gz" PKGBUILD "$pkgname-$pkgver-$pkgrel-$expected_arch.pkg.tar.zst" > "SHA256SUMS-$expected_arch"
