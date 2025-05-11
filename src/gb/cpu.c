#include "cpu.h"
#include "gb.h"  // IWYU pragma: keep

#define INSTR_BEGIN(name) \
  static GB_result_t name(GB_emulator_t *gb) { \
    switch(gb->cpu.phase) {

#define INSTR_BEGIN_ONE_PARAM(name, param_1_type, param_1_name) \
  static GB_result_t name(GB_emulator_t *gb, param_1_type param_1_name) { \
    switch(gb->cpu.phase) {

#define INSTR_BEGIN_TWO_PARAMS(name, param_1_type, param_1_name, param_2_type, param_2_name) \
  static GB_result_t name(GB_emulator_t *gb, param_1_type param_1_name, param_2_type param_2_name) { \
    switch(gb->cpu.phase) {

#define INSTR_TICK(n, body) \
      case n: \
        body \
        gb->cpu.phase++; \
        break;

#define INSTR_END \
    } \
    return GB_SUCCESS; \
  }

#define INSTR_DEFINE(code, name, body) static GB_result_t instr_##name##_##code(GB_emulator_t *gb) body
#define INSTR_REGISTER(table, code, name) table[code] = instr_##name##_##code;

static GB_cpu_instr_t main_instr_set[256];
static GB_cpu_instr_t cb_instr_set[256];

static GB_result_t memory_read(GB_emulator_t *gb) {
  // Real memory return 0xFF or 0x00 if not accessible
  gb->cpu.read_value = 0xFF;

  // Handle Boot ROM
  if (gb->cpu.addr < 0x0100 &&
      gb->memory.io &&
      gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_BOOT)] == 0x00) {
    gb->cpu.read_value = gb->memory.boot_rom[gb->cpu.addr];
    return GB_SUCCESS;
  }

  // Handle ROM 0
  if (gb->cpu.addr < 0x4000) {
    if (!gb->memory.rom_0) { return GB_ERROR_INVALID_MEMORY_ACCESS; }
    gb->cpu.read_value = gb->memory.rom_0[gb->cpu.addr];
    return GB_SUCCESS;
  }

  // Handle ROM switchable banks
  if (gb->cpu.addr < 0x8000) {
    if (gb->memory.mbc.rom_bank == 0 ||
        !gb->memory.rom_x[gb->memory.mbc.rom_bank - 1]) { return GB_ERROR_INVALID_MEMORY_ACCESS; }
    gb->cpu.read_value = gb->memory.rom_x[gb->memory.mbc.rom_bank - 1][gb->cpu.addr - 0x4000];
    return GB_SUCCESS;
  }

  // Handle VRAM
  if (gb->cpu.addr < 0xA000) {
    const uint8_t mode = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] & 0x03;
    if (mode != GB_PPU_MODE_DRAWING) {
      gb->cpu.read_value = gb->memory.vram[GB_MEMORY_VRAM_OFFSET(gb->cpu.addr)];
    }
    return GB_SUCCESS;
  }

  // Handle external RAM
  if (gb->cpu.addr < 0xC000) {
    if (!gb->memory.mbc.ram_enabled ||
        !gb->memory.external_ram[gb->memory.mbc.ram_bank]) { return GB_ERROR_INVALID_MEMORY_ACCESS; }
    gb->cpu.read_value = gb->memory.external_ram[gb->memory.mbc.ram_bank][gb->cpu.addr - 0xA000];
    return GB_SUCCESS;
  }

  // Handle WRAM
  if (gb->cpu.addr < 0xE000) {
    gb->cpu.read_value = gb->memory.wram[GB_MEMORY_WRAM_OFFSET(gb->cpu.addr)];
    return GB_SUCCESS;
  }

  // Handle echo RAM (in this case mirror of WRAM)
  if (gb->cpu.addr < 0xFE00) {
    gb->cpu.read_value = gb->memory.echo_ram[GB_MEMORY_ECHO_OFFSET(gb->cpu.addr)];
    return GB_SUCCESS;
  }

  // Handle OAM
  if (gb->cpu.addr < 0xFEA0) {
    const uint8_t mode = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] & 0x03;
    if (mode != GB_PPU_MODE_OAM && mode != GB_PPU_MODE_DRAWING) {
      gb->cpu.read_value = gb->memory.oam[GB_MEMORY_OAM_OFFSET(gb->cpu.addr)];
    }
    return GB_SUCCESS;
  }

  // Handle unsued area
  if (gb->cpu.addr < 0xFEFF) {
    gb->cpu.read_value = 0x00;  // Note: This doesn't return FFh
    return GB_SUCCESS;
  }

  // Handle I/O registers
  if (gb->cpu.addr < 0xFF80) {
    switch (gb->cpu.addr) {
      case GB_HARDWARE_REGISTER_KEY1: gb->cpu.read_value = 0xFF; break;
//     case GB_HARDWARE_REGISTER_LY:   gb->cpu.read_value = 0x90; break;
      default:                        gb->cpu.read_value = gb->memory.io[GB_MEMORY_IO_OFFSET(gb->cpu.addr)]; break;
    }
    return GB_SUCCESS;
  }

  // Handle HRAM
  if (gb->cpu.addr < 0xFFFF) {
    gb->cpu.read_value = gb->memory.hram[GB_MEMORY_HRAM_OFFSET(gb->cpu.addr)];
    return GB_SUCCESS;
  }

  // Handle interrupt enable register
  if (gb->cpu.addr == 0xFFFF) {
    gb->cpu.read_value = gb->memory.ie;
    return GB_SUCCESS;
  }

  return GB_ERROR_INVALID_MEMORY_ACCESS;
}

static GB_result_t memory_write(GB_emulator_t *gb) {
  if (gb->cpu.addr < 0x8000) {
    if (gb->cpu.addr < 0x2000) {
      // RAM enable/disable
      gb->memory.mbc.ram_enabled = (gb->cpu.write_value & 0x0F) == 0x0A;
    } else if (gb->cpu.addr < 0x4000) {
      // ROM bank lower 5 bits
      uint8_t bank = gb->cpu.write_value & 0x1F;
      if (bank == 0) { bank = 1; }  // Bank 0 cannot be selected
      gb->memory.mbc.rom_bank = (gb->memory.mbc.rom_bank & 0x60) | bank;
    } else if (gb->cpu.addr < 0x6000) {
      // RAM bank number or upper ROM bank bits
      if (gb->memory.mbc.mode == 0) {
        // Upper bits of ROM bank (MBC1)
        gb->memory.mbc.rom_bank = (gb->memory.mbc.rom_bank & 0x1F) | ((gb->cpu.write_value & 0x03) << 5);
      } else {
        // RAM bank number
        gb->memory.mbc.ram_bank = gb->cpu.write_value & 0x03;
      }
    } else if (gb->cpu.addr < 0x8000) {
      // Mode select
      gb->memory.mbc.mode = gb->cpu.write_value & 0x01;
      if (gb->memory.mbc.mode == 1) {
        gb->memory.mbc.rom_bank &= 0x1F;
      }
    }
    return GB_SUCCESS;
  }

  // Handle VRAM
  if (gb->cpu.addr < 0xA000) {
    const uint8_t mode = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] & 0x03;
    if (mode != GB_PPU_MODE_DRAWING) {
      gb->memory.vram[GB_MEMORY_VRAM_OFFSET(gb->cpu.addr)] = gb->cpu.write_value;
    }
    return GB_SUCCESS;
  }

  // Handle external RAM
  if (gb->cpu.addr < 0xC000) {
    if (!gb->memory.external_ram[0]) { return GB_ERROR_INVALID_MEMORY_ACCESS; }
    if (gb->memory.mbc.ram_enabled) {
      const uint8_t ram_bank = gb->memory.mbc.mode == 1 ? gb->memory.mbc.ram_bank : 0;
      if (ram_bank >= 16 || !gb->memory.external_ram[ram_bank]) { return GB_ERROR_INVALID_MEMORY_ACCESS; }

      uint8_t *ram_ptr = gb->memory.external_ram[ram_bank];
      if (!ram_ptr) { return GB_ERROR_INVALID_MEMORY_ACCESS; }
      ram_ptr[gb->cpu.addr - 0xA000] = gb->cpu.write_value;
    }
    return GB_SUCCESS;
  }

  // Handle WRAM
  if (gb->cpu.addr < 0xE000) {
    gb->memory.wram[GB_MEMORY_WRAM_OFFSET(gb->cpu.addr)] = gb->cpu.write_value;
    return GB_SUCCESS;
  }

  // Handle echo RAM (in this case mirror of WRAM)
  if (gb->cpu.addr < 0xFE00) {
    gb->memory.echo_ram[GB_MEMORY_ECHO_OFFSET(gb->cpu.addr)] = gb->cpu.write_value;
    return GB_SUCCESS;
  }

  // Handle OAM
  if (gb->cpu.addr < 0xFEA0) {
    const uint8_t mode = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] & 0x03;
    if (mode == GB_PPU_MODE_HBLANK || mode == GB_PPU_MODE_VBLANK) {
      gb->memory.oam[GB_MEMORY_OAM_OFFSET(gb->cpu.addr)] = gb->cpu.write_value;
    }
    return GB_SUCCESS;
  }

  // Handle unsued area
  if (gb->cpu.addr < 0xFEFF) {
    return GB_SUCCESS;
  }

  // Handle I/O registers
  if (gb->cpu.addr < 0xFF80) {
    if (gb->cpu.addr == GB_HARDWARE_REGISTER_DIV) {
//      const uint16_t old_div_counter = gb->timer.div_counter;
      gb->timer.div_counter = 0;
//      GB_timer_glitch(gb, old_div_counter);
      return GB_SUCCESS;
    } else if (gb->cpu.addr == GB_HARDWARE_REGISTER_SC) {
      if (gb->cpu.write_value & 0x80) {
        gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_IF)] &= ~(1 << 3);
      }
    } else if (gb->cpu.addr == GB_HARDWARE_REGISTER_BOOT) {
      gb->memory.ie = 0x01;
      gb->cpu.reg.ime = 1;
    } else if (gb->cpu.addr == GB_HARDWARE_REGISTER_DMA) {
      const uint16_t src = gb->cpu.write_value << 8;
      const uint16_t addr = gb->cpu.addr;
      for (uint16_t i = 0; i < 0xA0; i++) {
        gb->cpu.addr = src + i;
        GB_TRY(memory_read(gb));
        gb->memory.oam[i] = gb->cpu.read_value;
      }
      gb->cpu.addr = addr;
    } else if (gb->cpu.addr == GB_HARDWARE_REGISTER_LCDC) {
      uint8_t stat = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)];
      if (!(gb->cpu.write_value & GB_PPU_LCDC_ENABLE)) {
        gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_LY)] = 0;
        gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_STAT)] = (stat & ~0x03) | 0x00;
      }
    }
    gb->memory.io[GB_MEMORY_IO_OFFSET(gb->cpu.addr)] = gb->cpu.write_value;
    return GB_SUCCESS;
  }

  // Handle HRAM
  if (gb->cpu.addr < 0xFFFF) {
    gb->memory.hram[GB_MEMORY_HRAM_OFFSET(gb->cpu.addr)] = gb->cpu.write_value;
    return GB_SUCCESS;
  }

  // Handle interrupt enable register
  if (gb->cpu.addr == 0xFFFF) {
    gb->memory.ie = gb->cpu.write_value;
    return GB_SUCCESS;
  }

  return GB_ERROR_INVALID_MEMORY_ACCESS;
}

