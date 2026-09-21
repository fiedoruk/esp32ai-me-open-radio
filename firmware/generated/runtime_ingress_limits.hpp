#pragma once

#include <cstddef>

namespace open_radio {
namespace generated {

inline constexpr std::size_t kRuntimeIngressMailboxCapacity = 16;
inline constexpr std::size_t kRuntimeIngressProducerCount = 7;
inline constexpr std::size_t kRuntimeIngressRawTickBits = 32;
inline constexpr std::size_t kRuntimeIngressMaximumDrainPerPass = 16;

}
}
