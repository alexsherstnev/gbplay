#include "cpu.h"
#include "gb.h"  // IWYU pragma: keep

typedef uint8_t opcode_t(GB_emulator_t *gb);

static const uint16_t INTERRUPT_VECTORS[] = {
  0x0040,  // VBLANK
  0x0048,  // LCD STAT
  0x0050,  // TIMER
  0x0058,  // SERIAL
  0x0060   // JOYPAD
};
static opcode_t *main_instruction_set[256];
static char *main_instruction_set_dissasembly[256];
static opcode_t *cb_instruction_set[256];

static void memory_read(GB_emulator_t *gb, uint16_t addr, uint8_t *value) {
  // Real memory return 0xFF or 0x00 if not accessible
  *value = 0xFF;

  // Handle Boot ROM
  if (addr < 0x0100 &&
      gb->memory.io &&
      gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_BOOT)] == 0x00) {
    *value = gb->memory.boot_rom[addr];
    return;
  }

  // Handle ROM 0
  if (addr < 0x4000) {
    if (!gb->memory.rom_0) { return; }
    *value = gb->memory.rom_0[addr];
    return;
  }

  // Handle ROM switchable banks
  if (addr < 0x8000) {
    if (gb->memory.mbc.rom_bank == 0 ||
        !gb->memory.rom_x[gb->memory.mbc.rom_bank - 1]) { return; }
    *value = gb->memory.rom_x[gb->memory.mbc.rom_bank - 1][addr - 0x4000];
    return;
  }

  // Handle VRAM
  if (addr < 0xA000) {
    const uint8_t mode = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] & 0x03;
    if (mode != GB_PPU_MODE_DRAWING) {
      *value = gb->memory.vram[GB_MEMORY_VRAM_OFFSET(addr)];
    }
    return;
  }

  // Handle external RAM
  if (addr < 0xC000) {
    if (!gb->memory.mbc.ram_enabled ||
        !gb->memory.external_ram[gb->memory.mbc.ram_bank]) { return; }
    *value = gb->memory.external_ram[gb->memory.mbc.ram_bank][addr - 0xA000];
    return;
  }

  // Handle WRAM
  if (addr < 0xE000) {
    *value = gb->memory.wram[GB_MEMORY_WRAM_OFFSET(addr)];
    return;
  }

  // Handle echo RAM (in this case mirror of WRAM)
  if (addr < 0xFE00) {
    *value = gb->memory.echo_ram[GB_MEMORY_ECHO_OFFSET(addr)];
    return;
  }

  // Handle OAM
  if (addr < 0xFEA0) {
    const uint8_t mode = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] & 0x03;
    if (mode != GB_PPU_MODE_OAM && mode != GB_PPU_MODE_DRAWING) {
      *value = gb->memory.oam[GB_MEMORY_OAM_OFFSET(addr)];
    }
    return;
  }

  // Handle unsued area
  if (addr < 0xFEFF) {
    *value = 0x00;  // Note: This doesn't return FFh
    return;
  }

  // Handle I/O registers
  if (addr < 0xFF80) {
    switch (addr) {
      case GB_HARDWARE_REGISTER_DIV:  *value = gb->timer.div_counter >> 8; break;
      case GB_HARDWARE_REGISTER_KEY1: *value = 0xFF; break;
      default:                        *value = gb->memory.io[GB_MEMORY_IO_OFFSET(addr)]; break;
    }
    return;
  }

  // Handle HRAM
  if (addr < 0xFFFF) {
    *value = gb->memory.hram[GB_MEMORY_HRAM_OFFSET(addr)];
    return;
  }

  // Handle interrupt enable register
  if (addr == 0xFFFF) {
    *value = gb->memory.ie;
    return;
  }
}

static void memory_write(GB_emulator_t *gb, uint16_t addr, uint8_t value) {
  if (addr < 0x8000) {
    if (addr < 0x2000) {
      // RAM enable/disable
      gb->memory.mbc.ram_enabled = (value & 0x0F) == 0x0A;
    } else if (addr < 0x4000) {
      // ROM bank lower 5 bits
      uint8_t bank = value & 0x1F;
      if (bank == 0) { bank = 1; }  // Bank 0 cannot be selected
      gb->memory.mbc.rom_bank = (gb->memory.mbc.rom_bank & 0x60) | bank;
    } else if (addr < 0x6000) {
      // RAM bank number or upper ROM bank bits
      if (gb->memory.mbc.mode == 0) {
        // Upper bits of ROM bank (MBC1)
        gb->memory.mbc.rom_bank = (gb->memory.mbc.rom_bank & 0x1F) | ((value & 0x03) << 5);
      } else {
        // RAM bank number
        gb->memory.mbc.ram_bank = value & 0x03;
      }
    } else if (addr < 0x8000) {
      // Mode select
      gb->memory.mbc.mode = value & 0x01;
      if (gb->memory.mbc.mode == 1) {
        gb->memory.mbc.rom_bank &= 0x1F;
      }
    }
    return;
  }

  // Handle VRAM
  if (addr < 0xA000) {
    const uint8_t mode = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] & 0x03;
    if (mode != GB_PPU_MODE_DRAWING) {
      gb->memory.vram[GB_MEMORY_VRAM_OFFSET(addr)] = value;
    }
    return;
  }

  // Handle external RAM
  if (addr < 0xC000) {
    if (!gb->memory.external_ram[0]) { return; }
    if (gb->memory.mbc.ram_enabled) {
      const uint8_t ram_bank = gb->memory.mbc.mode == 1 ? gb->memory.mbc.ram_bank : 0;
      if (ram_bank >= 16 || !gb->memory.external_ram[ram_bank]) { return; }

      uint8_t *ram_ptr = gb->memory.external_ram[ram_bank];
      if (!ram_ptr) { return; }
      ram_ptr[addr - 0xA000] = value;
    }
    return;
  }

  // Handle WRAM
  if (addr < 0xE000) {
    gb->memory.wram[GB_MEMORY_WRAM_OFFSET(addr)] = value;
    return;
  }

  // Handle echo RAM (in this case mirror of WRAM)
  if (addr < 0xFE00) {
    gb->memory.echo_ram[GB_MEMORY_ECHO_OFFSET(addr)] = value;
    return;
  }

  // Handle OAM
  if (addr < 0xFEA0) {
    const uint8_t mode = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] & 0x03;
    if (mode == GB_PPU_MODE_HBLANK || mode == GB_PPU_MODE_VBLANK) {
      gb->memory.oam[GB_MEMORY_OAM_OFFSET(addr)] = value;
    }
    return;
  }

  // Handle unsued area
  if (addr < 0xFEFF) {
    return;
  }

  // Handle I/O registers
  if (addr < 0xFF80) {
    if (addr == GB_HARDWARE_REGISTER_DIV) {
//      const uint16_t old_div_counter = gb->timer.div_counter;
      gb->timer.div_counter = 0;
      //GB_timer_glitch(gb, old_div_counter);
      return;
    } else if (addr == GB_HARDWARE_REGISTER_SC) {
      if (value & 0x80) {
        gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_IF)] &= ~(1 << 3);
      }
    } else if (addr == GB_HARDWARE_REGISTER_BOOT) {
      gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_IE)] = 0x01;
      gb->cpu.reg.ie = 1;
    } else if (addr == GB_HARDWARE_REGISTER_DMA) {
      const uint16_t src = value << 8;
      for (uint16_t i = 0; i < 0xA0; i++) {
        uint8_t byte;
        memory_read(gb, src + i, &byte);
        gb->memory.oam[i] = byte;
      }
    } else if (addr == GB_HARDWARE_REGISTER_LCDC) {
      uint8_t stat = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)];
      if (!(value & 0x80)) {
        gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LY)] = 0;
        gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] = (stat & ~0x03) | 0x00;
      }
    }
    gb->memory.io[GB_MEMORY_IO_OFFSET(addr)] = value;
    return;
  }

  // Handle HRAM
  if (addr < 0xFFFF) {
    gb->memory.hram[GB_MEMORY_HRAM_OFFSET(addr)] = value;
    return;
  }

  // Handle interrupt enable register
  if (addr == 0xFFFF) {
    gb->memory.ie = value;
    return;
  }
}

static inline uint8_t ld_r8_r8(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r) {
  (void)gb;
  *r8_l = r8_r;

  return 4;
}

static inline uint8_t ld_r8_addr(GB_emulator_t *gb, uint8_t *r8, uint16_t addr) {
  memory_read(gb, addr, r8);

  return 8;
}

static inline uint8_t ld_addr_r8(GB_emulator_t *gb, uint16_t addr, uint8_t r8) {
  memory_write(gb, addr, r8);

  return 8;
}

static inline uint8_t ld_r8_addr_a16(GB_emulator_t *gb, uint8_t *r8) {
  uint8_t l, h;
  ld_r8_addr(gb, &l, gb->cpu.reg.pc++);
  ld_r8_addr(gb, &h, gb->cpu.reg.pc++);
  ld_r8_addr(gb, r8, (h << 8) | l);

  return 16;
}

static inline uint8_t ld_addr_a16_r8(GB_emulator_t *gb, uint8_t r8) {
  uint8_t l, h;
  ld_r8_addr(gb, &l, gb->cpu.reg.pc++);
  ld_r8_addr(gb, &h, gb->cpu.reg.pc++);
  ld_addr_r8(gb, (h << 8) | l, r8);

  return 16;
}

static inline uint8_t ld_addr_r16_n8(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, gb->cpu.reg.pc++);
  ld_addr_r8(gb, addr, value);

  return 12;
}

static inline uint8_t ld_addr_a16_r16(GB_emulator_t *gb, uint16_t r16) {
  uint8_t l, h;
  ld_r8_addr(gb, &l, gb->cpu.reg.pc++);
  ld_r8_addr(gb, &h, gb->cpu.reg.pc++);
  
  const uint16_t addr = (h << 8) | l;
  ld_addr_r8(gb, addr, (uint8_t)(r16 & 0xFF));
  ld_addr_r8(gb, addr + 1, (uint8_t)((r16 >> 8) & 0xFF));

  return 20;
}

static inline uint8_t ld_r16_n16(GB_emulator_t *gb, uint16_t *r16) {
  uint8_t l, h;
  ld_r8_addr(gb, &l, gb->cpu.reg.pc++);
  ld_r8_addr(gb, &h, gb->cpu.reg.pc++);
  *r16 = (h << 8) | l;

  return 12;
}

static inline uint8_t ld_r16_r16(GB_emulator_t *gb, uint16_t *r16_l, uint16_t r16_r) {
  (void)gb;
  *r16_l = r16_r;

  return 8;
}

static inline uint8_t ld_r16_r16_e8(GB_emulator_t *gb, uint16_t *r16_l, uint16_t r16_r) {
  int8_t e8;
  ld_r8_addr(gb, (uint8_t *)&e8, gb->cpu.reg.pc++);

  *r16_l = r16_r + e8;
  gb->cpu.reg.zero = 0;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = ((r16_r & 0x0F) + (e8 & 0x0F)) > 0x0F;
  gb->cpu.reg.carry = ((r16_r & 0xFF) + (e8 & 0xFF)) > 0xFF;

  return 12;
}

static inline uint8_t ldh_r8_addr_a8(GB_emulator_t *gb, uint8_t *r8) {
  uint8_t offset;
  ld_r8_addr(gb, &offset, gb->cpu.reg.pc++);
  ld_r8_addr(gb, r8, 0xFF00 + offset);

  return 12;
}

