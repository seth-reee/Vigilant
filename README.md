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
