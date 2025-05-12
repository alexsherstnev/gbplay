#include "log.h"
#include <time.h>

time_t g_current_time;
struct tm *g_time; 

static const char *type[LOG_LEVEL_COUNT] = {
  "DEBUG",
  "INFO",
  "WARNING",
  "ERROR"
};

static const char *colors[LOG_LEVEL_COUNT] = {
  "\x1b[0m",
  "\x1b[32m",
  "\x1b[1;33m",
  "\x1b[31m"
};

static void get_time() {
  time(&g_current_time);
  g_time = localtime(&g_current_time);
}

void logger(log_level_t level, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  get_time();

  printf("%s", colors[level]);
  printf("[%d/%d/%d -> %d:%d:%d][%s] ", g_time -> tm_mday,
                                        g_time -> tm_mon,
                                        g_time -> tm_year + 1900,
                                        g_time -> tm_hour,
                                        g_time -> tm_min, 
                                        g_time -> tm_sec,
                                        type[level]); 
  vfprintf(stdout, fmt, args);
  printf("\n%s", colors[LOG_LEVEL_DEBUG]);

  va_end(args);
}