static inline uint8_t ldh_addr_a8_r8(GB_emulator_t *gb, uint8_t r8) {
  uint8_t offset;
  ld_r8_addr(gb, &offset, gb->cpu.reg.pc++);
  ld_addr_r8(gb, 0xFF00 + offset, r8);
  
  return 12;
}

static inline uint8_t di(GB_emulator_t *gb) {
  gb->cpu.reg.ie = false;
//  gb->cpu.ie_pending_delay = 0;

  return 4;
}

static inline uint8_t ei(GB_emulator_t *gb) {
  gb->cpu.ie_pending_delay = 2;

  return 4;
}

static inline uint8_t jr_cnd_e8(GB_emulator_t *gb, bool cnd) {
  int8_t offset;
  uint8_t cycles = 8;
  ld_r8_addr(gb, (uint8_t *)&offset, gb->cpu.reg.pc++);
  if (cnd) {
    gb->cpu.reg.pc += offset;
    cycles += 4;
  }

  return cycles;
}

static inline uint8_t jp_cnd_a16(GB_emulator_t *gb, bool cnd) {
  uint8_t l, h, cycles = 12;
  ld_r8_addr(gb, &l, gb->cpu.reg.pc++);
  ld_r8_addr(gb, &h, gb->cpu.reg.pc++);
  if (cnd) {
    gb->cpu.reg.pc = (h << 8) | l;
    cycles += 4;
  }

  return cycles;
}

static inline uint8_t jp_hl(GB_emulator_t *gb) {
  gb->cpu.reg.pc = gb->cpu.reg.hl;

  return 4;
}

static inline uint8_t push_r16(GB_emulator_t *gb, uint8_t h, uint8_t l) {
  ld_addr_r8(gb, --gb->cpu.reg.sp, h);
  ld_addr_r8(gb, --gb->cpu.reg.sp, l);

  return 16;
}

static inline uint8_t pop_r16(GB_emulator_t *gb, uint8_t *h, uint8_t *l) {
  ld_r8_addr(gb, l, gb->cpu.reg.sp++);
  ld_r8_addr(gb, h, gb->cpu.reg.sp++);

  return 12;
}

static inline uint8_t pop_af(GB_emulator_t *gb) {
  pop_r16(gb, &gb->cpu.reg.a, &gb->cpu.reg.f);
  gb->cpu.reg.f &= 0xF0;

  return 12;
}

static inline uint8_t call_cnd_a16(GB_emulator_t *gb, bool cnd) {
  uint8_t l, h, cycles = 12;
  ld_r8_addr(gb, &l, gb->cpu.reg.pc++);
  ld_r8_addr(gb, &h, gb->cpu.reg.pc++);
  if (cnd) {
    push_r16(gb, (uint8_t)((gb->cpu.reg.pc >> 8) & 0xFF), (uint8_t)(gb->cpu.reg.pc & 0xFF));
    gb->cpu.reg.pc = (h << 8) | l;
    cycles += 12;
  }

  return cycles;
}

static inline uint8_t ret_a16(GB_emulator_t *gb) {
  uint8_t l, h;
  pop_r16(gb, &h, &l);
  gb->cpu.reg.pc = (h << 8) | l;

  return 16;
}

static inline uint8_t reti_a16(GB_emulator_t *gb) {
  ei(gb);
  ret_a16(gb);

  return 16;
}

static inline uint8_t ret_cnd_a16(GB_emulator_t *gb, bool cnd) {
  uint8_t cycles = 8;
  if (cnd) {
    ret_a16(gb);
    cycles += 12;
  }

  return cycles;
}

static inline uint8_t rst_addr(GB_emulator_t *gb, uint16_t addr) {
  push_r16(gb, (uint8_t)((gb->cpu.reg.pc >> 8) & 0xFF), (uint8_t)(gb->cpu.reg.pc & 0xFF));
  gb->cpu.reg.pc = addr;

  return 16;
}

static inline uint8_t add_full(uint8_t a, uint8_t b, uint8_t *carry, uint8_t *half_carry) {
  // Real CPU use 9-bit for summation
  const uint16_t result = a + b + (*carry);
  *half_carry = ((a & 0x0F) + (b & 0x0F) + (*carry)) > 0x0F;
  *carry = (result > 0xFF);

  return (uint8_t)result;
}

static inline uint8_t sub_full(uint8_t a, uint8_t b, uint8_t *carry, uint8_t *half_carry) {
  // Real CPU use 9-bit for summation
  const uint16_t result = a - b - (*carry);
  *half_carry = ((a & 0x0F) < ((b & 0x0F) + (*carry)));
  *carry = (result > 0xFF);

  return (uint8_t)result;
}

static inline uint8_t add_r8_r8_c(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r, uint8_t *carry) {
  uint8_t half_carry = 0;
  *r8_l = add_full(*r8_l, r8_r, carry, &half_carry);
  gb->cpu.reg.carry = *carry;
  gb->cpu.reg.half_carry = half_carry;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.zero = ((*r8_l) == 0);

  return 4;
}

static inline uint8_t add_r8_r8(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r) {
  uint8_t carry = 0;

  return add_r8_r8_c(gb, r8_l, r8_r, &carry);
}

static inline uint8_t add_r8_addr(GB_emulator_t *gb, uint8_t *r8, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  add_r8_r8(gb, r8, value);

  return 8;
}

static inline uint8_t add_r16_r16(GB_emulator_t *gb, uint16_t *r16_l, uint16_t r16_r) {
  const uint32_t result = (*r16_l) + r16_r;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = (((*r16_l) & 0x0FFF) + (r16_r & 0x0FFF)) > 0x0FFF;
  gb->cpu.reg.carry = (result > 0xFFFF);
  *r16_l = (uint16_t)result;

  return 8;
}

static inline uint8_t add_sp_e8(GB_emulator_t* gb) {
  int8_t e8;
  ld_r8_addr(gb, (uint8_t*)&e8, gb->cpu.reg.pc++);

  const uint16_t sp = gb->cpu.reg.sp;
  const uint16_t result = sp + e8;
  gb->cpu.reg.zero = 0;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = ((sp & 0x0F) + (e8 & 0x0F)) > 0x0F;
  gb->cpu.reg.carry = ((sp & 0xFF) + (e8 & 0xFF)) > 0xFF;
  gb->cpu.reg.sp = result;

  return 16;
}

static inline uint8_t adc_r8_r8(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r) {
  uint8_t carry = gb->cpu.reg.carry;

  return add_r8_r8_c(gb, r8_l, r8_r, &carry);
}

static inline uint8_t adc_r8_addr(GB_emulator_t *gb, uint8_t *r8, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  adc_r8_r8(gb, r8, value);

  return 8;
}

static inline uint8_t sub_r8_r8_c(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r, uint8_t *carry) {
  uint8_t half_carry = 0;
  *r8_l = sub_full(*r8_l, r8_r, carry, &half_carry);
  gb->cpu.reg.carry = *carry;
  gb->cpu.reg.half_carry = half_carry;
  gb->cpu.reg.subtract = 1;
  gb->cpu.reg.zero = ((*r8_l) == 0);

  return 4;
}

static inline uint8_t sub_r8_r8(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r) {
  uint8_t carry = 0;

  return sub_r8_r8_c(gb, r8_l, r8_r, &carry);
}

static inline uint8_t sub_r8_addr(GB_emulator_t *gb, uint8_t *r8, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  sub_r8_r8(gb, r8, value);

  return 8;
}

static inline uint8_t sbc_r8_r8(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r) {
  uint8_t carry = gb->cpu.reg.carry;

  return sub_r8_r8_c(gb, r8_l, r8_r, &carry);
}

static inline uint8_t sbc_r8_addr(GB_emulator_t *gb, uint8_t *r8, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  sbc_r8_r8(gb, r8, value);

  return 8;
}

static inline uint8_t and_r8_r8(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r) {
  *r8_l &= r8_r;
  gb->cpu.reg.carry = 0;
  gb->cpu.reg.half_carry = 1;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.zero = ((*r8_l) == 0);

  return 4;
}

static inline uint8_t and_r8_addr(GB_emulator_t *gb, uint8_t *r8, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  and_r8_r8(gb, r8, value);

  return 8;
}

static inline uint8_t or_r8_r8(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r) {
  *r8_l |= r8_r;
  gb->cpu.reg.carry = 0;
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.zero = ((*r8_l) == 0);

  return 4;
}

static inline uint8_t or_r8_addr(GB_emulator_t *gb, uint8_t *r8, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  or_r8_r8(gb, r8, value);

  return 8;
}

static inline uint8_t xor_r8_r8(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r) {
  *r8_l ^= r8_r;
  gb->cpu.reg.carry = 0;
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.zero = ((*r8_l) == 0);

  return 4;
}

static inline uint8_t xor_r8_addr(GB_emulator_t *gb, uint8_t *r8, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  xor_r8_r8(gb, r8, value);

  return 8;
}

static inline uint8_t cp_r8_r8(GB_emulator_t *gb, uint8_t r8_l, uint8_t r8_r) {
  gb->cpu.reg.carry = ((r8_l) < (r8_r));
  gb->cpu.reg.half_carry = ((r8_l & 0x0F) < (r8_r & 0x0F));
  gb->cpu.reg.subtract = 1;
  gb->cpu.reg.zero = (r8_l == r8_r);

  return 4;
}

static inline uint8_t cp_r8_addr(GB_emulator_t *gb, uint8_t r8, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  cp_r8_r8(gb, r8, value);

  return 8;
}

static inline uint8_t inc_r8(GB_emulator_t *gb, uint8_t *r8) {
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = (((*r8) & 0x0F) + 1) > 0x0F;
  (*r8)++;
  gb->cpu.reg.zero = ((*r8) == 0);

  return 4;
}

static inline uint8_t inc_r16(GB_emulator_t *gb, uint16_t *r16) {
  (void)gb;
  (*r16)++;

  return 8;
}

static inline uint8_t inc_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  inc_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 12;
}

static inline uint8_t dec_r8(GB_emulator_t *gb, uint8_t *r8) {
  gb->cpu.reg.subtract = 1;
  gb->cpu.reg.half_carry = ((*r8 & 0x0F) == 0x00);
  (*r8)--;
  gb->cpu.reg.zero = ((*r8) == 0);

  return 4;
}

static inline uint8_t dec_r16(GB_emulator_t *gb, uint16_t *r16) {
  (void)gb;
  (*r16)--;

  return 8;
}

static inline uint8_t dec_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  dec_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 12;
}

static inline uint8_t rlca(GB_emulator_t *gb) {
  gb->cpu.reg.zero = 0;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.carry = (gb->cpu.reg.a & 0x80) != 0;
  gb->cpu.reg.a = (gb->cpu.reg.a << 1) | gb->cpu.reg.carry;

  return 4;
}

static inline uint8_t rla(GB_emulator_t *gb) {
  gb->cpu.reg.zero = 0;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = 0;
  const uint8_t temp = gb->cpu.reg.carry;
  gb->cpu.reg.carry = (gb->cpu.reg.a & 0x80) != 0;
  gb->cpu.reg.a = (gb->cpu.reg.a << 1) | temp;

  return 4;
}

static inline uint8_t cpl(GB_emulator_t *gb) {
  gb->cpu.reg.a = ~gb->cpu.reg.a;
  gb->cpu.reg.subtract = 1;
  gb->cpu.reg.half_carry = 1;

  return 4;
}

static inline uint8_t ccf(GB_emulator_t *gb) {
  gb->cpu.reg.carry = !gb->cpu.reg.carry;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = 0;

  return 4;
}

