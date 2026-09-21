#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace open_radio {

// ADR-016, docs/124 T3. What an upgrade keeps and what it throws away.
//
// Until now a new build erased NVS wholesale. The rationale was real — two
// incidents where stale state from a previous release resurrected — but both
// were DERIVED state, and the code names one of them: a last-known-good
// endpoint. The erase paid with the owner's Wi-Fi, speaker, stations, language
// and display settings to kill a bug that lives entirely in the cache.
//
// This header is the classification, host-portable and testable without a
// device. It is deliberately allowlist-shaped: a key nobody has classified is
// erased, so forgetting to classify something is safe rather than silently
// preserving state whose staleness nobody reasoned about.

enum class UpgradeAction : std::uint8_t {
  // The owner chose it. Carry it forward.
  Preserve,
  // The device computed it. Recompute it.
  Erase
};

// Reasons a full factory reset still wins over any classification.
enum class ResetReason : std::uint8_t {
  None,
  // Stored schema is newer than this firmware understands. Downgrade, or a
  // record from a branch that never shipped.
  SchemaFromTheFuture,
  // Older than the floor this firmware carries migrations for.
  SchemaBelowFloor,
  // Stored bytes did not survive their own integrity check.
  Corrupt,
  // The owner asked for the customer first-run path explicitly.
  OwnerRequested
};

struct UpgradeDecision {
  bool fullReset = false;
  ResetReason reason = ResetReason::None;
};

// Configuration schema the firmware writes, and the oldest it can carry
// forward. Raising the floor is a deliberate act: it strands every cube that
// skipped the releases in between.
inline constexpr std::uint8_t kConfigSchemaCurrent = 2;
inline constexpr std::uint8_t kConfigSchemaFloor = 1;

inline UpgradeDecision decideUpgrade(std::uint8_t storedSchema, bool storedIntact,
                                     bool ownerRequestedFactoryPath) {
  if (ownerRequestedFactoryPath) return {true, ResetReason::OwnerRequested};
  if (!storedIntact) return {true, ResetReason::Corrupt};
  if (storedSchema > kConfigSchemaCurrent) return {true, ResetReason::SchemaFromTheFuture};
  if (storedSchema < kConfigSchemaFloor) return {true, ResetReason::SchemaBelowFloor};
  return {false, ResetReason::None};
}

// Keys the owner chose, spelled out. Anything absent from this list is derived.
inline constexpr const char* kPreservedKeys[] = {
  // The A/B configuration record: locale, volume, brightness, theme,
  // home-screen variant, screensaver. The single most valuable thing in
  // NVS, and the first draft of this list did not contain it — enumerating the
  // real key set is what caught that, not review.
  "config_a",
  "config_b",
  // The remembered networks, which this comment used to claim were inside the
  // A/B record. They are not: device_network_runtime.hpp writes them under
  // their own key. Absent from this list they classified as derived — that is,
  // as something an upgrade may erase — and they survived only because the
  // runtime deletion loop enumerates lkg4_* explicitly instead of following
  // this classification. Anyone who ever made the loop obey the contract, as
  // the comments around it invite, would have wiped every owner's Wi-Fi.
  "wifi_profiles",
  "slots1",       // station slots, format 1 — read for migration
  "slots2",       // station slots, format 2 (index + stable id)
  "slotsv",       // slot format version
  "bt_addr",      // remembered speaker
  "bt_name",
  "noise_color",
  "play_mode",
  "build_sha"     // bookkeeping: the comparison that detects the upgrade at all
};

// Prefixes whose whole family is derived, however many keys it spawns.
inline constexpr const char* kDerivedKeyPrefixes[] = {
  "lkg"           // last-known-good endpoints, keyed by directory index and
                  // therefore stale both across releases and across a catalog
                  // refresh within one (ADR-016)
};

inline bool hasPrefix(const char* key, const char* prefix) {
  return std::strncmp(key, prefix, std::strlen(prefix)) == 0;
}

inline UpgradeAction classifyKey(const char* key) {
  if (key == nullptr || key[0] == '\0') return UpgradeAction::Erase;
  for (const char* prefix : kDerivedKeyPrefixes) {
    if (hasPrefix(key, prefix)) return UpgradeAction::Erase;
  }
  for (const char* preserved : kPreservedKeys) {
    if (std::strcmp(key, preserved) == 0) return UpgradeAction::Preserve;
  }
  // Unclassified is derived. Forgetting to add a key here costs a recomputation;
  // forgetting the other way round would preserve state nobody reasoned about.
  return UpgradeAction::Erase;
}

}  // namespace open_radio
