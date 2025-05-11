#include "ppu.h"
#include "gb.h"  // IWYU pragma: keep

static void reset(GB_emulator_t *gb) {
  // General
  memset(gb->ppu.framebuffer, 0, GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT * sizeof(uint8_t));
  gb->ppu.cycles = 0;

  // OAM scanline
  memset(gb->ppu.oam_scanline.visible_sprite_indices, 0, GB_MAX_OAM_SPRITES * sizeof(uint8_t));
  gb->ppu.oam_scanline.visible_sprite_count = 0;
  memset(gb->ppu.oam_scanline.active_sprite_indices, 0, GB_MAX_OAM_SPRITES_PER_LINE * sizeof(uint8_t));
  gb->ppu.oam_scanline.active_sprite_count = 0;

  // Pixel fetcher
  gb->ppu.pixel_fetcher.step = GB_PPU_PIXEL_FETCHER_STEP_TILE;
  gb->ppu.pixel_fetcher.fetch_x = 0;
  gb->ppu.pixel_fetcher.x = 0;

  // BG FIFO
  memset(gb->ppu.bg_fifo.pixels, 0, 8 * sizeof(uint8_t));
  gb->ppu.bg_fifo.count = 0;
}

static GB_result_t set_ppu_mode(GB_emulator_t *gb, uint8_t new_mode) {
  const uint8_t stat = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)];
  const uint8_t new_stat = (stat & ~GB_PPU_STAT_MODE) | new_mode;

  if (new_stat != stat) {
    gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] = new_stat;

    switch (new_mode) {
      case GB_PPU_MODE_HBLANK:
        if (stat & GB_PPU_STAT_HBLANK_INT_SELECT) { GB_TRY(GB_interrupt_request(gb, GB_INTERRUPT_STAT)); }
        break;
      case GB_PPU_MODE_VBLANK:
        if (stat & GB_PPU_STAT_VBLANK_INT_SELECT) { GB_TRY(GB_interrupt_request(gb, GB_INTERRUPT_STAT)); }
        break;
      case GB_PPU_MODE_OAM:
        if (stat & GB_PPU_STAT_OAM_INT_SELECT)    { GB_TRY(GB_interrupt_request(gb, GB_INTERRUPT_STAT)); }
        break;
    }
  }

  return GB_SUCCESS;
}

static GB_result_t lyc_cmp(GB_emulator_t *gb) {
  const uint8_t stat = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)];
  const uint8_t ly   = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LY)];
  const uint8_t lyc  = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LYC)];

  const bool lyc_coincidence = (ly == lyc);
  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] = (stat & ~GB_PPU_STAT_LYC_EQ_LY) | (lyc_coincidence << 2);

  if (lyc_coincidence && (stat & GB_PPU_STAT_LYC_INT_SELECT)) {
    GB_TRY(GB_interrupt_request(gb, GB_INTERRUPT_STAT));
  }

  return GB_SUCCESS;
}

static GB_result_t handle_mode_hblank(GB_emulator_t *gb) {
  if (gb->ppu.cycles >= 375) {  // 456 - 80 - 1 due current cycle
    gb->ppu.cycles = 0;

    // Next line
    gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LY)]++;
    GB_TRY(lyc_cmp(gb));

    if (gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LY)] >= GB_SCREEN_HEIGHT) {
      GB_TRY(GB_interrupt_request(gb, GB_INTERRUPT_VBLANK));
      set_ppu_mode(gb, GB_PPU_MODE_VBLANK);
    } else {
      // Reset OAM scanline buffer
      gb->ppu.oam_scanline.active_sprite_count = 0;
      gb->ppu.oam_scanline.visible_sprite_count = 0;
      set_ppu_mode(gb, GB_PPU_MODE_OAM);
    }
  } else {
    gb->ppu.cycles++;
  }

  return GB_SUCCESS;
}