static uint8_t get_pending_interrupts(GB_emulator_t *gb) {
  // IE
  gb->cpu.addr = GB_HARDWARE_REGISTER_IE;
  GB_TRY(memory_read(gb));
  const uint8_t ie = gb->cpu.read_value;
  
  // IFLAG
  gb->cpu.addr = GB_HARDWARE_REGISTER_IF;
  GB_TRY(memory_read(gb));
  const uint8_t iflag = gb->cpu.read_value;

  return ie & iflag & 0x1F;
}

static uint8_t find_triggered_interrupt(GB_emulator_t *gb) {
  // Loop over all interrupts
  const uint8_t pending_interrupts = get_pending_interrupts(gb);
  for (uint8_t i = 0; i < 5; ++i) {
    const uint8_t mask = (1 << i);
    if (pending_interrupts & mask) {
      return i;
    }
  }

  return 0;
}

static GB_result_t fetch(GB_emulator_t *gb);
static GB_result_t handle_interrupt(GB_emulator_t *gb);
static GB_result_t check_interrupts(GB_emulator_t *gb) {
  gb->cpu.phase = 0;
  gb->cpu.instr = fetch;
  
  if (gb->cpu.ime_pending_delay > 0) {
    gb->cpu.ime_pending_delay--;
    if (gb->cpu.ime_pending_delay == 0) {
      gb->cpu.reg.ime = true;
    }
  }

  const uint8_t pending_interrupts = get_pending_interrupts(gb);
  if (pending_interrupts) {
    gb->cpu.halted = false;
    if (gb->cpu.reg.ime) {
      gb->cpu.instr = handle_interrupt;
    }
  }

  return GB_SUCCESS;
}

static uint8_t add_full(uint8_t a, uint8_t b, uint8_t *carry, uint8_t *half_carry) {
  // Real CPU use 9-bit for summation
  const uint16_t result = a + b + (*carry);
  *half_carry = ((a & 0x0F) + (b & 0x0F) + (*carry)) > 0x0F;
  *carry = (result > 0xFF);

  return (uint8_t)result;
}

static uint8_t sub_full(uint8_t a, uint8_t b, uint8_t *carry, uint8_t *half_carry) {
  // Real CPU use 9-bit for subtract
  const uint16_t result = a - b - (*carry);
  *half_carry = ((a & 0x0F) < ((b & 0x0F) + (*carry)));
  *carry = (result > 0xFF);

  return (uint8_t)result;
}

static void add_r8_r8_c(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r, uint8_t *carry) {
  uint8_t half_carry = 0;
  *r8_l = add_full(*r8_l, r8_r, carry, &half_carry);
  gb->cpu.reg.carry = *carry;
  gb->cpu.reg.half_carry = half_carry;
  gb->cpu.reg.subtract = 0;
  gb->cpu.reg.zero = ((*r8_l) == 0);
}

static void sub_r8_r8_c(GB_emulator_t *gb, uint8_t *r8_l, uint8_t r8_r, uint8_t *carry) {
  uint8_t half_carry = 0;
  *r8_l = sub_full(*r8_l, r8_r, carry, &half_carry);
  gb->cpu.reg.carry = *carry;
  gb->cpu.reg.half_carry = half_carry;
  gb->cpu.reg.subtract = 1;
  gb->cpu.reg.zero = ((*r8_l) == 0);
}

INSTR_BEGIN(fetch)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;                      }); // T1
  INSTR_TICK(1, { gb->cpu.reg.pc++;                                   }); // T2
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                            }); // T3
  INSTR_TICK(3, { gb->cpu.phase = 0;
                  gb->cpu.instr = main_instr_set[gb->cpu.read_value];
                  return gb->cpu.instr(gb);                           }); // T4
INSTR_END

INSTR_BEGIN(handle_interrupt)
  INSTR_TICK(0,  {                                                                }); // T14
  INSTR_TICK(1,  {                                                                }); // T15
  INSTR_TICK(2,  {                                                                }); // T16
  INSTR_TICK(3,  {                                                                }); // T17
  INSTR_TICK(4,  {                                                                }); // T18
  INSTR_TICK(5,  { gb->cpu.reg.ime = false;                                       }); // T1
  INSTR_TICK(6,  { gb->cpu.reg.sp--;                                              }); // T2
  INSTR_TICK(7,  { gb->cpu.addr = gb->cpu.reg.sp;                                 }); // T3
  INSTR_TICK(8,  { gb->cpu.write_value = (uint8_t)((gb->cpu.reg.pc >> 8) & 0xFF); }); // T4
  INSTR_TICK(9,  { GB_TRY(memory_write(gb));                                      }); // T5
  INSTR_TICK(10, { gb->cpu.reg.sp--;                                              }); // T6
  INSTR_TICK(11, { gb->cpu.addr = gb->cpu.reg.sp;                                 }); // T7
  INSTR_TICK(12, { gb->cpu.write_value = (uint8_t)(gb->cpu.reg.pc & 0xFF);        }); // T8
  INSTR_TICK(13, { GB_TRY(memory_write(gb));                                      }); // T9
  INSTR_TICK(14, { gb->cpu.addr = GB_HARDWARE_REGISTER_IF;                        }); // T10
  INSTR_TICK(15, { GB_TRY(memory_read(gb));                                       }); // T11
  INSTR_TICK(16, { const uint8_t iflag = gb->cpu.read_value;
                   gb->cpu.read_value = find_triggered_interrupt(gb);
                   gb->cpu.addr = GB_HARDWARE_REGISTER_IF;
                   gb->cpu.write_value = iflag & ~(1 << gb->cpu.read_value);      }); // T12
  INSTR_TICK(17, { GB_TRY(memory_write(gb));                                      }); // T13
  INSTR_TICK(18, { gb->cpu.reg.pc = 0x0040 + (gb->cpu.read_value * 8);            }); // T19
  INSTR_TICK(19, { gb->cpu.phase = 0; gb->cpu.instr = fetch; return GB_SUCCESS;   }); // T20
INSTR_END

INSTR_BEGIN(nop)
  INSTR_TICK(0, { return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(ld_r8_r8, uint8_t *, r8_l, uint8_t, r8_r)
  INSTR_TICK(0, { *r8_l = r8_r; return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(ld_addr_r8, uint16_t, addr, uint8_t, r8)
  INSTR_TICK(0, {                              }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;         }); // T4
  INSTR_TICK(2, { gb->cpu.write_value = r8;    }); // T5
  INSTR_TICK(3, { GB_TRY(memory_write(gb));    }); // T6
  INSTR_TICK(4, { return check_interrupts(gb); }); // T8
INSTR_END

INSTR_BEGIN(ld_addr_hli_a)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.hl;       }); // T4
  INSTR_TICK(1, { gb->cpu.reg.hl++;                    }); // T5
  INSTR_TICK(2, { gb->cpu.write_value = gb->cpu.reg.a; }); // T6
  INSTR_TICK(3, { GB_TRY(memory_write(gb));            }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);         }); // T8
INSTR_END

INSTR_BEGIN(ld_addr_hld_a)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.hl;       }); // T4
  INSTR_TICK(1, { gb->cpu.reg.hl--;                    }); // T5
  INSTR_TICK(2, { gb->cpu.write_value = gb->cpu.reg.a; }); // T6
  INSTR_TICK(3, { GB_TRY(memory_write(gb));            }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);         }); // T8
INSTR_END

INSTR_BEGIN_TWO_PARAMS(ld_r8_addr, uint8_t *, r8, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;         }); // T4
  INSTR_TICK(2, { GB_TRY(memory_read(gb));     }); // T5
  INSTR_TICK(3, { *r8 = gb->cpu.read_value;    }); // T6
  INSTR_TICK(4, { return check_interrupts(gb); }); // T8
INSTR_END

INSTR_BEGIN(ld_a_addr_hli)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.hl;      }); // T4
  INSTR_TICK(1, { gb->cpu.reg.hl++;                   }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));            }); // T6
  INSTR_TICK(3, { gb->cpu.reg.a = gb->cpu.read_value; }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);        }); // T8
INSTR_END