static inline uint8_t rrca(GB_emulator_t *gb) {
  const uint8_t lsb = gb->cpu.reg.a & 0x01;
  gb->cpu.reg.a = (gb->cpu.reg.a >> 1) | (lsb << 7);
  gb->cpu.reg.carry = lsb;
  gb->cpu.reg.zero = 0;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = 0;

  return 4;
}

static inline uint8_t rra(GB_emulator_t *gb) {
  uint8_t lsb = gb->cpu.reg.a & 0x01;
  uint8_t carry = gb->cpu.reg.carry ? 0x80 : 0;
  gb->cpu.reg.a = (gb->cpu.reg.a >> 1) | carry;
  gb->cpu.reg.carry = lsb;
  gb->cpu.reg.zero = 0;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = 0;

  return 4;
}

static inline uint8_t daa(GB_emulator_t *gb) {
  if (!gb->cpu.reg.subtract) {
    if (gb->cpu.reg.carry || gb->cpu.reg.a > 0x99) {
      gb->cpu.reg.a = (gb->cpu.reg.a + 0x60) & 0xFF;
      gb->cpu.reg.carry = 1;
    }
    if (gb->cpu.reg.half_carry || (gb->cpu.reg.a & 0xF) > 0x9) {
      gb->cpu.reg.a = (gb->cpu.reg.a + 0x06) & 0xFF;
      gb->cpu.reg.half_carry = 0;
    }
  } else if (gb->cpu.reg.carry && gb->cpu.reg.half_carry) {
    gb->cpu.reg.a = (gb->cpu.reg.a + 0x9A) & 0xFF;
    gb->cpu.reg.half_carry = 0;
  } else if (gb->cpu.reg.carry) {
    gb->cpu.reg.a = (gb->cpu.reg.a + 0xA0) & 0xFF;
  } else if (gb->cpu.reg.half_carry) {
    gb->cpu.reg.a = (gb->cpu.reg.a + 0xFA) & 0xFF;
    gb->cpu.reg.half_carry = 0;
  }
  gb->cpu.reg.zero = (gb->cpu.reg.a == 0);

  return 4;
}

static inline uint8_t scf(GB_emulator_t *gb) {
  gb->cpu.reg.carry = 1;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.half_carry = 0;

  return 4;
}

static inline uint8_t halt(GB_emulator_t *gb) {
  gb->cpu.halted = true;
//  gb->cpu.halt_exit_delay = false;

  return 4;
}

static inline uint8_t stop(GB_emulator_t *gb) {
  // TODO: implement
  gb->cpu.reg.pc++;
  gb->cpu.stopped = true;

  return 4;
}

static inline uint8_t rlc_r8(GB_emulator_t *gb, uint8_t *r8) {
  gb->cpu.reg.carry = ((*r8) & 0x80) != 0;
  *r8 = (*r8 << 1) | gb->cpu.reg.carry;
  gb->cpu.reg.zero = ((*r8) == 0);
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;

  return 8;
}

static inline uint8_t rlc_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  rlc_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

static inline uint8_t rl_r8(GB_emulator_t *gb, uint8_t *r8) {
  const uint8_t carry = gb->cpu.reg.carry;
  gb->cpu.reg.carry = ((*r8) & 0x80) != 0;
  *r8 = (*r8 << 1) | carry;
  gb->cpu.reg.zero = ((*r8) == 0);
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;

  return 8;
}

static inline uint8_t rl_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  rl_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

static inline uint8_t rrc_r8(GB_emulator_t *gb, uint8_t *r8) {
  gb->cpu.reg.carry = ((*r8) & 0x01) != 0;
  *r8 = (*r8 >> 1) | (gb->cpu.reg.carry << 7);
  gb->cpu.reg.zero = ((*r8) == 0);
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;

  return 8;
}

static inline uint8_t rrc_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  rrc_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

static inline uint8_t rr_r8(GB_emulator_t *gb, uint8_t *r8) {
  const uint8_t carry = gb->cpu.reg.carry;
  gb->cpu.reg.carry = ((*r8) & 0x01) != 0;
  *r8 = (*r8 >> 1) | (carry << 7);
  gb->cpu.reg.zero = ((*r8) == 0);
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;

  return 8;
}

static inline uint8_t rr_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  rr_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

static inline uint8_t sla_r8(GB_emulator_t *gb, uint8_t *r8) {
  gb->cpu.reg.carry = ((*r8) & 0x80) != 0;
  *r8 <<= 1;
  gb->cpu.reg.zero = (*r8 == 0);
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;

  return 8;
}

static inline uint8_t sla_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  sla_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

static inline uint8_t sra_r8(GB_emulator_t *gb, uint8_t *r8) {
  gb->cpu.reg.carry = (*r8 & 0x01);
  *r8 = (*r8 >> 1) | (*r8 & 0x80);
  gb->cpu.reg.zero = (*r8 == 0);
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;

  return 8;
}

static inline uint8_t sra_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  sra_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

static inline uint8_t swap_r8(GB_emulator_t *gb, uint8_t *r8) {
  *r8 = ((*r8 & 0x0F) << 4) | ((*r8 & 0xF0) >> 4);
  gb->cpu.reg.zero = (*r8 == 0);
  gb->cpu.reg.carry = 0;
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;

  return 8;
}

static inline uint8_t swap_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  swap_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

static inline uint8_t srl_r8(GB_emulator_t *gb, uint8_t *r8) {
  gb->cpu.reg.carry = ((*r8) & 0x01) != 0;
  *r8 >>= 1;
  gb->cpu.reg.zero = (*r8 == 0);
  gb->cpu.reg.half_carry = 0;
  gb->cpu.reg.subtract = 0;

  return 8;
}

