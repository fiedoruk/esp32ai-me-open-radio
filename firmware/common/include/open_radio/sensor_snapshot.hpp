#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace open_radio {

// The latest reading from an environment module, published so that the owner
// loop can read it without waiting for anything and without ever seeing half of
// one reading and half of another.
//
// Why not the existing ingress: `RuntimeFact` carries producer, epoch, sequence,
// tick and kind — and no payload. It answers "something happened", which is the
// right shape for health transitions and the wrong shape for a value. Pushing
// readings through it would either need a payload on every fact in the system or
// a side channel pretending not to be one. This is the side channel, made
// explicit and bounded (docs/124 T7).
//
// A sensor task writes; the owner loop and the console read. They are on
// different cores. A plain struct assignment across cores is a torn read
// waiting to happen: temperature from one sample beside humidity from the next
// is not an error anyone would notice, which is exactly why it has to be
// impossible rather than unlikely.
//
// The mechanism is a seqlock. The writer bumps a counter to odd, writes, and
// bumps it to even. A reader takes the counter, reads, takes it again, and
// accepts the result only if it was even and unchanged. The reader never
// blocks the writer — which matters, because the writer is a sensor task that
// must not be delayed by a display push — and the writer never blocks at all.
//
// Readings never reach NVS. That is not tidiness: NVS here is 20,480 bytes,
// five pages, and the Arduino core erases the whole partition before setup()
// when it runs out of free ones (docs/125 §6). A value that changes every
// thirty seconds has no business anywhere near it.

enum class SensorStatus : std::uint8_t {
  // No module answered at this address. The normal state of a cube nobody
  // plugged anything into, and not a fault.
  Absent,
  // Answering, but the reading is not trustworthy yet. SCD4x needs a first
  // measurement interval before its output means anything.
  Warming,
  // A value the interface may show.
  Ok,
  // Answered and then stopped, or answered with something impossible.
  Fault,
  // Removed from the bus after misbehaving, so one wedged module cannot take
  // the loop with it. Deliberately distinct from Fault: a person can be told
  // "it stopped" differently from "we stopped asking".
  Quarantined
};

// Integers, in the smallest unit the interface will render, so nothing has to
// agree about floating point between a sensor task, a renderer and a test.
// kAbsentValue marks a quantity this module does not measure — the SCD41 has no
// pressure, the BME688 has no CO2 sensor at all — as opposed to one it measures
// as zero.
inline constexpr std::int32_t kAbsentValue = INT32_MIN;

struct SensorReading {
  SensorStatus status = SensorStatus::Absent;
  // Device milliseconds when the value was taken, so the interface can say how
  // old it is instead of implying it is now.
  std::uint32_t observedAtMs = 0;
  std::int32_t co2Ppm = kAbsentValue;
  std::int32_t temperatureCentiCelsius = kAbsentValue;
  std::int32_t humidityCentiPercent = kAbsentValue;
  std::int32_t pressureCentiHectopascal = kAbsentValue;
  // Raw gas resistance only. The BME688's air-quality index and its CO2
  // equivalent come from Bosch's BSEC, a closed binary whose licence cannot be
  // combined with GPL-3.0 (docs/120), so this project ships the resistance and
  // never a number derived from it.
  std::int32_t gasResistanceOhm = kAbsentValue;
  // Increments on every published reading. A reader that sees the same
  // generation twice knows nothing new arrived, without comparing values.
  std::uint32_t generation = 0;

  bool has(std::int32_t value) const { return value != kAbsentValue; }
};

// What the sensor task DERIVES from its own history, as opposed to what it
// reads from a module. It is published here, beside the readings, because the
// history it comes from is about three kilobytes and belongs to the task that
// fills it; shipping that across to the interface core so it could recompute
// the same four numbers would move a lot of memory to arrive at the same
// answer.
//
// Every field is kAbsentValue until there is enough history to support it. "Not
// enough history yet" and "steady" are different statements, and a screen that
// rendered the first as the second would be inventing a measurement.
struct AirDerived {
  // open_radio::AirBand, as a byte, so this header does not have to include the
  // assessment header and the renderer never sees either.
  std::uint8_t band = 0;
  // open_radio::AirEvent, same reasoning.
  std::uint8_t event = 0;
  std::int32_t co2RatePpmPerHour = kAbsentValue;
  std::int32_t co2NextThresholdPpm = kAbsentValue;
  std::int32_t co2MinutesToNext = kAbsentValue;
  std::int32_t dewPointCentiCelsius = kAbsentValue;
  // The highest CO2 this module can measure — 5,000 on an SCD41, 2,000 on an
  // SCD40. Carried to the screen because a reading sitting AT the ceiling is
  // not a measurement of the room, it is the sensor running out of scale, and
  // those two have to look different.
  std::int32_t co2CeilingPpm = kAbsentValue;
  // The three-hour pressure change in centi-hectopascals, and the gas element's
  // departure from its own running median as a percentage of it.
  //
  // Both belong here rather than beside the readings for the same reason as the
  // CO2 rate: they are statements about the HISTORY, and the history lives with
  // the task that keeps it. The pressure figure needs three hours before it can
  // be anything but kAbsentValue, which is exactly why it must be able to say
  // so — an arrow drawn level on a device that has been on for ten minutes
  // would be a confident lie in the shape people trust most.
  std::int32_t pressureChangeCentiHpa3h = kAbsentValue;
  std::int32_t gasDeparturePercent = kAbsentValue;
  // Minutes since the gas element powered on, present ONLY while its burn-in
  // is still running — the screen writes the calibration line from this.
  std::int32_t gasWarmupMinutes = kAbsentValue;
  // open_radio::AlertId and open_radio::AlertLevel, as bytes. The ONE thing the
  // device has decided to say, after hysteresis, dwell and rate limiting — see
  // docs/131. Carried as a byte rather than a string because the wording is
  // chosen where the locale is known, which is the interface core.
  std::uint8_t alertId = 0;
  std::uint8_t alertLevel = 0;
  std::uint32_t generation = 0;
};