INSTR_BEGIN(ld_a_addr_hld)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.hl;      }); // T4
  INSTR_TICK(1, { gb->cpu.reg.hl--;                   }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));            }); // T6
  INSTR_TICK(3, { gb->cpu.reg.a = gb->cpu.read_value; }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);        }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(ld_r8_n8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc; }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;              }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));       }); // T6
  INSTR_TICK(3, { *r8 = gb->cpu.read_value;      }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);   }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(ld_addr_r16_n8, uint16_t, addr)
  INSTR_TICK(0, {                                           }); // T10
  INSTR_TICK(1, {                                           }); // T11
  INSTR_TICK(2, { gb->cpu.addr = gb->cpu.reg.pc;            }); // T4
  INSTR_TICK(3, { gb->cpu.reg.pc++;                         }); // T5
  INSTR_TICK(4, { GB_TRY(memory_read(gb));                  }); // T6
  INSTR_TICK(5, { gb->cpu.write_value = gb->cpu.read_value; }); // T7
  INSTR_TICK(6, { gb->cpu.addr = addr;                      }); // T8
  INSTR_TICK(7, { GB_TRY(memory_write(gb));                 }); // T9
  INSTR_TICK(8, { return check_interrupts(gb);              }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(ld_addr_a16_r8, uint8_t, r8)
  INSTR_TICK(0,  {                                                                 }); // T14
  INSTR_TICK(1,  {                                                                 }); // T15
  INSTR_TICK(2,  { gb->cpu.addr = gb->cpu.reg.pc;                                  }); // T4
  INSTR_TICK(3,  { gb->cpu.reg.pc++;                                               }); // T5
  INSTR_TICK(4,  { GB_TRY(memory_read(gb));                                        }); // T6
  INSTR_TICK(5,  { gb->cpu.write_value = gb->cpu.read_value;                       }); // T7
  INSTR_TICK(6,  { gb->cpu.addr = gb->cpu.reg.pc;                                  }); // T8
  INSTR_TICK(7,  { gb->cpu.reg.pc++;                                               }); // T9
  INSTR_TICK(8,  { GB_TRY(memory_read(gb));                                        }); // T10
  INSTR_TICK(9,  { gb->cpu.addr = (gb->cpu.read_value << 8) | gb->cpu.write_value; }); // T11
  INSTR_TICK(10, { gb->cpu.write_value = r8;                                       }); // T12
  INSTR_TICK(11, { GB_TRY(memory_write(gb));                                       }); // T13
  INSTR_TICK(12, { return check_interrupts(gb);                                    }); // T16
INSTR_END

INSTR_BEGIN_ONE_PARAM(ld_r8_addr_a16, uint8_t *, r8)
  INSTR_TICK(0,  {                                                                 }); // T14
  INSTR_TICK(1,  {                                                                 }); // T15
  INSTR_TICK(2,  { gb->cpu.addr = gb->cpu.reg.pc;                                  }); // T4
  INSTR_TICK(3,  { gb->cpu.reg.pc++;                                               }); // T5
  INSTR_TICK(4,  { GB_TRY(memory_read(gb));                                        }); // T6
  INSTR_TICK(5,  { gb->cpu.write_value = gb->cpu.read_value;                       }); // T7
  INSTR_TICK(6,  { gb->cpu.addr = gb->cpu.reg.pc;                                  }); // T8
  INSTR_TICK(7,  { gb->cpu.reg.pc++;                                               }); // T9
  INSTR_TICK(8,  { GB_TRY(memory_read(gb));                                        }); // T10
  INSTR_TICK(9,  { gb->cpu.addr = (gb->cpu.read_value << 8) | gb->cpu.write_value; }); // T11
  INSTR_TICK(10, { GB_TRY(memory_read(gb));                                        }); // T12
  INSTR_TICK(11, { *r8 = gb->cpu.read_value;                                       }); // T13
  INSTR_TICK(12, { return check_interrupts(gb);                                    }); // T16
INSTR_END

INSTR_BEGIN_ONE_PARAM(ld_r16_n16, uint16_t *, r16)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;                          }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                                       }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                                }); // T6
  INSTR_TICK(3, { gb->cpu.write_value = gb->cpu.read_value;               }); // T7
  INSTR_TICK(4, { gb->cpu.addr = gb->cpu.reg.pc;                          }); // T8
  INSTR_TICK(5, { gb->cpu.reg.pc++;                                       }); // T9
  INSTR_TICK(6, { GB_TRY(memory_read(gb));                                }); // T10
  INSTR_TICK(7, { *r16 = (gb->cpu.read_value << 8) | gb->cpu.write_value; }); // T11
  INSTR_TICK(8, { return check_interrupts(gb);                            }); // T12
INSTR_END

INSTR_BEGIN(ld_addr_a16_sp)
  INSTR_TICK(0,  {                                                                 }); // T17
  INSTR_TICK(1,  {                                                                 }); // T18
  INSTR_TICK(2,  {                                                                 }); // T19
  INSTR_TICK(3,  { gb->cpu.addr = gb->cpu.reg.pc;                                  }); // T4
  INSTR_TICK(4,  { gb->cpu.reg.pc++;                                               }); // T5
  INSTR_TICK(5,  { GB_TRY(memory_read(gb));                                        }); // T6
  INSTR_TICK(6,  { gb->cpu.write_value = gb->cpu.read_value;                       }); // T7
  INSTR_TICK(7,  { gb->cpu.addr = gb->cpu.reg.pc;                                  }); // T8
  INSTR_TICK(8,  { gb->cpu.reg.pc++;                                               }); // T9
  INSTR_TICK(9,  { GB_TRY(memory_read(gb));                                        }); // T10
  INSTR_TICK(10, { gb->cpu.addr = (gb->cpu.read_value << 8) | gb->cpu.write_value; }); // T11
  INSTR_TICK(11, { gb->cpu.write_value = gb->cpu.reg.sp & 0xFF;                    }); // T12
  INSTR_TICK(12, { GB_TRY(memory_write(gb));                                       }); // T13
  INSTR_TICK(13, { gb->cpu.addr++;                                                 }); // T14
  INSTR_TICK(14, { gb->cpu.write_value = (gb->cpu.reg.sp >> 8) & 0xFF;             }); // T15
  INSTR_TICK(15, { GB_TRY(memory_write(gb));                                       }); // T16
  INSTR_TICK(16, { return check_interrupts(gb);                                    }); // T20
INSTR_END

INSTR_BEGIN_ONE_PARAM(ldh_r8_addr_a8, uint8_t *, r8)
  INSTR_TICK(0, {                                             }); // T10
  INSTR_TICK(1, {                                             }); // T11
  INSTR_TICK(2, { gb->cpu.addr = gb->cpu.reg.pc;              }); // T4
  INSTR_TICK(3, { gb->cpu.reg.pc++;                           }); // T5
  INSTR_TICK(4, { GB_TRY(memory_read(gb));                    }); // T6
  INSTR_TICK(5, { gb->cpu.addr = 0xFF00 + gb->cpu.read_value; }); // T7
  INSTR_TICK(6, { GB_TRY(memory_read(gb));                    }); // T8
  INSTR_TICK(7, { *r8 = gb->cpu.read_value;                   }); // T9
  INSTR_TICK(8, { return check_interrupts(gb);                }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(ldh_addr_a8_r8, uint8_t, r8)
  INSTR_TICK(0, {                                             }); // T10
  INSTR_TICK(1, {                                             }); // T11
  INSTR_TICK(2, { gb->cpu.addr = gb->cpu.reg.pc;              }); // T4
  INSTR_TICK(3, { gb->cpu.reg.pc++;                           }); // T5
  INSTR_TICK(4, { GB_TRY(memory_read(gb));                    }); // T6
  INSTR_TICK(5, { gb->cpu.addr = 0xFF00 + gb->cpu.read_value; }); // T7
  INSTR_TICK(6, { gb->cpu.write_value = r8;                   }); // T8
  INSTR_TICK(7, { GB_TRY(memory_write(gb));                   }); // T9
  INSTR_TICK(8, { return check_interrupts(gb);                }); // T12
INSTR_END

INSTR_BEGIN_TWO_PARAMS(ld_r16_r16_e8, uint16_t *, r16_l, uint16_t, r16_r)
  INSTR_TICK(0, {                                                            }); // T8
  INSTR_TICK(1, {                                                            }); // T9
  INSTR_TICK(2, {                                                            }); // T10
  INSTR_TICK(3, {                                                            }); // T11
  INSTR_TICK(4, { gb->cpu.addr = gb->cpu.reg.pc;                             }); // T4
  INSTR_TICK(5, { gb->cpu.reg.pc++;                                          }); // T5
  INSTR_TICK(6, { GB_TRY(memory_read(gb));                                   }); // T6
  INSTR_TICK(7, { const int8_t e8 = (int8_t)gb->cpu.read_value;
                  *r16_l = r16_r + e8;
                  gb->cpu.reg.zero = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.half_carry = ((r16_r & 0x0F) + (e8 & 0x0F)) > 0x0F;
                  gb->cpu.reg.carry = ((r16_r & 0xFF) + (e8 & 0xFF)) > 0xFF; }); // T7
  INSTR_TICK(8, { return check_interrupts(gb);                               }); // T12
INSTR_END

INSTR_BEGIN_TWO_PARAMS(ld_r16_r16, uint16_t *, r16_l, uint16_t, r16_r)
  INSTR_TICK(0, {                              }); // T4
  INSTR_TICK(1, {                              }); // T5
  INSTR_TICK(2, {                              }); // T6
  INSTR_TICK(3, { *r16_l = r16_r;              }); // T7
  INSTR_TICK(4, { return check_interrupts(gb); }); // T8
INSTR_END

INSTR_BEGIN_TWO_PARAMS(add_r16_r16, uint16_t *, r16_l, uint16_t, r16_r)
  INSTR_TICK(0, { gb->cpu.reg.subtract = 0;                                                   }); // T4
  INSTR_TICK(1, { gb->cpu.reg.half_carry = (((*r16_l) & 0x0FFF) + (r16_r & 0x0FFF)) > 0x0FFF; }); // T5
  INSTR_TICK(2, { gb->cpu.reg.carry = ((*r16_l) + r16_r) > 0xFFFF;                            }); // T6
  INSTR_TICK(3, { *r16_l = (*r16_l) + r16_r;                                                  }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);                                                }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(inc_r16, uint16_t *, r16)
  INSTR_TICK(0, {                              }); // T4
  INSTR_TICK(1, {                              }); // T5
  INSTR_TICK(2, {                              }); // T6
  INSTR_TICK(3, { (*r16)++;                    }); // T7
  INSTR_TICK(4, { return check_interrupts(gb); }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(dec_r16, uint16_t *, r16)
  INSTR_TICK(0, {                              }); // T4
  INSTR_TICK(1, {                              }); // T5
  INSTR_TICK(2, {                              }); // T6
  INSTR_TICK(3, { (*r16)--;                    }); // T7
  INSTR_TICK(4, { return check_interrupts(gb); }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(inc_r8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.half_carry = (((*r8) & 0x0F) + 1) > 0x0F;
                  (*r8)++;
                  gb->cpu.reg.zero = ((*r8) == 0);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_ONE_PARAM(dec_r8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.reg.subtract = 1;
                  gb->cpu.reg.half_carry = ((*r8 & 0x0F) == 0x00);
                  (*r8)--;
                  gb->cpu.reg.zero = ((*r8) == 0);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_ONE_PARAM(inc_addr, uint16_t, addr)
  INSTR_TICK(0, { gb->cpu.addr = addr;                                               }); // T4
  INSTR_TICK(1, { GB_TRY(memory_read(gb));                                           }); // T5
  INSTR_TICK(2, { gb->cpu.reg.subtract = 0;                                          }); // T6
  INSTR_TICK(3, { gb->cpu.reg.half_carry = ((gb->cpu.read_value & 0x0F) + 1) > 0x0F; }); // T7
  INSTR_TICK(4, { gb->cpu.read_value++;                                              }); // T8
  INSTR_TICK(5, { gb->cpu.reg.zero = (gb->cpu.read_value == 0);                      }); // T9
  INSTR_TICK(6, { gb->cpu.write_value = gb->cpu.read_value;                          }); // T10
  INSTR_TICK(7, { GB_TRY(memory_write(gb));                                          }); // T11
  INSTR_TICK(8, { return check_interrupts(gb);                                       }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(dec_addr, uint16_t, addr)
  INSTR_TICK(0, { gb->cpu.addr = addr;                                          }); // T4
  INSTR_TICK(1, { GB_TRY(memory_read(gb));                                      }); // T5
  INSTR_TICK(2, { gb->cpu.reg.subtract = 1;                                     }); // T6
  INSTR_TICK(3, { gb->cpu.reg.half_carry = (gb->cpu.read_value & 0x0F) == 0x00; }); // T7
  INSTR_TICK(4, { gb->cpu.read_value--;                                         }); // T8
  INSTR_TICK(5, { gb->cpu.reg.zero = (gb->cpu.read_value == 0);                 }); // T9
  INSTR_TICK(6, { gb->cpu.write_value = gb->cpu.read_value;                     }); // T10
  INSTR_TICK(7, { GB_TRY(memory_write(gb));                                     }); // T11
  INSTR_TICK(8, { return check_interrupts(gb);                                  }); // T12
INSTR_END

INSTR_BEGIN_TWO_PARAMS(add_r8_r8, uint8_t *, r8_l, uint8_t, r8_r)
  INSTR_TICK(0, { uint8_t carry = 0;
                  add_r8_r8_c(gb, r8_l, r8_r, &carry);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(add_r8_addr, uint8_t *, r8, uint16_t, addr)
  INSTR_TICK(0, {                                                  }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;                             }); // T4
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                         }); // T5
  INSTR_TICK(3, { uint8_t carry = 0;
                  add_r8_r8_c(gb, r8, gb->cpu.read_value, &carry); }); // T6
  INSTR_TICK(4, { return check_interrupts(gb);                     }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(add_r8_n8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;                   }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                                }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                         }); // T6
  INSTR_TICK(3, { uint8_t carry = 0;
                  add_r8_r8_c(gb, r8, gb->cpu.read_value, &carry); }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);                     }); // T8
