#pragma once

#include <cstdint>
#include <string_view>

namespace sidera {

enum class RenderPath : uint8_t {
    Forward = 0,
    Deferred = 1,
    ForwardPlus = 2
};

[[nodiscard]] constexpr std::string_view getRenderPathName(RenderPath path) noexcept {
    switch (path) {
        case RenderPath::Forward:     return "Forward";
        case RenderPath::Deferred:    return "Deferred";
        case RenderPath::ForwardPlus: return "Forward+";
    }
    return "Unknown";
}

} // namespace sidera
