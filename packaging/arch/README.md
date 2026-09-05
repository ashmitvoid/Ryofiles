# Arch / CachyOS packaging

`PKGBUILD` is the development package recipe for Ryofiles while the project is
built directly from Git. It intentionally does not change desktop defaults,
portal configuration, MIME defaults, or Ryoku shell settings during install.

## Build and install

From this directory on an Arch-based system:

```fish
makepkg -si
```

The package installs the `ryofiles` binary, desktop entry, AppStream metadata,
license, and third-party notices using normal system locations under `/usr`.
Removing the package reverses those files through pacman.

## Network backends

The base `gvfs` dependency provides GIO remote-filesystem support including
SFTP and FTP. Additional protocol packages are optional:

- `gvfs-smb` — SMB/CIFS shares
- `gvfs-dnssd` — WebDAV and DNS-SD support
- `gvfs-nfs` — NFS locations

Ryofiles never installs or enables these optional protocol packages itself.

## Archive support

`libarchive` is a required runtime dependency for the native archive extraction
core. Ryofiles links it directly and does not shell out to `tar`, `unzip`, or
`7z` for extraction. The current package ships the secure headless extraction
engine in the main binary while archive UI and compression workflows remain
under development.

## Defaults and portal integration

Installing Ryofiles does **not** make it the default file manager and does not
replace the XDG FileChooser portal. Those integrations remain separate,
explicit, and reversible release steps.
