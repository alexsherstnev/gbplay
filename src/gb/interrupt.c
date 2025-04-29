#include "interrupt.h"
#include "gb.h"  // IWYU pragma: keep

GB_result_t GB_interrupt_request(GB_emulator_t *gb, uint8_t interrupt) {
  if (!gb)            { return GB_ERROR_INVALID_EMULATOR; }
  if (!gb->memory.io) { return GB_ERROR_INVALID_ARGUMENT; }

  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_IF)] |= interrupt;

  return GB_SUCCESS;
}

