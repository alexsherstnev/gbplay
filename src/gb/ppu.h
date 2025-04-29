#pragma once

#include "defs.h"

typedef enum {
  GB_PPU_MODE_HBLANK = 0,
  GB_PPU_MODE_VBLANK = 1,
  GB_PPU_MODE_OAM = 2,
  GB_PPU_MODE_DRAWING = 3
} GB_ppu_mode_t;

typedef enum {
  GB_PPU_PIXEL_FETCHER_STEP_TILE,
  GB_PPU_PIXEL_FETCHER_STEP_DATA_LOW,
  GB_PPU_PIXEL_FETCHER_STEP_DATA_HIGH,
  GB_PPU_PIXEL_FETCHER_STEP_SLEEP,
  GB_PPU_PIXEL_FETCHER_STEP_PUSH,
} GB_ppu_pixel_fetcher_step_t;

;
#pragma pack(push, 1)

typedef struct {
  uint8_t y;
  uint8_t x;
  uint8_t tile_index;
  uint8_t flags;
} GB_oam_sprite_t;

#pragma pack(pop)

typedef struct {
  uint8_t pixels[8];
  uint8_t count;
} GB_ppu_pixel_fifo_t;

typedef struct {
  uint8_t visible_sprite_indices[GB_MAX_OAM_SPRITES];
  uint8_t visible_sprite_count;
  uint8_t active_sprite_indices[GB_MAX_OAM_SPRITES_PER_LINE];
  uint8_t active_sprite_count;
} GB_ppu_oam_scanline_t;

typedef struct {
  GB_ppu_pixel_fetcher_step_t step;
  uint16_t next_step_cycle;
  uint8_t fetch_x;
  uint8_t x;
  uint8_t scy;
  uint8_t scx;
  bool tile_addr_mode;
  uint8_t tile_index;
  uint8_t tile_low;
  uint8_t tile_high;
  uint8_t wx;
  uint8_t wy;
  uint8_t window_line;
  bool window_entered;
} GB_ppu_pixel_fetcher_t;

typedef struct {
  uint8_t framebuffer[GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT];
  uint16_t cycles;
  GB_ppu_oam_scanline_t oam_scanline;
  GB_ppu_pixel_fetcher_t pixel_fetcher;
  GB_ppu_pixel_fifo_t bg_fifo;
} GB_ppu_t;

GB_result_t GB_ppu_init(GB_emulator_t *gb);
GB_result_t GB_ppu_free(GB_emulator_t *gb);
GB_result_t GB_ppu_tick(GB_emulator_t *gb);

