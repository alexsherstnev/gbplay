#pragma once

#include "defs.h"
#include "memory.h"
#include "interrupt.h"
#include "cpu.h"
#include "ppu.h"
#include "timer.h"

struct GB_emulator {
  GB_memory_t memory;
  GB_cpu_t cpu;
  GB_ppu_t ppu;
  GB_timer_t timer;
  GB_error_t last_error;
};

GB_result_t GB_emulator_init(GB_emulator_t *gb);
GB_result_t GB_emulator_free(GB_emulator_t *gb);
GB_result_t GB_emulator_tick(GB_emulator_t *gb);
GB_result_t GB_emulator_load_rom(GB_emulator_t *gb, const char *path);
GB_error_t GB_emulator_get_last_error(GB_emulator_t *gb);
void GB_emulator_set_error(GB_emulator_t *gb, GB_result_t code, const char* file, uint32_t line, const char *fmt, ...);

