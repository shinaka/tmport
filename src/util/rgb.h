#pragma once

#include <cstdint>

namespace tmport {

struct Rgb8 {
    uint8_t r = 0, g = 0, b = 0;
    friend bool operator==(const Rgb8&, const Rgb8&) = default;
};

}  // namespace tmport
