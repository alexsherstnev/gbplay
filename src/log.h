#pragma once

#include <stdio.h>
#include <stdarg.h>

typedef enum {
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_COUNT
} log_level_t;

void logger(log_level_t level, const char *fmt, ...);

#define LOG_DEBUG(...) logger(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) logger(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARNING(...) logger(LOG_LEVEL_WARNING, __VA_ARGS__)
#define LOG_ERROR(...) logger(LOG_LEVEL_ERROR, __VA_ARGS__)

