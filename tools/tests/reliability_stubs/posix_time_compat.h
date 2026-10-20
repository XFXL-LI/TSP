#pragma once
#include <time.h>

// The firmware uses POSIX localtime_r. Windows native tests are single-threaded;
// provide only this compile-time compatibility shim, never a firmware override.
#if defined(_WIN32)
inline struct tm *localtime_r(const time_t *value, struct tm *result) {
    const struct tm *converted = localtime(value);
    if (converted == nullptr) return nullptr;
    *result = *converted;
    return result;
}
#endif
