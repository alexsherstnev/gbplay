#include "gb.h"
#include <stdarg.h>

GB_result_t GB_emulator_init(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  GB_TRY(GB_memory_init(gb));
  GB_TRY(GB_cpu_init(gb));
  GB_TRY(GB_ppu_init(gb));
  GB_TRY(GB_timer_init(gb));

//  // Test CPU
//  gb->memory.rom_0 = malloc(0x2000);
//  gb->memory.rom_0[0] = 0x00;
//  gb->memory.rom_0[1] = 0x01;
//  gb->memory.rom_0[2] = 0x0B;
//  gb->memory.rom_0[3] = 0x0A;
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_BOOT)] = 0x01;
//
//  // Test PPU
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LCDC)] |= GB_PPU_LCDC_ENABLE;
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LCDC)] |= GB_PPU_LCDC_BG_WINDOW_TILES;
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LCDC)] |= GB_PPU_LCDC_BG_WINDOW_ENABLE;
//
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_BGP)] = 0xE4;
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_OBP0)] = 0x1B;
//
//  for (int i = 0; i < 16; i++) { gb->memory.vram[i] = 0xFF;      } // Tile 0 black
//  for (int i = 0; i < 16; i++) { gb->memory.vram[16 + i] = 0x00; } // Tile 1 white
//  for (int i = 0; i < 16; i++) { gb->memory.vram[32 + i] = i < 8 ? 0x00 : 0xFF; } // Tile 2 lines
//
//  const uint8_t cross_tile[16] = {
//    0x81, 0x00, // 10000001
//    0x42, 0x00, // 01000010
//    0x24, 0x00, // 00100100
//    0x18, 0x00, // 00011000
//    0x18, 0x00, // 00011000
//    0x24, 0x00, // 00100100
//    0x42, 0x00, // 01000010
//    0x81, 0x00  // 10000001
//  };
//  memcpy(gb->memory.vram + 48, cross_tile, 16);  // Tile 3 cross
//
//  for (int y = 0; y < 32; y++) {
//    for (int x = 0; x < 32; x++) {
//      gb->memory.vram[0x1800 + y * 32 + x] = ((x + y) % 2);
//    }
//  }
//
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_WX)] = 7 + 20;
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_WY)] = 48;
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LCDC)] |= (1 << 5);
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LCDC)] |= (1 << 6);
//
//  for (int y = 0; y < 32; y++) {
//    for (int x = 0; x < 32; x++) {
//      gb->memory.vram[0x1C00 + y * 32 + x] = 2;
//    }
//  }
//
//  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LCDC)] |= (1 << 1);
//
//  GB_oam_sprite_t *sprite = (GB_oam_sprite_t *)&gb->memory.oam[0];
//  sprite->x = 84;
//  sprite->y = 84;
//  sprite->tile_index = 3;
//  sprite->flags = 0;
//
//  sprite = (GB_oam_sprite_t *)&gb->memory.oam[4];
//  sprite->x = 24;
//  sprite->y = 20;
//  sprite->tile_index = 3;
//  sprite->flags = 0;

  return GB_SUCCESS;
}

GB_result_t GB_emulator_free(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  // Currently don't care about result status code of each free method
  GB_timer_free(gb);
  GB_ppu_free(gb);
  GB_cpu_free(gb);
  GB_memory_free(gb);

  return GB_SUCCESS;
}

GB_result_t GB_emulator_tick(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  GB_TRY(GB_cpu_tick(gb));
  GB_TRY(GB_ppu_tick(gb));
  GB_TRY(GB_timer_tick(gb));

  return GB_SUCCESS;
}

