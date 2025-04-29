#pragma once

#include "defs.h"

;
#pragma pack(push, 1)

typedef struct {
  /* $0100:$0103 */ uint8_t entry_point[4];        // Initial jump instruction
  /* $0104:$0133 */ uint8_t nintendo_logo[0x30];   // Nintendo logo (must match exactly)
  /* $0134:$013E */ uint8_t title[11];             // Game title in ASCII (zero-padded)
  /* $013F:$0142 */ uint8_t manufacturer_code[4];  // Publisher code
  /* $0143:$0143 */ uint8_t cgb_flag;              // Color Game Boy support
  /* $0144:$0145 */ uint8_t new_licensee_code[2];  // New licensee code (ASCII)
  /* $0146:$0146 */ uint8_t sgb_flag;              // Super Game Boy support
  /* $0147:$0147 */ uint8_t cartridge_type;        // Cartridge hardware type
  /* $0148:$0148 */ uint8_t rom_size;              // ROM size indicator
  /* $0149:$0149 */ uint8_t ram_size;              // RAM size indicator
  /* $014A:$014A */ uint8_t destination_code;      // Regional lockout
  /* $014B:$014B */ uint8_t old_licensee;          // Legacy licensee code
  /* $014C:$014C */ uint8_t rom_version;           // Mask ROM version
  /* $014D:$014D */ uint8_t header_checksum;       // Header checksum
  /* $014E:$014F */ uint8_t global_checksum[2];    // Entire ROM checksum (unused)
} GB_rom_header_t;

#pragma pack(pop)

typedef struct {
  uint16_t rom_bank;     // Active ROM bank number
  uint8_t  ram_bank;     // Active RAM bank number (if supported)
  uint8_t  mode;         // Operational mode (used by MBC1 to switch between ROM and RAM)
  bool     ram_enabled;  // Flag to enable/disable access to the RAM
} GB_mbc_t;

typedef struct {
  /* $0000:$0100 */ uint8_t  *boot_rom;
  /* $0000:$3FFF */ uint8_t  *rom_0;
  /* $4000:$7FFF */ uint8_t  *rom_x[512];
  /* $8000:$9FFF */ uint8_t  *vram;
  /* $A000:$BFFF */ uint8_t  *external_ram[16];
  /* $C000:$CFFF */ uint8_t  *wram;
  /* $E000:$FDFF */ uint8_t  *echo_ram;
  /* $FE00:$FE9F */ uint8_t  *oam;
  /* $FF00:$FF7F */ uint8_t  *io;
  /* $FF80:$FFFE */ uint8_t  *hram;
  /* $FFFF:$FFFF */ uint8_t   ie;
                    GB_mbc_t  mbc;
} GB_memory_t;

GB_result_t GB_memory_init(GB_emulator_t *gb);
GB_result_t GB_memory_free(GB_emulator_t *gb);
GB_result_t GB_memory_read_rom_header(GB_emulator_t *gb, GB_rom_header_t *header);