INSTR_END

INSTR_BEGIN(add_sp_e8)
  INSTR_TICK(0,  {                                }); // T8
  INSTR_TICK(1,  {                                }); // T9
  INSTR_TICK(2,  {                                }); // T10
  INSTR_TICK(3,  {                                }); // T11
  INSTR_TICK(4,  {                                }); // T12
  INSTR_TICK(5,  {                                }); // T13
  INSTR_TICK(6,  {                                }); // T14
  INSTR_TICK(7,  {                                }); // T15
  INSTR_TICK(8,  { gb->cpu.addr = gb->cpu.reg.pc; }); // T4
  INSTR_TICK(9,  { gb->cpu.reg.pc++;              }); // T5
  INSTR_TICK(10, { GB_TRY(memory_read(gb));       }); // T6
  INSTR_TICK(11, { const int8_t e8 = (int8_t)gb->cpu.read_value;
                   const uint16_t sp = gb->cpu.reg.sp;
                   const uint16_t result = sp + e8;
                   gb->cpu.reg.zero = 0;
                   gb->cpu.reg.subtract = 0;
                   gb->cpu.reg.half_carry = ((sp & 0x0F) + (e8 & 0x0F)) > 0x0F;
                   gb->cpu.reg.carry = ((sp & 0xFF) + (e8 & 0xFF)) > 0xFF;
                   gb->cpu.reg.sp = result;       }); // T7
  INSTR_TICK(12, { return check_interrupts(gb);   }); // T16
INSTR_END

INSTR_BEGIN_TWO_PARAMS(adc_r8_r8, uint8_t *, r8_l, uint8_t, r8_r)
  INSTR_TICK(0, { uint8_t carry = gb->cpu.reg.carry;
                  add_r8_r8_c(gb, r8_l, r8_r, &carry);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(adc_r8_addr, uint8_t *, r8, uint16_t, addr)
  INSTR_TICK(0, {                                                  }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;                             }); // T4
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                         }); // T5
  INSTR_TICK(3, { uint8_t carry = gb->cpu.reg.carry;
                  add_r8_r8_c(gb, r8, gb->cpu.read_value, &carry); }); // T6
  INSTR_TICK(4, { return check_interrupts(gb);                     }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(adc_r8_n8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;                   }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                                }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                         }); // T6
  INSTR_TICK(3, { uint8_t carry = gb->cpu.reg.carry;
                  add_r8_r8_c(gb, r8, gb->cpu.read_value, &carry); }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);                     }); // T8
INSTR_END

INSTR_BEGIN_TWO_PARAMS(sub_r8_r8, uint8_t *, r8_l, uint8_t, r8_r)
  INSTR_TICK(0, { uint8_t carry = 0;
                  sub_r8_r8_c(gb, r8_l, r8_r, &carry);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(sub_r8_addr, uint8_t *, r8, uint16_t, addr)
  INSTR_TICK(0, {                                                  }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;                             }); // T4
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                         }); // T5
  INSTR_TICK(3, { uint8_t carry = 0;
                  sub_r8_r8_c(gb, r8, gb->cpu.read_value, &carry); }); // T6
  INSTR_TICK(4, { return check_interrupts(gb);                     }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(sub_r8_n8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;                   }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                                }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                         }); // T6
  INSTR_TICK(3, { uint8_t carry = 0;
                  sub_r8_r8_c(gb, r8, gb->cpu.read_value, &carry); }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);                     }); // T8
INSTR_END

INSTR_BEGIN_TWO_PARAMS(sbc_r8_r8, uint8_t *, r8_l, uint8_t, r8_r)
  INSTR_TICK(0, { uint8_t carry = gb->cpu.reg.carry;
                  sub_r8_r8_c(gb, r8_l, r8_r, &carry);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(sbc_r8_addr, uint8_t *, r8, uint16_t, addr)
  INSTR_TICK(0, {                                                  }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;                             }); // T4
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                         }); // T5
  INSTR_TICK(3, { uint8_t carry = gb->cpu.reg.carry;
                  sub_r8_r8_c(gb, r8, gb->cpu.read_value, &carry); }); // T6
  INSTR_TICK(4, { return check_interrupts(gb);                     }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(sbc_r8_n8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;                   }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                                }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                         }); // T6
  INSTR_TICK(3, { uint8_t carry = gb->cpu.reg.carry;
                  sub_r8_r8_c(gb, r8, gb->cpu.read_value, &carry); }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);                     }); // T8
INSTR_END

INSTR_BEGIN_TWO_PARAMS(and_r8_r8, uint8_t *, r8_l, uint8_t, r8_r)
  INSTR_TICK(0, { *r8_l &= r8_r;
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 1;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.zero = ((*r8_l) == 0);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(and_r8_addr, uint8_t *, r8, uint16_t, addr)
  INSTR_TICK(0, {                                  }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;             }); // T4
  INSTR_TICK(2, { GB_TRY(memory_read(gb));         }); // T5
  INSTR_TICK(3, { *r8 &= gb->cpu.read_value;
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 1;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.zero = ((*r8) == 0); }); // T6
  INSTR_TICK(4, { return check_interrupts(gb);     }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(and_r8_n8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;   }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));         }); // T6
  INSTR_TICK(3, { *r8 &= gb->cpu.read_value;
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 1;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.zero = ((*r8) == 0); }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);     }); // T8
INSTR_END

INSTR_BEGIN_TWO_PARAMS(or_r8_r8, uint8_t *, r8_l, uint8_t, r8_r)
  INSTR_TICK(0, { *r8_l |= r8_r;
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.zero = ((*r8_l) == 0);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(or_r8_addr, uint8_t *, r8, uint16_t, addr)
  INSTR_TICK(0, {                                  }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;             }); // T4
  INSTR_TICK(2, { GB_TRY(memory_read(gb));         }); // T5
  INSTR_TICK(3, { *r8 |= gb->cpu.read_value;
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.zero = ((*r8) == 0); }); // T6
  INSTR_TICK(4, { return check_interrupts(gb);     }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(or_r8_n8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;   }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));         }); // T6
  INSTR_TICK(3, { *r8 |= gb->cpu.read_value;
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.zero = ((*r8) == 0); }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);     }); // T8
INSTR_END

INSTR_BEGIN_TWO_PARAMS(xor_r8_r8, uint8_t *, r8_l, uint8_t, r8_r)
  INSTR_TICK(0, { *r8_l ^= r8_r;
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.zero = ((*r8_l) == 0);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(xor_r8_addr, uint8_t *, r8, uint16_t, addr)
  INSTR_TICK(0, {                                  }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;             }); // T4
  INSTR_TICK(2, { GB_TRY(memory_read(gb));         }); // T5
  INSTR_TICK(3, { *r8 ^= gb->cpu.read_value;
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.zero = ((*r8) == 0); }); // T6
  INSTR_TICK(4, { return check_interrupts(gb);     }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(xor_r8_n8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;   }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));         }); // T6
  INSTR_TICK(3, { *r8 ^= gb->cpu.read_value;
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.zero = ((*r8) == 0); }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);     }); // T8
INSTR_END

