# Bloons Supermonkey 2 — Nintendo Switch port (Ninja Kiwi engine wrapper)
 
This is a native wrapper / loader that runs the original ARM64 build of Bloons Supermonkey 2 on Switch homebrew. It contains no game code and no game assets.
 
## Install & run
 
You need files from the Android build of Bloons Supermonkey 2 Version 1.10 (com.ninjakiwi.supermonkey).
 
Copy the `.nro` to your SD card (e.g. `sdmc:/switch/smk_nx/smk_nx.nro`), then place your game files next to the `.nro`, in the same folder:
 
```
sdmc:/switch/smk_nx
├── smk_nx.nro
├── libnative.so                   <- from your APK: lib/arm64-v8a/
└── Assets/                        <- from your APK: the whole assets/ folder
```

Optionally, drop a `cursor.png` (64x64, transparency supported) in the same folder to replace the on-screen cursor with your own.
 
## Configuration
 
On first launch the wrapper writes a documented `config.txt` next to the `.nro`. Options are `name value` lines (whitespace-separated); `#` starts a comment.
 
* `portrait` — the game renders portrait (1080x1920), so the picture is rotated 90° to fill the screen (hold the console rotated to play): `1` (default) rotates clockwise (right Joy-Con up); `2` rotates the other way (left Joy-Con up); `0` disables rotation, 16:9 pillar. Set `portrait 0` if you play docked on a TV and don't want a sideways picture.
* `language` — two-letter code, or `auto`.
## Controls
 
| Input | Action |
| --- | --- |
| `+` | Toggle the on-screen cursor |
| `-` | Toggle gyro pointing (tilt/turn the controller to aim) |
| Left stick | Move the cursor |
| `L` / `R` | Recenter the cursor to the middle of the screen (helps gyro aiming) |
| `A` / `ZR` / `ZL` | Tap / confirm (ZL and ZR let you play one-handed) |
| `B` | Back — closes menus, backs out of screens |
| D-pad up / down | Adjust sensitivity of whatever is driving the cursor |
 
A USB mouse works in both handheld and docked: move to control the cursor, left-click to tap, and use the scroll wheel to change sensitivity. Your stick, mouse and gyro sensitivities are remembered in `pointer.cfg` automatically after in-game adjustment.
 
## Save editing
 
On first launch the wrapper also writes a commented `saves.txt`. Uncomment a line, set a value, restart — the edit is applied once per boot, before the game loads anything.
 
```
money_weapons = 100000
money_research = 10000
research.sharpDarts = 5
weapon.dart = 5
```
 
Also available: `weapon.<category>` (dart, boomerang, bomb, magic, energy, ice, storm), `detected_hacks`, `music`, `sfx`, `premium`. Every save in the folder is patched and each is backed up to `<name>.save.bak` the first time.
 
The game's own save files (`Profile.save`, `OldProfile.save`) live in the same folder — back them up if you care about your progress.

## Requirements (to build)
 
Install devkitPro with the Switch toolchain and these packages:
 
```
pacman -S switch-dev
pacman -S switch-mesa switch-libdrm_nouveau switch-sdl2 switch-freetype switch-libpng switch-zlib switch-bzip2
```
 
Then `make`. Run `sh run_audit.sh` first to type-check the whole tree without devkitPro.
 
## Credits
 
The loader/shim infrastructure (`so_util`, `libc_shim`, `imports`, `error`) derives from the SoLoader lineage — TheOfficialFloW's Vita/Switch loader tradition, by way of the open-source `colorsheep_nx` and `btd5_nx` Switch ports, all MIT-licensed. `nx_pointer` and `nx_data_root` come from the same family of ports, and the `saves.txt` editor follows `papapear_nx`.
 
Bloons Supermonkey 2 is © Ninja Kiwi; this project is not affiliated with or endorsed by them.
 
