#include <cstdio>

#include "swbfo/core/build_info.h"

int main() {
    if (swbfo::core::version().empty()) {
        std::printf("FAIL: empty version\n");
        return 1;
    }
    std::printf("core_test: all passed (version %.*s)\n", int(swbfo::core::version().size()),
                swbfo::core::version().data());
    return 0;
}