INSTR_BEGIN_TWO_PARAMS(cp_r8_r8, uint8_t, r8_l, uint8_t, r8_r)
  INSTR_TICK(0, { gb->cpu.reg.carry = ((r8_l) < (r8_r));
                  gb->cpu.reg.half_carry = ((r8_l & 0x0F) < (r8_r & 0x0F));
                  gb->cpu.reg.subtract = 1;
                  gb->cpu.reg.zero = (r8_l == r8_r);
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(cp_r8_addr, uint8_t, r8, uint16_t, addr)
  INSTR_TICK(0, {                                                }); // T7
  INSTR_TICK(1, { gb->cpu.addr = addr;                           }); // T4
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                       }); // T5
  INSTR_TICK(3, { gb->cpu.reg.carry = ((r8) < (gb->cpu.read_value));
                  gb->cpu.reg.half_carry = ((r8 & 0x0F) < (gb->cpu.read_value & 0x0F));
                  gb->cpu.reg.subtract = 1;
                  gb->cpu.reg.zero = (r8 == gb->cpu.read_value); }); // T6
  INSTR_TICK(4, { return check_interrupts(gb);                   }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(cp_r8_n8, uint8_t, r8)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;                 }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                              }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                       }); // T6
  INSTR_TICK(3, { gb->cpu.reg.carry = ((r8) < (gb->cpu.read_value));
                  gb->cpu.reg.half_carry = ((r8 & 0x0F) < (gb->cpu.read_value & 0x0F));
                  gb->cpu.reg.subtract = 1;
                  gb->cpu.reg.zero = (r8 == gb->cpu.read_value); }); // T7
  INSTR_TICK(4, { return check_interrupts(gb);                   }); // T8
INSTR_END

INSTR_BEGIN(di)
  INSTR_TICK(0, { gb->cpu.reg.ime = false; gb->cpu.ime_pending_delay = 0; return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(ei)
  INSTR_TICK(0, { gb->cpu.ime_pending_delay = 2; return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_ONE_PARAM(jr_cnd_e8, bool, cnd)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.pc;                }); // T4
  INSTR_TICK(1, { gb->cpu.reg.pc++;                             }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                      }); // T6
  INSTR_TICK(3, { if (!cnd) { gb->cpu.phase = 7; }              }); // T7
  INSTR_TICK(4, {                                               }); // T8
  INSTR_TICK(5, {                                               }); // T9
  INSTR_TICK(6, {                                               }); // T10
  INSTR_TICK(7, { gb->cpu.reg.pc += (int8_t)gb->cpu.read_value; }); // T11
  INSTR_TICK(8, { return check_interrupts(gb);                  }); // T12/8
INSTR_END

INSTR_BEGIN_ONE_PARAM(jp_cnd_a16, bool, cnd)
  INSTR_TICK(0,  { gb->cpu.addr = gb->cpu.reg.pc;                                    }); // T4
  INSTR_TICK(1,  { gb->cpu.reg.pc++;                                                 }); // T5
  INSTR_TICK(2,  { GB_TRY(memory_read(gb));                                          }); // T6
  INSTR_TICK(3,  { gb->cpu.write_value = gb->cpu.read_value;                         }); // T7
  INSTR_TICK(4,  { gb->cpu.addr = gb->cpu.reg.pc;                                    }); // T8
  INSTR_TICK(5,  { gb->cpu.reg.pc++;                                                 }); // T9
  INSTR_TICK(6,  { GB_TRY(memory_read(gb));                                          }); // T10
  INSTR_TICK(7,  { if (!cnd) { gb->cpu.phase = 11; }                                 }); // T11
  INSTR_TICK(8,  {                                                                   }); // T12
  INSTR_TICK(9,  {                                                                   }); // T13
  INSTR_TICK(10, {                                                                   }); // T14
  INSTR_TICK(11, { gb->cpu.reg.pc = (gb->cpu.read_value << 8) | gb->cpu.write_value; }); // T15
  INSTR_TICK(12, { return check_interrupts(gb);                                      }); // T16/12
INSTR_END

INSTR_BEGIN(jp_hl)
  INSTR_TICK(0, { gb->cpu.reg.pc = gb->cpu.reg.hl; return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN_TWO_PARAMS(push_r16, uint8_t, h, uint8_t, l)
  INSTR_TICK(0,  {                                }); // T12
  INSTR_TICK(1,  {                                }); // T13
  INSTR_TICK(2,  {                                }); // T14
  INSTR_TICK(3,  {                                }); // T15
  INSTR_TICK(4,  { gb->cpu.reg.sp--;              }); // T4
  INSTR_TICK(5,  { gb->cpu.addr = gb->cpu.reg.sp; }); // T5
  INSTR_TICK(6,  { gb->cpu.write_value = h;       }); // T6
  INSTR_TICK(7,  { GB_TRY(memory_write(gb));      }); // T7
  INSTR_TICK(8,  { gb->cpu.reg.sp--;              }); // T8
  INSTR_TICK(9,  { gb->cpu.addr = gb->cpu.reg.sp; }); // T9
  INSTR_TICK(10, { gb->cpu.write_value = l;       }); // T10
  INSTR_TICK(11, { GB_TRY(memory_write(gb));      }); // T11
  INSTR_TICK(12, { return check_interrupts(gb);   }); // T16
INSTR_END

INSTR_BEGIN_TWO_PARAMS(pop_r16, uint8_t *, h, uint8_t *, l)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.sp; }); // T4
  INSTR_TICK(1, { gb->cpu.reg.sp++;              }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));       }); // T6
  INSTR_TICK(3, { *l = gb->cpu.read_value;       }); // T7
  INSTR_TICK(4, { gb->cpu.addr = gb->cpu.reg.sp; }); // T8
  INSTR_TICK(5, { gb->cpu.reg.sp++;              }); // T9
  INSTR_TICK(6, { GB_TRY(memory_read(gb));       }); // T10
  INSTR_TICK(7, { *h = gb->cpu.read_value;       }); // T11
  INSTR_TICK(8, { return check_interrupts(gb);   }); // T12
INSTR_END

