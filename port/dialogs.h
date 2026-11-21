#pragma once
#include <stddef.h>

int nx_choose(const char *title, const char *message, const char *const *choices,
              size_t count, size_t initial, int can_cancel);