static inline uint8_t srl_addr(GB_emulator_t *gb, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  srl_r8(gb, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

static inline uint8_t bit_r8(GB_emulator_t *gb, uint8_t bit, uint8_t r8) {
  gb->cpu.reg.zero = !(r8 & (1 << bit));
  gb->cpu.reg.half_carry = 1;
  gb->cpu.reg.subtract = 0;

  return 8;
}

static inline uint8_t bit_addr(GB_emulator_t *gb, uint8_t bit, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  bit_r8(gb, bit, value);

  return 12;
}

static inline uint8_t res_r8(GB_emulator_t *gb, uint8_t bit, uint8_t *r8) {
  (void)gb;
  *r8 &= ~(1 << bit);

  return 8;
}

static inline uint8_t res_addr(GB_emulator_t *gb, uint8_t bit, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  res_r8(gb, bit, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

static inline uint8_t set_r8(GB_emulator_t *gb, uint8_t bit, uint8_t *r8) {
  (void)gb;
  *r8 |= (1 << bit);

  return 8;
}

static inline uint8_t set_addr(GB_emulator_t *gb, uint8_t bit, uint16_t addr) {
  uint8_t value;
  ld_r8_addr(gb, &value, addr);
  set_r8(gb, bit, &value);
  ld_addr_r8(gb, addr, value);

  return 16;
}

#define MAIN_OPCODE_LIST(gen) \
  gen(0x00, nop,            { (void)gb; return 4;                                            }) \
  gen(0x01, ld_bc_n16,      { return ld_r16_n16(gb, &gb->cpu.reg.bc);                        }) \
  gen(0x02, ld_addr_bc_a,   { return ld_addr_r8(gb, gb->cpu.reg.bc, gb->cpu.reg.a);          }) \
  gen(0x03, inc_bc,         { return inc_r16(gb, &gb->cpu.reg.bc);                           }) \
  gen(0x04, inc_b,          { return inc_r8(gb, &gb->cpu.reg.b);                             }) \
  gen(0x05, dec_b,          { return dec_r8(gb, &gb->cpu.reg.b);                             }) \
  gen(0x06, ld_b_n8,        { return ld_r8_addr(gb, &gb->cpu.reg.b, gb->cpu.reg.pc++);       }) \
  gen(0x07, rlca,           { return rlca(gb);                                               }) \
  gen(0x08, ld_addr_a16_sp, { return ld_addr_a16_r16(gb, gb->cpu.reg.sp);                    }) \
  gen(0x09, add_hl_bc,      { return add_r16_r16(gb, &gb->cpu.reg.hl, gb->cpu.reg.bc);       }) \
  gen(0x0A, ld_a_addr_bc,   { return ld_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.bc);         }) \
  gen(0x0B, dec_bc,         { return dec_r16(gb, &gb->cpu.reg.bc);                           }) \
  gen(0x0C, inc_c,          { return inc_r8(gb, &gb->cpu.reg.c);                             }) \
  gen(0x0D, dec_c,          { return dec_r8(gb, &gb->cpu.reg.c);                             }) \
  gen(0x0E, ld_c_n8,        { return ld_r8_addr(gb, &gb->cpu.reg.c, gb->cpu.reg.pc++);       }) \
  gen(0x0F, rrca,           { return rrca(gb);                                               }) \
  gen(0x10, stop,           { return stop(gb);                                               }) \
  gen(0x11, ld_de_n16,      { return ld_r16_n16(gb, &gb->cpu.reg.de);                        }) \
  gen(0x12, ld_addr_de_a,   { return ld_addr_r8(gb, gb->cpu.reg.de, gb->cpu.reg.a);          }) \
  gen(0x13, inc_de,         { return inc_r16(gb, &gb->cpu.reg.de);                           }) \
  gen(0x14, inc_d,          { return inc_r8(gb, &gb->cpu.reg.d);                             }) \
  gen(0x15, dec_d,          { return dec_r8(gb, &gb->cpu.reg.d);                             }) \
  gen(0x16, ld_d_n8,        { return ld_r8_addr(gb, &gb->cpu.reg.d, gb->cpu.reg.pc++);       }) \
  gen(0x17, rla,            { return rla(gb);                                                }) \
  gen(0x18, jr_e8,          { return jr_cnd_e8(gb, true);                                    }) \
  gen(0x19, add_hl_de,      { return add_r16_r16(gb, &gb->cpu.reg.hl, gb->cpu.reg.de);       }) \
  gen(0x1A, ld_a_addr_de,   { return ld_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.de);         }) \
  gen(0x1B, dec_de,         { return dec_r16(gb, &gb->cpu.reg.de);                           }) \
  gen(0x1C, inc_e,          { return inc_r8(gb, &gb->cpu.reg.e);                             }) \
  gen(0x1D, dec_e,          { return dec_r8(gb, &gb->cpu.reg.e);                             }) \
  gen(0x1E, ld_e_n8,        { return ld_r8_addr(gb, &gb->cpu.reg.e, gb->cpu.reg.pc++);       }) \
  gen(0x1F, rra,            { return rra(gb);                                                }) \
  gen(0x20, jr_nz_e8,       { return jr_cnd_e8(gb, !gb->cpu.reg.zero);                       }) \
  gen(0x21, ld_hl_n16,      { return ld_r16_n16(gb, &gb->cpu.reg.hl);                        }) \
  gen(0x22, ld_addr_hli_a,  { return ld_addr_r8(gb, gb->cpu.reg.hl++, gb->cpu.reg.a);        }) \
  gen(0x23, inc_hl,         { return inc_r16(gb, &gb->cpu.reg.hl);                           }) \
  gen(0x24, inc_h,          { return inc_r8(gb, &gb->cpu.reg.h);                             }) \
  gen(0x25, dec_h,          { return dec_r8(gb, &gb->cpu.reg.h);                             }) \
  gen(0x26, ld_h_n8,        { return ld_r8_addr(gb, &gb->cpu.reg.h, gb->cpu.reg.pc++);       }) \
  gen(0x27, daa,            { return daa(gb);                                                }) \
  gen(0x28, jr_z_e8,        { return jr_cnd_e8(gb, gb->cpu.reg.zero);                        }) \
  gen(0x29, add_hl_hl,      { return add_r16_r16(gb, &gb->cpu.reg.hl, gb->cpu.reg.hl);       }) \
  gen(0x2A, ld_a_addr_hli,  { return ld_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl++);       }) \
  gen(0x2B, dec_hl,         { return dec_r16(gb, &gb->cpu.reg.hl);                           }) \
  gen(0x2C, inc_l,          { return inc_r8(gb, &gb->cpu.reg.l);                             }) \
  gen(0x2D, dec_l,          { return dec_r8(gb, &gb->cpu.reg.l);                             }) \
  gen(0x2E, ld_l_n8,        { return ld_r8_addr(gb, &gb->cpu.reg.l, gb->cpu.reg.pc++);       }) \
  gen(0x2F, cpl,            { return cpl(gb);                                                }) \
  gen(0x30, jr_nc_e8,       { return jr_cnd_e8(gb, !gb->cpu.reg.carry);                      }) \
  gen(0x31, ld_sp_n16,      { return ld_r16_n16(gb, &gb->cpu.reg.sp);                        }) \
  gen(0x32, ld_addr_hld_a,  { return ld_addr_r8(gb, gb->cpu.reg.hl--, gb->cpu.reg.a);        }) \
  gen(0x33, inc_sp,         { return inc_r16(gb, &gb->cpu.reg.sp);                           }) \
  gen(0x34, inc_addr_hl,    { return inc_addr(gb, gb->cpu.reg.hl);                           }) \
  gen(0x35, dec_addr_hl,    { return dec_addr(gb, gb->cpu.reg.hl);                           }) \
  gen(0x36, ld_addr_hl_n8,  { return ld_addr_r16_n8(gb, gb->cpu.reg.hl);                     }) \
  gen(0x37, scf,            { return scf(gb);                                                }) \
  gen(0x38, jr_c_e8,        { return jr_cnd_e8(gb, gb->cpu.reg.carry);                       }) \
  gen(0x39, add_hl_sp,      { return add_r16_r16(gb, &gb->cpu.reg.hl, gb->cpu.reg.sp);       }) \
  gen(0x3A, ld_a_addr_hld,  { return ld_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl--);       }) \
  gen(0x3B, dec_sp,         { return dec_r16(gb, &gb->cpu.reg.sp);                           }) \
  gen(0x3C, inc_a,          { return inc_r8(gb, &gb->cpu.reg.a);                             }) \
  gen(0x3D, dec_a,          { return dec_r8(gb, &gb->cpu.reg.a);                             }) \
  gen(0x3E, ld_a_n8,        { return ld_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.pc++);       }) \
  gen(0x3F, ccf,            { return ccf(gb);                                                }) \
  gen(0x40, ld_b_b,         { return ld_r8_r8(gb, &gb->cpu.reg.b, gb->cpu.reg.b);            }) \
  gen(0x41, ld_b_c,         { return ld_r8_r8(gb, &gb->cpu.reg.b, gb->cpu.reg.c);            }) \
  gen(0x42, ld_b_d,         { return ld_r8_r8(gb, &gb->cpu.reg.b, gb->cpu.reg.d);            }) \
  gen(0x43, ld_b_e,         { return ld_r8_r8(gb, &gb->cpu.reg.b, gb->cpu.reg.e);            }) \
  gen(0x44, ld_b_h,         { return ld_r8_r8(gb, &gb->cpu.reg.b, gb->cpu.reg.h);            }) \
  gen(0x45, ld_b_l,         { return ld_r8_r8(gb, &gb->cpu.reg.b, gb->cpu.reg.l);            }) \
  gen(0x46, ld_b_addr_hl,   { return ld_r8_addr(gb, &gb->cpu.reg.b, gb->cpu.reg.hl);         }) \
  gen(0x47, ld_b_a,         { return ld_r8_r8(gb, &gb->cpu.reg.b, gb->cpu.reg.a);            }) \
  gen(0x48, ld_c_b,         { return ld_r8_r8(gb, &gb->cpu.reg.c, gb->cpu.reg.b);            }) \
  gen(0x49, ld_c_c,         { return ld_r8_r8(gb, &gb->cpu.reg.c, gb->cpu.reg.c);            }) \
  gen(0x4A, ld_c_d,         { return ld_r8_r8(gb, &gb->cpu.reg.c, gb->cpu.reg.d);            }) \
  gen(0x4B, ld_c_e,         { return ld_r8_r8(gb, &gb->cpu.reg.c, gb->cpu.reg.e);            }) \
  gen(0x4C, ld_c_h,         { return ld_r8_r8(gb, &gb->cpu.reg.c, gb->cpu.reg.h);            }) \
  gen(0x4D, ld_c_l,         { return ld_r8_r8(gb, &gb->cpu.reg.c, gb->cpu.reg.l);            }) \
  gen(0x4E, ld_c_addr_hl,   { return ld_r8_addr(gb, &gb->cpu.reg.c, gb->cpu.reg.hl);         }) \
  gen(0x4F, ld_c_a,         { return ld_r8_r8(gb, &gb->cpu.reg.c, gb->cpu.reg.a);            }) \
  gen(0x50, ld_d_b,         { return ld_r8_r8(gb, &gb->cpu.reg.d, gb->cpu.reg.b);            }) \
  gen(0x51, ld_d_c,         { return ld_r8_r8(gb, &gb->cpu.reg.d, gb->cpu.reg.c);            }) \
  gen(0x52, ld_d_d,         { return ld_r8_r8(gb, &gb->cpu.reg.d, gb->cpu.reg.d);            }) \
  gen(0x53, ld_d_e,         { return ld_r8_r8(gb, &gb->cpu.reg.d, gb->cpu.reg.e);            }) \
  gen(0x54, ld_d_h,         { return ld_r8_r8(gb, &gb->cpu.reg.d, gb->cpu.reg.h);            }) \
  gen(0x55, ld_d_l,         { return ld_r8_r8(gb, &gb->cpu.reg.d, gb->cpu.reg.l);            }) \
  gen(0x56, ld_d_addr_hl,   { return ld_r8_addr(gb, &gb->cpu.reg.d, gb->cpu.reg.hl);         }) \
  gen(0x57, ld_d_a,         { return ld_r8_r8(gb, &gb->cpu.reg.d, gb->cpu.reg.a);            }) \
  gen(0x58, ld_e_b,         { return ld_r8_r8(gb, &gb->cpu.reg.e, gb->cpu.reg.b);            }) \
  gen(0x59, ld_e_c,         { return ld_r8_r8(gb, &gb->cpu.reg.e, gb->cpu.reg.c);            }) \
  gen(0x5A, ld_e_d,         { return ld_r8_r8(gb, &gb->cpu.reg.e, gb->cpu.reg.d);            }) \
  gen(0x5B, ld_e_e,         { return ld_r8_r8(gb, &gb->cpu.reg.e, gb->cpu.reg.e);            }) \
  gen(0x5C, ld_e_h,         { return ld_r8_r8(gb, &gb->cpu.reg.e, gb->cpu.reg.h);            }) \
  gen(0x5D, ld_e_l,         { return ld_r8_r8(gb, &gb->cpu.reg.e, gb->cpu.reg.l);            }) \
  gen(0x5E, ld_e_addr_hl,   { return ld_r8_addr(gb, &gb->cpu.reg.e, gb->cpu.reg.hl);         }) \
  gen(0x5F, ld_e_a,         { return ld_r8_r8(gb, &gb->cpu.reg.e, gb->cpu.reg.a);            }) \
  gen(0x60, ld_h_b,         { return ld_r8_r8(gb, &gb->cpu.reg.h, gb->cpu.reg.b);            }) \
  gen(0x61, ld_h_c,         { return ld_r8_r8(gb, &gb->cpu.reg.h, gb->cpu.reg.c);            }) \
  gen(0x62, ld_h_d,         { return ld_r8_r8(gb, &gb->cpu.reg.h, gb->cpu.reg.d);            }) \
  gen(0x63, ld_h_e,         { return ld_r8_r8(gb, &gb->cpu.reg.h, gb->cpu.reg.e);            }) \
  gen(0x64, ld_h_h,         { return ld_r8_r8(gb, &gb->cpu.reg.h, gb->cpu.reg.h);            }) \
  gen(0x65, ld_h_l,         { return ld_r8_r8(gb, &gb->cpu.reg.h, gb->cpu.reg.l);            }) \
  gen(0x66, ld_h_addr_hl,   { return ld_r8_addr(gb, &gb->cpu.reg.h, gb->cpu.reg.hl);         }) \
  gen(0x67, ld_h_a,         { return ld_r8_r8(gb, &gb->cpu.reg.h, gb->cpu.reg.a);            }) \
  gen(0x68, ld_l_b,         { return ld_r8_r8(gb, &gb->cpu.reg.l, gb->cpu.reg.b);            }) \
  gen(0x69, ld_l_c,         { return ld_r8_r8(gb, &gb->cpu.reg.l, gb->cpu.reg.c);            }) \
  gen(0x6A, ld_l_d,         { return ld_r8_r8(gb, &gb->cpu.reg.l, gb->cpu.reg.d);            }) \
  gen(0x6B, ld_l_e,         { return ld_r8_r8(gb, &gb->cpu.reg.l, gb->cpu.reg.e);            }) \
  gen(0x6C, ld_l_h,         { return ld_r8_r8(gb, &gb->cpu.reg.l, gb->cpu.reg.h);            }) \
  gen(0x6D, ld_l_l,         { return ld_r8_r8(gb, &gb->cpu.reg.l, gb->cpu.reg.l);            }) \
  gen(0x6E, ld_l_addr_hl,   { return ld_r8_addr(gb, &gb->cpu.reg.l, gb->cpu.reg.hl);         }) \
  gen(0x6F, ld_l_a,         { return ld_r8_r8(gb, &gb->cpu.reg.l, gb->cpu.reg.a);            }) \
  gen(0x70, ld_addr_hl_b,   { return ld_addr_r8(gb, gb->cpu.reg.hl, gb->cpu.reg.b);          }) \
  gen(0x71, ld_addr_hl_c,   { return ld_addr_r8(gb, gb->cpu.reg.hl, gb->cpu.reg.c);          }) \
  gen(0x72, ld_addr_hl_d,   { return ld_addr_r8(gb, gb->cpu.reg.hl, gb->cpu.reg.d);          }) \
  gen(0x73, ld_addr_hl_e,   { return ld_addr_r8(gb, gb->cpu.reg.hl, gb->cpu.reg.e);          }) \
  gen(0x74, ld_addr_hl_h,   { return ld_addr_r8(gb, gb->cpu.reg.hl, gb->cpu.reg.h);          }) \
  gen(0x75, ld_addr_hl_l,   { return ld_addr_r8(gb, gb->cpu.reg.hl, gb->cpu.reg.l);          }) \
  gen(0x76, halt,           { return halt(gb);                                               }) \
  gen(0x77, ld_addr_hl_a,   { return ld_addr_r8(gb, gb->cpu.reg.hl, gb->cpu.reg.a);          }) \
  gen(0x78, ld_a_b,         { return ld_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.b);            }) \
  gen(0x79, ld_a_c,         { return ld_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.c);            }) \
  gen(0x7A, ld_a_d,         { return ld_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.d);            }) \
  gen(0x7B, ld_a_e,         { return ld_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.e);            }) \
  gen(0x7C, ld_a_h,         { return ld_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.h);            }) \
  gen(0x7D, ld_a_l,         { return ld_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.l);            }) \
  gen(0x7E, ld_a_addr_hl,   { return ld_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl);         }) \
  gen(0x7F, ld_a_a,         { return ld_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.a);            }) \
  gen(0x80, add_a_b,        { return add_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.b);           }) \
  gen(0x81, add_a_c,        { return add_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.c);           }) \
  gen(0x82, add_a_d,        { return add_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.d);           }) \
  gen(0x83, add_a_e,        { return add_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.e);           }) \
  gen(0x84, add_a_h,        { return add_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.h);           }) \
  gen(0x85, add_a_l,        { return add_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.l);           }) \
  gen(0x86, add_a_addr_hl,  { return add_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl);        }) \
  gen(0x87, add_a_a,        { return add_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.a);           }) \
  gen(0x88, adc_a_b,        { return adc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.b);           }) \
  gen(0x89, adc_a_c,        { return adc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.c);           }) \
  gen(0x8A, adc_a_d,        { return adc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.d);           }) \
  gen(0x8B, adc_a_e,        { return adc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.e);           }) \
  gen(0x8C, adc_a_h,        { return adc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.h);           }) \
  gen(0x8D, adc_a_l,        { return adc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.l);           }) \
  gen(0x8E, adc_a_addr_hl,  { return adc_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl);        }) \
  gen(0x8F, adc_a_a,        { return adc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.a);           }) \
  gen(0x90, sub_a_b,        { return sub_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.b);           }) \
  gen(0x91, sub_a_c,        { return sub_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.c);           }) \
  gen(0x92, sub_a_d,        { return sub_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.d);           }) \
  gen(0x93, sub_a_e,        { return sub_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.e);           }) \
  gen(0x94, sub_a_h,        { return sub_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.h);           }) \
  gen(0x95, sub_a_l,        { return sub_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.l);           }) \
  gen(0x96, sub_a_addr_hl,  { return sub_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl);        }) \
  gen(0x97, sub_a_a,        { return sub_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.a);           }) \
  gen(0x98, sbc_a_b,        { return sbc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.b);           }) \
  gen(0x99, sbc_a_c,        { return sbc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.c);           }) \
  gen(0x9A, sbc_a_d,        { return sbc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.d);           }) \
  gen(0x9B, sbc_a_e,        { return sbc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.e);           }) \
  gen(0x9C, sbc_a_h,        { return sbc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.h);           }) \
  gen(0x9D, sbc_a_l,        { return sbc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.l);           }) \
  gen(0x9E, sbc_a_addr_hl,  { return sbc_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl);        }) \
  gen(0x9F, sbc_a_a,        { return sbc_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.a);           }) \
  gen(0xA0, and_a_b,        { return and_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.b);           }) \
  gen(0xA1, and_a_c,        { return and_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.c);           }) \
  gen(0xA2, and_a_d,        { return and_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.d);           }) \
  gen(0xA3, and_a_e,        { return and_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.e);           }) \
  gen(0xA4, and_a_h,        { return and_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.h);           }) \
  gen(0xA5, and_a_l,        { return and_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.l);           }) \
  gen(0xA6, and_a_addr_hl,  { return and_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl);        }) \
  gen(0xA7, and_a_a,        { return and_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.a);           }) \
  gen(0xA8, xor_a_b,        { return xor_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.b);           }) \
  gen(0xA9, xor_a_c,        { return xor_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.c);           }) \
  gen(0xAA, xor_a_d,        { return xor_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.d);           }) \
  gen(0xAB, xor_a_e,        { return xor_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.e);           }) \
  gen(0xAC, xor_a_h,        { return xor_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.h);           }) \
  gen(0xAD, xor_a_l,        { return xor_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.l);           }) \
  gen(0xAE, xor_a_addr_hl,  { return xor_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl);        }) \
  gen(0xAF, xor_a_a,        { return xor_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.a);           }) \
  gen(0xB0, or_a_b,         { return or_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.b);            }) \
  gen(0xB1, or_a_c,         { return or_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.c);            }) \
  gen(0xB2, or_a_d,         { return or_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.d);            }) \
  gen(0xB3, or_a_e,         { return or_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.e);            }) \
  gen(0xB4, or_a_h,         { return or_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.h);            }) \
  gen(0xB5, or_a_l,         { return or_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.l);            }) \
  gen(0xB6, or_a_addr_hl,   { return or_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.hl);         }) \
  gen(0xB7, or_a_a,         { return or_r8_r8(gb, &gb->cpu.reg.a, gb->cpu.reg.a);            }) \
  gen(0xB8, cp_a_b,         { return cp_r8_r8(gb, gb->cpu.reg.a, gb->cpu.reg.b);             }) \
  gen(0xB9, cp_a_c,         { return cp_r8_r8(gb, gb->cpu.reg.a, gb->cpu.reg.c);             }) \
  gen(0xBA, cp_a_d,         { return cp_r8_r8(gb, gb->cpu.reg.a, gb->cpu.reg.d);             }) \
  gen(0xBB, cp_a_e,         { return cp_r8_r8(gb, gb->cpu.reg.a, gb->cpu.reg.e);             }) \
  gen(0xBC, cp_a_h,         { return cp_r8_r8(gb, gb->cpu.reg.a, gb->cpu.reg.h);             }) \
  gen(0xBD, cp_a_l,         { return cp_r8_r8(gb, gb->cpu.reg.a, gb->cpu.reg.l);             }) \
  gen(0xBE, cp_a_addr_hl,   { return cp_r8_addr(gb, gb->cpu.reg.a, gb->cpu.reg.hl);          }) \
  gen(0xBF, cp_a_a,         { return cp_r8_r8(gb, gb->cpu.reg.a, gb->cpu.reg.a);             }) \
  gen(0xC0, ret_nz_a16,     { return ret_cnd_a16(gb, !gb->cpu.reg.zero);                     }) \
  gen(0xC1, pop_bc,         { return pop_r16(gb, &gb->cpu.reg.b, &gb->cpu.reg.c);            }) \
  gen(0xC2, jp_nz_a16,      { return jp_cnd_a16(gb, !gb->cpu.reg.zero);                      }) \
  gen(0xC3, jp_a16,         { return jp_cnd_a16(gb, true);                                   }) \
  gen(0xC4, call_nz_a16,    { return call_cnd_a16(gb, !gb->cpu.reg.zero);                    }) \
  gen(0xC5, push_bc,        { return push_r16(gb, gb->cpu.reg.b, gb->cpu.reg.c);             }) \
  gen(0xC6, add_a_n8,       { return add_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.pc++);      }) \
  gen(0xC7, rst_00,         { return rst_addr(gb, 0x00);                                     }) \
  gen(0xC8, ret_z_a16,      { return ret_cnd_a16(gb, gb->cpu.reg.zero);                      }) \
  gen(0xC9, ret_a16,        { return ret_a16(gb);                                            }) \
  gen(0xCA, jp_z_a16,       { return jp_cnd_a16(gb, gb->cpu.reg.zero);                       }) \
  gen(0xCB, prefix,         { (void)gb; return 0;                                            }) \
  gen(0xCC, call_z_a16,     { return call_cnd_a16(gb, gb->cpu.reg.zero);                     }) \
  gen(0xCD, call_a16,       { return call_cnd_a16(gb, true);                                 }) \
  gen(0xCE, adc_a_n8,       { return adc_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.pc++);      }) \
  gen(0xCF, rst_08,         { return rst_addr(gb, 0x08);                                     }) \
  gen(0xD0, ret_nc_a16,     { return ret_cnd_a16(gb, !gb->cpu.reg.carry);                    }) \
  gen(0xD1, pop_de,         { return pop_r16(gb, &gb->cpu.reg.d, &gb->cpu.reg.e);            }) \
  gen(0xD2, jp_nc_a16,      { return jp_cnd_a16(gb, !gb->cpu.reg.carry);                     }) \
  gen(0xD3, ill,            { (void)gb; return 0;                                            }) \
  gen(0xD4, call_nc_a16,    { return call_cnd_a16(gb, !gb->cpu.reg.carry);                   }) \
  gen(0xD5, push_de,        { return push_r16(gb, gb->cpu.reg.d, gb->cpu.reg.e);             }) \
  gen(0xD6, sub_a_n8,       { return sub_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.pc++);      }) \
  gen(0xD7, rst_10,         { return rst_addr(gb, 0x10);                                     }) \
  gen(0xD8, ret_c_a16,      { return ret_cnd_a16(gb, gb->cpu.reg.carry);                     }) \
  gen(0xD9, reti_a16,       { return reti_a16(gb);                                           }) \
  gen(0xDA, jp_c_a16,       { return jp_cnd_a16(gb, gb->cpu.reg.carry);                      }) \
  gen(0xDB, ill,            { (void)gb; return 0;                                            }) \
  gen(0xDC, call_c_a16,     { return call_cnd_a16(gb, gb->cpu.reg.carry);                    }) \
  gen(0xDD, ill,            { (void)gb; return 0;                                            }) \
  gen(0xDE, sbc_a_n8,       { return sbc_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.pc++);      }) \
  gen(0xDF, rst_18,         { return rst_addr(gb, 0x18);                                     }) \
  gen(0xE0, ldh_addr_a8_a,  { return ldh_addr_a8_r8(gb, gb->cpu.reg.a);                      }) \
  gen(0xE1, pop_hl,         { return pop_r16(gb, &gb->cpu.reg.h, &gb->cpu.reg.l);            }) \
  gen(0xE2, ldh_addr_c_a,   { return ld_addr_r8(gb, 0xFF00 + gb->cpu.reg.c, gb->cpu.reg.a);  }) \
  gen(0xE3, ill,            { (void)gb; return 0;                                            }) \
  gen(0xE4, ill,            { (void)gb; return 0;                                            }) \
  gen(0xE5, push_hl,        { return push_r16(gb, gb->cpu.reg.h, gb->cpu.reg.l);             }) \
  gen(0xE6, and_a_n8,       { return and_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.pc++);      }) \
  gen(0xE7, rst_20,         { return rst_addr(gb, 0x20);                                     }) \
  gen(0xE8, add_sp_e8,      { return add_sp_e8(gb);                                          }) \
  gen(0xE9, jp_hl,          { return jp_hl(gb);                                              }) \
  gen(0xEA, ld_addr_a16_a,  { return ld_addr_a16_r8(gb, gb->cpu.reg.a);                      }) \
  gen(0xEB, ill,            { (void)gb; return 0;                                            }) \
  gen(0xEC, ill,            { (void)gb; return 0;                                            }) \
  gen(0xED, ill,            { (void)gb; return 0;                                            }) \
  gen(0xEE, xor_a_n8,       { return xor_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.pc++);      }) \
  gen(0xEF, rst_28,         { return rst_addr(gb, 0x28);                                     }) \
  gen(0xF0, ldh_a_addr_a8,  { return ldh_r8_addr_a8(gb, &gb->cpu.reg.a);                     }) \
  gen(0xF1, pop_af,         { return pop_af(gb);                                             }) \
  gen(0xF2, ldh_a_addr_c,   { return ld_r8_addr(gb, &gb->cpu.reg.a, 0xFF00 + gb->cpu.reg.c); }) \
  gen(0xF3, di,             { return di(gb);                                                 }) \
  gen(0xF4, ill,            { (void)gb; return 0;                                            }) \
  gen(0xF5, push_af,        { return push_r16(gb, gb->cpu.reg.a, gb->cpu.reg.f);             }) \
  gen(0xF6, or_a_n8,        { return or_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.pc++);       }) \
  gen(0xF7, rst_30,         { return rst_addr(gb, 0x30);                                     }) \
  gen(0xF8, ld_hl_sp_e8,    { return ld_r16_r16_e8(gb, &gb->cpu.reg.hl, gb->cpu.reg.sp);     }) \
  gen(0xF9, ld_sp_hl,       { return ld_r16_r16(gb, &gb->cpu.reg.sp, gb->cpu.reg.hl);        }) \
  gen(0xFA, ld_a_addr_a16,  { return ld_r8_addr_a16(gb, &gb->cpu.reg.a);                     }) \
  gen(0xFB, ei,             { return ei(gb);                                                 }) \
  gen(0xFC, ill,            { (void)gb; return 0;                                            }) \
  gen(0xFD, ill,            { (void)gb; return 0;                                            }) \
  gen(0xFE, cp_a_n8,        { return cp_r8_addr(gb, gb->cpu.reg.a, gb->cpu.reg.pc++);        }) \
  gen(0xFF, rst_38,         { return rst_addr(gb, 0x38);                                     })

