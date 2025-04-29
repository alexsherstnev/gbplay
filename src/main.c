#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <time.h>
#include "gb/gb.h"

#define WINDOW_TITLE      ("GBPlay")
#define WINDOW_WIDTH      (GB_SCREEN_WIDTH * 2)
#define WINDOW_HEIGHT     (GB_SCREEN_HEIGHT * 2)
#define TARGET_FPS        (59.73)
#define TARGET_FRAME_TIME (1000.0 / TARGET_FPS)

SDL_Window    *g_window   = NULL;
SDL_Renderer  *g_renderer = NULL;
SDL_Texture   *g_frame    = NULL;
GB_emulator_t  g_emulator;
uint32_t       g_framebuffer[GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT];
uint32_t       g_gb_lcd_2_rgb_palette[4];

double get_current_time_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void precise_sleep(double ms) {
  static double sleep_threshold = 2.0;
  const double target_time = get_current_time_ms() + ms;
  double remaining;
  while ((remaining = target_time - get_current_time_ms()) > 0) {
    if (remaining > sleep_threshold) {
      struct timespec ts = {
        .tv_sec = (time_t)(remaining * 0.7 / 1000),
        .tv_nsec = (long)(fmod(remaining * 0.7, 1000.0) * 1e6)
      };
      clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);

      // Calibrate sleep threshold
      double oversleep = get_current_time_ms() - (target_time - remaining);
      if (oversleep > 0.5) { sleep_threshold *= 0.9; }
    } else {
      struct timespec ts = { .tv_nsec = (long)(remaining * 1e6) };
      clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
    }
  }
}

uint8_t gamma_correction(uint8_t color, double gamma) {
  return (uint8_t)(255.0 * pow(color / 255.0, 1.0 / gamma));
}

void handle_input(GB_emulator_t *gb) {
  if (!gb || !gb->memory.io) { return; }

  const uint8_t p1 = gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_P1JOYP)];
  uint8_t joy = 0x0F;  // All buttons released (active low)
  const bool select_d_pad   = !(p1 & (1 << 4));
  const bool select_buttons = !(p1 & (1 << 5));
  if (select_d_pad || select_buttons) {
    const bool *keyboard = SDL_GetKeyboardState(NULL);

    if (select_buttons) {
      if (keyboard[SDL_SCANCODE_RETURN]) { joy &= ~(1 << 3); }  // Start
      if (keyboard[SDL_SCANCODE_RSHIFT]) { joy &= ~(1 << 2); }  // Select
      if (keyboard[SDL_SCANCODE_Z])      { joy &= ~(1 << 1); }  // B
      if (keyboard[SDL_SCANCODE_X])      { joy &= ~(1 << 0); }  // A
    }

    if (select_d_pad) {
      if (keyboard[SDL_SCANCODE_DOWN])   { joy &= ~(1 << 3); }  // Down
      if (keyboard[SDL_SCANCODE_UP])     { joy &= ~(1 << 2); }  // Up
      if (keyboard[SDL_SCANCODE_LEFT])   { joy &= ~(1 << 1); }  // Left
      if (keyboard[SDL_SCANCODE_RIGHT])  { joy &= ~(1 << 0); }  // Right
    }
  }

  gb->memory.io[GB_MEMORY_IO_OFFSET(GB_HARDWARE_REGISTER_P1JOYP)] = (p1 & 0xF0) | (joy & 0x0F);

  // Trigger interrupt
  if ((joy & 0x0F) != 0x0F) {
    GB_interrupt_request(gb, GB_INTERRUPT_JOYPAD);
  }
}

void render_frame() {
  if (!g_renderer || !g_frame) { return; }

  for (uint32_t i = 0; i < GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT; ++i) {
    const uint8_t color = g_emulator.ppu.framebuffer[i];
    g_framebuffer[i] = g_gb_lcd_2_rgb_palette[color & 0b11];
  }

  SDL_UpdateTexture(g_frame, NULL, g_framebuffer, GB_SCREEN_WIDTH * sizeof(uint32_t));
  SDL_RenderClear(g_renderer);
  SDL_RenderTexture(g_renderer, g_frame, NULL, NULL);
  SDL_RenderPresent(g_renderer);
}

void init_gb_lcd_2_rgb_palette() {
  const double gamma = 2.2;
  uint8_t rgb_palette[] = {
    224, 248, 207,  // White
    136, 192, 112,  // Light gray
    52,  104, 86 ,  // Dark gray
    8,   24,  32    // Black
  };

  for (uint8_t i = 0; i < 4; i++) {
    uint8_t r, g, b;
    r = rgb_palette[i * 3 + 0];
    g = rgb_palette[i * 3 + 1];
    b = rgb_palette[i * 3 + 2];

    // Apply gamma & color grading
    r = gamma_correction(r, gamma);
    g = gamma_correction(g, gamma) * 0.9;
    b = gamma_correction(b, gamma) * 0.7;

    // Cache palette          = R           G           B          A
    g_gb_lcd_2_rgb_palette[i] = (r << 24) | (g << 16) | (b << 8) | 0xFF;
  }
}

bool init(int argc, char *argv[]) {
  // Base SDL initialization
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { return false; }

  // Main window
  g_window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!g_window) { return false; }

  // Default renderer
  g_renderer = SDL_CreateRenderer(g_window, NULL);
  if (!g_renderer) { return false; }

  // Frame with nearest filtration
  g_frame = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT);
  if (!g_frame) { return false; }
  SDL_SetTextureScaleMode(g_frame, SDL_SCALEMODE_NEAREST);

  // Pallete
  init_gb_lcd_2_rgb_palette();

  // Emulator
  if (GB_FAILED(GB_emulator_init(&g_emulator))) { return false; }

  // Load ROM
  if (argc > 1) {
    if (GB_FAILED(GB_emulator_load_rom(&g_emulator, argv[1]))) {
      return false;
    }
  }

  return true;
}

void main_loop() {
  bool running = true;
  SDL_Event event;
  double next_frame_time = get_current_time_ms();
  while (running) {
    // Host event
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
        break;
      }
    }

    // Simulation
    for (uint32_t t_cycle = 0; t_cycle < GB_CYCLES_PER_FRAME; ++t_cycle) {
      handle_input(&g_emulator);
      if (GB_FAILED(GB_emulator_tick(&g_emulator))) {
        running = false;
        break;
      }
    }

    // Host render
    render_frame();

    // VSync
    const double current_time = get_current_time_ms();
    if (next_frame_time > current_time) {
      precise_sleep(next_frame_time - current_time);
    } else {
      // Huge delay - re-sync
      next_frame_time = current_time;
    }
    next_frame_time += TARGET_FRAME_TIME;
  }
}

void destroy() {
  GB_emulator_free(&g_emulator);

  if (g_frame) {
    SDL_DestroyTexture(g_frame);
    g_frame = NULL;
  }

  if (g_renderer) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = NULL;
  }

  if (g_window) {
    SDL_DestroyWindow(g_window);
    g_window = NULL;
  }

  SDL_Quit();
}

int main(int argc, char *argv[]) {
  if (init(argc, argv)) {
    main_loop();
  }
  destroy();

  return 0;
}

