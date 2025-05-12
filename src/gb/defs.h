#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define GB_BIG_ENDIAN
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define GB_LITTLE_ENDIAN
#else
#error "Unable to detect endianness."
#endif

#define GB_ERROR_MESSAGE_MAX_LENGTH   (256)

#define GB_CYCLES_PER_FRAME           (70224)   // T-cycles of one full GB frame

#define GB_SCREEN_WIDTH               (160)
#define GB_SCREEN_HEIGHT              (144)
#define GB_MAX_OAM_SPRITES            (40)
#define GB_MAX_OAM_SPRITES_PER_LINE   (10)
#define GB_PPU_LCDC_ENABLE            (1 << 7)  // LCD & PPU enable: 0 = Off; 1 = On
#define GB_PPU_LCDC_WINDOW_TILE_MAP   (1 << 6)  // Window tile map area: 0 = 9800–9BFF; 1 = 9C00–9FFF
#define GB_PPU_LCDC_WINDOW_ENABLE     (1 << 5)  // Window enable: 0 = Off; 1 = On
#define GB_PPU_LCDC_BG_WINDOW_TILES   (1 << 4)  // BG & Window tile data area: 0 = 8800–97FF; 1 = 8000–8FFF
#define GB_PPU_LCDC_BG_TILE_MAP       (1 << 3)  // BG tile map area: 0 = 9800–9BFF; 1 = 9C00–9FFF
#define GB_PPU_LCDC_OBJ_SIZE          (1 << 2)  // OBJ size: 0 = 8x8; 1 = 8x16
#define GB_PPU_LCDC_OBJ_ENABLE        (1 << 1)  // OBJ enable: 0 = Off; 1 = On
#define GB_PPU_LCDC_BG_WINDOW_ENABLE  (1 << 0)  // BG & Window enable / priority: 0 = Off; 1 = On
#define GB_PPU_STAT_LYC_INT_SELECT    (1 << 6)  // LYC int select (Read/Write): If set, selects the LYC == LY condition for the STAT interrupt
#define GB_PPU_STAT_OAM_INT_SELECT    (1 << 5)  // Mode OAM int select (Read/Write): If set, selects the Mode OAM condition for the STAT interrupt
#define GB_PPU_STAT_VBLANK_INT_SELECT (1 << 4)  // Mode VBLANK int select (Read/Write): If set, selects the Mode VBLANK condition for the STAT interrupt
#define GB_PPU_STAT_HBLANK_INT_SELECT (1 << 3)  // Mode HBLANK int select (Read/Write): If set, selects the Mode HBLANK condition for the STAT interrupt
#define GB_PPU_STAT_LYC_EQ_LY         (1 << 2)  // LYC == LY (Read-only): Set when LY contains the same value as LYC; it is constantly updated
#define GB_PPU_STAT_MODE              ((1 << 1) | (1 << 0))  // PPU mode (Read-only): Indicates the PPU’s current status
#define GB_PPU_OAM_FLAG_PRIORITY      (1 << 7)  // Priority: 0 = No, 1 = BG and Window color indices 1–3 are drawn over this OBJ
#define GB_PPU_OAM_FLAG_Y_FLIP        (1 << 6)  // Y flip: 0 = Normal, 1 = Entire OBJ is vertically mirrored
#define GB_PPU_OAM_FLAG_X_FLIP        (1 << 5)  // X flip: 0 = Normal, 1 = Entire OBJ is horizontally mirrored
#define GB_PPU_OAM_FLAG_PALLETE       (1 << 4)  // DMG palette: 0 = OBP0, 1 = OBP1

#define GB_INTERRUPT_VBLANK           (0x01)    // VBlank
#define GB_INTERRUPT_STAT             (0x02)    // LCD STAT
#define GB_INTERRUPT_TIMER            (0x04)    // Timer
#define GB_INTERRUPT_SERIAL           (0x08)    // Serial
#define GB_INTERRUPT_JOYPAD           (0x10)    // Joypad