// One slot per module ADDRESS on the bus — two, which is not the same as two
// supported parts. This firmware speaks to three: the SCD40 and the SCD41 are
// one sensor in two accuracies, sharing an address and this driver and
// differing in the ceiling they can measure to, so they can never occupy two
// slots at once. Three parts, two seats.
//
// That distinction is why the variant probe exists (scd4x.hpp): the slot cannot
// tell you which of the two you have, and the difference between them is
// 2,000 ppm and 5,000 ppm of headroom.
//
// Note also what a slot is NOT: it is not a quantity. A module measuring
// something this firmware has no field for — particulates, light, sound —
// contributes nothing until that quantity is added deliberately, with a
// threshold that carries a source and a place on the screen. Wiring an unknown
// module into an existing field would be worse than ignoring it.
inline constexpr std::size_t kSensorSlotCount = 2;

class SensorSnapshot {
 public:
  // Writer side. Called from the sensor task only.
  void publish(std::size_t slot, const SensorReading& reading) {
    if (slot >= kSensorSlotCount) return;
    auto& cell = cells_[slot];
    const std::uint32_t start = cell.sequence.load(std::memory_order_relaxed);
    cell.sequence.store(start + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    cell.value = reading;
    cell.value.generation = start / 2 + 1;
    std::atomic_thread_fence(std::memory_order_release);
    cell.sequence.store(start + 2, std::memory_order_relaxed);
  }

  // Reader side. The retry is bounded, and on exhausting it the reader is told
  // there is no new value rather than handed something torn — the one outcome
  // that must never happen.
  //
  // That bound has a measured cost, recorded rather than hidden: against a
  // writer publishing in a tight loop the reader is refused almost every time
  // (200,000 publications, 86 completed reads, zero torn). That is the seqlock
  // working, not failing, but it would be dishonest to call this wait-free: it
  // is wait-free for the WRITER, which is the sensor task that must never be
  // delayed, and best-effort for the reader.
  //
  // At the rate this is actually driven — an SCD41 publishes every five seconds
  // — a read succeeds on its first attempt. The test pins that too, against a
  // publication every 200 microseconds, four orders of magnitude faster than
  // the real thing: over 99% of reads complete.
  bool read(std::size_t slot, SensorReading& out) const {
    if (slot >= kSensorSlotCount) return false;
    const auto& cell = cells_[slot];
    for (int attempt = 0; attempt < kReadAttempts; ++attempt) {
      const std::uint32_t before = cell.sequence.load(std::memory_order_relaxed);
      if ((before & 1U) != 0U) continue;
      std::atomic_thread_fence(std::memory_order_acquire);
      const SensorReading candidate = cell.value;
      std::atomic_thread_fence(std::memory_order_acquire);
      if (cell.sequence.load(std::memory_order_relaxed) == before) {
        out = candidate;
        return true;
      }
    }
    return false;
  }

  // Convenience for callers that would otherwise invent their own fallback:
  // a failed read is indistinguishable from a slot nobody has written.
  SensorReading readOrAbsent(std::size_t slot) const {
    SensorReading reading;
    if (!read(slot, reading)) return SensorReading{};
    return reading;
  }

  // How many readings have ever been published to this slot. Used by the health
  // path to tell "never answered" from "answered and stopped".
  std::uint32_t publishedCount(std::size_t slot) const {
    if (slot >= kSensorSlotCount) return 0;
    return cells_[slot].sequence.load(std::memory_order_relaxed) / 2;
  }

  // The derived values, under their own seqlock. Separate from the readings
  // because they are produced on a different schedule — a reading arrives every
  // five seconds, a trend can only be recomputed when a new minute exists — and
  // a shared sequence would make each publication invalidate the other's reads.
  void publishDerived(const AirDerived& derived) {
    const std::uint32_t start = derived_.sequence.load(std::memory_order_relaxed);
    derived_.sequence.store(start + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    derived_.value = derived;
    derived_.value.generation = start / 2 + 1;
    std::atomic_thread_fence(std::memory_order_release);
    derived_.sequence.store(start + 2, std::memory_order_relaxed);
  }

  AirDerived readDerived() const {
    for (int attempt = 0; attempt < kReadAttempts; ++attempt) {
      const std::uint32_t before = derived_.sequence.load(std::memory_order_relaxed);
      if ((before & 1U) != 0U) continue;
      std::atomic_thread_fence(std::memory_order_acquire);
      const AirDerived candidate = derived_.value;
      std::atomic_thread_fence(std::memory_order_acquire);
      if (derived_.sequence.load(std::memory_order_relaxed) == before) return candidate;
    }
    return AirDerived{};
  }

 private:
  static constexpr int kReadAttempts = 8;

  struct Cell {
    std::atomic<std::uint32_t> sequence{0};
    SensorReading value{};
  };

  struct DerivedCell {
    std::atomic<std::uint32_t> sequence{0};
    AirDerived value{};
  };

  Cell cells_[kSensorSlotCount];
  DerivedCell derived_;
};

}  // namespace open_radio