#define CB_OPCODE_LIST(gen) \
  gen(0x00, rlc_b,          { return rlc_r8(gb, &gb->cpu.reg.b);                             }) \
  gen(0x01, rlc_c,          { return rlc_r8(gb, &gb->cpu.reg.c);                             }) \
  gen(0x02, rlc_d,          { return rlc_r8(gb, &gb->cpu.reg.d);                             }) \
  gen(0x03, rlc_e,          { return rlc_r8(gb, &gb->cpu.reg.e);                             }) \
  gen(0x04, rlc_h,          { return rlc_r8(gb, &gb->cpu.reg.h);                             }) \
  gen(0x05, rlc_l,          { return rlc_r8(gb, &gb->cpu.reg.l);                             }) \
  gen(0x06, rlc_add_hl,     { return rlc_addr(gb, gb->cpu.reg.hl);                           }) \
  gen(0x07, rlc_a,          { return rlc_r8(gb, &gb->cpu.reg.a);                             }) \
  gen(0x08, rrc_b,          { return rrc_r8(gb, &gb->cpu.reg.b);                             }) \
  gen(0x09, rrc_c,          { return rrc_r8(gb, &gb->cpu.reg.c);                             }) \
  gen(0x0A, rrc_d,          { return rrc_r8(gb, &gb->cpu.reg.d);                             }) \
  gen(0x0B, rrc_e,          { return rrc_r8(gb, &gb->cpu.reg.e);                             }) \
  gen(0x0C, rrc_h,          { return rrc_r8(gb, &gb->cpu.reg.h);                             }) \
  gen(0x0D, rrc_l,          { return rrc_r8(gb, &gb->cpu.reg.l);                             }) \
  gen(0x0E, rrc_add_hl,     { return rrc_addr(gb, gb->cpu.reg.hl);                           }) \
  gen(0x0F, rrc_a,          { return rrc_r8(gb, &gb->cpu.reg.a);                             }) \
  gen(0x10, rl_b,           { return rl_r8(gb, &gb->cpu.reg.b);                              }) \
  gen(0x11, rl_c,           { return rl_r8(gb, &gb->cpu.reg.c);                              }) \
  gen(0x12, rl_d,           { return rl_r8(gb, &gb->cpu.reg.d);                              }) \
  gen(0x13, rl_e,           { return rl_r8(gb, &gb->cpu.reg.e);                              }) \
  gen(0x14, rl_h,           { return rl_r8(gb, &gb->cpu.reg.h);                              }) \
  gen(0x15, rl_l,           { return rl_r8(gb, &gb->cpu.reg.l);                              }) \
  gen(0x16, rl_add_hl,      { return rl_addr(gb, gb->cpu.reg.hl);                            }) \
  gen(0x17, rl_a,           { return rl_r8(gb, &gb->cpu.reg.a);                              }) \
  gen(0x18, rr_b,           { return rr_r8(gb, &gb->cpu.reg.b);                              }) \
  gen(0x19, rr_c,           { return rr_r8(gb, &gb->cpu.reg.c);                              }) \
  gen(0x1A, rr_d,           { return rr_r8(gb, &gb->cpu.reg.d);                              }) \
  gen(0x1B, rr_e,           { return rr_r8(gb, &gb->cpu.reg.e);                              }) \
  gen(0x1C, rr_h,           { return rr_r8(gb, &gb->cpu.reg.h);                              }) \
  gen(0x1D, rr_l,           { return rr_r8(gb, &gb->cpu.reg.l);                              }) \
  gen(0x1E, rr_add_hl,      { return rr_addr(gb, gb->cpu.reg.hl);                            }) \
  gen(0x1F, rr_a,           { return rr_r8(gb, &gb->cpu.reg.a);                              }) \
  gen(0x20, sla_b,          { return sla_r8(gb, &gb->cpu.reg.b);                             }) \
  gen(0x21, sla_c,          { return sla_r8(gb, &gb->cpu.reg.c);                             }) \
  gen(0x22, sla_d,          { return sla_r8(gb, &gb->cpu.reg.d);                             }) \
  gen(0x23, sla_e,          { return sla_r8(gb, &gb->cpu.reg.e);                             }) \
  gen(0x24, sla_h,          { return sla_r8(gb, &gb->cpu.reg.h);                             }) \
  gen(0x25, sla_l,          { return sla_r8(gb, &gb->cpu.reg.l);                             }) \
  gen(0x26, sla_add_hl,     { return sla_addr(gb, gb->cpu.reg.hl);                           }) \
  gen(0x27, sla_a,          { return sla_r8(gb, &gb->cpu.reg.a);                             }) \
  gen(0x28, sra_b,          { return sra_r8(gb, &gb->cpu.reg.b);                             }) \
  gen(0x29, sra_c,          { return sra_r8(gb, &gb->cpu.reg.c);                             }) \
  gen(0x2A, sra_d,          { return sra_r8(gb, &gb->cpu.reg.d);                             }) \
  gen(0x2B, sra_e,          { return sra_r8(gb, &gb->cpu.reg.e);                             }) \
  gen(0x2C, sra_h,          { return sra_r8(gb, &gb->cpu.reg.h);                             }) \
  gen(0x2D, sra_l,          { return sra_r8(gb, &gb->cpu.reg.l);                             }) \
  gen(0x2E, sra_add_hl,     { return sra_addr(gb, gb->cpu.reg.hl);                           }) \
  gen(0x2F, sra_a,          { return sra_r8(gb, &gb->cpu.reg.a);                             }) \
  gen(0x30, swap_b,         { return swap_r8(gb, &gb->cpu.reg.b);                            }) \
  gen(0x31, swap_c,         { return swap_r8(gb, &gb->cpu.reg.c);                            }) \
  gen(0x32, swap_d,         { return swap_r8(gb, &gb->cpu.reg.d);                            }) \
  gen(0x33, swap_e,         { return swap_r8(gb, &gb->cpu.reg.e);                            }) \
  gen(0x34, swap_h,         { return swap_r8(gb, &gb->cpu.reg.h);                            }) \
  gen(0x35, swap_l,         { return swap_r8(gb, &gb->cpu.reg.l);                            }) \
  gen(0x36, swap_add_hl,    { return swap_addr(gb, gb->cpu.reg.hl);                          }) \
  gen(0x37, swap_a,         { return swap_r8(gb, &gb->cpu.reg.a);                            }) \
  gen(0x38, srl_b,          { return srl_r8(gb, &gb->cpu.reg.b);                             }) \
  gen(0x39, srl_c,          { return srl_r8(gb, &gb->cpu.reg.c);                             }) \
  gen(0x3A, srl_d,          { return srl_r8(gb, &gb->cpu.reg.d);                             }) \
  gen(0x3B, srl_e,          { return srl_r8(gb, &gb->cpu.reg.e);                             }) \
  gen(0x3C, srl_h,          { return srl_r8(gb, &gb->cpu.reg.h);                             }) \
  gen(0x3D, srl_l,          { return srl_r8(gb, &gb->cpu.reg.l);                             }) \
  gen(0x3E, srl_add_hl,     { return srl_addr(gb, gb->cpu.reg.hl);                           }) \
  gen(0x3F, srl_a,          { return srl_r8(gb, &gb->cpu.reg.a);                             }) \
  gen(0x40, bit_0_b,        { return bit_r8(gb, 0, gb->cpu.reg.b);                           }) \
  gen(0x41, bit_0_c,        { return bit_r8(gb, 0, gb->cpu.reg.c);                           }) \
  gen(0x42, bit_0_d,        { return bit_r8(gb, 0, gb->cpu.reg.d);                           }) \
  gen(0x43, bit_0_e,        { return bit_r8(gb, 0, gb->cpu.reg.e);                           }) \
  gen(0x44, bit_0_h,        { return bit_r8(gb, 0, gb->cpu.reg.h);                           }) \
  gen(0x45, bit_0_l,        { return bit_r8(gb, 0, gb->cpu.reg.l);                           }) \
  gen(0x46, bit_0_add_hl,   { return bit_addr(gb, 0, gb->cpu.reg.hl) ;                       }) \
  gen(0x47, bit_0_a,        { return bit_r8(gb, 0, gb->cpu.reg.a);                           }) \
  gen(0x48, bit_1_b,        { return bit_r8(gb, 1, gb->cpu.reg.b);                           }) \
  gen(0x49, bit_1_c,        { return bit_r8(gb, 1, gb->cpu.reg.c);                           }) \
  gen(0x4A, bit_1_d,        { return bit_r8(gb, 1, gb->cpu.reg.d);                           }) \
  gen(0x4B, bit_1_e,        { return bit_r8(gb, 1, gb->cpu.reg.e);                           }) \
  gen(0x4C, bit_1_h,        { return bit_r8(gb, 1, gb->cpu.reg.h);                           }) \
  gen(0x4D, bit_1_l,        { return bit_r8(gb, 1, gb->cpu.reg.l);                           }) \
  gen(0x4E, bit_1_add_hl,   { return bit_addr(gb, 1, gb->cpu.reg.hl);                        }) \
  gen(0x4F, bit_1_a,        { return bit_r8(gb, 1, gb->cpu.reg.a);                           }) \
  gen(0x50, bit_2_b,        { return bit_r8(gb, 2, gb->cpu.reg.b);                           }) \
  gen(0x51, bit_2_c,        { return bit_r8(gb, 2, gb->cpu.reg.c);                           }) \
  gen(0x52, bit_2_d,        { return bit_r8(gb, 2, gb->cpu.reg.d);                           }) \
  gen(0x53, bit_2_e,        { return bit_r8(gb, 2, gb->cpu.reg.e);                           }) \
  gen(0x54, bit_2_h,        { return bit_r8(gb, 2, gb->cpu.reg.h);                           }) \
  gen(0x55, bit_2_l,        { return bit_r8(gb, 2, gb->cpu.reg.l);                           }) \
  gen(0x56, bit_2_add_hl,   { return bit_addr(gb, 2, gb->cpu.reg.hl);                        }) \
  gen(0x57, bit_2_a,        { return bit_r8(gb, 2, gb->cpu.reg.a);                           }) \
  gen(0x58, bit_3_b,        { return bit_r8(gb, 3, gb->cpu.reg.b);                           }) \
  gen(0x59, bit_3_c,        { return bit_r8(gb, 3, gb->cpu.reg.c);                           }) \
  gen(0x5A, bit_3_d,        { return bit_r8(gb, 3, gb->cpu.reg.d);                           }) \
  gen(0x5B, bit_3_e,        { return bit_r8(gb, 3, gb->cpu.reg.e);                           }) \
  gen(0x5C, bit_3_h,        { return bit_r8(gb, 3, gb->cpu.reg.h);                           }) \
  gen(0x5D, bit_3_l,        { return bit_r8(gb, 3, gb->cpu.reg.l);                           }) \
  gen(0x5E, bit_3_add_hl,   { return bit_addr(gb, 3, gb->cpu.reg.hl);                        }) \
  gen(0x5F, bit_3_a,        { return bit_r8(gb, 3, gb->cpu.reg.a);                           }) \
  gen(0x60, bit_4_b,        { return bit_r8(gb, 4, gb->cpu.reg.b);                           }) \
  gen(0x61, bit_4_c,        { return bit_r8(gb, 4, gb->cpu.reg.c);                           }) \
  gen(0x62, bit_4_d,        { return bit_r8(gb, 4, gb->cpu.reg.d);                           }) \
  gen(0x63, bit_4_e,        { return bit_r8(gb, 4, gb->cpu.reg.e);                           }) \
  gen(0x64, bit_4_h,        { return bit_r8(gb, 4, gb->cpu.reg.h);                           }) \
  gen(0x65, bit_4_l,        { return bit_r8(gb, 4, gb->cpu.reg.l);                           }) \
  gen(0x66, bit_4_add_hl,   { return bit_addr(gb, 4, gb->cpu.reg.hl);                        }) \
  gen(0x67, bit_4_a,        { return bit_r8(gb, 4, gb->cpu.reg.a);                           }) \
  gen(0x68, bit_5_b,        { return bit_r8(gb, 5, gb->cpu.reg.b);                           }) \
  gen(0x69, bit_5_c,        { return bit_r8(gb, 5, gb->cpu.reg.c);                           }) \
  gen(0x6A, bit_5_d,        { return bit_r8(gb, 5, gb->cpu.reg.d);                           }) \
  gen(0x6B, bit_5_e,        { return bit_r8(gb, 5, gb->cpu.reg.e);                           }) \
  gen(0x6C, bit_5_h,        { return bit_r8(gb, 5, gb->cpu.reg.h);                           }) \
  gen(0x6D, bit_5_l,        { return bit_r8(gb, 5, gb->cpu.reg.l);                           }) \
  gen(0x6E, bit_5_add_hl,   { return bit_addr(gb, 5, gb->cpu.reg.hl);                        }) \
  gen(0x6F, bit_5_a,        { return bit_r8(gb, 5, gb->cpu.reg.a);                           }) \
  gen(0x70, bit_6_b,        { return bit_r8(gb, 6, gb->cpu.reg.b);                           }) \
  gen(0x71, bit_6_c,        { return bit_r8(gb, 6, gb->cpu.reg.c);                           }) \
  gen(0x72, bit_6_d,        { return bit_r8(gb, 6, gb->cpu.reg.d);                           }) \
  gen(0x73, bit_6_e,        { return bit_r8(gb, 6, gb->cpu.reg.e);                           }) \
  gen(0x74, bit_6_h,        { return bit_r8(gb, 6, gb->cpu.reg.h);                           }) \
  gen(0x75, bit_6_l,        { return bit_r8(gb, 6, gb->cpu.reg.l);                           }) \
  gen(0x76, bit_6_add_hl,   { return bit_addr(gb, 6, gb->cpu.reg.hl);                        }) \
  gen(0x77, bit_6_a,        { return bit_r8(gb, 6, gb->cpu.reg.a);                           }) \
  gen(0x78, bit_7_b,        { return bit_r8(gb, 7, gb->cpu.reg.b);                           }) \
  gen(0x79, bit_7_c,        { return bit_r8(gb, 7, gb->cpu.reg.c);                           }) \
  gen(0x7A, bit_7_d,        { return bit_r8(gb, 7, gb->cpu.reg.d);                           }) \
  gen(0x7B, bit_7_e,        { return bit_r8(gb, 7, gb->cpu.reg.e);                           }) \
  gen(0x7C, bit_7_h,        { return bit_r8(gb, 7, gb->cpu.reg.h);                           }) \
  gen(0x7D, bit_7_l,        { return bit_r8(gb, 7, gb->cpu.reg.l);                           }) \
  gen(0x7E, bit_7_add_hl,   { return bit_addr(gb, 7, gb->cpu.reg.hl);                        }) \
  gen(0x7F, bit_7_a,        { return bit_r8(gb, 7, gb->cpu.reg.a);                           }) \
  gen(0x80, res_0_b,        { return res_r8(gb, 0, &gb->cpu.reg.b);                          }) \
  gen(0x81, res_0_c,        { return res_r8(gb, 0, &gb->cpu.reg.c);                          }) \
  gen(0x82, res_0_d,        { return res_r8(gb, 0, &gb->cpu.reg.d);                          }) \
  gen(0x83, res_0_e,        { return res_r8(gb, 0, &gb->cpu.reg.e);                          }) \
  gen(0x84, res_0_h,        { return res_r8(gb, 0, &gb->cpu.reg.h);                          }) \
  gen(0x85, res_0_l,        { return res_r8(gb, 0, &gb->cpu.reg.l);                          }) \
  gen(0x86, res_0_add_hl,   { return res_addr(gb, 0, gb->cpu.reg.hl) ;                       }) \
  gen(0x87, res_0_a,        { return res_r8(gb, 0, &gb->cpu.reg.a);                          }) \
  gen(0x88, res_1_b,        { return res_r8(gb, 1, &gb->cpu.reg.b);                          }) \
  gen(0x89, res_1_c,        { return res_r8(gb, 1, &gb->cpu.reg.c);                          }) \
  gen(0x8A, res_1_d,        { return res_r8(gb, 1, &gb->cpu.reg.d);                          }) \
  gen(0x8B, res_1_e,        { return res_r8(gb, 1, &gb->cpu.reg.e);                          }) \
  gen(0x8C, res_1_h,        { return res_r8(gb, 1, &gb->cpu.reg.h);                          }) \
  gen(0x8D, res_1_l,        { return res_r8(gb, 1, &gb->cpu.reg.l);                          }) \
  gen(0x8E, res_1_add_hl,   { return res_addr(gb, 1, gb->cpu.reg.hl);                        }) \
  gen(0x8F, res_1_a,        { return res_r8(gb, 1, &gb->cpu.reg.a);                          }) \
  gen(0x90, res_2_b,        { return res_r8(gb, 2, &gb->cpu.reg.b);                          }) \
  gen(0x91, res_2_c,        { return res_r8(gb, 2, &gb->cpu.reg.c);                          }) \
  gen(0x92, res_2_d,        { return res_r8(gb, 2, &gb->cpu.reg.d);                          }) \
  gen(0x93, res_2_e,        { return res_r8(gb, 2, &gb->cpu.reg.e);                          }) \
  gen(0x94, res_2_h,        { return res_r8(gb, 2, &gb->cpu.reg.h);                          }) \
  gen(0x95, res_2_l,        { return res_r8(gb, 2, &gb->cpu.reg.l);                          }) \
  gen(0x96, res_2_add_hl,   { return res_addr(gb, 2, gb->cpu.reg.hl);                        }) \
  gen(0x97, res_2_a,        { return res_r8(gb, 2, &gb->cpu.reg.a);                          }) \
  gen(0x98, res_3_b,        { return res_r8(gb, 3, &gb->cpu.reg.b);                          }) \
  gen(0x99, res_3_c,        { return res_r8(gb, 3, &gb->cpu.reg.c);                          }) \
  gen(0x9A, res_3_d,        { return res_r8(gb, 3, &gb->cpu.reg.d);                          }) \
  gen(0x9B, res_3_e,        { return res_r8(gb, 3, &gb->cpu.reg.e);                          }) \
  gen(0x9C, res_3_h,        { return res_r8(gb, 3, &gb->cpu.reg.h);                          }) \
  gen(0x9D, res_3_l,        { return res_r8(gb, 3, &gb->cpu.reg.l);                          }) \
  gen(0x9E, res_3_add_hl,   { return res_addr(gb, 3, gb->cpu.reg.hl);                        }) \
  gen(0x9F, res_3_a,        { return res_r8(gb, 3, &gb->cpu.reg.a);                          }) \
  gen(0xA0, res_4_b,        { return res_r8(gb, 4, &gb->cpu.reg.b);                          }) \
  gen(0xA1, res_4_c,        { return res_r8(gb, 4, &gb->cpu.reg.c);                          }) \
  gen(0xA2, res_4_d,        { return res_r8(gb, 4, &gb->cpu.reg.d);                          }) \
  gen(0xA3, res_4_e,        { return res_r8(gb, 4, &gb->cpu.reg.e);                          }) \
  gen(0xA4, res_4_h,        { return res_r8(gb, 4, &gb->cpu.reg.h);                          }) \
  gen(0xA5, res_4_l,        { return res_r8(gb, 4, &gb->cpu.reg.l);                          }) \
  gen(0xA6, res_4_add_hl,   { return res_addr(gb, 4, gb->cpu.reg.hl);                        }) \
  gen(0xA7, res_4_a,        { return res_r8(gb, 4, &gb->cpu.reg.a);                          }) \
  gen(0xA8, res_5_b,        { return res_r8(gb, 5, &gb->cpu.reg.b);                          }) \
  gen(0xA9, res_5_c,        { return res_r8(gb, 5, &gb->cpu.reg.c);                          }) \
  gen(0xAA, res_5_d,        { return res_r8(gb, 5, &gb->cpu.reg.d);                          }) \
  gen(0xAB, res_5_e,        { return res_r8(gb, 5, &gb->cpu.reg.e);                          }) \
  gen(0xAC, res_5_h,        { return res_r8(gb, 5, &gb->cpu.reg.h);                          }) \
  gen(0xAD, res_5_l,        { return res_r8(gb, 5, &gb->cpu.reg.l);                          }) \
  gen(0xAE, res_5_add_hl,   { return res_addr(gb, 5, gb->cpu.reg.hl);                        }) \
  gen(0xAF, res_5_a,        { return res_r8(gb, 5, &gb->cpu.reg.a);                          }) \
  gen(0xB0, res_6_b,        { return res_r8(gb, 6, &gb->cpu.reg.b);                          }) \
  gen(0xB1, res_6_c,        { return res_r8(gb, 6, &gb->cpu.reg.c);                          }) \
  gen(0xB2, res_6_d,        { return res_r8(gb, 6, &gb->cpu.reg.d);                          }) \
  gen(0xB3, res_6_e,        { return res_r8(gb, 6, &gb->cpu.reg.e);                          }) \
  gen(0xB4, res_6_h,        { return res_r8(gb, 6, &gb->cpu.reg.h);                          }) \
  gen(0xB5, res_6_l,        { return res_r8(gb, 6, &gb->cpu.reg.l);                          }) \
  gen(0xB6, res_6_add_hl,   { return res_addr(gb, 6, gb->cpu.reg.hl);                        }) \
  gen(0xB7, res_6_a,        { return res_r8(gb, 6, &gb->cpu.reg.a);                          }) \
  gen(0xB8, res_7_b,        { return res_r8(gb, 7, &gb->cpu.reg.b);                          }) \
  gen(0xB9, res_7_c,        { return res_r8(gb, 7, &gb->cpu.reg.c);                          }) \
  gen(0xBA, res_7_d,        { return res_r8(gb, 7, &gb->cpu.reg.d);                          }) \
  gen(0xBB, res_7_e,        { return res_r8(gb, 7, &gb->cpu.reg.e);                          }) \
  gen(0xBC, res_7_h,        { return res_r8(gb, 7, &gb->cpu.reg.h);                          }) \
  gen(0xBD, res_7_l,        { return res_r8(gb, 7, &gb->cpu.reg.l);                          }) \
  gen(0xBE, res_7_add_hl,   { return res_addr(gb, 7, gb->cpu.reg.hl);                        }) \
  gen(0xBF, res_7_a,        { return res_r8(gb, 7, &gb->cpu.reg.a);                          }) \
  gen(0xC0, set_0_b,        { return set_r8(gb, 0, &gb->cpu.reg.b);                          }) \
  gen(0xC1, set_0_c,        { return set_r8(gb, 0, &gb->cpu.reg.c);                          }) \
  gen(0xC2, set_0_d,        { return set_r8(gb, 0, &gb->cpu.reg.d);                          }) \
  gen(0xC3, set_0_e,        { return set_r8(gb, 0, &gb->cpu.reg.e);                          }) \
  gen(0xC4, set_0_h,        { return set_r8(gb, 0, &gb->cpu.reg.h);                          }) \
  gen(0xC5, set_0_l,        { return set_r8(gb, 0, &gb->cpu.reg.l);                          }) \
  gen(0xC6, set_0_add_hl,   { return set_addr(gb, 0, gb->cpu.reg.hl) ;                       }) \
  gen(0xC7, set_0_a,        { return set_r8(gb, 0, &gb->cpu.reg.a);                          }) \
  gen(0xC8, set_1_b,        { return set_r8(gb, 1, &gb->cpu.reg.b);                          }) \
  gen(0xC9, set_1_c,        { return set_r8(gb, 1, &gb->cpu.reg.c);                          }) \
  gen(0xCA, set_1_d,        { return set_r8(gb, 1, &gb->cpu.reg.d);                          }) \
  gen(0xCB, set_1_e,        { return set_r8(gb, 1, &gb->cpu.reg.e);                          }) \
  gen(0xCC, set_1_h,        { return set_r8(gb, 1, &gb->cpu.reg.h);                          }) \
  gen(0xCD, set_1_l,        { return set_r8(gb, 1, &gb->cpu.reg.l);                          }) \
  gen(0xCE, set_1_add_hl,   { return set_addr(gb, 1, gb->cpu.reg.hl);                        }) \
  gen(0xCF, set_1_a,        { return set_r8(gb, 1, &gb->cpu.reg.a);                          }) \
  gen(0xD0, set_2_b,        { return set_r8(gb, 2, &gb->cpu.reg.b);                          }) \
  gen(0xD1, set_2_c,        { return set_r8(gb, 2, &gb->cpu.reg.c);                          }) \
  gen(0xD2, set_2_d,        { return set_r8(gb, 2, &gb->cpu.reg.d);                          }) \
  gen(0xD3, set_2_e,        { return set_r8(gb, 2, &gb->cpu.reg.e);                          }) \
  gen(0xD4, set_2_h,        { return set_r8(gb, 2, &gb->cpu.reg.h);                          }) \
  gen(0xD5, set_2_l,        { return set_r8(gb, 2, &gb->cpu.reg.l);                          }) \
  gen(0xD6, set_2_add_hl,   { return set_addr(gb, 2, gb->cpu.reg.hl);                        }) \
  gen(0xD7, set_2_a,        { return set_r8(gb, 2, &gb->cpu.reg.a);                          }) \
  gen(0xD8, set_3_b,        { return set_r8(gb, 3, &gb->cpu.reg.b);                          }) \
  gen(0xD9, set_3_c,        { return set_r8(gb, 3, &gb->cpu.reg.c);                          }) \
  gen(0xDA, set_3_d,        { return set_r8(gb, 3, &gb->cpu.reg.d);                          }) \
  gen(0xDB, set_3_e,        { return set_r8(gb, 3, &gb->cpu.reg.e);                          }) \
  gen(0xDC, set_3_h,        { return set_r8(gb, 3, &gb->cpu.reg.h);                          }) \
  gen(0xDD, set_3_l,        { return set_r8(gb, 3, &gb->cpu.reg.l);                          }) \
  gen(0xDE, set_3_add_hl,   { return set_addr(gb, 3, gb->cpu.reg.hl);                        }) \
  gen(0xDF, set_3_a,        { return set_r8(gb, 3, &gb->cpu.reg.a);                          }) \
  gen(0xE0, set_4_b,        { return set_r8(gb, 4, &gb->cpu.reg.b);                          }) \
  gen(0xE1, set_4_c,        { return set_r8(gb, 4, &gb->cpu.reg.c);                          }) \
  gen(0xE2, set_4_d,        { return set_r8(gb, 4, &gb->cpu.reg.d);                          }) \
  gen(0xE3, set_4_e,        { return set_r8(gb, 4, &gb->cpu.reg.e);                          }) \
  gen(0xE4, set_4_h,        { return set_r8(gb, 4, &gb->cpu.reg.h);                          }) \
  gen(0xE5, set_4_l,        { return set_r8(gb, 4, &gb->cpu.reg.l);                          }) \
  gen(0xE6, set_4_add_hl,   { return set_addr(gb, 4, gb->cpu.reg.hl);                        }) \
  gen(0xE7, set_4_a,        { return set_r8(gb, 4, &gb->cpu.reg.a);                          }) \
  gen(0xE8, set_5_b,        { return set_r8(gb, 5, &gb->cpu.reg.b);                          }) \
  gen(0xE9, set_5_c,        { return set_r8(gb, 5, &gb->cpu.reg.c);                          }) \
  gen(0xEA, set_5_d,        { return set_r8(gb, 5, &gb->cpu.reg.d);                          }) \
  gen(0xEB, set_5_e,        { return set_r8(gb, 5, &gb->cpu.reg.e);                          }) \
  gen(0xEC, set_5_h,        { return set_r8(gb, 5, &gb->cpu.reg.h);                          }) \
  gen(0xED, set_5_l,        { return set_r8(gb, 5, &gb->cpu.reg.l);                          }) \
  gen(0xEE, set_5_add_hl,   { return set_addr(gb, 5, gb->cpu.reg.hl);                        }) \
  gen(0xEF, set_5_a,        { return set_r8(gb, 5, &gb->cpu.reg.a);                          }) \
  gen(0xF0, set_6_b,        { return set_r8(gb, 6, &gb->cpu.reg.b);                          }) \
  gen(0xF1, set_6_c,        { return set_r8(gb, 6, &gb->cpu.reg.c);                          }) \
  gen(0xF2, set_6_d,        { return set_r8(gb, 6, &gb->cpu.reg.d);                          }) \
  gen(0xF3, set_6_e,        { return set_r8(gb, 6, &gb->cpu.reg.e);                          }) \
  gen(0xF4, set_6_h,        { return set_r8(gb, 6, &gb->cpu.reg.h);                          }) \
  gen(0xF5, set_6_l,        { return set_r8(gb, 6, &gb->cpu.reg.l);                          }) \
  gen(0xF6, set_6_add_hl,   { return set_addr(gb, 6, gb->cpu.reg.hl);                        }) \
  gen(0xF7, set_6_a,        { return set_r8(gb, 6, &gb->cpu.reg.a);                          }) \
  gen(0xF8, set_7_b,        { return set_r8(gb, 7, &gb->cpu.reg.b);                          }) \
  gen(0xF9, set_7_c,        { return set_r8(gb, 7, &gb->cpu.reg.c);                          }) \
  gen(0xFA, set_7_d,        { return set_r8(gb, 7, &gb->cpu.reg.d);                          }) \
  gen(0xFB, set_7_e,        { return set_r8(gb, 7, &gb->cpu.reg.e);                          }) \
  gen(0xFC, set_7_h,        { return set_r8(gb, 7, &gb->cpu.reg.h);                          }) \
  gen(0xFD, set_7_l,        { return set_r8(gb, 7, &gb->cpu.reg.l);                          }) \
  gen(0xFE, set_7_add_hl,   { return set_addr(gb, 7, gb->cpu.reg.hl);                        }) \
  gen(0xFF, set_7_a,        { return set_r8(gb, 7, &gb->cpu.reg.a);                          })

