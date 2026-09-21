#pragma once

#include <cstddef>
#include <cstdint>

namespace open_radio {
namespace generated {

inline constexpr std::size_t kRuntimeEventQueueCapacity = 16;
inline constexpr std::size_t kRuntimeTimerCapacity = 8;
inline constexpr std::size_t kRuntimeDiagnosticCapacity = 32;
inline constexpr std::uint64_t kRuntimeMaximumRetryDelayMs = 600000ULL;

}
}