INSTR_BEGIN(pop_af)
  INSTR_TICK(0, { gb->cpu.addr = gb->cpu.reg.sp;             }); // T4
  INSTR_TICK(1, { gb->cpu.reg.sp++;                          }); // T5
  INSTR_TICK(2, { GB_TRY(memory_read(gb));                   }); // T6
  INSTR_TICK(3, { gb->cpu.reg.f = gb->cpu.read_value & 0xF0; }); // T7
  INSTR_TICK(4, { gb->cpu.addr = gb->cpu.reg.sp;             }); // T8
  INSTR_TICK(5, { gb->cpu.reg.sp++;                          }); // T9
  INSTR_TICK(6, { GB_TRY(memory_read(gb));                   }); // T10
  INSTR_TICK(7, { gb->cpu.reg.a = gb->cpu.read_value;        }); // T11
  INSTR_TICK(8, { return check_interrupts(gb);               }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(call_cnd_a16, bool, cnd)
  INSTR_TICK(0,  { gb->cpu.addr = gb->cpu.reg.pc;                                    }); // T4
  INSTR_TICK(1,  { gb->cpu.reg.pc++;                                                 }); // T5
  INSTR_TICK(2,  { GB_TRY(memory_read(gb));                                          }); // T6
  INSTR_TICK(3,  { gb->cpu.write_value = gb->cpu.read_value;                         }); // T7
  INSTR_TICK(4,  { gb->cpu.addr = gb->cpu.reg.pc;                                    }); // T8
  INSTR_TICK(5,  { gb->cpu.reg.pc++;                                                 }); // T9
  INSTR_TICK(6,  { GB_TRY(memory_read(gb));                                          }); // T10
  INSTR_TICK(7,  { if (!cnd) { gb->cpu.phase = 19; }                                 }); // T11
  INSTR_TICK(8,  { gb->cpu.target = (gb->cpu.read_value << 8) | gb->cpu.write_value; }); // T12
  INSTR_TICK(9,  { gb->cpu.reg.sp--;                                                 }); // T13
  INSTR_TICK(10, { gb->cpu.addr = gb->cpu.reg.sp;                                    }); // T14
  INSTR_TICK(11, { gb->cpu.write_value = (gb->cpu.reg.pc >> 8) & 0xFF;               }); // T15
  INSTR_TICK(12, { GB_TRY(memory_write(gb));                                         }); // T16
  INSTR_TICK(13, { gb->cpu.reg.sp--;                                                 }); // T17
  INSTR_TICK(14, { gb->cpu.addr = gb->cpu.reg.sp;                                    }); // T18
  INSTR_TICK(15, { gb->cpu.write_value = gb->cpu.reg.pc & 0xFF;                      }); // T19
  INSTR_TICK(16, { GB_TRY(memory_write(gb));                                         }); // T20
  INSTR_TICK(17, {                                                                   }); // T21
  INSTR_TICK(18, {                                                                   }); // T22
  INSTR_TICK(19, { gb->cpu.reg.pc = gb->cpu.target;                                  }); // T23
  INSTR_TICK(20, { return check_interrupts(gb);                                      }); // T24/12
INSTR_END

INSTR_BEGIN(ret_a16)
  INSTR_TICK(0,  { gb->cpu.addr = gb->cpu.reg.sp;                                    }); // T4
  INSTR_TICK(1,  { gb->cpu.reg.sp++;                                                 }); // T5
  INSTR_TICK(2,  { GB_TRY(memory_read(gb));                                          }); // T6
  INSTR_TICK(3,  { gb->cpu.write_value = gb->cpu.read_value;                         }); // T7
  INSTR_TICK(4,  { gb->cpu.addr = gb->cpu.reg.sp;                                    }); // T8
  INSTR_TICK(5,  { gb->cpu.reg.sp++;                                                 }); // T9
  INSTR_TICK(6,  { GB_TRY(memory_read(gb));                                          }); // T10
  INSTR_TICK(7,  { gb->cpu.reg.pc = (gb->cpu.read_value << 8) | gb->cpu.write_value; }); // T11
  INSTR_TICK(8,  {                                                                   }); // T12
  INSTR_TICK(9,  {                                                                   }); // T13
  INSTR_TICK(10, {                                                                   }); // T14
  INSTR_TICK(11, {                                                                   }); // T15
  INSTR_TICK(12, { return check_interrupts(gb);                                      }); // T16
INSTR_END

INSTR_BEGIN(reti_a16)
  INSTR_TICK(0,  { gb->cpu.addr = gb->cpu.reg.sp;                                    }); // T4
  INSTR_TICK(1,  { gb->cpu.reg.sp++;                                                 }); // T5
  INSTR_TICK(2,  { GB_TRY(memory_read(gb));                                          }); // T6
  INSTR_TICK(3,  { gb->cpu.write_value = gb->cpu.read_value;                         }); // T7
  INSTR_TICK(4,  { gb->cpu.addr = gb->cpu.reg.sp;                                    }); // T8
  INSTR_TICK(5,  { gb->cpu.reg.sp++;                                                 }); // T9
  INSTR_TICK(6,  { GB_TRY(memory_read(gb));                                          }); // T10
  INSTR_TICK(7,  { gb->cpu.reg.pc = (gb->cpu.read_value << 8) | gb->cpu.write_value; }); // T11
  INSTR_TICK(8,  { gb->cpu.ime_pending_delay = 2;                                    }); // T12
  INSTR_TICK(9,  {                                                                   }); // T13
  INSTR_TICK(10, {                                                                   }); // T14
  INSTR_TICK(11, {                                                                   }); // T15
  INSTR_TICK(12, { return check_interrupts(gb);                                      }); // T16
INSTR_END

INSTR_BEGIN_ONE_PARAM(ret_cnd_a16, bool, cnd)
  INSTR_TICK(0,  {                                                                   }); // T4
  INSTR_TICK(1,  {                                                                   }); // T5
  INSTR_TICK(2,  {                                                                   }); // T6
  INSTR_TICK(3,  { if (!cnd) { gb->cpu.phase = 15; }                                 }); // T7
  INSTR_TICK(4,  { gb->cpu.addr = gb->cpu.reg.sp;                                    }); // T8
  INSTR_TICK(5,  { gb->cpu.reg.sp++;                                                 }); // T9
  INSTR_TICK(6,  { GB_TRY(memory_read(gb));                                          }); // T10
  INSTR_TICK(7,  { gb->cpu.write_value = gb->cpu.read_value;                         }); // T11
  INSTR_TICK(8,  { gb->cpu.addr = gb->cpu.reg.sp;                                    }); // T12
  INSTR_TICK(9,  { gb->cpu.reg.sp++;                                                 }); // T13
  INSTR_TICK(10, { GB_TRY(memory_read(gb));                                          }); // T14
  INSTR_TICK(11, { gb->cpu.reg.pc = (gb->cpu.read_value << 8) | gb->cpu.write_value; }); // T15
  INSTR_TICK(12, {                                                                   }); // T16
  INSTR_TICK(13, {                                                                   }); // T17
  INSTR_TICK(14, {                                                                   }); // T18
  INSTR_TICK(15, {                                                                   }); // T19
  INSTR_TICK(16, { return check_interrupts(gb);                                      }); // T20/8
INSTR_END

INSTR_BEGIN_ONE_PARAM(rst_addr, uint16_t, addr)
  INSTR_TICK(0,  { gb->cpu.reg.sp--;                                   }); // T4
  INSTR_TICK(1,  { gb->cpu.addr = gb->cpu.reg.sp;                      }); // T5
  INSTR_TICK(2,  { gb->cpu.write_value = (gb->cpu.reg.pc >> 8) & 0xFF; }); // T6
  INSTR_TICK(3,  { GB_TRY(memory_write(gb));                           }); // T7
  INSTR_TICK(4,  { gb->cpu.reg.sp--;                                   }); // T8
  INSTR_TICK(5,  { gb->cpu.addr = gb->cpu.reg.sp;                      }); // T9
  INSTR_TICK(6,  { gb->cpu.write_value = gb->cpu.reg.pc & 0xFF;        }); // T10
  INSTR_TICK(7,  { GB_TRY(memory_write(gb));                           }); // T11
  INSTR_TICK(8,  { gb->cpu.reg.pc = addr;                              }); // T12
  INSTR_TICK(9,  {                                                     }); // T13
  INSTR_TICK(10, {                                                     }); // T14
  INSTR_TICK(11, {                                                     }); // T15
  INSTR_TICK(12, { return check_interrupts(gb);                        }); // T16
INSTR_END

INSTR_BEGIN(rlca)
  INSTR_TICK(0, { gb->cpu.reg.zero = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.carry = (gb->cpu.reg.a & 0x80) != 0;
                  gb->cpu.reg.a = (gb->cpu.reg.a << 1) | gb->cpu.reg.carry;
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(rla)
  INSTR_TICK(0, { gb->cpu.reg.zero = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.half_carry = 0;
                  const uint8_t temp = gb->cpu.reg.carry;
                  gb->cpu.reg.carry = (gb->cpu.reg.a & 0x80) != 0;
                  gb->cpu.reg.a = (gb->cpu.reg.a << 1) | temp;
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(cpl)
  INSTR_TICK(0, { gb->cpu.reg.a = ~gb->cpu.reg.a;
                  gb->cpu.reg.subtract = 1;
                  gb->cpu.reg.half_carry = 1;
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(ccf)
  INSTR_TICK(0, { gb->cpu.reg.carry = !gb->cpu.reg.carry;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.half_carry = 0;
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(rrca)
  INSTR_TICK(0, { const uint8_t lsb = gb->cpu.reg.a & 0x01;
                  gb->cpu.reg.a = (gb->cpu.reg.a >> 1) | (lsb << 7);
                  gb->cpu.reg.carry = lsb;
                  gb->cpu.reg.zero = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.half_carry = 0;
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(rra)
  INSTR_TICK(0, { const uint8_t lsb = gb->cpu.reg.a & 0x01;
                  const uint8_t carry = gb->cpu.reg.carry ? 0x80 : 0;
                  gb->cpu.reg.a = (gb->cpu.reg.a >> 1) | carry;
                  gb->cpu.reg.carry = lsb;
                  gb->cpu.reg.zero = 0;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.half_carry = 0;
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(daa)
  INSTR_TICK(0, { if (!gb->cpu.reg.subtract) {
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
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(scf)
  INSTR_TICK(0, { gb->cpu.reg.carry = 1;
                  gb->cpu.reg.subtract = 0;
                  gb->cpu.reg.half_carry = 0;
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(halt)
  INSTR_TICK(0, { gb->cpu.halted = true;
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(stop)
  INSTR_TICK(0, { // TODO: implement
                  gb->cpu.reg.pc++;
                  gb->cpu.stopped = true;
                  return check_interrupts(gb); }); // T4
INSTR_END

INSTR_BEGIN(prefix)
  INSTR_TICK(0, { }); // T4
  INSTR_TICK(1, { gb->cpu.addr = gb->cpu.reg.pc; }); // T4
  INSTR_TICK(2, { gb->cpu.reg.pc++;              }); // T5
  INSTR_TICK(3, { GB_TRY(memory_read(gb));       }); // T6
  INSTR_TICK(4, { gb->cpu.phase = 0;
                  gb->cpu.instr = cb_instr_set[gb->cpu.read_value];
                  return gb->cpu.instr(gb);             }); // T7
INSTR_END

INSTR_BEGIN_ONE_PARAM(rlc_r8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.reg.carry = ((*r8) & 0x80) != 0;
                  *r8 = (*r8 << 1) | gb->cpu.reg.carry;
                  gb->cpu.reg.zero = ((*r8) == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0; 
                  return check_interrupts(gb); }); // T8
INSTR_END

INSTR_BEGIN_ONE_PARAM(rlc_addr, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T16
  INSTR_TICK(1, {                              }); // T17
  INSTR_TICK(2, {                              }); // T18
  INSTR_TICK(3, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));     }); // T13
  INSTR_TICK(5, { gb->cpu.reg.carry = (gb->cpu.read_value & 0x80) != 0;
                  gb->cpu.write_value = (gb->cpu.read_value << 1) | gb->cpu.reg.carry;
                  gb->cpu.reg.zero = (gb->cpu.write_value == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;    }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(7, { GB_TRY(memory_write(gb));    }); // T15
  INSTR_TICK(8, { return check_interrupts(gb); }); // T20
INSTR_END

INSTR_BEGIN_ONE_PARAM(rl_r8, uint8_t *, r8)
  INSTR_TICK(0, { const uint8_t carry = gb->cpu.reg.carry;
                  gb->cpu.reg.carry = ((*r8) & 0x80) != 0;
                  *r8 = (*r8 << 1) | carry;
                  gb->cpu.reg.zero = ((*r8) == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(rl_addr, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T16
  INSTR_TICK(1, {                              }); // T17
  INSTR_TICK(2, {                              }); // T18
  INSTR_TICK(3, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));     }); // T13
  INSTR_TICK(5, { const uint8_t carry = gb->cpu.reg.carry;
                  gb->cpu.reg.carry = (gb->cpu.read_value & 0x80) != 0;
                  gb->cpu.write_value = (gb->cpu.read_value << 1) | carry;
                  gb->cpu.reg.zero = (gb->cpu.write_value == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;    }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(7, { GB_TRY(memory_write(gb));    }); // T15
  INSTR_TICK(8, { return check_interrupts(gb); }); // T20
INSTR_END

INSTR_BEGIN_ONE_PARAM(rrc_r8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.reg.carry = ((*r8) & 0x01) != 0;
                  *r8 = (*r8 >> 1) | (gb->cpu.reg.carry << 7);
                  gb->cpu.reg.zero = ((*r8) == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(rrc_addr, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T16
  INSTR_TICK(1, {                              }); // T17
  INSTR_TICK(2, {                              }); // T18
  INSTR_TICK(3, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));     }); // T13
  INSTR_TICK(5, { gb->cpu.reg.carry = (gb->cpu.read_value & 0x01) != 0;
                  gb->cpu.write_value = (gb->cpu.read_value >> 1) | (gb->cpu.reg.carry << 7);
                  gb->cpu.reg.zero = (gb->cpu.write_value == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;    }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(7, { GB_TRY(memory_write(gb));    }); // T15
  INSTR_TICK(8, { return check_interrupts(gb); }); // T20
INSTR_END

INSTR_BEGIN_ONE_PARAM(rr_r8, uint8_t *, r8)
  INSTR_TICK(0, { const uint8_t carry = gb->cpu.reg.carry;
                  gb->cpu.reg.carry = ((*r8) & 0x01) != 0;
                  *r8 = (*r8 >> 1) | (carry << 7);
                  gb->cpu.reg.zero = ((*r8) == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(rr_addr, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T16
  INSTR_TICK(1, {                              }); // T17
  INSTR_TICK(2, {                              }); // T18
  INSTR_TICK(3, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));     }); // T13
  INSTR_TICK(5, { const uint8_t carry = gb->cpu.reg.carry;
                  gb->cpu.reg.carry = (gb->cpu.read_value & 0x01) != 0;
                  gb->cpu.write_value = (gb->cpu.read_value >> 1) | (carry << 7);
                  gb->cpu.reg.zero = (gb->cpu.write_value == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;    }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(7, { GB_TRY(memory_write(gb));    }); // T15
  INSTR_TICK(8, { return check_interrupts(gb); }); // T20
INSTR_END

INSTR_BEGIN_ONE_PARAM(sla_r8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.reg.carry = ((*r8) & 0x80) != 0;
                  *r8 <<= 1;
                  gb->cpu.reg.zero = (*r8 == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(sla_addr, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T16
  INSTR_TICK(1, {                              }); // T17
  INSTR_TICK(2, {                              }); // T18
  INSTR_TICK(3, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));     }); // T13
  INSTR_TICK(5, { gb->cpu.reg.carry = (gb->cpu.read_value & 0x80) != 0;
                  gb->cpu.write_value = gb->cpu.read_value << 1;
                  gb->cpu.reg.zero = (gb->cpu.write_value == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;    }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(7, { GB_TRY(memory_write(gb));    }); // T15
  INSTR_TICK(8, { return check_interrupts(gb); }); // T20
INSTR_END

INSTR_BEGIN_ONE_PARAM(sra_r8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.reg.carry = (*r8 & 0x01);
                  *r8 = (*r8 >> 1) | (*r8 & 0x80);
                  gb->cpu.reg.zero = (*r8 == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(sra_addr, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T16
  INSTR_TICK(1, {                              }); // T17
  INSTR_TICK(2, {                              }); // T18
  INSTR_TICK(3, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));     }); // T13
  INSTR_TICK(5, { gb->cpu.reg.carry = (gb->cpu.read_value & 0x01);
                  gb->cpu.write_value = (gb->cpu.read_value >> 1) | (gb->cpu.read_value & 0x80);
                  gb->cpu.reg.zero = (gb->cpu.write_value == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;    }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(7, { GB_TRY(memory_write(gb));    }); // T15
  INSTR_TICK(8, { return check_interrupts(gb); }); // T20
INSTR_END

INSTR_BEGIN_ONE_PARAM(swap_r8, uint8_t *, r8)
  INSTR_TICK(0, { *r8 = ((*r8 & 0x0F) << 4) | ((*r8 & 0xF0) >> 4);
                  gb->cpu.reg.zero = (*r8 == 0);
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(swap_addr, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T16
  INSTR_TICK(1, {                              }); // T17
  INSTR_TICK(2, {                              }); // T18
  INSTR_TICK(3, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));     }); // T13
  INSTR_TICK(5, { gb->cpu.write_value = ((gb->cpu.read_value & 0x0F) << 4) | ((gb->cpu.read_value & 0xF0) >> 4);
                  gb->cpu.reg.zero = (gb->cpu.write_value == 0);
                  gb->cpu.reg.carry = 0;
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;    }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(7, { GB_TRY(memory_write(gb));    }); // T15
  INSTR_TICK(8, { return check_interrupts(gb); }); // T20
INSTR_END

INSTR_BEGIN_ONE_PARAM(srl_r8, uint8_t *, r8)
  INSTR_TICK(0, { gb->cpu.reg.carry = ((*r8) & 0x01) != 0;
                  *r8 >>= 1;
                  gb->cpu.reg.zero = (*r8 == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;
                  return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_ONE_PARAM(srl_addr, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T16
  INSTR_TICK(1, {                              }); // T17
  INSTR_TICK(2, {                              }); // T18
  INSTR_TICK(3, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));     }); // T13
  INSTR_TICK(5, { gb->cpu.reg.carry = (gb->cpu.read_value & 0x01) != 0;
                  gb->cpu.write_value = gb->cpu.read_value >> 1;
                  gb->cpu.reg.zero = (gb->cpu.write_value == 0);
                  gb->cpu.reg.half_carry = 0;
                  gb->cpu.reg.subtract = 0;    }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;         }); // T12
  INSTR_TICK(7, { GB_TRY(memory_write(gb));    }); // T15
  INSTR_TICK(8, { return check_interrupts(gb); }); // T20
INSTR_END

INSTR_BEGIN_TWO_PARAMS(bit_r8, uint8_t, bit, uint8_t, r8)
  INSTR_TICK(0, { gb->cpu.reg.zero = !(r8 & (1 << bit));
                  gb->cpu.reg.half_carry = 1;
                  gb->cpu.reg.subtract = 0;
                  return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_TWO_PARAMS(bit_addr, uint8_t, bit, uint16_t, addr)
  INSTR_TICK(0, {                              }); // T8
  INSTR_TICK(1, { gb->cpu.addr = addr;         }); // T9
  INSTR_TICK(2, { GB_TRY(memory_read(gb));     }); // T10
  INSTR_TICK(3, { gb->cpu.reg.zero = !(gb->cpu.read_value & (1 << bit));
                  gb->cpu.reg.half_carry = 1;
                  gb->cpu.reg.subtract = 0;    }); // T11
  INSTR_TICK(4, { return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_TWO_PARAMS(res_r8, uint8_t, bit, uint8_t *, r8)
  INSTR_TICK(0, { *r8 &= ~(1 << bit); return check_interrupts(gb); }); // T12
INSTR_END

INSTR_BEGIN_TWO_PARAMS(res_addr, uint8_t, bit, uint16_t, addr)
  INSTR_TICK(0, {                                                         }); // T16
  INSTR_TICK(1, {                                                         }); // T17
  INSTR_TICK(2, {                                                         }); // T18
  INSTR_TICK(3, { gb->cpu.addr = addr;                                    }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));                                }); // T13
  INSTR_TICK(5, { gb->cpu.write_value = gb->cpu.read_value & ~(1 << bit); }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;                                    }); // T12
  INSTR_TICK(7, { GB_TRY(memory_write(gb));                               }); // T15
  INSTR_TICK(8, { return check_interrupts(gb);                            }); // T20
INSTR_END

INSTR_BEGIN_TWO_PARAMS(set_r8, uint8_t, bit, uint8_t *, r8)
  INSTR_TICK(0, { *r8 |= (1 << bit); return check_interrupts(gb); }); // T8
INSTR_END

INSTR_BEGIN_TWO_PARAMS(set_addr, uint8_t, bit, uint16_t, addr)
  INSTR_TICK(0, {                                                        }); // T8
  INSTR_TICK(1, {                                                        }); // T9
  INSTR_TICK(2, {                                                        }); // T10
  INSTR_TICK(3, { gb->cpu.addr = addr;                                   }); // T12
  INSTR_TICK(4, { GB_TRY(memory_read(gb));                               }); // T13
  INSTR_TICK(5, { gb->cpu.write_value = gb->cpu.read_value | (1 << bit); }); // T14
  INSTR_TICK(6, { gb->cpu.addr = addr;                                   }); // T11
  INSTR_TICK(7, { GB_TRY(memory_write(gb));                              }); // T15
  INSTR_TICK(8, { return check_interrupts(gb);                           }); // T16
INSTR_END

#define MAIN_INSTR_SET(gen) \
  gen(0x00, nop,            { return nop(gb);                                                }) \
  gen(0x01, ld_bc_n16,      { return ld_r16_n16(gb, &gb->cpu.reg.bc);                        }) \
  gen(0x02, ld_addr_bc_a,   { return ld_addr_r8(gb, gb->cpu.reg.bc, gb->cpu.reg.a);          }) \
  gen(0x03, inc_bc,         { return inc_r16(gb, &gb->cpu.reg.bc);                           }) \
  gen(0x04, inc_b,          { return inc_r8(gb, &gb->cpu.reg.b);                             }) \
  gen(0x05, dec_b,          { return dec_r8(gb, &gb->cpu.reg.b);                             }) \
  gen(0x06, ld_b_n8,        { return ld_r8_n8(gb, &gb->cpu.reg.b);                           }) \
  gen(0x07, rlca,           { return rlca(gb);                                               }) \
  gen(0x08, ld_addr_a16_sp, { return ld_addr_a16_sp(gb);                                     }) \
  gen(0x09, add_hl_bc,      { return add_r16_r16(gb, &gb->cpu.reg.hl, gb->cpu.reg.bc);       }) \
  gen(0x0A, ld_a_addr_bc,   { return ld_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.bc);         }) \
  gen(0x0B, dec_bc,         { return dec_r16(gb, &gb->cpu.reg.bc);                           }) \
  gen(0x0C, inc_c,          { return inc_r8(gb, &gb->cpu.reg.c);                             }) \
  gen(0x0D, dec_c,          { return dec_r8(gb, &gb->cpu.reg.c);                             }) \
  gen(0x0E, ld_c_n8,        { return ld_r8_n8(gb, &gb->cpu.reg.c);                           }) \
  gen(0x0F, rrca,           { return rrca(gb);                                               }) \
  gen(0x10, stop,           { return stop(gb);                                               }) \
  gen(0x11, ld_de_n16,      { return ld_r16_n16(gb, &gb->cpu.reg.de);                        }) \
  gen(0x12, ld_addr_de_a,   { return ld_addr_r8(gb, gb->cpu.reg.de, gb->cpu.reg.a);          }) \
  gen(0x13, inc_de,         { return inc_r16(gb, &gb->cpu.reg.de);                           }) \
  gen(0x14, inc_d,          { return inc_r8(gb, &gb->cpu.reg.d);                             }) \
  gen(0x15, dec_d,          { return dec_r8(gb, &gb->cpu.reg.d);                             }) \
  gen(0x16, ld_d_n8,        { return ld_r8_n8(gb, &gb->cpu.reg.d);                           }) \
  gen(0x17, rla,            { return rla(gb);                                                }) \
  gen(0x18, jr_e8,          { return jr_cnd_e8(gb, true);                                    }) \
  gen(0x19, add_hl_de,      { return add_r16_r16(gb, &gb->cpu.reg.hl, gb->cpu.reg.de);       }) \
  gen(0x1A, ld_a_addr_de,   { return ld_r8_addr(gb, &gb->cpu.reg.a, gb->cpu.reg.de);         }) \
  gen(0x1B, dec_de,         { return dec_r16(gb, &gb->cpu.reg.de);                           }) \
  gen(0x1C, inc_e,          { return inc_r8(gb, &gb->cpu.reg.e);                             }) \
  gen(0x1D, dec_e,          { return dec_r8(gb, &gb->cpu.reg.e);                             }) \
  gen(0x1E, ld_e_n8,        { return ld_r8_n8(gb, &gb->cpu.reg.e);                           }) \
  gen(0x1F, rra,            { return rra(gb);                                                }) \
  gen(0x20, jr_nz_e8,       { return jr_cnd_e8(gb, !gb->cpu.reg.zero);                       }) \
  gen(0x21, ld_hl_n16,      { return ld_r16_n16(gb, &gb->cpu.reg.hl);                        }) \
  gen(0x22, ld_addr_hli_a,  { return ld_addr_hli_a(gb);                                      }) \
  gen(0x23, inc_hl,         { return inc_r16(gb, &gb->cpu.reg.hl);                           }) \
  gen(0x24, inc_h,          { return inc_r8(gb, &gb->cpu.reg.h);                             }) \
  gen(0x25, dec_h,          { return dec_r8(gb, &gb->cpu.reg.h);                             }) \
  gen(0x26, ld_h_n8,        { return ld_r8_n8(gb, &gb->cpu.reg.h);                           }) \
  gen(0x27, daa,            { return daa(gb);                                                }) \
  gen(0x28, jr_z_e8,        { return jr_cnd_e8(gb, gb->cpu.reg.zero);                        }) \
  gen(0x29, add_hl_hl,      { return add_r16_r16(gb, &gb->cpu.reg.hl, gb->cpu.reg.hl);       }) \
  gen(0x2A, ld_a_addr_hli,  { return ld_a_addr_hli(gb);                                      }) \
  gen(0x2B, dec_hl,         { return dec_r16(gb, &gb->cpu.reg.hl);                           }) \
  gen(0x2C, inc_l,          { return inc_r8(gb, &gb->cpu.reg.l);                             }) \
  gen(0x2D, dec_l,          { return dec_r8(gb, &gb->cpu.reg.l);                             }) \
  gen(0x2E, ld_l_n8,        { return ld_r8_n8(gb, &gb->cpu.reg.l);                           }) \
  gen(0x2F, cpl,            { return cpl(gb);                                                }) \
  gen(0x30, jr_nc_e8,       { return jr_cnd_e8(gb, !gb->cpu.reg.carry);                      }) \
  gen(0x31, ld_sp_n16,      { return ld_r16_n16(gb, &gb->cpu.reg.sp);                        }) \
  gen(0x32, ld_addr_hld_a,  { return ld_addr_hld_a(gb);                                      }) \
  gen(0x33, inc_sp,         { return inc_r16(gb, &gb->cpu.reg.sp);                           }) \
  gen(0x34, inc_addr_hl,    { return inc_addr(gb, gb->cpu.reg.hl);                           }) \
  gen(0x35, dec_addr_hl,    { return dec_addr(gb, gb->cpu.reg.hl);                           }) \
  gen(0x36, ld_addr_hl_n8,  { return ld_addr_r16_n8(gb, gb->cpu.reg.hl);                     }) \
  gen(0x37, scf,            { return scf(gb);                                                }) \
  gen(0x38, jr_c_e8,        { return jr_cnd_e8(gb, gb->cpu.reg.carry);                       }) \
  gen(0x39, add_hl_sp,      { return add_r16_r16(gb, &gb->cpu.reg.hl, gb->cpu.reg.sp);       }) \
  gen(0x3A, ld_a_addr_hld,  { return ld_a_addr_hld(gb);                                      }) \
  gen(0x3B, dec_sp,         { return dec_r16(gb, &gb->cpu.reg.sp);                           }) \
  gen(0x3C, inc_a,          { return inc_r8(gb, &gb->cpu.reg.a);                             }) \
  gen(0x3D, dec_a,          { return dec_r8(gb, &gb->cpu.reg.a);                             }) \
  gen(0x3E, ld_a_n8,        { return ld_r8_n8(gb, &gb->cpu.reg.a);                           }) \
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
  gen(0xC6, add_a_n8,       { return add_r8_n8(gb, &gb->cpu.reg.a);                          }) \
  gen(0xC7, rst_00,         { return rst_addr(gb, 0x00);                                     }) \
  gen(0xC8, ret_z_a16,      { return ret_cnd_a16(gb, gb->cpu.reg.zero);                      }) \
  gen(0xC9, ret_a16,        { return ret_a16(gb);                                            }) \
  gen(0xCA, jp_z_a16,       { return jp_cnd_a16(gb, gb->cpu.reg.zero);                       }) \
  gen(0xCB, prefix,         { return prefix(gb);                                             }) \
  gen(0xCC, call_z_a16,     { return call_cnd_a16(gb, gb->cpu.reg.zero);                     }) \
  gen(0xCD, call_a16,       { return call_cnd_a16(gb, true);                                 }) \
  gen(0xCE, adc_a_n8,       { return adc_r8_n8(gb, &gb->cpu.reg.a);                          }) \
  gen(0xCF, rst_08,         { return rst_addr(gb, 0x08);                                     }) \
  gen(0xD0, ret_nc_a16,     { return ret_cnd_a16(gb, !gb->cpu.reg.carry);                    }) \
  gen(0xD1, pop_de,         { return pop_r16(gb, &gb->cpu.reg.d, &gb->cpu.reg.e);            }) \
  gen(0xD2, jp_nc_a16,      { return jp_cnd_a16(gb, !gb->cpu.reg.carry);                     }) \
  gen(0xD3, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xD4, call_nc_a16,    { return call_cnd_a16(gb, !gb->cpu.reg.carry);                   }) \
  gen(0xD5, push_de,        { return push_r16(gb, gb->cpu.reg.d, gb->cpu.reg.e);             }) \
  gen(0xD6, sub_a_n8,       { return sub_r8_n8(gb, &gb->cpu.reg.a);                          }) \
  gen(0xD7, rst_10,         { return rst_addr(gb, 0x10);                                     }) \
  gen(0xD8, ret_c_a16,      { return ret_cnd_a16(gb, gb->cpu.reg.carry);                     }) \
  gen(0xD9, reti_a16,       { return reti_a16(gb);                                           }) \
  gen(0xDA, jp_c_a16,       { return jp_cnd_a16(gb, gb->cpu.reg.carry);                      }) \
  gen(0xDB, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xDC, call_c_a16,     { return call_cnd_a16(gb, gb->cpu.reg.carry);                    }) \
  gen(0xDD, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xDE, sbc_a_n8,       { return sbc_r8_n8(gb, &gb->cpu.reg.a);                          }) \
  gen(0xDF, rst_18,         { return rst_addr(gb, 0x18);                                     }) \
  gen(0xE0, ldh_addr_a8_a,  { return ldh_addr_a8_r8(gb, gb->cpu.reg.a);                      }) \
  gen(0xE1, pop_hl,         { return pop_r16(gb, &gb->cpu.reg.h, &gb->cpu.reg.l);            }) \
  gen(0xE2, ldh_addr_c_a,   { return ld_addr_r8(gb, 0xFF00 + gb->cpu.reg.c, gb->cpu.reg.a);  }) \
  gen(0xE3, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xE4, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xE5, push_hl,        { return push_r16(gb, gb->cpu.reg.h, gb->cpu.reg.l);             }) \
  gen(0xE6, and_a_n8,       { return and_r8_n8(gb, &gb->cpu.reg.a);                          }) \
  gen(0xE7, rst_20,         { return rst_addr(gb, 0x20);                                     }) \
  gen(0xE8, add_sp_e8,      { return add_sp_e8(gb);                                          }) \
  gen(0xE9, jp_hl,          { return jp_hl(gb);                                              }) \
  gen(0xEA, ld_addr_a16_a,  { return ld_addr_a16_r8(gb, gb->cpu.reg.a);                      }) \
  gen(0xEB, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xEC, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xED, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xEE, xor_a_n8,       { return xor_r8_n8(gb, &gb->cpu.reg.a);                          }) \
  gen(0xEF, rst_28,         { return rst_addr(gb, 0x28);                                     }) \
  gen(0xF0, ldh_a_addr_a8,  { return ldh_r8_addr_a8(gb, &gb->cpu.reg.a);                     }) \
  gen(0xF1, pop_af,         { return pop_af(gb);                                             }) \
  gen(0xF2, ldh_a_addr_c,   { return ld_r8_addr(gb, &gb->cpu.reg.a, 0xFF00 + gb->cpu.reg.c); }) \
  gen(0xF3, di,             { return di(gb);                                                 }) \
  gen(0xF4, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xF5, push_af,        { return push_r16(gb, gb->cpu.reg.a, gb->cpu.reg.f);             }) \
  gen(0xF6, or_a_n8,        { return or_r8_n8(gb, &gb->cpu.reg.a);                           }) \
  gen(0xF7, rst_30,         { return rst_addr(gb, 0x30);                                     }) \
  gen(0xF8, ld_hl_sp_e8,    { return ld_r16_r16_e8(gb, &gb->cpu.reg.hl, gb->cpu.reg.sp);     }) \
  gen(0xF9, ld_sp_hl,       { return ld_r16_r16(gb, &gb->cpu.reg.sp, gb->cpu.reg.hl);        }) \
  gen(0xFA, ld_a_addr_a16,  { return ld_r8_addr_a16(gb, &gb->cpu.reg.a);                     }) \
  gen(0xFB, ei,             { return ei(gb);                                                 }) \
  gen(0xFC, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xFD, ill,            { (void)gb; return GB_ERROR_ILLEGAL_OPCODE;                      }) \
  gen(0xFE, cp_a_n8,        { return cp_r8_n8(gb, gb->cpu.reg.a);                            }) \
  gen(0xFF, rst_38,         { return rst_addr(gb, 0x38);                                     })

#define CB_INSTR_SET(gen) \
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

MAIN_INSTR_SET(INSTR_DEFINE);
CB_INSTR_SET(INSTR_DEFINE);

GB_result_t GB_cpu_init(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  memset(&gb->cpu, 0, sizeof(GB_cpu_t));

#define GEN(code, name, body) INSTR_REGISTER(main_instr_set, code, name)
  MAIN_INSTR_SET(GEN)
#undef GEN

#define GEN(code, name, body) INSTR_REGISTER(cb_instr_set, code, name)
  CB_INSTR_SET(GEN)
#undef GEN

  gb->cpu.phase = 0;
  gb->cpu.instr = fetch;

  return GB_SUCCESS;
}

GB_result_t GB_cpu_free(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  return GB_SUCCESS;
}

GB_result_t GB_cpu_tick(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  if (gb->cpu.halted) {
    GB_TRY(check_interrupts(gb));
  } else {
    GB_TRY(gb->cpu.instr(gb));
  }

  return GB_SUCCESS;
}

