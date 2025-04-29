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

  gb->timer.div_cycles++;
  if (gb->timer.div_cycles >= 256) {
    gb->timer.div_cycles -= 256;
    gb->timer.div_counter++;
    gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_DIV)] = gb->timer.div_counter >> 8;
  }

  const uint8_t tac = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TAC)];
  if (tac & 0x04) {
    uint16_t threshold = 0;
    switch (tac & 0x03) {
      case 0: threshold = 1024; break;  // 4096 Hz
      case 1: threshold = 16;   break;  // 262144 Hz
      case 2: threshold = 64;   break;  // 65536 Hz
      case 3: threshold = 256;  break;  // 16384 Hz
    }

    gb->timer.tima_cycles++;
    if (gb->timer.tima_cycles >= threshold) {
      while(gb->timer.tima_cycles >= threshold) {
        gb->timer.tima_cycles -= threshold;

        const uint8_t tima = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TIMA)];
        gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TIMA)]++;
        if (tima == 0xFF) {
          gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TIMA)] = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_TMA)];
          GB_TRY(GB_interrupt_request(gb, GB_INTERRUPT_TIMER));
        }
      }
    }
  } else {
    gb->timer.tima_cycles = 0;
  }

  return GB_SUCCESS;
}