#define GB_HARDWARE_REGISTER_P1JOYP   (0xFF00)  // P1/Joypad
#define GB_HARDWARE_REGISTER_SB       (0xFF01)  // Serial transfer data
#define GB_HARDWARE_REGISTER_SC       (0xFF02)  // Serial transfer control
#define GB_HARDWARE_REGISTER_DIV      (0xFF04)  // Divider register
#define GB_HARDWARE_REGISTER_TIMA     (0xFF05)  // Timer counter
#define GB_HARDWARE_REGISTER_TMA      (0xFF06)  // Timer modulo
#define GB_HARDWARE_REGISTER_TAC      (0xFF07)  // Timer control
#define GB_HARDWARE_REGISTER_IF       (0xFF0F)  // Interrupt flag
#define GB_HARDWARE_REGISTER_NR10     (0xFF10)  // Sound channel 1 sweep
#define GB_HARDWARE_REGISTER_NR11     (0xFF11)  // Sound channel 1 length timer & duty cycle
#define GB_HARDWARE_REGISTER_NR12     (0xFF12)  // Sound channel 1 volume & envelope
#define GB_HARDWARE_REGISTER_NR13     (0xFF13)  // Sound channel 1 period low
#define GB_HARDWARE_REGISTER_NR14     (0xFF14)  // Sound channel 1 period high & control
#define GB_HARDWARE_REGISTER_NR21     (0xFF16)  // Sound channel 2 length timer & duty cycle
#define GB_HARDWARE_REGISTER_NR22     (0xFF17)  // Sound channel 2 volume & envelope
#define GB_HARDWARE_REGISTER_NR23     (0xFF18)  // Sound channel 2 period low
#define GB_HARDWARE_REGISTER_NR24     (0xFF19)  // Sound channel 2 period high & control
#define GB_HARDWARE_REGISTER_NR30     (0xFF1A)  // Sound channel 3 DAC enable
#define GB_HARDWARE_REGISTER_NR31     (0xFF1B)  // Sound channel 3 length timer
#define GB_HARDWARE_REGISTER_NR32     (0xFF1C)  // Sound channel 3 output level
#define GB_HARDWARE_REGISTER_NR33     (0xFF1D)  // Sound channel 3 period low
#define GB_HARDWARE_REGISTER_NR34     (0xFF1E)  // Sound channel 3 period high & control
#define GB_HARDWARE_REGISTER_NR41     (0xFF20)  // Sound channel 4 length timer
#define GB_HARDWARE_REGISTER_NR42     (0xFF21)  // Sound channel 4 volume & envelope
#define GB_HARDWARE_REGISTER_NR43     (0xFF22)  // Sound channel 4 frequency & randomness
#define GB_HARDWARE_REGISTER_NR44     (0xFF23)  // Sound channel 4 control
#define GB_HARDWARE_REGISTER_NR50     (0xFF24)  // Master volume & VIN panning
#define GB_HARDWARE_REGISTER_NR51     (0xFF25)  // Sound panning
#define GB_HARDWARE_REGISTER_NR52     (0xFF26)  // Sound on/off
#define GB_HARDWARE_REGISTER_LCDC     (0xFF40)  // LCD control
#define GB_HARDWARE_REGISTER_STAT     (0xFF41)  // LCD status
#define GB_HARDWARE_REGISTER_SCY      (0xFF42)  // Viewport Y position
#define GB_HARDWARE_REGISTER_SCX      (0xFF43)  // Viewport X position
#define GB_HARDWARE_REGISTER_LY       (0xFF44)  // LCD Y coordinate
#define GB_HARDWARE_REGISTER_LYC      (0xFF45)  // LY compare
#define GB_HARDWARE_REGISTER_DMA      (0xFF46)  // OAM DMA source address & start
#define GB_HARDWARE_REGISTER_BGP      (0xFF47)  // BG palette data
#define GB_HARDWARE_REGISTER_OBP0     (0xFF48)  // OBJ palette 0 data
#define GB_HARDWARE_REGISTER_OBP1     (0xFF49)  // OBJ palette 1 data
#define GB_HARDWARE_REGISTER_WY       (0xFF4A)  // Window Y position
#define GB_HARDWARE_REGISTER_WX       (0xFF4B)  // Window X position plus 7
#define GB_HARDWARE_REGISTER_KEY1     (0xFF4D)  // Prepare speed switch
#define GB_HARDWARE_REGISTER_BOOT     (0xFF50)  // Set to non-zero to disable boot ROM
#define GB_HARDWARE_REGISTER_IE       (0xFFFF)  // Interrupt enable

#define GB_MEMORY_IO_OFFSET(addr)     ((uint16_t)((addr) - 0xFF00))
#define GB_MEMORY_OAM_OFFSET(addr)    ((uint16_t)((addr) - 0xFE00))
#define GB_MEMORY_HRAM_OFFSET(addr)   ((uint16_t)((addr) - 0xFF80))
#define GB_MEMORY_VRAM_OFFSET(addr)   ((uint16_t)((addr) - 0x8000))
#define GB_MEMORY_WRAM_OFFSET(addr)   ((uint16_t)((addr) - 0xC000))
#define GB_MEMORY_ECHO_OFFSET(addr)   ((uint16_t)((addr) - 0xE000))

typedef enum {
  GB_SUCCESS = 0,

  /* General errors */
  GB_ERROR_UNKNOWN,
  GB_ERROR_INVALID_EMULATOR,
  GB_ERROR_INVALID_ARGUMENT,
  GB_ERROR_IO,

  /** Memory errors */
  GB_ERROR_OUT_OF_MEMORY,
  GB_ERROR_INVALID_MEMORY_ACCESS,

  /* CPU errors */
  GB_ERROR_ILLEGAL_OPCODE,

  GB_RESULT_MAX
} GB_result_t;

typedef struct {
  GB_result_t code;
  char message[GB_ERROR_MESSAGE_MAX_LENGTH];
  const char *file;
  uint32_t line;
} GB_error_t;

#define GB_FAILED(result) ((result) != GB_SUCCESS)
#define GB_ERROR(gb, code, fmt, ...) GB_emulator_set_error(gb, code, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define GB_TRY(expr) \
  do { \
    GB_result_t result = (expr); \
    if (GB_FAILED(result)) { return result; } \
  } while (0)

struct GB_emulator;
typedef struct GB_emulator GB_emulator_t;