GB_result_t GB_emulator_load_rom(GB_emulator_t *gb, const char *path) {
  if (!gb)   { return GB_ERROR_INVALID_EMULATOR; }
  if (!path) { return GB_ERROR_INVALID_ARGUMENT; }

  // Open ROM file
  FILE *file = fopen(path, "rb");
  if (!file) { return GB_ERROR_IO; }

  // Calculate ROM size
  fseek(file, 0, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (file_size < (sizeof(GB_rom_header_t) + 0x0100)) {
    fclose(file);
    return GB_ERROR_IO;
  }

  // Allocate temp ROM buffer
  uint8_t *rom_data = malloc(file_size);
  if (!rom_data) {
    fclose(file);
    return GB_ERROR_OUT_OF_MEMORY;
  }

  // Read the ROM data from the file
  if (fread(rom_data, 1, file_size, file) != file_size) {
    free(rom_data);
    fclose(file);
    return GB_ERROR_IO;
  }
  fclose(file);
  
  // Load ROM bank 0
  gb->memory.rom_0 = malloc(0x4000);
  if (!gb->memory.rom_0) {
    free(rom_data);
    return GB_ERROR_OUT_OF_MEMORY;
  }
  memcpy(gb->memory.rom_0, rom_data, 0x4000);

  // Read ROM header
  GB_rom_header_t rom_header;
  if (GB_FAILED(GB_memory_read_rom_header(gb, &rom_header))) {
    free(rom_data);
    return GB_ERROR_IO;
  }

  // Determine the number of ROM banks based on the header
  size_t rom_bank_count = 2;  // Default to 2 banks (32KB)
  if (rom_header.rom_size <= 0x08) {
    rom_bank_count = 2 << rom_header.rom_size;
  } else {
    free(rom_data);
    return GB_ERROR_IO;
  }

  // Load switchable ROM banks
  for (size_t rom_bank = 1; rom_bank < rom_bank_count; rom_bank++) {
    const size_t offset = rom_bank * 0x4000;
    if ((offset + 0x4000) > file_size) {
      break;
    }
    gb->memory.rom_x[rom_bank - 1] = malloc(0x4000);
    memcpy(gb->memory.rom_x[rom_bank - 1], rom_data + offset, 0x4000);
  }

  size_t ram_bank_count = 0;
  switch (rom_header.ram_size) {
    case 0x00: ram_bank_count = 0; break;
    case 0x01: ram_bank_count = 1; break;
    case 0x02: ram_bank_count = 1; break;
    case 0x03: ram_bank_count = 4; break;
    case 0x04: ram_bank_count = 16; break;
    case 0x05: ram_bank_count = 8; break;
    default: 
      return GB_ERROR_IO;
  }

  for (size_t i = 0; i < ram_bank_count; i++) {
    gb->memory.external_ram[i] = malloc(0x2000);
    if (!gb->memory.external_ram[i]) {
      for (size_t j = 0; j < i; j++) free(gb->memory.external_ram[j]);
      return GB_ERROR_OUT_OF_MEMORY;
    }
    memset(gb->memory.external_ram[i], 0, 0x2000);
  }

  // Init MBC
  gb->memory.mbc.rom_bank = 1;
  gb->memory.mbc.ram_bank = 0;
  gb->memory.mbc.mode = 0;
  gb->memory.mbc.ram_enabled = false;

  free(rom_data);

//  gb->cpu.reg.a = 0x01;
//  gb->cpu.reg.carry = 1;
//  gb->cpu.reg.half_carry = 1;
//  gb->cpu.reg.subtract = 0;
//  gb->cpu.reg.zero = 1;
//  gb->cpu.reg.b = 0x00;
//  gb->cpu.reg.c = 0x13;
//  gb->cpu.reg.d = 0x00;
//  gb->cpu.reg.e = 0xD8;
//  gb->cpu.reg.h = 0x01;
//  gb->cpu.reg.l = 0x4D;
//  gb->cpu.reg.sp = 0xFFFE;
//  gb->cpu.reg.pc = 0x0100;
//  gb->memory.io[0x50] = 0x01;
//  gb->memory.io[0x40] = 0x91;
//  gb->memory.io[0x47] = 0xE4;

  return GB_SUCCESS;
}

GB_error_t GB_emulator_get_last_error(GB_emulator_t *gb) {
  // Copy error
  const GB_error_t error = gb->last_error;

  // Clear error
  gb->last_error.code = GB_SUCCESS;
  gb->last_error.file = NULL;
  gb->last_error.line = 0;

  return error;
}

void GB_emulator_set_error(GB_emulator_t *gb, GB_result_t code, const char* file, uint32_t line, const char *fmt, ...) {
  gb->last_error.code = code;
  gb->last_error.file = file;
  gb->last_error.line = line;

  // Format error message
  va_list args;
  va_start(args, fmt);
  vsnprintf(gb->last_error.message, GB_ERROR_MESSAGE_MAX_LENGTH, fmt, args);
  va_end(args);
}

