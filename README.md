# <img src="vigilant.png" width="48" height="48" alt=""> Vigilant

Vigilant helps you see and manage what starts with your Omarchy desktop or when your system boots.

## What it does

- Shows desktop startup apps and systemd services and timers.
- Lets you enable, disable, or delete entries you manage.
- Saves a backup and restores it if you need to undo changes.
- Opens an entry's file in your default Omarchy editor from the right-click menu.
- Follows your current Omarchy theme.

## Install

Download the latest Omarchy package from [Releases](https://github.com/seth-reee/vigilant/releases), then install it from your Downloads folder:

```bash
sudo pacman -U ./vigilant-*.pkg.tar.zst
```

The package recipe supports Omarchy on x86_64 and ARM64, using Arch Linux and Arch Linux ARM respectively. Install the package whose filename matches your machine's architecture. To build from the tagged release, install `base-devel`, `cmake`, `ninja`, and `qt6-base`, then run `makepkg -s` in `packaging/`.

**ARM64 status:** The ARM64 package built and launched headlessly under QEMU, but remains untested on a real ARM64 Omarchy desktop.

Open **Vigilant** from the application menu.

## Use

Choose **Save backup** before changing startup entries. Select an entry to enable or disable it. **Delete** is available for files in your user account. Choose **Load backup** to restore a saved list. System service changes may ask for your password and take effect at the next boot; desktop startup changes take effect at the next login.

## License

Vigilant is made by [seth-reee](https://github.com/seth-reee) and is available under the [MIT License](LICENSE).

## Automated releases

Every push to `master` prepares the next patch version, builds native x86_64 and ARM64 Arch packages, and publishes a GitHub release after both architecture jobs pass. The workflow updates the version and pinned source checksum, tags the tested commit, and verifies uploaded asset checksums before publishing. No extra secret is required. An explicitly increased, untagged version in CMake and PKGBUILD is respected.

Pull requests build development packages without publishing. Manual runs on `master` also release; matching version tags build and publish that version. Package and source artifacts are available from the **Arch Linux packages** workflow. Published releases are verified on retries and never replaced.

Existing tests run during packaging where available, followed by packaged executable startup checks with an isolated D-Bus session. Both architectures verify package metadata and ELF architecture. CI does not establish real ARM desktop, Wayland, hardware GPU, or live network behavior.

## Arch Linux and NixOS behavior

Vigilant detects the host using `/etc/os-release` (`ID` and `ID_LIKE`), with the
NixOS marker as a fallback. Installing Nix on Arch does not switch the host mode.
It resolves system commands from the host's standard locations, including
NixOS's system profile and privileged-wrapper directory. The Omarchy editor is
used when available; otherwise the desktop's default file handler opens sources.

On Arch, ordinary desktop and systemd enable/disable actions work as before.
On NixOS, system service enablement is shown for inspection and must be changed
through NixOS configuration. Manually maintained user services and desktop
startup entries remain editable.

Files that resolve into `/nix/store`, including Home Manager links and linked
parent directories, are labeled **Nix managed** and cannot be changed or deleted
through Vigilant. This protection applies on either host. Save backup omits
managed entries, and Load backup rejects writes, removals, and state changes
that would replace or shadow a currently managed entry, including older backups.
Use NixOS or Home Manager configuration and rebuild to change those entries.
Vigilant does not run rebuilds or modify your Nix configuration.

## Nix package

The flake supports `x86_64-linux` and `aarch64-linux`:

```sh
nix build .#vigilant
nix run .#vigilant
```

Add `inputs.vigilant.url = "github:seth-reee/Vigilant";` to your NixOS flake,
pass `inputs` through `specialArgs`, and include this in your module:

```nix
{ inputs, pkgs, ... }: {
  environment.systemPackages = [
    inputs.vigilant.packages.${pkgs.stdenv.hostPlatform.system}.vigilant
  ];
}
```

For Home Manager, use the same expression in `home.packages` with `inputs` in
`extraSpecialArgs`. NixOS system entries are read-only in Vigilant, so this mode
needs no extra privileged helper or custom authorization rules.

CI builds both architectures and publishes Nix runtime closures alongside the
Arch packages. Download the matching `.nar.xz`, `.store-path`, and
`SHA256SUMS-nix-SYSTEM` files, then replace VERSION and SYSTEM below:

```sh
sha256sum -c SHA256SUMS-nix-SYSTEM
xz -dc vigilant-VERSION-nix-SYSTEM.nar.xz | sudo nix-store --import
nix profile install "$(cat vigilant-VERSION-nix-SYSTEM.store-path)"
```

Nix must already be installed. Omit `sudo` for single-user Nix installations.
The declarative flake installation is preferred on NixOS.
