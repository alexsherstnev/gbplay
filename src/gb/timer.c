#include "timer.h"
#include "gb.h"  // IWYU pragma: keep

static GB_result_t reset(GB_emulator_t *gb) {
  if (!gb) { return GB_ERROR_INVALID_EMULATOR; }

  gb->timer.div_cycles = 0;
  gb->timer.div_counter = 0;
  gb->timer.tima_cycles = 0;

  return GB_SUCCESS;
}

GB_result_t GB_timer_init(GB_emulator_t *gb) {
  return reset(gb);
}

GB_result_t GB_timer_free(GB_emulator_t *gb) {
  return reset(gb);
}

GB_result_t GB_timer_tick(GB_emulator_t *gb) {
  if (!gb)            { return GB_ERROR_INVALID_EMULATOR; }
  if (!gb->memory.io) { return GB_ERROR_INVALID_ARGUMENT; }

  const uint16_t prev_div_counter = gb->timer.div_counter;
  gb->timer.div_counter++;
  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_DIV)] = gb->timer.div_counter >> 8;

  const uint8_t tac = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TAC)];
  const bool timer_enabled = tac & 0x04;
  if (timer_enabled) {
    uint8_t timer_bit;
    switch (tac & 0x03) {
      case 0: timer_bit = 9; break; // 4096 Hz
      case 1: timer_bit = 3; break; // 262144 Hz
      case 2: timer_bit = 5; break; // 65536 Hz
      case 3: timer_bit = 7; break; // 16384 Hz
      default: timer_bit = 9; break; // fallback (никогда не должен случиться)
    }

    const bool prev_bit = (prev_div_counter >> timer_bit) & 1;
    const bool curr_bit = (gb->timer.div_counter >> timer_bit) & 1;
    if (prev_bit == 1 && curr_bit == 0) {
      uint8_t tima = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TIMA)];
      if (tima == 0xFF) {
        gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TIMA)] = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TMA)];
        GB_TRY(GB_interrupt_request(gb, GB_INTERRUPT_TIMER));
      } else {
        gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TIMA)] = tima + 1;
      }
    }
  }

  return GB_SUCCESS;
}
