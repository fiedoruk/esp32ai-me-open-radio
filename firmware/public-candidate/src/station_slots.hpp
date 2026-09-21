#pragma once

#include <Preferences.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>

#include "open_radio/station_binding.hpp"
#include "station_catalog.hpp"

namespace open_radio::public_candidate {

// Which station sits in each of the nine tiles.
//
// The nine the firmware ships with are a starting point, not a verdict: the
// only complaint 0.1 ever drew was "my station is missing". An installer picks
// replacements from the embedded directory while standing on the device's own
// access point, where there is no internet, which is exactly why the directory
// is compiled in rather than fetched.
//
// A slot holds an index into that directory, or kBuiltIn to mean "leave the
// factory station here".
//
// Format 2 (docs/124 T2) stores the station's stable id next to the index.
// Format 1 stored the index alone, which was safe only while the directory
// never changed: it is sorted partly by vote count, so a catalog refresh
// reorders it, the stored index stays in range, and the tile quietly starts
// playing a different station. Nothing errors and nothing logs. The id turns
// that silent substitution into a repoint, and an unresolvable id into the
// factory station rather than a guess.
class StationSlots {
 public:
  static constexpr std::size_t kSlotCount = 9;
  static constexpr std::int16_t kBuiltIn = open_radio::kFactorySlot;
  // Bumped whenever the stored layout changes. Format 1 is migrated rather
  // than discarded — it still carries the owner's choices, just without proof
  // of identity. Anything else unknown is treated as absent: the worst case is
  // a cube that plays its factory nine, which is a working radio.
  static constexpr std::uint8_t kFormatVersion = 2;
  static constexpr std::uint8_t kLegacyFormatVersion = 1;
  // Pinned, NOT derived from the catalog. Deriving it would make sizeof(Record)
  // move the day a broadcaster with a longer id enters the directory, and every
  // stored record would silently fail its length check and read as absent —
  // a catalog refresh quietly wiping everyone's tiles.
  static constexpr std::size_t kIdBytes = open_radio::kStoredIdBytesMax;
  static_assert(open_radio::generated::kDirectoryIdBytesMax < kIdBytes,
                "a directory id no longer fits the pinned stored width; widening it "
                "is a stored-format change and needs a version bump, not a constant edit");

  // Non-zero when the last load had to heal a slot: the catalog moved under a
  // saved tile, or a saved station left the directory. The caller logs it, so
  // a silent substitution becomes a visible event.
  std::size_t repointed() const { return repointed_; }
  std::size_t lost() const { return lost_; }
  bool migratedFromLegacy() const { return migratedFromLegacy_; }