static GB_result_t handle_mode_vblank(GB_emulator_t *gb) {
  if (gb->ppu.cycles >= 455) {
    gb->ppu.cycles = 0;

    // Next line
    uint8_t new_ly = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LY)] + 1;
    if (new_ly >= (GB_SCREEN_HEIGHT + 10)) { new_ly = 0; }
    gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LY)] = new_ly;
    GB_TRY(lyc_cmp(gb));

    if (new_ly == 0) {
      // Reset OAM scanline buffer
      gb->ppu.oam_scanline.active_sprite_count = 0;
      gb->ppu.oam_scanline.visible_sprite_count = 0;
      gb->ppu.pixel_fetcher.window_line = 0;
      set_ppu_mode(gb, GB_PPU_MODE_OAM);
    }
  } else {
    gb->ppu.cycles++;
  }

  return GB_SUCCESS;
}

static GB_result_t handle_mode_oam(GB_emulator_t *gb) {
  const uint8_t lcdc = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LCDC)]; 
  if (lcdc & GB_PPU_LCDC_OBJ_ENABLE) {
    const uint8_t ly = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LY)];
    const uint8_t sprite_height = 8 << ((lcdc & GB_PPU_LCDC_OBJ_SIZE) != 0);
    if (gb->ppu.cycles < GB_MAX_OAM_SPRITES) {
      // Step 1 - trying to find all visible sprites on current processing line
      GB_oam_sprite_t *sprites = (GB_oam_sprite_t *)(gb->memory.oam);

      const int16_t sprite_y = sprites[gb->ppu.cycles].y - 16;
      if (ly >= sprite_y && ly < (sprite_y + sprite_height)) {
        gb->ppu.oam_scanline.visible_sprite_indices[gb->ppu.oam_scanline.visible_sprite_count++] = gb->ppu.cycles;
      }
    } else {
      // Step 2 - select only GB_MAX_OAM_SPRITES_PER_LINE sprites which has lowest X coordinate
      uint8_t visible_sprite_index = gb->ppu.cycles - GB_MAX_OAM_SPRITES;
      if (visible_sprite_index < gb->ppu.oam_scanline.visible_sprite_count) {
        GB_oam_sprite_t *visible_sprite = (GB_oam_sprite_t *)(gb->memory.oam + gb->ppu.oam_scanline.visible_sprite_indices[visible_sprite_index] * sizeof(GB_oam_sprite_t));
        uint8_t insert_position = gb->ppu.oam_scanline.active_sprite_count;
        for (uint8_t i = 0; i < gb->ppu.oam_scanline.active_sprite_count; ++i) {
          GB_oam_sprite_t *active_sprite = (GB_oam_sprite_t *)(gb->memory.oam + gb->ppu.oam_scanline.active_sprite_indices[i] * sizeof(GB_oam_sprite_t));
          if (visible_sprite->x < active_sprite->x) {
            insert_position = i;
            break;
          }
        }

        if (insert_position < GB_MAX_OAM_SPRITES_PER_LINE) {
          const uint8_t last_active_sprite_index = gb->ppu.oam_scanline.active_sprite_count < GB_MAX_OAM_SPRITES_PER_LINE ? gb->ppu.oam_scanline.active_sprite_count
                                                                                                                          : GB_MAX_OAM_SPRITES_PER_LINE - 1;
          for (uint8_t i = last_active_sprite_index; i > insert_position; i--) {
            gb->ppu.oam_scanline.active_sprite_indices[i] = gb->ppu.oam_scanline.active_sprite_indices[i - 1];
          }
          gb->ppu.oam_scanline.active_sprite_indices[insert_position] = gb->ppu.oam_scanline.visible_sprite_indices[visible_sprite_index];
          if (gb->ppu.oam_scanline.active_sprite_count < GB_MAX_OAM_SPRITES_PER_LINE) {
            gb->ppu.oam_scanline.active_sprite_count++;
          }
        }
      }
    }
  }

  if (gb->ppu.cycles >= 79) {
    gb->ppu.cycles = 0;

    // Reset fetcher
    gb->ppu.pixel_fetcher.step = GB_PPU_PIXEL_FETCHER_STEP_TILE;
    gb->ppu.pixel_fetcher.next_step_cycle = 0;
    gb->ppu.pixel_fetcher.fetch_x = 0;
    gb->ppu.pixel_fetcher.x = 0;
    gb->ppu.pixel_fetcher.scy = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_SCY)];
    gb->ppu.pixel_fetcher.scx = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_SCX)];
    gb->ppu.pixel_fetcher.tile_addr_mode  = false;
    gb->ppu.pixel_fetcher.tile_index = 0;
    gb->ppu.pixel_fetcher.tile_low = 0;
    gb->ppu.pixel_fetcher.tile_high = 0;
    gb->ppu.pixel_fetcher.wy = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_WY)];
    gb->ppu.pixel_fetcher.wx = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_WX)];
    gb->ppu.pixel_fetcher.window_entered = false;

    // Reset BG FIFO
    gb->ppu.bg_fifo.count = 0;

    set_ppu_mode(gb, GB_PPU_MODE_DRAWING);
  } else {
    gb->ppu.cycles++;
  }

  return GB_SUCCESS;
}

