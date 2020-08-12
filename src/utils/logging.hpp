#pragma once

#include <cstdio>

#define QM_LOG_INFO(x, ...) do { std::printf("\033[0;32m"); std::printf(x, __VA_ARGS__); std::printf("\033[0m"); } while(0)
#define QM_LOG_WARN(x, ...)  do { std::printf("\033[0;33m"); std::printf(x, __VA_ARGS__); std::printf("\033[0m"); } while(0)
#define QM_LOG_ERROR(x, ...) do { std::printf("\033[0;31m"); std::printf(x, __VA_ARGS__); std::printf("\033[0m"); } while(0)