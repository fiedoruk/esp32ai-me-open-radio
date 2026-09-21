#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace open_radio {

// T2 of docs/124. A saved tile currently stores nothing but an index into the
// compiled directory. That was safe while the directory never changed; it is
// not safe now that a catalog refresh reorders it — the stored bytes stay
// valid, the index stays in range, and the tile silently starts playing a
// different station. Nothing errors, nothing logs, and the owner finds out by
// listening.
//
// This is the host-portable half of the fix: given what was stored and the
// directory as it is today, decide what the slot should point at. No Arduino,
// no Preferences, no allocation, no search unless the fast path fails.
//
// The rule the whole file exists to enforce: when identity cannot be proved,
// fall back to the factory station. A cube playing its factory nine is a
// working radio. A cube playing a station the owner never chose is a defect
// that looks like a working radio, which is worse.

// The directory as the resolver needs to see it: stable ids, in directory
// order. The device passes the generated table; a test passes a literal array.
struct DirectoryView {
  const char* const* ids = nullptr;
  std::size_t count = 0;

  const char* id(std::size_t index) const {
    return index < count && ids != nullptr ? ids[index] : nullptr;
  }
};

enum class SlotBinding : std::uint8_t {
  // The slot keeps the station the firmware shipped with.
  Factory,
  // Stored id still sits at the stored index. The common case, and free.
  Kept,
  // Stored id moved: the directory was reordered under a saved slot. Resolved
  // to where the station lives now.
  Repointed,
  // Stored id is no longer in the directory. Deliberately dropped to factory
  // rather than guessed at.
  Lost,
  // A record written before ids existed. The index is honoured because there is
  // nothing better to do with it, but identity is unproven and the caller
  // should say so rather than pretend the slot is confirmed.
  LegacyIndex
};

struct SlotResolution {
  SlotBinding binding = SlotBinding::Factory;
  // Directory index to play, or kFactorySlot when the factory station wins.
  std::int16_t index = -1;
};

inline constexpr std::int16_t kFactorySlot = -1;

// storedId may be null or empty, which means the record predates stable ids.
// Longest id the caller may hand over. A stored buffer that is full to the last
// byte has no terminator, and comparing it would run off the end.
inline constexpr std::size_t kStoredIdBytesMax = 48;

inline bool terminatedWithin(const char* value, std::size_t limit) {
  if (value == nullptr) return false;
  for (std::size_t index = 0; index < limit; ++index) {
    if (value[index] == '\0') return true;
  }
  return false;
}

inline SlotResolution resolveSlot(std::int16_t storedIndex, const char* storedId,
                                  const DirectoryView& directory) {
  // A stored id that is not NUL-terminated inside its own buffer is corrupt.
  // Refusing it is the difference between a factory tile and undefined
  // behaviour in strcmp.
  const bool usableId = storedId != nullptr && storedId[0] != '\0' &&
                        terminatedWithin(storedId, kStoredIdBytesMax);

  // Any negative value other than the one sentinel is corrupt, and corrupt
  // input does not get to drive a search.
  if (storedIndex < 0 && storedIndex != kFactorySlot) {
    return {SlotBinding::Factory, kFactorySlot};
  }
  if (storedIndex == kFactorySlot) {
    // A slot the previous boot could not resolve keeps its id. If the station
    // came back to the directory, recover it instead of leaving the owner's
    // choice deleted for good.
    if (!usableId) return {SlotBinding::Factory, kFactorySlot};
    for (std::size_t candidate = 0; candidate < directory.count; ++candidate) {
      const char* id = directory.id(candidate);
      if (id != nullptr && std::strcmp(id, storedId) == 0) {
        return {SlotBinding::Repointed, static_cast<std::int16_t>(candidate)};
      }
    }
    return {SlotBinding::Factory, kFactorySlot};
  }

  const auto index = static_cast<std::size_t>(storedIndex);
  const bool hasId = usableId;

  if (!hasId) {
    // Pre-migration record. Honour an in-range index, refuse an out-of-range
    // one: a stale index into a shrunken directory is not a station.
    if (index >= directory.count) return {SlotBinding::Factory, kFactorySlot};
    return {SlotBinding::LegacyIndex, storedIndex};
  }

  const char* atIndex = directory.id(index);
  if (atIndex != nullptr && std::strcmp(atIndex, storedId) == 0) {
    return {SlotBinding::Kept, storedIndex};
  }

  // The station moved, or the directory shrank. Find it by identity. The
  // directory is a few hundred entries and this runs once per slot at boot, so
  // a linear scan is the right amount of machinery.
  for (std::size_t candidate = 0; candidate < directory.count; ++candidate) {
    const char* id = directory.id(candidate);
    if (id != nullptr && std::strcmp(id, storedId) == 0) {
      return {SlotBinding::Repointed, static_cast<std::int16_t>(candidate)};
    }
  }
  return {SlotBinding::Lost, kFactorySlot};
}

// True when the resolution should be written back, so the next boot takes the
// fast path instead of scanning again.
//
// Deliberately NOT true for Lost. Writing a lost slot back would replace the
// owner's id with an empty one, which deletes the only evidence of what they
// chose — and a station that left the directory in one refresh can return in
// the next. Lost costs one scan per boot and keeps the choice recoverable.
inline bool resolutionNeedsRewrite(const SlotResolution& resolution) {
  return resolution.binding == SlotBinding::Repointed;
}

}  // namespace open_radio
