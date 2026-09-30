# Building and releasing Vigilant

Use the existing ARM Docker image `omarchy-qt-arm64:latest`. The working
environment was validated on this machine on 2026-09-30 with QEMU 11.1.1,
Arch Linux ARM, GCC 16.1.1, and Qt 6.11.2. Do not rediscover the build setup.

## ARM64 package

From this repository:

```bash
bash packaging/build-arm.sh /home/darrikm/Projects/release-artifacts/Vigilant
```

The script runs native ARM tools under Docker/QEMU as the host UID, validates
the source checksum, uses a separate clean build directory, and outputs
`vigilant-VERSION-RELEASE-aarch64.pkg.tar.zst`. Put the pinned source archive
in the output directory before building an unpublished release. Otherwise
makepkg downloads it from the URL in `packaging/PKGBUILD`.

Docker socket access requires sandbox escalation in Codex. Request it for the
build command instead of repeatedly retrying inaccessible Docker commands.

If the image is absent, rebuild it once and reuse it:

```bash
docker build --platform linux/arm64 -t omarchy-qt-arm64:latest -f packaging/Dockerfile.arm64 packaging
```

The Dockerfile's first RUN invokes pacman directly. The original base image's
Bash 5.3.15 crashed before shell commands ran; upgrading Bash/readline through
direct `pacman -Syu` fixed it. Do not begin bootstrap with `bash -c`, change
QEMU CPU settings, disable seccomp, or reset host binfmt to solve that known
failure. The validated build needs none of those changes. Dependencies include
`polkit`; makepkg checks runtime dependencies as well as build dependencies.

## x86_64 package

```bash
cd packaging
SRCDEST=/home/darrikm/Projects/release-artifacts/Vigilant PKGDEST=/home/darrikm/Projects/release-artifacts/Vigilant makepkg --cleanbuild --force
```

Never reuse CMake caches between host/container paths or architectures.
Do not bypass source checksums or package dependency checks.

## Release source and checks

Keep the version in CMakeLists.txt, main.cpp, and PKGBUILD aligned. The recipe
uses an uploaded release source asset, not GitHub's automatically generated
tag archive. Generate it from the final source with the following command
(replace 0.2.1 for future versions), then pin its SHA256 in PKGBUILD:

```bash
tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner --transform='s,^,Vigilant-0.2.1/,' -cf - CMakeLists.txt LICENSE README.md main.cpp vigilant.desktop vigilant.png | gzip -n > /home/darrikm/Projects/release-artifacts/Vigilant/vigilant-0.2.1.tar.gz
```

The archive deliberately contains application source only, avoiding a
self-referential PKGBUILD checksum. Recreate it if any included file changes.
Both packages must use this exact archive. Inspect `.PKGINFO`, package contents,
and executable architecture; smoke-test ARM with Qt offscreen in Docker.
Real ARM desktop behavior must not be described as tested unless it was.

When publishing is authorized, push master and the version tag, then upload
both packages, the pinned source archive, PKGBUILD, and SHA256SUMS to GitHub
Releases. Verify uploaded asset hashes. Do not replace an existing release
or tag without an explicit reason and authorization.
