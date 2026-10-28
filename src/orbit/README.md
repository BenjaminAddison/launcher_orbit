# Orbit UI: developer notes

The Cardputer's interface, built with `-D ORBIT_UI=1`
(`boards/m5stack-cardputer/platformio.ini`). Without that flag none of this compiles and
the Launcher keeps its original UI. Every change to shared files sits behind `ORBIT_UI`.

<p align="center">
  <img src="../../media/orbit/install.gif" width="49%" alt="Install screen animation">
  <img src="../../media/orbit/theme-cycle.gif" width="49%" alt="Home screen in each of the seven themes">
</p>

## Files

| File | What's in it |
|---|---|
| `orbit_gfx.*` | Raster core: band buffer, clipping, fills and Bayer-dithered fills, lines, rings, run-length sprites, 2-bit glyphs, UTF-8 text, emblems, and small math helpers that stand in for libm |
| `orbit_ui.*` | UI kit and runner: header, status icons, hint bar, list view, sheets, dialogs, keyboard, steps panel, ring progress, toasts, the scene stack and transitions, and the frame loop |
| `orbit_screens.h` | Every screen's class |
| `orbit_home.cpp` | Sky, stars and planet, the home carousel, charge mode, power off |
| `orbit_boot.cpp` | Launch pad countdown, app picker, liftoff particles |
| `orbit_sd.cpp` | SD browser |
| `orbit_job.cpp` | Install and download screen |
| `orbit_wifi.cpp` | Radar scan, network list, handshake |
| `orbit_ota.cpp` | Generic list screen, Online hub, downlink screen, firmware page |
| `orbit_web.cpp` | QR encoder (version 2-M), Web UI mode and run screens, USB drive |
| `orbit_pman.cpp` | Partition map, range slider |
| `orbit_settings.cpp` | Settings, brightness, theme picker, key binding, app dossier |
| `orbit_assets.*` | Fonts, glyphs, sprites, themes. Generated, don't edit |
| `orbit_bridge.*` | The device side: panel, input, battery and Wi-Fi status, and the Launcher entry points (`loopOptions`, `displayRedStripe`, `displayError`/`displayMsg`, `progressHandler`, `keyboard`, boot screen, home menu, charge mode, LauncherHub lists) |

Everything except `orbit_bridge.cpp` is plain C++ with no Arduino or IDF calls, so it also
builds on a desktop (`-DORBIT_NATIVE`, see below).

## How a frame is drawn

- A frame is drawn in three bands of 45 rows, into one 240×45 buffer (21.6 KB). Each band
  redraws the scene stack, clipped to its rows, and is pushed to the panel. Scenes must
  therefore draw the same thing every time within a frame: change state in `update()`,
  never in `draw()`.
- About 30 fps. Nothing is drawn while the backlight is off.
- There's no alpha. Fades, glows and dimmed backdrops use a 4×4 Bayer dither (`Gfx::mask`,
  `dfill`). A transition draws both scenes, each clipped to its side of the wipe.
- Colours are theme tokens (`C(T_acc)` and so on). `setTheme()` swaps the whole palette.

## Scenes and the Launcher

- `push()`, `pop()` and `run()` manage a stack of `Scene`s. `run()` keeps drawing frames
  until the scene sets `res`.
- Screens like the SD browser or settings stay on the stack while the Launcher's own code
  runs underneath. Stripes, progress, rings and downlink screens are transient:
  `orbEndTransient()` removes them before a screen takes over again (`orbRun()`).
- Only the UI task draws. Calls that come from another task (the web server's upload
  handlers) just record state, and `orbSyncRemote()` shows it on the next UI frame.
- `orbWork()` runs blocking work (Wi-Fi scan, HTTPS GET) on a worker task, so the screen
  keeps animating.

## Display

On an SPI ST7789 (`TFT_DATABUS_N == 0`, `TFT_DISPLAY_DRIVER_N == 1`, which is the Cardputer)
the bridge drives the panel itself through `SPIClass`. It uses Arduino_GFX's own init table,
SPI mode, clock, MADCTL values and window offsets. `tft->begin()` is never called, so the
Arduino_GFX panel and bus classes aren't linked, which saves about 18 KB of flash. `tft`
stays as a shell whose calls do nothing. Other panels would go through `tft` as before.

If the screen is dark or shifted on hardware, look at the panel section at the top of
`orbit_bridge.cpp` first.

## Input

`plat::poll()` turns the Launcher's key flags into `K_UP`, `K_DOWN`, `K_LEFT`, `K_RIGHT`,
`K_OK` and `K_BACK`, plus `K_CHAR` for other keys:
- Holding Enter for 300 ms gives `K_HOLD`; screens opt in with `holdMode()`.
- Text screens (`textMode()`) get every character, including the navigation keys.
- Fn + `` ` `` cancels text entry.

## Themes

There are seven: Nova, Aurora, Solar, Ice, Volt, Mono and Paper.
- The choice is kept in NVS, namespace `launcher`, key `orbitTheme`.
- The first boot picks the theme closest to the old `FGCOLOR`.
- `FGCOLOR`, `ALCOLOR` and `BGCOLOR` follow the theme, so any leftover old drawing still
  matches.

## Changing the art

Fonts, glyphs, sprites and theme palettes live in `support_files/orbit_assets/art/`. These
are the same tables the design mockup runs on. Regenerate the C++ with:

```
node support_files/orbit_assets/gen_assets.js
```

## Screenshots and the desktop harness

`support_files/orbit_assets/harness/` builds `src/orbit` for the desktop (needs g++) and
plays scripted scenarios on a fixed 60 fps clock with the mockup's sample data. To
regenerate every image in `media/orbit/` (needs Python with Pillow):

```
support_files/orbit_assets/harness/make_gallery.sh
```

In `shots.cpp`, the `S..` scenarios mirror the design mockup's screen registry and were used
to compare the port with the mockup pixel by pixel. `addGallery()` holds the README screens
and animations.

## Flash budget

The Cardputer's app partition is 0x150000 bytes. Measured with LTO, this build has about
5.7 KB left; upstream without the Orbit UI has about 33.8 KB. Habits that keep it in budget:

- **No libm in the UI.** `fsin`, `fcos`, `expNeg`, `atan2_`, `fmodp`, `ifloor` and `iceil`
  replace sinf, cosf, expf, atan2f, fmodf, floorf and ceilf. The first two alone avoid about
  2 KB of argument-reduction code. `ifloor` and `iceil` use the FPU's FLOOR.S and CEIL.S.
- **Float literals need an `f`.** Anything else drags in software double math.
- **`TextOpt` is one packed word**, not a struct: 190-odd call sites add up.
- **Static tables should start at zero**, so they land in `.bss`. A default member
  initializer puts the whole table in `.data`, which costs both flash and RAM.
- **Sprites are run-length coded**, and their palettes come from a lookup table.

The UI is laid out for 240×135 (`orb::W`, `orb::H`). Another board would need a panel of that
size, or a layout pass.
