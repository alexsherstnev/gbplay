#pragma once

#include "defs.h"

typedef GB_result_t (*GB_cpu_instr_t)(GB_emulator_t *gb);

;
#pragma pack(push, 1)

typedef struct {
  union {
    struct {
#if defined(GB_BIG_ENDIAN)
      uint8_t a;
      union {
        uint8_t f;
        struct {
          uint8_t unused     : 4;
          uint8_t carry      : 1;
          uint8_t half_carry : 1;
          uint8_t subtract   : 1;
          uint8_t zero       : 1;
        };
      };
      uint8_t b, c,
              d, e,
              h, l;
#elif defined(GB_LITTLE_ENDIAN)
      union {
        uint8_t f;
        struct {
          uint8_t unsued     : 4;
          uint8_t carry      : 1;
          uint8_t half_carry : 1;
          uint8_t subtract   : 1;
          uint8_t zero       : 1;
        };
      };
      uint8_t a;
      uint8_t c, b,
              e, d,
              l, h;
#endif
    };

    struct {
      uint16_t af,
               bc,
               de,
               hl;
    };
  };
  uint16_t sp, pc;
  uint8_t ime;
} GB_register_file_t;

#pragma pack(pop)

typedef struct {
  GB_register_file_t reg;
  GB_cpu_instr_t instr;
  uint8_t phase;
  uint16_t addr;
  uint16_t target;
  uint8_t read_value;
  uint8_t write_value;
  uint8_t ime_pending_delay;
  bool halted;
  bool stopped;
} GB_cpu_t;

GB_result_t GB_cpu_init(GB_emulator_t *gb);
GB_result_t GB_cpu_free(GB_emulator_t *gb);
GB_result_t GB_cpu_tick(GB_emulator_t *gb);

