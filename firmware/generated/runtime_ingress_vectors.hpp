#pragma once

#include <array>

#include "open_radio/runtime_ingress.hpp"

namespace open_radio {
namespace generated {

inline constexpr std::array<GoldenIngressFact, 7> kRuntimeIngressFacts0{{
  {RuntimeProducer::Storage, 1U, 1U, 4294967280U, RuntimeEventKind::ConfigReady},
  {RuntimeProducer::LocalOutput, 1U, 1U, 4294967281U, RuntimeEventKind::LocalOutputAvailable},
  {RuntimeProducer::Wifi, 1U, 1U, 4294967282U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Resolver, 1U, 1U, 4294967283U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Stream, 1U, 1U, 5U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Bluetooth, 1U, 1U, 6U, RuntimeEventKind::BluetoothRemembered},
  {RuntimeProducer::Bluetooth, 1U, 2U, 7U, RuntimeEventKind::BluetoothConnected}
}};

inline constexpr std::array<GoldenIngressFact, 9> kRuntimeIngressFacts1{{
  {RuntimeProducer::Storage, 1U, 1U, 100U, RuntimeEventKind::ConfigReady},
  {RuntimeProducer::Wifi, 1U, 1U, 101U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Stream, 1U, 1U, 102U, RuntimeEventKind::StreamStalled},
  {RuntimeProducer::LocalOutput, 1U, 1U, 103U, RuntimeEventKind::LocalOutputAvailable},
  {RuntimeProducer::Resolver, 1U, 1U, 104U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Stream, 1U, 2U, 105U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Bluetooth, 1U, 1U, 106U, RuntimeEventKind::BluetoothRemembered},
  {RuntimeProducer::Bluetooth, 1U, 2U, 107U, RuntimeEventKind::BluetoothConnected},
  {RuntimeProducer::Bluetooth, 1U, 3U, 108U, RuntimeEventKind::BluetoothLost}
}};

inline constexpr std::array<GoldenIngressFact, 9> kRuntimeIngressFacts2{{
  {RuntimeProducer::Storage, 1U, 1U, 1000U, RuntimeEventKind::ConfigReady},
  {RuntimeProducer::LocalOutput, 1U, 1U, 1001U, RuntimeEventKind::LocalOutputAvailable},
  {RuntimeProducer::Wifi, 1U, 1U, 1002U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Resolver, 1U, 1U, 1003U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Stream, 1U, 1U, 1004U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Wifi, 2U, 1U, 1005U, RuntimeEventKind::WifiLost},
  {RuntimeProducer::Wifi, 1U, 2U, 1006U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Wifi, 2U, 2U, 1007U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Stream, 1U, 2U, 1008U, RuntimeEventKind::StreamHealthy}
}};

inline constexpr std::array<GoldenIngressFact, 18> kRuntimeIngressFacts3{{
  {RuntimeProducer::Power, 1U, 1U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 2U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 3U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 4U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 5U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 6U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 7U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 8U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 9U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 10U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 11U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 12U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 13U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 14U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 15U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 16U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 17U, 2000U, RuntimeEventKind::PowerInterrupted},
  {RuntimeProducer::Power, 1U, 18U, 2000U, RuntimeEventKind::PowerInterrupted}
}};

inline constexpr std::array<GoldenIngressFact, 7> kRuntimeIngressFacts4{{
  {RuntimeProducer::Storage, 1U, 1U, 100U, RuntimeEventKind::ConfigReady},
  {RuntimeProducer::LocalOutput, 1U, 1U, 101U, RuntimeEventKind::LocalOutputAvailable},
  {RuntimeProducer::Wifi, 1U, 1U, 102U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Resolver, 1U, 1U, 103U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Stream, 1U, 1U, 104U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Bluetooth, 1U, 1U, 105U, RuntimeEventKind::BluetoothConnected},
  {RuntimeProducer::Stream, 1U, 2U, 90U, RuntimeEventKind::StreamStalled}
}};

inline constexpr std::array<GoldenIngressFact, 6> kRuntimeIngressFacts5{{
  {RuntimeProducer::Wifi, 1U, 1U, 10U, RuntimeEventKind::BluetoothConnected},
  {RuntimeProducer::Storage, 1U, 1U, 11U, RuntimeEventKind::ConfigReady},
  {RuntimeProducer::LocalOutput, 1U, 1U, 12U, RuntimeEventKind::LocalOutputAvailable},
  {RuntimeProducer::Wifi, 1U, 1U, 13U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Resolver, 1U, 1U, 14U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Stream, 1U, 1U, 15U, RuntimeEventKind::StreamHealthy}
}};

inline constexpr std::array<GoldenIngressFact, 9> kRuntimeIngressFacts6{{
  {RuntimeProducer::Storage, 1U, 1U, 50U, RuntimeEventKind::ConfigReady},
  {RuntimeProducer::LocalOutput, 1U, 1U, 51U, RuntimeEventKind::LocalOutputAvailable},
  {RuntimeProducer::Wifi, 1U, 1U, 52U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Wifi, 1U, 1U, 53U, RuntimeEventKind::WifiLost},
  {RuntimeProducer::Wifi, 1U, 3U, 54U, RuntimeEventKind::WifiLost},
  {RuntimeProducer::Wifi, 1U, 2U, 55U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Wifi, 1U, 4U, 56U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Resolver, 1U, 1U, 57U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Stream, 1U, 1U, 58U, RuntimeEventKind::StreamHealthy}
}};

inline constexpr std::array<GoldenIngressFact, 11> kRuntimeIngressFacts7{{
  {RuntimeProducer::Storage, 1U, 1U, 500U, RuntimeEventKind::ConfigReady},
  {RuntimeProducer::Wifi, 1U, 1U, 500U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Bluetooth, 1U, 2U, 500U, RuntimeEventKind::BluetoothConnected},
  {RuntimeProducer::Resolver, 1U, 1U, 500U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::LocalOutput, 1U, 1U, 500U, RuntimeEventKind::LocalOutputAvailable},
  {RuntimeProducer::Bluetooth, 1U, 1U, 500U, RuntimeEventKind::BluetoothRemembered},
  {RuntimeProducer::Stream, 1U, 1U, 500U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Wifi, 1U, 2U, 501U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Resolver, 1U, 2U, 501U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Stream, 1U, 2U, 501U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Bluetooth, 1U, 3U, 501U, RuntimeEventKind::BluetoothConnected}
}};

inline constexpr std::array<GoldenIngressFact, 11> kRuntimeIngressFacts8{{
  {RuntimeProducer::Bluetooth, 1U, 1U, 500U, RuntimeEventKind::BluetoothRemembered},
  {RuntimeProducer::Wifi, 1U, 1U, 500U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::LocalOutput, 1U, 1U, 500U, RuntimeEventKind::LocalOutputAvailable},
  {RuntimeProducer::Stream, 1U, 1U, 500U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Bluetooth, 1U, 2U, 500U, RuntimeEventKind::BluetoothConnected},
  {RuntimeProducer::Storage, 1U, 1U, 500U, RuntimeEventKind::ConfigReady},
  {RuntimeProducer::Resolver, 1U, 1U, 500U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Wifi, 1U, 2U, 501U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Resolver, 1U, 2U, 501U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Stream, 1U, 2U, 501U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Bluetooth, 1U, 3U, 501U, RuntimeEventKind::BluetoothConnected}
}};

inline constexpr std::array<GoldenIngressFact, 11> kRuntimeIngressFacts9{{
  {RuntimeProducer::Stream, 1U, 1U, 500U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Wifi, 1U, 1U, 500U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Bluetooth, 1U, 1U, 500U, RuntimeEventKind::BluetoothRemembered},
  {RuntimeProducer::Resolver, 1U, 1U, 500U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Storage, 1U, 1U, 500U, RuntimeEventKind::ConfigReady},
  {RuntimeProducer::LocalOutput, 1U, 1U, 500U, RuntimeEventKind::LocalOutputAvailable},
  {RuntimeProducer::Bluetooth, 1U, 2U, 500U, RuntimeEventKind::BluetoothConnected},
  {RuntimeProducer::Wifi, 1U, 2U, 501U, RuntimeEventKind::WifiConnected},
  {RuntimeProducer::Resolver, 1U, 2U, 501U, RuntimeEventKind::ResolverReady},
  {RuntimeProducer::Stream, 1U, 2U, 501U, RuntimeEventKind::StreamHealthy},
  {RuntimeProducer::Bluetooth, 1U, 3U, 501U, RuntimeEventKind::BluetoothConnected}
}};

inline constexpr std::array<GoldenIngressTrace, 10> kRuntimeIngressTraces{{
  {"tick-rollover-boot", 0U, nullptr, kRuntimeIngressFacts0.data(), kRuntimeIngressFacts0.size(), {RuntimeState::Playing, OutputKind::Bluetooth, 7U, 7U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 7U, 0U, 7U, 0xe4129a21d63de787ULL}},
  {"delayed-stream-and-bluetooth-loss", 0U, nullptr, kRuntimeIngressFacts1.data(), kRuntimeIngressFacts1.size(), {RuntimeState::DegradedPlaying, OutputKind::LocalSpeaker, 9U, 9U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 9U, 0U, 9U, 0x1d71633871368196ULL}},
  {"producer-epoch-restart", 0U, nullptr, kRuntimeIngressFacts2.data(), kRuntimeIngressFacts2.size(), {RuntimeState::Playing, OutputKind::LocalSpeaker, 8U, 8U, 0U, 0U, 0U, 0U, 1U, 0U, 0U, 8U, 0U, 8U, 0xcb14ab0eaf66ce66ULL}},
  {"mailbox-saturation", 0U, nullptr, kRuntimeIngressFacts3.data(), kRuntimeIngressFacts3.size(), {RuntimeState::ConfigRequired, OutputKind::LocalSpeaker, 16U, 16U, 2U, 0U, 0U, 0U, 0U, 0U, 0U, 16U, 0U, 16U, 0x83593ebd993ef7e7ULL}},
  {"backward-callback-tick", 0U, nullptr, kRuntimeIngressFacts4.data(), kRuntimeIngressFacts4.size(), {RuntimeState::Playing, OutputKind::Bluetooth, 7U, 6U, 0U, 0U, 0U, 0U, 0U, 1U, 0U, 7U, 0U, 6U, 0x55795141c684dc77ULL}},
  {"invalid-producer-kind", 0U, nullptr, kRuntimeIngressFacts5.data(), kRuntimeIngressFacts5.size(), {RuntimeState::Playing, OutputKind::LocalSpeaker, 5U, 5U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 5U, 0U, 5U, 0xb410dc487cfa9c06ULL}},
  {"duplicate-and-stale-sequence", 0U, nullptr, kRuntimeIngressFacts6.data(), kRuntimeIngressFacts6.size(), {RuntimeState::Playing, OutputKind::LocalSpeaker, 7U, 7U, 0U, 0U, 1U, 1U, 0U, 0U, 0U, 7U, 0U, 7U, 0x258cb08eba6d5de7ULL}},
  {"boot-permutation-11", 11U, "healthy-boot", kRuntimeIngressFacts7.data(), kRuntimeIngressFacts7.size(), {RuntimeState::Playing, OutputKind::Bluetooth, 10U, 10U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 10U, 0U, 10U, 0x9b09db1d03de4d07ULL}},
  {"boot-permutation-29", 29U, "healthy-boot", kRuntimeIngressFacts8.data(), kRuntimeIngressFacts8.size(), {RuntimeState::Playing, OutputKind::Bluetooth, 11U, 11U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 0U, 11U, 0x8a862b1c633f73c6ULL}},
  {"boot-permutation-47", 47U, "healthy-boot", kRuntimeIngressFacts9.data(), kRuntimeIngressFacts9.size(), {RuntimeState::Playing, OutputKind::Bluetooth, 11U, 11U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 0U, 11U, 0x8a862b1c633f73c6ULL}}
}};

}
}
