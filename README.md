# PS4Mount

> [!CAUTION]
> **PS4Mount is a work in progress. Use it at your own risk.**
>
> On some consoles and firmwares it can still make the system report that its **database is
> corrupted**. When that happens the console rebuilds its database on the next start, games added by
> PS4Mount can disappear from the library until PS4Mount runs again, and you may have to restart the
> console to get out of repeated error prompts. A forced restart can lose data that was not saved yet.
> **Back up your save data** (to a USB drive) before using PS4Mount, and
> [report any problem](#reporting-problems) with the log.

Play PS4 fake PKGs on a jailbroken PS5 straight from a USB drive, without installing them.

Put the game (and its update) `.pkg` files on a USB drive, plug it in and load `ps4mount.elf`.
PS4Mount finds the packages, adds the games to the home screen and lets you play them from the
drive. Nothing is copied to the console storage, and deleting a game on the console only removes
it from the library: the `.pkg` stays on the drive.

![PS4Mount icon](assets/icon0.png)

## Screenshots

Notifications on the home screen while PS4Mount scans the USB drives:

![Scan notifications on the PS5 home screen](docs/notifications.png)

The web panel, opened from the **Media** tab:

![PS4Mount web panel](docs/web-panel.png)

## Features

- Detects PS4 fake PKGs (game, update and add-ons) in the root of any connected USB drive, or in other
  folders chosen in the settings.
- Adds the games to the home screen with their icon, name and update version.
- Applies updates: put the update `.pkg` next to the game and it is picked up automatically.
- Installs add-ons (DLC), with data or unlock-only. An add-on is matched to its game by title ID, or by
  the add-on IDs the game lists (so add-ons of another region of the same game work too).
- Adds many games in groups, so a drive with dozens of games does not overload the console.
- Games run from the drive; no internal storage is used for the packages.
- Deleting a game from the PS5 removes it from the library only. The `.pkg` is never deleted.
- Notifications while scanning (how many games were found on each drive) and for added and removed
  games.
- A web panel, also available as **PS4Mount** in the **Media** tab, to see the games and add or
  remove them from the library.

## Requirements

- A jailbroken PS5 with fake PKG support (for example etaHEN with kstuff).
  Any firmware with fake PKG support. From 6.00 the games are registered the usual way (reported
  working on 6.50, tested on 13.20). Below 6.00 that made the console report a corrupted database
  (5.50), so there PS4Mount registers each game on its own (confirmed working on 5.50), and does not
  add games if the firmware lacks that function. The startup notification and the log say which one
  is used.
- An ELF loader listening on port **9021** to send the payload.
- Access to the kernel log: either **klogsrv** running on port 3232, or no other payload reading
  `/dev/klog`. PS4Mount reads the system log to know when a game starts and when a delete is
  confirmed. Without it, games cannot start (see [How it works](#how-it-works)).
- A USB drive formatted as **exFAT**.
- PS4 **fake** PKGs (game, and optionally its update and add-ons). Retail PKGs are not supported.
- Avoid game PKGs with patches, add-ons (DLC) or updates merged into them: they may not work. Use the
  original game PKG, with its update and add-ons as separate `.pkg` files.

## Usage

Download `ps4mount.elf` from the [Releases](https://github.com/gasb07/ps4mount/releases) page
(or [build it](#building)).

1. Copy the PS4 game `.pkg` (and its update and add-on `.pkg` files, if you have them) to the **root**
   of the USB drive. The file names do not matter.
2. Plug the drive into the PS5.
3. Send `ps4mount.elf` to the ELF loader (port 9021). Do this after every console boot, or add it to
   the payloads your jailbreak loads automatically.
4. Wait for the notifications:
   - `Scanning usb0 for PS4 games...`
   - `usb0: 2 PS4 game(s) found, 2 new or updated`
   - `Adding 1/2: <game name> (CUSAxxxxx)`
   - `<game name> ready to play`
5. Start the game from the home screen.

On the first run PS4Mount moves every package it uses into a folder per game on the same drive:

```
/ps4/CUSAxxxxx/app.pkg                          game
/ps4/CUSAxxxxx/patch.pkg                        update (optional)
/ps4/CUSAxxxxx/addcont/CUSAyyyyy/<LABEL>/ac.pkg add-on (optional)
```

A game is reported as ready only after its add-ons are installed too. An add-on whose game is not on the
same drive stays where it is and waits: it is moved and installed as soon as its game is on the drive.
The scan notification says how many add-ons are waiting.

Keep this layout. To add more games later, copy their `.pkg` files to the root of the drive again.

### Web panel

Open **PS4Mount** in the **Media** tab, or browse to `http://<PS5 IP>:8770` from any device on
the same network. The panel shows every game found on the connected drives, with its state:

| State | Meaning |
| --- | --- |
| Ready | In the library and ready to play |
| Running | The game is open |
| Not in library | Removed from the library; the `.pkg` is still on the drive |
| Adding | Being added to the library |
| Installed on console | A normal installation of this game exists; PS4Mount leaves it alone |
| On another drive | The same game is also on another connected drive, which is the one used |

Buttons:

- **Remove from library**: removes the game from the home screen. The `.pkg` stays on the drive.
- **Move pkgs back**: removes the game from the library and moves its `.pkg` files back to where they
  came from (see [Moving pkgs back](#moving-pkgs-back)).
- **Add to library**: adds a removed game back.
- **Scan for games**: checks the drives now and shows how many games were found on each one.
- **Settings**: see below.
- **Log**: shows the PS4Mount log.
- **Stop**: stops PS4Mount. Its games cannot start until it is loaded again.

### Settings

![PS4Mount settings](docs/settings.png)

| Setting | Default | Meaning |
| --- | --- | --- |
| Folders to search for pkgs | `/` | Folders, relative to each drive, where new `.pkg` files are picked up (one per line). `/` is the root. The `ps4` folder cannot be used: it holds the games already moved. |
| Add new games in groups | On | Adds many new games in groups instead of one after the other, which keeps the console stable when a drive has a lot of games. |
| Games per group | 5 | Size of each group (1 to 50). |
| Install add-ons (DLC) | On | Moves and installs add-on `.pkg` files. When off, add-ons are left where they are. |

The settings also have **Move all games back** and **Pick up moved-back pkgs again** (see below).

Settings are saved in `/data/ps4mount/config.txt`.

The panel has no password. Anyone on your local network can open it.

### Removing games

Deleting a game from the PS5 (Options > Delete on the home screen, or Settings > Storage), or using
**Remove from library** in the panel, removes the game from the library and leaves the `.pkg` on the
drive. A removed game comes back
when you reconnect the drive or click **Add to library**.

To delete the game for good, delete its `/ps4/CUSAxxxxx` folder from the drive on a computer.

### Moving pkgs back

PS4Mount records where every `.pkg` came from in `ps4/CUSAxxxxx/origin.txt` on the drive: the folder
(any of the scan folders) and the original file name. **Move pkgs back** on a game, or **Move all games
back** in the settings, undoes the move:

- The game leaves the library (several games are handled in groups, like when adding).
- The game, its update, older updates kept aside and its add-ons go back to their original folder with
  their original name. A folder that no longer exists is created again, and an existing file is never
  replaced: ` (1)` is added to the name.
- `.pkg` files moved by versions before 0.5.0 have no record and go to the drive root as
  `CUSAxxxxx - <game> - game.pkg`, `... - update.pkg` and `... - <add-on label>.pkg`.
- The files PS4Mount created for the game are removed. Folders are only removed when empty, and no
  `.pkg` is ever deleted.

The `.pkg` files moved back stay where they are on the next scans (they are listed in
`/data/ps4mount/restored.txt`). To add them again, use **Pick up moved-back pkgs again** in the
settings. A `.pkg` you move, rename or replace yourself is picked up as usual.

## Good to know

- Load PS4Mount soon after every boot. While it is not running, the console does not see the
  packages and may hide the games; they come back as soon as PS4Mount runs again.
- Do not unplug the drive while a game is running.
- **Two or more drives connected at the same time can make the whole console interface slow** (home
  screen, Control Center, game hubs), with or without PS4Mount: the system checks its storage devices
  more often with each extra drive or partition. A drive with extra partitions (for example a boot
  partition added by a multiboot tool) counts as several. If the interface gets slow, keep one drive
  connected, ideally with a single partition.
- For a game with an update, the library and the PS5's own "installed" notification show the version
  of the game pkg (for example 01.00). The game runs with the update; the web panel shows its version.
- Games already installed the normal way on the console are never touched, and their add-ons on the
  drive are not installed.
- **Close PS4Mount games before putting the console in rest mode.** Rest mode powers the drive off, and a
  game left open cannot read its files from the drive after waking up: it locks up, and closing it can
  hang on the "Closing" screen, which needs a console restart. Games that were not open stay in the
  library and work normally after waking up.
- **Games with more than about 29 add-ons:** while a game runs, the console mounts each of its packages (the game,
  the update and every add-on) with two virtual devices: a PS4 package holds an outer filesystem with the
  real content as a second filesystem inside it. The console has 64 of these devices, so a running game
  can use at most about 30 packages: the game, its update and roughly 29 add-ons. A game with more
  add-ons (for example Civilization VII with 35) starts normally but only sees the first add-ons, in
  alphabetical order of their label. This is a console limit that applies to installed games too; it
  does not limit how many games you can have.
- If a game fails to start right after being added, start it again. The log (panel > **Log**) shows
  what happened.

## Reporting problems

If something does not work, open an issue with the
[bug report form](https://github.com/gasb07/ps4mount/issues/new?template=bug_report.yml). It asks for
what is needed to find the cause:

1. **PS4Mount version**: shown at the top of the web panel and in the startup notification.
2. **PS5 firmware**, the **jailbreak / HEN** and every **payload** you load, with their versions.
3. Whether **klogsrv** is running.
4. **The game**: name and title ID (`CUSAxxxxx`), and whether it has an update and add-ons.
5. **Your drives**: how many are connected, how they are formatted (exFAT) and whether a drive has
   more than one partition.
6. **What happened**: what you did, what you expected, and the exact message or error code shown on
   the console.
7. **The log**: open the web panel, click **Log** and copy the text, or download
   `/data/ps4mount/log.txt` over FTP. Include the part from the moment the problem happened.

Do not attach `.pkg` files or links to them.

## How it works

A PS4 PKG install on the PS5 is mostly a byte copy of the package to `/user/app/<TITLE_ID>/app.pkg`
plus some metadata. PS4Mount skips the copy:

1. It writes the files an install would leave on the console, generated from the package itself:
   `app.json`, `app.pbm`, `app.xml`, `patch.*`, the PlayGo records (`/user/playgo/...`,
   `/system_data/playgo/<TITLE_ID>/pfs.pbm`), the app metadata, and the fake license (`.rif`/`.idx`)
   decrypted from the package.
2. The game files go into an internal folder, `/system_data/ps4mount/app/<TITLE_ID>` (and
   `/system_data/ps4mount/patch/<TITLE_ID>` for an update), where `app.pkg` and `patch.pkg` are
   symbolic links to the packages on the drive. That folder is mounted over `/user/app/<TITLE_ID>`
   (and `/user/patch/<TITLE_ID>`) with `nullfs`. The drive itself is never mounted there.
3. It asks the system to register the title (`sceAppInstUtilAppInstallAll`), which adds it to the
   library and refreshes the home screen. PS4Mount never writes to the system databases itself.
4. A delete from the console can only remove the links, never the packages. While a delete screen
   is open (on the home screen or in Settings > Storage) PS4Mount also unmounts the game folders,
   so the system can remove `/user/app/<TITLE_ID>` without an error.

State and log are kept in `/data/ps4mount/`. The Media tab shortcut is `PMNT00001`
(`/user/app/PMNT00001`).

## Building

Requirements: the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) in `/opt/ps5-payload-sdk`
(or set `PS5_PAYLOAD_SDK`), `xxd` and bash.

```sh
./build.sh          # builds build/ps4mount.elf
```

Unit tests (crypto, the generated install files and the `param.sfo` parser) run on the computer, with
no console or SDK:

```sh
bash test/run.sh
```

GitHub Actions runs them before every build.

Optional tools:

```sh
python3 tools/make_icon.py                                 # redraws assets/icon0.png (needs Pillow)
python3 tools/gen_keys.py <LibOrbisPkg>/LibOrbisPkg/Util/Keys.cs   # regenerates src/keys.h
```

### Releases

GitHub Actions (`.github/workflows/build.yml`) builds every push and pull request with the PS5
Payload SDK release set in `SDK_VERSION`, and keeps `ps4mount.elf` as a build artifact.

To publish a release:

1. Set the new version in `src/main.c` (`#define VERSION "PS4Mount X.Y.Z"`) and commit it.
2. Tag the commit and push the tag:
   ```sh
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```

The workflow checks that the tag matches `VERSION`, builds the payload and creates the GitHub
release with `ps4mount.elf` and its SHA-256 checksum attached.

| Path | Content |
| --- | --- |
| `src/main.c` | Daemon: drive scan, registration, mounts, klog events, panel actions |
| `src/pkg.c` | PKG reader and fake-key entry decryption |
| `src/build.c` | Generators for the install files (app/patch metadata, PlayGo records, license) |
| `src/db.c` | Library rows (`app.db`, `appinfo.db`) |
| `src/crypto.c` | SHA-256, AES-128-CBC, CRC32C, RSA-2048 |
| `src/logfile.c` | Keeps `log.txt` under 1 MB |
| `src/web.c`, `web/index.html` | Web panel |
| `test/` | Unit tests (`bash test/run.sh`) |
| `tools/pdb_*_template.bin` | PlayGo record layouts (no title data) |
| `third_party/sqlite/` | SQLite amalgamation |

## Credits

PS4Mount does not contain code copied from other projects except where noted below.

| Project | Author | License | Use |
| --- | --- | --- | --- |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | John Törnblom | GPL-3.0-or-later | Toolchain, headers and runtime linked into the payload |
| [LibOrbisPkg](https://github.com/maxton/LibOrbisPkg) | maxton | LGPL-3.0 | Source of the fake PKG key (`PkgDerivedKey3Keyset`, in `src/keys.h`) and reference for the PKG entry encryption, entry IDs and the `playgo-chunk.dat` layout |
| [SQLite](https://sqlite.org) | SQLite authors | Public domain | `third_party/sqlite/` (amalgamation 3.46.1), used to write the library databases |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) | drakmor | GPL-3.0 | Inspiration for auto-mounting games from USB drives and for registering titles with `sceAppInstUtilAppInstallAll` on firmware 12.00+. No code was used. |
| [ps5-app-dumper](https://github.com/EchoStretch/ps5-app-dumper) | EchoStretch | see project | Reference for the Media tab shortcut (`applicationCategoryType` 65536 with `deeplinkUri`). No code was used. |

## License

PS4Mount is licensed under the GNU General Public License v3.0 or later, see [LICENSE](LICENSE).
It links the PS5 Payload SDK runtime (GPL-3.0-or-later). `src/keys.h` carries data published by
LibOrbisPkg under the LGPL-3.0. SQLite is in the public domain.

This project is not affiliated with or endorsed by Sony Interactive Entertainment. Only use it with
games you own.