static GB_result_t handle_mode_drawing(GB_emulator_t *gb) {
  const uint8_t lcdc = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LCDC)];
  const uint8_t ly = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LY)];
  const uint8_t bgp = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_BGP)];

  if (lcdc & GB_PPU_LCDC_WINDOW_ENABLE) {
    const uint8_t wx_position = gb->ppu.pixel_fetcher.wx < 7 ? 0 : (gb->ppu.pixel_fetcher.wx - 7);
    if (!gb->ppu.pixel_fetcher.window_entered &&
        ly >= gb->ppu.pixel_fetcher.wy &&
        gb->ppu.pixel_fetcher.x == wx_position) {
      gb->ppu.pixel_fetcher.step = GB_PPU_PIXEL_FETCHER_STEP_TILE;
      gb->ppu.pixel_fetcher.next_step_cycle = gb->ppu.cycles;
      gb->ppu.pixel_fetcher.fetch_x = 0;
      gb->ppu.pixel_fetcher.window_entered = true;
      gb->ppu.bg_fifo.count = 0;
      if (ly > gb->ppu.pixel_fetcher.wy) { gb->ppu.pixel_fetcher.window_line++; }
    }
  } else {
    gb->ppu.pixel_fetcher.window_entered = false;
  }

  if (gb->ppu.cycles >= gb->ppu.pixel_fetcher.next_step_cycle) {
    switch (gb->ppu.pixel_fetcher.step) {
      case GB_PPU_PIXEL_FETCHER_STEP_TILE:
        if (lcdc & GB_PPU_LCDC_BG_WINDOW_ENABLE) {
          uint16_t base_addr;
          uint8_t tile_x, tile_y;
          gb->ppu.pixel_fetcher.tile_addr_mode = lcdc & GB_PPU_LCDC_BG_WINDOW_TILES;
          if (gb->ppu.pixel_fetcher.window_entered) {
            base_addr = (lcdc & GB_PPU_LCDC_WINDOW_TILE_MAP) ? 0x9C00 : 0x9800;
            tile_x = gb->ppu.pixel_fetcher.fetch_x;
            tile_y = gb->ppu.pixel_fetcher.window_line;
          } else {
            const uint8_t scx = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_SCX)];
            gb->ppu.pixel_fetcher.scx = (scx & 0xF8) | (gb->ppu.pixel_fetcher.scx & 0x07);
            gb->ppu.pixel_fetcher.scy = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_SCY)];
            base_addr = (lcdc & GB_PPU_LCDC_BG_TILE_MAP) ? 0x9C00 : 0x9800;
            tile_x = (gb->ppu.pixel_fetcher.fetch_x + gb->ppu.pixel_fetcher.scx) % 256;
            tile_y = (ly + gb->ppu.pixel_fetcher.scy) % 256;
          }
          const uint16_t tile_addr = base_addr + (tile_y / 8) * 32 + (tile_x / 8);
          gb->ppu.pixel_fetcher.tile_index = gb->memory.vram[GB_MEMORY_VRAM_OFFSET(tile_addr)];
          gb->ppu.pixel_fetcher.step = GB_PPU_PIXEL_FETCHER_STEP_DATA_LOW;
        } else {
          const uint8_t bg_color = (bgp >> 0) & 0x03;
          memset(gb->ppu.bg_fifo.pixels, bg_color, 8);
          gb->ppu.bg_fifo.count = 8;
          gb->ppu.pixel_fetcher.step = GB_PPU_PIXEL_FETCHER_STEP_PUSH;
        }
        gb->ppu.pixel_fetcher.next_step_cycle += 2;
        break;
      case GB_PPU_PIXEL_FETCHER_STEP_DATA_LOW:
      case GB_PPU_PIXEL_FETCHER_STEP_DATA_HIGH:
        {
          uint16_t tile_addr;
          if (gb->ppu.pixel_fetcher.tile_addr_mode) {
            tile_addr = 0x8000 + gb->ppu.pixel_fetcher.tile_index * 16;
          } else {
            int8_t signed_tile_index = (int8_t)gb->ppu.pixel_fetcher.tile_index;
            if (signed_tile_index >= 0) {
              tile_addr = 0x9000 + signed_tile_index * 16;
            } else {
              tile_addr = 0x8800 + (signed_tile_index + 128) * 16;
            }
          }
          const uint8_t tile_line = gb->ppu.pixel_fetcher.window_entered ? gb->ppu.pixel_fetcher.window_line
                                                                         : (ly + gb->ppu.pixel_fetcher.scy);
          tile_addr += (tile_line % 8) * 2;
          if (gb->ppu.pixel_fetcher.step == GB_PPU_PIXEL_FETCHER_STEP_DATA_LOW) {
            gb->ppu.pixel_fetcher.tile_low = gb->memory.vram[GB_MEMORY_VRAM_OFFSET(tile_addr)];
            gb->ppu.pixel_fetcher.step = GB_PPU_PIXEL_FETCHER_STEP_DATA_HIGH;
          } else {
            gb->ppu.pixel_fetcher.tile_high = gb->memory.vram[GB_MEMORY_VRAM_OFFSET(tile_addr + 1)];
            gb->ppu.pixel_fetcher.step = GB_PPU_PIXEL_FETCHER_STEP_SLEEP;
          }
        }
        gb->ppu.pixel_fetcher.next_step_cycle += 2;
        break;
      case GB_PPU_PIXEL_FETCHER_STEP_SLEEP:
        gb->ppu.pixel_fetcher.step = GB_PPU_PIXEL_FETCHER_STEP_PUSH;
        gb->ppu.pixel_fetcher.next_step_cycle += 2;
        break;
      case GB_PPU_PIXEL_FETCHER_STEP_PUSH:
        if (gb->ppu.bg_fifo.count == 0) {
          for (uint8_t i = 0; i < 8; ++i) {
            const uint8_t bit = 7 - i;
            const uint8_t pixel = (((gb->ppu.pixel_fetcher.tile_high >> bit) & 0x01) << 1) | ((gb->ppu.pixel_fetcher.tile_low >> bit) & 0x01);
            gb->ppu.bg_fifo.pixels[gb->ppu.bg_fifo.count++] = pixel;
          }
        }
        gb->ppu.pixel_fetcher.fetch_x += 8;
        gb->ppu.pixel_fetcher.step = GB_PPU_PIXEL_FETCHER_STEP_TILE;
        gb->ppu.pixel_fetcher.next_step_cycle += 2;
        break;
    }
  }

  if (gb->ppu.pixel_fetcher.x == 0 && !gb->ppu.pixel_fetcher.window_entered) {
    const uint8_t scx_low = gb->ppu.pixel_fetcher.scx & 0x07;
    if (gb->ppu.bg_fifo.count >= scx_low) {
      memmove(gb->ppu.bg_fifo.pixels,
              gb->ppu.bg_fifo.pixels + scx_low,
              gb->ppu.bg_fifo.count  - scx_low);
      gb->ppu.bg_fifo.count -= scx_low;
    }
  }

  if (gb->ppu.bg_fifo.count > 0) {
    const uint8_t bg_color_index = gb->ppu.bg_fifo.pixels[0];
    const uint8_t bg_color = (bgp >> (bg_color_index * 2)) & 0x03;
    uint8_t final_color = bg_color;

    // Sprites enabled
    if (lcdc & GB_PPU_LCDC_OBJ_ENABLE) {
      const uint8_t obp0 = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_OBP0)];
      const uint8_t obp1 = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_OBP1)];
      const uint8_t sprite_height = 8 << ((lcdc & GB_PPU_LCDC_OBJ_SIZE) != 0);
      const uint8_t tile_mask = (sprite_height == 16) ? 0xFE : 0xFF;
      for (int8_t i = 0; i < gb->ppu.oam_scanline.active_sprite_count; i++) {
        GB_oam_sprite_t *sprite = (GB_oam_sprite_t *)&gb->memory.oam[gb->ppu.oam_scanline.active_sprite_indices[i] * sizeof(GB_oam_sprite_t)];
        const int16_t sprite_x = sprite->x - 8;
        const int16_t sprite_y = sprite->y - 16;

        if (sprite_y < 0 || sprite_y >= GB_SCREEN_HEIGHT) { continue; }
        if (gb->ppu.pixel_fetcher.x < sprite_x || gb->ppu.pixel_fetcher.x > (sprite_x + 8)) { continue; }

        int8_t rel_y = ly - (sprite->y - 16);
        if (rel_y < 0 || rel_y >= sprite_height) { continue; }
        if (sprite->flags & GB_PPU_OAM_FLAG_Y_FLIP) { rel_y = sprite_height - 1 - rel_y; }

        uint8_t tile = sprite->tile_index & tile_mask;
        const uint16_t addr = tile * 16 + rel_y * 2;
        const uint8_t low = gb->memory.vram[addr];
        const uint8_t high = gb->memory.vram[addr + 1];
        const uint8_t bit = (sprite->flags & GB_PPU_OAM_FLAG_X_FLIP) ? (gb->ppu.pixel_fetcher.x - sprite_x)
                                                                     : (7 - (gb->ppu.pixel_fetcher.x - sprite_x));
        const uint8_t pixel = ((high >> bit) & 1) << 1 | ((low >> bit) & 1);
        if (pixel) {
          if (!(sprite->flags & GB_PPU_OAM_FLAG_PRIORITY) || bg_color_index == 0) {
            const uint8_t palette = (sprite->flags & GB_PPU_OAM_FLAG_PALLETE) ? obp1 : obp0;
            final_color = (palette >> (pixel * 2)) & 0x03;
            break;
          }
        }
      }
    }

    gb->ppu.framebuffer[ly * GB_SCREEN_WIDTH + gb->ppu.pixel_fetcher.x] = final_color;
    memmove(&gb->ppu.bg_fifo.pixels[0], &gb->ppu.bg_fifo.pixels[1], gb->ppu.bg_fifo.count - 1);
    gb->ppu.bg_fifo.count--;

    gb->ppu.pixel_fetcher.x++;
  }

  gb->ppu.cycles++;

  if (gb->ppu.pixel_fetcher.x >= GB_SCREEN_WIDTH) {
    set_ppu_mode(gb, GB_PPU_MODE_HBLANK);
  }

  return GB_SUCCESS;
}