#define DEFINE_OPCODE(code, name, body)        static uint8_t opcode_##name##_##code(GB_emulator_t *gb) body
#define REGISTER_MAIN_OPCODE(code, name, body) main_instruction_set[code] = &opcode_##name##_##code;
#define REGISTER_MAIN_OPCODE_DISSASEMBLY(code, name, body) main_instruction_set_dissasembly[code] = #name;
#define REGISTER_CB_OPCODE(code, name, body)   cb_instruction_set[code] = &opcode_##name##_##code;

MAIN_OPCODE_LIST(DEFINE_OPCODE)
CB_OPCODE_LIST(DEFINE_OPCODE)

GB_result_t GB_cpu_init(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  memset(&gb->cpu, 0, sizeof(GB_cpu_t));

  MAIN_OPCODE_LIST(REGISTER_MAIN_OPCODE);
  MAIN_OPCODE_LIST(REGISTER_MAIN_OPCODE_DISSASEMBLY);
  CB_OPCODE_LIST(REGISTER_CB_OPCODE);

  return GB_SUCCESS;
}

GB_result_t GB_cpu_free(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  // Currently identical to init. Reserved for future use or cleanup.
  memset(&gb->cpu, 0, sizeof(GB_cpu_t));

  return GB_SUCCESS;
}

