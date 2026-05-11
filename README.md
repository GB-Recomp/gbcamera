# gbcamera

Static recompilation of the **Game Boy Camera** (US "Gold" cart, MBC `$FC`)
into portable C, built with
[GB-Recomp/gb-recompiled](https://github.com/GB-Recomp/gb-recompiled).

This is the first non-Pokemon entry in the GB-Recomp org and the first cart
recompiled here that has no [pret](https://github.com/pret)-style
disassembly. Sources are generated directly from the ROM — symbol names
are generic (`gbcamera__func_BB_HHHH`) rather than human-readable.

## What you get

- Game Boy Camera fully playable through gb-recompiled's runtime — the
  runtime's MBC `$FC` emulation hands the cart frames from your host
  webcam (Linux v4l2 / macOS AVFoundation / Windows Media Foundation),
  downsampled + dithered to the 128×112 4-shade tile format.
- Game Boy Printer emulation works alongside — prints save to
  `<cwd>/prints/*.png`.

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

Produces a `gbcamera` executable (default profile is MinSizeRel + dead-
strip — sub-MB binary).

## Run

Drop a Game Boy Camera ROM at `roms/gbcamera.gb` next to the executable:

```sh
mkdir -p roms
cp '/path/to/Game Boy Camera Gold (USA) (SGB Enhanced).gb' roms/gbcamera.gb
./gbcamera
```

The launcher auto-starts (single-cart mode). First boot extracts the ROM
into `assets/gbcamera/` and runs from there afterward. The ROM file is
no longer needed once that's done. Press Esc for the runtime menu.

The runtime requires a working webcam on the host; without one the
camera viewfinder will stay black. `gbcam_list_devices()` (called once
at boot) lists what was found in the log.

## Notes on the build

This cart was recompiled **without a `.map` file** (none exists for the
Camera ROM). Two consequences:

1. The asset manifest is a one-entry blob (the entire 1 MiB ROM goes to
   `assets/gbcamera/rom.bin`) rather than the per-section split the
   Pokemon repos use.
2. Symbols inside the recompiled C aren't named after their source
   labels — they're `gbcamera__func_<bank>_<addr>` instead.

Both are fine for actually running the cart; they just mean the
generated code is less readable if you go spelunking.

## Regenerating

```sh
tools/regen.sh /path/to/gbcamera.gb /path/to/gb-recompiled
```

Rebuilds the C sources from the supplied ROM via gbrecomp + the
bss_rom_data patch. See the script header for details.

## In a compilation

```cmake
FetchContent_Declare(gbcamera
    GIT_REPOSITORY https://github.com/GB-Recomp/gbcamera.git
    GIT_TAG main
)
FetchContent_MakeAvailable(gbcamera)
# Link `gbcamera_cart` into your launcher and call gbcamera_main(argc, argv)
# from g_games[].
```
