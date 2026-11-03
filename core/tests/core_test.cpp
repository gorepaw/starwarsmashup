#include <cstdio>

#include "swm/core/build_info.h"

int main() {
    if (swm::core::version().empty()) {
        std::printf("FAIL: empty version\n");
        return 1;
    }
    std::printf("core_test: all passed (version %.*s)\n", int(swm::core::version().size()),
                swm::core::version().data());
    return 0;
}
