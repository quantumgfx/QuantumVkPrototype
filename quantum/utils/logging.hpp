#pragma once

#include <cstdio>

#define QM_LOG_INFO(x, ...) std::printf(x, __VA_ARGS__)
#define QM_LOG_WARN(x, ...)  std::printf(x, __VA_ARGS__)
#define QM_LOG_ERROR(x, ...)  std::printf(x, __VA_ARGS__)