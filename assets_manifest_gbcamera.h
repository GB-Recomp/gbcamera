/* Manually generated minimal manifest — no pret/source .map available for
 * Game Boy Camera, so the whole ROM is staged as one opaque section. The
 * asset extractor stores it as `rom.bin` and the runtime reads it back
 * into the BSS rom_data buffer on every boot. Functionally equivalent to
 * the old runtime-load path, but goes through the same gb_load_assets
 * code path the Pokemon games use. */
#ifndef GBCAMERA_ASSETS_MANIFEST_H
#define GBCAMERA_ASSETS_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#ifndef PG1_ASSET_ENTRY_DEFINED
#define PG1_ASSET_ENTRY_DEFINED
typedef struct {
    uint32_t rom_offset;
    uint32_t size;
    const char* path;
} Pg1AssetEntry;
#endif

#define GBCAMERA_ASSETS_MANIFEST_COUNT 1

static const Pg1AssetEntry GBCAMERA_ASSETS_MANIFEST[GBCAMERA_ASSETS_MANIFEST_COUNT] = {
    { 0x00000000, 0x00100000, "rom.bin" }, /* full 1 MiB ROM */
};

#endif
