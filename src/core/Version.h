#pragma once

#include <cstdint>
#include <string_view>

namespace sidera {

struct EngineVersion {
    uint32_t major = 1;
    uint32_t minor = 0;
    uint32_t patch = 0;
    std::string_view prerelease = "rc1";
};

inline constexpr uint32_t kSideraVersionMajor = 1;
inline constexpr uint32_t kSideraVersionMinor = 0;
inline constexpr uint32_t kSideraVersionPatch = 0;
inline constexpr std::string_view kSideraVersionPrerelease = "rc1";
inline constexpr std::string_view kSideraVersionString = "1.0.0-rc1";
inline constexpr std::string_view kSideraEngineBanner = "SIDERA Engine 1.0.0-rc1";

[[nodiscard]] constexpr EngineVersion getEngineVersion() noexcept {
    return EngineVersion{
        kSideraVersionMajor,
        kSideraVersionMinor,
        kSideraVersionPatch,
        kSideraVersionPrerelease
    };
}

} // namespace sidera
