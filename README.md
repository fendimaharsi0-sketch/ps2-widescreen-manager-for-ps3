# PS2 Widescreen Manager

A standalone PS3 homebrew app that manages widescreen patches for PS2 games
directly on the console. It lists the PS2 games in `/dev_hdd0/PS2ISO/`,
and lets you enable widescreen for each game — either by writing a
`.CONFIG` patch file next to the ISO, or by patching the ISO itself.

No PC needed after installation.

## Features

- Game list from `/dev_hdd0/PS2ISO/` with title, serial, CRC, patch
  database status, and activation state (`yes` / `patched` / `yes+iso` /
  `no`).
- One-button widescreen toggle per game (X): writes `<ISO>.CONFIG`
  next to the ISO, or deletes it.
- Patch editor (Triangle): check or uncheck individual patches. Only
  16:9 widescreen patches are enabled by default; other widescreen
  variants can be enabled manually.
- Direct ISO patching (Start): writes the patch bytes into the ISO
  itself, with a byte-identical restore (Start again). A `.wsrest`
  backup is kept next to the ISO until the restore is done.
- Merged patch sources: the bundled PCSX2 widescreen patch database
  (4,719 patch files, snapshot 2026-11-10) merged with the webMAN
  CONFIG database (1,549 entries, snapshot 2026-11-02). Patch file
  entries win over database entries for the same address.
- Custom `.CONFIG` protection: a hand-made `.CONFIG` is never
  overwritten or deleted by the app. It is backed up once to
  `.CONFIG.orig` before any write, and restored when the app's own
  `.CONFIG` is removed.
- Your own patch files: drop `.pnach` files in
  `/dev_hdd0/PS2ISO/pnach/` — any filename works. A game is matched
  from the `gametitle` inside the file and the checksum in the
  filename, and your files always win over the bundled database.
- Serial fallback: if no patch file matches a game's checksum, the
  serial number is tried as a last resort.
- Duplicate picker: if several patch files match a game, the app
  lets you choose one instead of refusing. The list shows each
  file's name, and the editor shows the file's header (title and
  comments) above the patch list, so you can see what a file is for
  before enabling it — for example, which file belongs to an
  English-patched ISO.
- Old-value priority (Select, session only): by default the old value
  comes from the patch file; Select switches to reading it from the
  game's ELF.
- Progress dialog while scanning, XMB-friendly main loop, exit
  confirmation.
- Full layout at 720p/1080p, compact 8-row layout at 480p (including
  480p CRT safe-area margins).

## Requirements

- A PS3 that can install and run homebrew packages.
- PS2 games as ISO files in `/dev_hdd0/PS2ISO/`.
- PS2 games played through Sony's `ps2_netemu`, which is what reads
  `.CONFIG` patch files.

## Usage

Main list:

- Up / Down — move. L1 / R1 — page.
- X — toggle widescreen for the selected game (write or delete
  `.CONFIG`).
- Triangle — open the patch editor.
- Start — patch the ISO directly, or restore it if already patched.
- Select — toggle old-value priority (patch file vs. ELF).
- Circle — exit (asks for confirmation).

If several patch files match a game, X / Triangle / Start opens a
file picker first, then the editor.

Patch editor:

- Up / Down — move. L1 / R1 — page.
- X — check / uncheck a patch.
- Start — patch the ISO with the checked patches only, or restore it.
- Circle — write `.CONFIG` from the checked patches and close.
- Triangle — close without writing.

## How it works