GB_result_t GB_cpu_tick(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  if (gb->cpu.cycles_remaining > 0) {
    gb->cpu.cycles_remaining--;
    return GB_SUCCESS;
  }

  if (gb->cpu.halted) {
    gb->cpu.cycles_remaining += 3;
  } else {
    memory_read(gb, gb->cpu.reg.pc++, &gb->cpu.opcode);

    uint8_t cycles;
    if (gb->cpu.opcode == 0xCB) {
      memory_read(gb, gb->cpu.reg.pc++, &gb->cpu.opcode);
      cycles = cb_instruction_set[gb->cpu.opcode](gb);
    } else {
      cycles = main_instruction_set[gb->cpu.opcode](gb);
    }
    if (cycles > 0) { cycles--; }
    gb->cpu.cycles_remaining = cycles;

    // The flag is only set after the instruction following EI
    if (gb->cpu.ie_pending_delay > 0) {
      gb->cpu.ie_pending_delay--;
      if (gb->cpu.ie_pending_delay == 0) {
        gb->cpu.reg.ie = true;
      }
    }
  }

  uint8_t ie, iflag;
  memory_read(gb, GB_HARDWARE_REGISTER_IE, &ie);
  memory_read(gb, GB_HARDWARE_REGISTER_IF, &iflag);

  if ((ie & iflag)) {
    if (gb->cpu.halted) {
      gb->cpu.halted = false;
    }

    if (gb->cpu.reg.ie) {
      gb->cpu.reg.ie = false;

      for (uint8_t i = 0; i < 5; i++) {
        const uint8_t mask = (1 << i);
        if ((ie & iflag) & mask) {
          // Reset
          memory_write(gb, GB_HARDWARE_REGISTER_IF, iflag & ~mask);
          memory_write(gb, --gb->cpu.reg.sp, (uint8_t)((gb->cpu.reg.pc >> 8) & 0xFF));
          memory_write(gb, --gb->cpu.reg.sp, (uint8_t)(gb->cpu.reg.pc & 0xFF));
          gb->cpu.reg.pc = INTERRUPT_VECTORS[i];
          gb->cpu.cycles_remaining = 19;
          break;
        }
      }
    }
  }

  return GB_SUCCESS;
}

