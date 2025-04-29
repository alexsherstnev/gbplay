#pragma once

#include "defs.h"

typedef struct {
  uint16_t div_cycles;
  uint16_t div_counter;
  uint16_t tima_cycles;
} GB_timer_t;

GB_result_t GB_timer_init(GB_emulator_t *gb);
GB_result_t GB_timer_free(GB_emulator_t *gb);
GB_result_t GB_timer_tick(GB_emulator_t *gb);