GB_result_t GB_ppu_init(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  reset(gb);
  set_ppu_mode(gb, GB_PPU_MODE_OAM);

  return GB_SUCCESS;
}

GB_result_t GB_ppu_free(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  reset(gb);

  return GB_SUCCESS;
}

GB_result_t GB_ppu_tick(GB_emulator_t *gb) {
  if (!gb)             { return GB_ERROR_INVALID_EMULATOR; }
  if (!gb->memory.io   ||
      !gb->memory.vram ||
      !gb->memory.oam) { return GB_ERROR_INVALID_ARGUMENT; }

  // LCDC enabled?
  const uint8_t lcdc = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LCDC)];
  if (!(lcdc & GB_PPU_LCDC_ENABLE)) { return GB_SUCCESS; }

  // Process current mode
  const uint8_t stat = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)];
  const uint8_t mode = stat & GB_PPU_STAT_MODE;
  switch (mode) {
    case GB_PPU_MODE_HBLANK:
      GB_TRY(handle_mode_hblank(gb));
      break;
    case GB_PPU_MODE_VBLANK:
      GB_TRY(handle_mode_vblank(gb));
      break;
    case GB_PPU_MODE_OAM:
      GB_TRY(handle_mode_oam(gb));
      break;
    case GB_PPU_MODE_DRAWING:
      GB_TRY(handle_mode_drawing(gb));
      break;
  }

  return GB_SUCCESS;
}