  void load(Preferences& preferences) {
    reset();
    // Version first, then the record it names. Reading a key before knowing
    // which format wrote it is how a format change quietly loses everyone's
    // slots — the version is the only field whose meaning never moves.
    const std::uint8_t version = preferences.getUChar(kVersionKey, 0);
    if (version != kFormatVersion && version != kLegacyFormatVersion) return;

    std::array<std::int16_t, kSlotCount> stored{};
    IdTable storedIds{};
    if (version == kFormatVersion) {
      // Format 2 keeps indices and ids in ONE blob. Two blobs would be two NVS
      // writes: lose power between them and the record holds new indices beside
      // the previous ids, which resolves the tiles straight back to the stations
      // the owner just replaced. A single record cannot be half-applied.
      Record record{};
      if (preferences.getBytes(kRecordKey, &record, sizeof(record)) != sizeof(record)) return;
      stored = record.indices;
      storedIds = record.ids;
    } else {
      if (preferences.getBytes(kKey, stored.data(), sizeof(stored)) != sizeof(stored)) return;
      migratedFromLegacy_ = true;
    }

    const open_radio::DirectoryView directory{open_radio::generated::kDirectoryIds,
                                              open_radio::generated::kDirectoryCount};
    // A format-1 record holds a bare index into the directory 0.2.1 shipped.
    // Translate it through that pinned order BEFORE resolving, so the tile is
    // carried across by identity. Honouring the raw index against today's
    // directory instead would be the exact silent substitution format 2 exists
    // to end: the directory is sorted by vote count, so one popular station
    // added at the top shifts everything below it and every upgraded cube
    // quietly plays something its owner never chose. An index the pinned order
    // cannot explain is corrupt, and corrupt falls back to the factory station
    // rather than to a guess.
    if (migratedFromLegacy_) {
      for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (stored[slot] == kBuiltIn) continue;
        const char* legacyId = legacyIdAt(stored[slot]);
        if (legacyId == nullptr) {
          stored[slot] = kBuiltIn;
          continue;
        }
        std::strncpy(storedIds[slot].data(), legacyId, kIdBytes - 1);
      }
    }
    bool healed = migratedFromLegacy_;
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
      const char* id = storedIds[slot].data();
      const auto resolution = open_radio::resolveSlot(stored[slot], id, directory);
      if (resolution.binding == open_radio::SlotBinding::Repointed) ++repointed_;
      if (resolution.binding == open_radio::SlotBinding::Lost) ++lost_;
      if (open_radio::resolutionNeedsRewrite(resolution)) healed = true;
      stored[slot] = resolution.index;
      apply(slot, resolution.index);
    }
    // Persist the healed layout so the next boot takes the fast path, and so a
    // legacy record gains the identity it never had. Ids already stored are
    // carried through: a slot whose station left the directory keeps the name
    // of what the owner chose, so it recovers if that station comes back.
    if (healed) persist(preferences, stored, storedIds);
  }

  bool store(Preferences& preferences, const std::int16_t* assignments) {
    std::array<std::int16_t, kSlotCount> next{};
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
      const std::int16_t value = assignments[slot];
      if (value != kBuiltIn &&
          (value < 0 || static_cast<std::size_t>(value) >= open_radio::generated::kDirectoryCount)) {
        return false;
      }
      next[slot] = value;
    }
    // Record WHICH station each index meant, at the moment the owner chose it.
    // Writing the indices alone was the same defect as the format-1 migration,
    // hiding in the ordinary path: a choice made in the console reached format 2
    // carrying no identity, so the next catalog refresh moved it exactly as
    // format 1 would have. The console offers the current directory, so the
    // current id at that index is precisely what was picked.
    IdTable chosen{};
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
      if (next[slot] == kBuiltIn) continue;
      const char* id = open_radio::generated::kDirectoryIds[next[slot]];
      if (id == nullptr || id[0] == '\0' || std::strlen(id) >= kIdBytes) continue;
      std::strncpy(chosen[slot].data(), id, kIdBytes - 1);
    }
    if (!persist(preferences, next, chosen)) return false;
    reset();
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) apply(slot, next[slot]);
    return true;
  }

  std::int16_t directoryIndex(std::size_t slot) const {
    return slot < kSlotCount ? assigned_[slot] : kBuiltIn;
  }

  bool overridden(std::size_t slot) const { return directoryIndex(slot) != kBuiltIn; }

  // How many verified edges this slot can rotate through. Zero means the slot
  // still holds its factory station and the catalog's own candidates apply.
  std::size_t endpointCount(std::size_t slot) const {
    const std::int16_t index = directoryIndex(slot);
    return index == kBuiltIn ? 0 : open_radio::generated::kDirectory[index].endpointCount;
  }

  // The nth alternate, wrapping. A directory station rotates exactly like a
  // curated one: the same wheel, the same recovery from a dead edge. Which
  // list a station came from must not decide how well it survives.
  const char* url(std::size_t slot, std::size_t candidate) const {
    const std::size_t count = endpointCount(slot);
    if (count == 0) return nullptr;
    const auto& station = open_radio::generated::kDirectory[directoryIndex(slot)];
    return open_radio::generated::kDirectoryEndpoints[station.endpointOffset + candidate % count].url;
  }

  const char* name(std::size_t slot) const {
    if (!overridden(slot)) return open_radio::generated::kStations[slot].displayName;
    return names_[slot].data();
  }

  const char* shortName(std::size_t slot) const {
    if (!overridden(slot)) return nullptr;
    return shorts_[slot].data();
  }

 private:
  // What a 0.2.1 index meant, from the pinned order that release shipped.
  // Null for anything that cannot be interpreted — out of that order, or an id
  // too wide for the stored field. Both are answered with the factory station,
  // because the one thing worse than losing a tile is confidently restoring the
  // wrong one.
  static const char* legacyIdAt(std::int16_t index) {
    if (index < 0) return nullptr;
    const auto position = static_cast<std::size_t>(index);
    if (position >= open_radio::generated::kLegacyDirectoryCount) return nullptr;
    const char* id = open_radio::generated::kLegacyDirectoryIds[position];
    if (id == nullptr || id[0] == '\0') return nullptr;
    if (std::strlen(id) >= kIdBytes) return nullptr;
    return id;
  }

  // Format 1 read only, for migration. Format 2 lives in its own key so a
  // downgrade cannot read a v2 record as if it were a bare index array.
  static constexpr const char* kKey = "slots1";
  static constexpr const char* kRecordKey = "slots2";
  static constexpr const char* kVersionKey = "slotsv";
  static constexpr std::size_t kNameBytes = 39;
  static constexpr std::size_t kShortBytes = 8;

  using IdTable = std::array<std::array<char, kIdBytes>, kSlotCount>;

  // Indices and ids travel together or not at all.
  struct Record {
    std::array<std::int16_t, kSlotCount> indices{};
    IdTable ids{};
  };

  // One write path for both the console's assignment and the healed load, so
  // the two can never drift into writing different layouts.
  bool persist(Preferences& preferences, const std::array<std::int16_t, kSlotCount>& next,
               const IdTable& carried) {
    Record record{};
    record.indices = next;
    record.ids = carried;
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
      const std::int16_t index = next[slot];
      if (index == kBuiltIn) continue;
      record.ids[slot].fill('\0');
      const char* id = open_radio::generated::kDirectoryIds[index];
      // An id that does not fit cannot be matched later, so refusing to store
      // it is better than storing a truncated one that silently never matches.
      if (std::strlen(id) >= kIdBytes) return false;
      std::strncpy(record.ids[slot].data(), id, kIdBytes - 1);
    }
    if (preferences.putBytes(kRecordKey, &record, sizeof(record)) != sizeof(record)) return false;
    // Version last: a torn write leaves the previous version in place, so the
    // next boot re-runs the migration instead of trusting a partial record.
    // Its result is checked — an unreported failure here means store() claims
    // success and the next boot comes up on the previous format.
    return preferences.putUChar(kVersionKey, kFormatVersion) == sizeof(kFormatVersion);
  }

  void reset() {
    assigned_.fill(kBuiltIn);
    for (auto& value : names_) value[0] = '\0';
    for (auto& value : shorts_) value[0] = '\0';
    repointed_ = 0;
    lost_ = 0;
    migratedFromLegacy_ = false;
  }

  void apply(std::size_t slot, std::int16_t index) {
    if (index == kBuiltIn ||
        static_cast<std::size_t>(index) >= open_radio::generated::kDirectoryCount) {
      assigned_[slot] = kBuiltIn;
      return;
    }
    assigned_[slot] = index;
    const char* source = open_radio::generated::kDirectory[index].name;
    std::strncpy(names_[slot].data(), source, kNameBytes);
    names_[slot][kNameBytes] = '\0';
    writeShortName(slot, source);
  }

  // The tile is 7 characters wide at most, so a monogram is cut from the first
  // word that carries meaning. "Radio" alone identifies nothing in a Polish
  // directory where half the entries start with it.
  void writeShortName(std::size_t slot, const char* source) {
    auto& target = shorts_[slot];
    std::size_t written = 0;
    bool skippedGenericPrefix = false;
    const char* cursor = source;
    while (*cursor == ' ') ++cursor;
    if (std::strncmp(cursor, "Radio ", 6) == 0 || std::strncmp(cursor, "RADIO ", 6) == 0) {
      cursor += 6;
      skippedGenericPrefix = true;
    }
    for (; *cursor != '\0' && written < kShortBytes - 1; ++cursor) {
      if (*cursor == ' ' && written > 0) break;
      const unsigned char value = static_cast<unsigned char>(*cursor);
      if (value < 0x20 || value > 0x7e) continue;
      target[written++] = static_cast<char>(std::toupper(value));
    }
    if (written == 0 && skippedGenericPrefix) {
      std::strncpy(target.data(), "RADIO", kShortBytes - 1);
      written = 5;
    }
    target[written] = '\0';
  }

  // Zero-initialising this meant that before load() every tile read as
  // directory index 0 — a real station — rather than "factory". Nothing calls
  // select() that early today, but the class should not depend on main.cpp
  // keeping that order: an unloaded slot must look unassigned, not bound to
  // whichever station happens to sit first in the catalogue.
  static constexpr std::array<std::int16_t, kSlotCount> unassigned() {
    std::array<std::int16_t, kSlotCount> value{};
    for (auto& item : value) item = kBuiltIn;
    return value;
  }
  std::array<std::int16_t, kSlotCount> assigned_ = unassigned();
  std::array<std::array<char, kNameBytes + 1>, kSlotCount> names_{};
  std::array<std::array<char, kShortBytes>, kSlotCount> shorts_{};
  std::size_t repointed_ = 0;
  std::size_t lost_ = 0;
  bool migratedFromLegacy_ = false;
};

}  // namespace open_radio::public_candidate
