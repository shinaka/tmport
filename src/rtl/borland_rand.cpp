#include "rtl/borland_rand.h"

namespace tmport {

int BorlandRand::random(int n) {
    // 32-bit IMUL wraps; IDIV truncates toward zero (same as C++ integer division).
    const auto product = static_cast<int32_t>(static_cast<uint32_t>(rand()) * static_cast<uint32_t>(n));
    return product / 32768;
}

}  // namespace tmport