Games are identified by Title ID and by a checksum of the boot ELF
(XOR-fold of its 32-bit words — this is what the PS2 scene calls the
game's "CRC").

A `.CONFIG` file is a stream of 32-bit words for Sony's built-in
patch command (`0x0a`): each patch is a triple of address, old value,
and new value, at most 32 per file. At load time the new value is
written only if the memory still holds the old value. That is why a
`.CONFIG` patch can silently do nothing for a game whose memory
differs, and why direct ISO patching is offered as the stronger
option: it writes the exact bytes into the ISO, so it does not depend
on the old value at all.

`.CONFIG` files written by this app carry a `WSM1` marker at the end
of the file, so app-made files can be told apart from hand-made ones.

## Where the patch data comes from

The `data/` folders in this repository are empty on purpose
(`.gitkeep` only). Download the data once from its source and
put it where the build expects it:

- **Widescreen patches → `data/patches/`**: the
  [PCSX2](https://github.com/PCSX2/pcsx2_patches) `pcsx2_patches`
  repository — the same patch files PCSX2 itself uses for widescreen
  fixes and cheat codes. Clone it (or download a snapshot) and copy
  its `.pnach` files into `data/patches/`. Snapshot used for
  testing: 2026-11-10, 4,719 files. The app only enables 16:9
  widescreen groups by default.
- **CONFIG database → `data/configdb/`**: the community PS2CONFIG
  database from
  [webMAN MOD](https://github.com/aldostools/webMAN-MOD/tree/master/_Projects_/updater/PS2CONFIG/USRDIR/CONFIG)
  (`_Projects_/updater/PS2CONFIG/USRDIR/CONFIG`) — copy its `NET`
  and `CUSTOM` folders into `data/configdb/`. Snapshot used for
  testing: master 2026-11-02, 1,549 entries. Single entries can
  also be downloaded from https://ps3.aldostools.org/ps2config.html
- **App icons → `data/`**: `ICON0.PNG` and `PIC1.PNG` are not in
  this repository either — place them directly in `data/`.

## Safety notes

- Back up your game images first. The app is designed to restore an
  ISO to its original state: the original bytes are saved in
  `<ISO>.wsrest` next to the ISO, and Start again writes them back
  byte-identically. But writing to a file is inherently destructive —
  a crash, a power loss, or an interrupted write while patching can
  still corrupt the ISO beyond what the backup can repair. Keep a
  backup image taken from your own original disc, and patch a copy
  if you can.
- ISO patching modifies the game file. The original bytes are saved
  in `<ISO>.wsrest` next to the ISO; Start again restores them
  byte-identically and deletes the backup. Do not delete `.wsrest`
  while a game shows as patched.
- A hand-made `.CONFIG` is backed up to `.CONFIG.orig` the first time
  the app writes over it. The app never deletes a file it did not
  create, except for its own `.CONFIG` when patching the ISO.
- Writing a `.CONFIG` to a game that is already ISO-patched is
  refused; restore the ISO first.
- The app only touches files in `/dev_hdd0/PS2ISO/` and its own
  folder (`PS2W00001`). It has no settings file; the `.CONFIG` files
  themselves are the state.

## Status

Development build. Confirmed on hardware (CFW PS3; HEN has not been
tested):

- App boots and scans `/dev_hdd0/PS2ISO/`.
- The patch editor's ISO patch applies only the checked patches.
- The 480p compact layout displays correctly.
- A watermarked `.CONFIG` applies in-game, and `.CONFIG.orig`
  restores a hand-made `.CONFIG` when the app's own file is removed.

Which method to use depends on the game. Some games work with just
the `.CONFIG` hook; others only work effectively with a direct ISO
patch. The `.CONFIG` hook is the cleaner way — it leaves the game
file untouched — but it has a lower success rate, because the patch
only applies if the game's memory still holds the expected old value.
The ISO patch has a higher chance of working, but it alters the
image file, so keep a backup; in most cases the original state can
be restored from the `.wsrest` backup.

## Building

Requires the PSL1GHT SDK (`ppu-gcc`):

```sh
./build.sh
```

This produces a `.pkg` (ContentID `UP0001-PS2W00001_00-0000000000000001`,
app ID `PS2W00001`).

The `data/` folders ship empty (`.gitkeep` only). Download the
patch data once from its source (see below), then run the build.
If you already have the data elsewhere, point the build at it
with `PATCHES_SRC` / `CONFIGDB_SRC` instead of copying it in.

The host test suite needs no SDK — just a host compiler and the
PCSX2 patch snapshot above:

```sh
tests/run_tests.sh
```

## Credits and data sources

- Widescreen patch database: the [PCSX2](https://github.com/PCSX2/pcsx2)
  `pcsx2_patches` repository (patch files, snapshot 2026-11-10) — a
  community collection, bundled as-is, no curation. It belongs to the
  PCSX2 project and its contributors.
- CONFIG database: webMAN MOD snapshot (2026-11-02) — a community
  collection, bundled as-is, no curation. It belongs to the webMAN
  MOD project and its contributors.
- Built with [PSL1GHT](https://github.com/ps3dev/PSL1GHT).

## License

GPL-3.0-or-later (the app's own code, same as PCSX2). The bundled
patch databases are not covered by this license: each one stays with
its own community, credited in Credits and data sources above.
