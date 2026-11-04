#pragma once

// Minimal check macros shared by the core tests.

#include <cmath>
#include <cstdio>

namespace swm::test {

inline int failures = 0;

inline bool near(double a, double b, double tolerance = 1e-6) {
    return std::fabs(a - b) <= tolerance;
}

inline int finish(const char* name) {
    if (failures == 0) {
        std::printf("%s: all passed\n", name);
    }
    return failures == 0 ? 0 : 1;
}

} // namespace swm::test

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++swm::test::failures;                                     \
        }                                                              \
    } while (0)
