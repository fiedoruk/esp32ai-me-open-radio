#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace open_radio {

// Everything about talking to a Sensirion SCD4x that does not need a bus.
//
// The split is deliberate. Command framing, the CRC every word carries, and the
// datasheet's conversion arithmetic are pure functions and are tested on the
// host against the vectors Sensirion publishes. What is left for the device is
// "write these bytes, read those bytes" — which is the part a test cannot check
// anyway, and the part that must not be carrying arithmetic.
//
// Why this project writes its own instead of taking Sensirion's driver: the
// upstream one is BSD-3-Clause and could be vendored, but it wants a platform
// layer, a delay implementation and a bus of its own, and the whole of what we
// need is four commands. The licence was never the obstacle; the surface was.
//
// The parts this speaks to are the SCD41 (Unit CO2L, adopted) and the SCD40
// (Unit CO2, the cheaper alternative). They share an address and a command set,
// so one driver serves both — but they do NOT share a ceiling: 5,000 ppm
// against 2,000, and a bedroom at night routinely passes 2,000 (docs/120).
//
// An earlier version of this comment said the firmware could not tell them
// apart and did not need to. The first half is false — get_sensor_variant
// answers it — and the second half was only true while nothing on screen
// depended on the top of the scale. Both stopped being true in 0.4.

inline constexpr std::uint8_t kScd4xAddress = 0x62;

// The SCD4x checksums every two-byte word: CRC-8, polynomial 0x31, initialised
// to 0xFF, no final XOR. A reading that fails it is discarded rather than
// scaled, because the conversion below turns any sixteen bits into a plausible
// temperature — there is no value a corrupt word cannot impersonate.
inline std::uint8_t scd4xCrc(const std::uint8_t* data, std::size_t length) {
  std::uint8_t crc = 0xFF;
  for (std::size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80U) != 0U ? static_cast<std::uint8_t>((crc << 1) ^ 0x31U)
                                : static_cast<std::uint8_t>(crc << 1);
    }
  }
  return crc;
}

enum class Scd4xCommand : std::uint16_t {
  StartPeriodicMeasurement = 0x21B1,
  ReadMeasurement = 0xEC05,
  StopPeriodicMeasurement = 0x3F86,
  GetDataReadyStatus = 0xE4B8,
  SetAutomaticSelfCalibration = 0x2416,
  SetTemperatureOffset = 0x241D,
  GetTemperatureOffset = 0x2318,
  PersistSettings = 0x3615,
  GetSerialNumber = 0x3682,
  PerformForcedRecalibration = 0x362F,
  Reinit = 0x3646,
  GetSensorVariant = 0x202F
};

// The ceiling each variant can actually measure to. This is not a preference:
// an SCD40 asked about air at 3,000 ppm answers 2,000 and keeps answering
// 2,000, with no error and no flag. A screen that printed that as a reading
// would under-report without ever being wrong on the wire — the same failure
// shape as a drifting ASC baseline, and the reason this project would rather
// say "at the ceiling" than show a number it cannot stand behind.
inline constexpr std::int32_t kScd40CeilingPpm = 2000;
inline constexpr std::int32_t kScd41CeilingPpm = 5000;

enum class Scd4xVariant : std::uint8_t { Unknown, Scd40, Scd41 };

// get_sensor_variant returns one word; the variant is bits 15..12, with 0 for
// the SCD40 and 1 for the SCD41. Older parts do not implement the command at
// all — they NAK it or return a CRC failure — and that is why Unknown exists
// and is treated as the conservative case rather than assumed to be the part
// this project happens to have adopted.
inline Scd4xVariant scd4xDecodeVariant(const std::uint8_t* bytes, std::size_t length) {
  if (bytes == nullptr || length < 3 || scd4xCrc(bytes, 2) != bytes[2]) {
    return Scd4xVariant::Unknown;
  }
  const std::uint16_t word = static_cast<std::uint16_t>(bytes[0] << 8 | bytes[1]);
  switch ((word >> 12) & 0x000FU) {
    case 0x0: return Scd4xVariant::Scd40;
    case 0x1: return Scd4xVariant::Scd41;
    default: return Scd4xVariant::Unknown;
  }
}

// What to believe about the top of the scale. An unidentified part is given the
// LOWER ceiling, because being told "this may be at its limit" when it is not
// costs a person nothing, and the reverse costs them the one reading that
// mattered.
inline std::int32_t scd4xCeilingPpm(Scd4xVariant variant) {
  return variant == Scd4xVariant::Scd41 ? kScd41CeilingPpm : kScd40CeilingPpm;
}

// Commands go on the wire most significant byte first. A word argument follows
// with its own CRC.
inline std::array<std::uint8_t, 2> scd4xFrame(Scd4xCommand command) {
  const auto value = static_cast<std::uint16_t>(command);
  return {static_cast<std::uint8_t>(value >> 8),
          static_cast<std::uint8_t>(value & 0xFF)};
}

inline std::array<std::uint8_t, 5> scd4xFrameWithArgument(Scd4xCommand command,
                                                          std::uint16_t argument) {
  const auto value = static_cast<std::uint16_t>(command);
  std::array<std::uint8_t, 5> frame{};
  frame[0] = static_cast<std::uint8_t>(value >> 8);
  frame[1] = static_cast<std::uint8_t>(value & 0xFF);
  frame[2] = static_cast<std::uint8_t>(argument >> 8);
  frame[3] = static_cast<std::uint8_t>(argument & 0xFF);
  frame[4] = scd4xCrc(&frame[2], 2);
  return frame;
}

struct Scd4xMeasurement {
  bool valid = false;
  std::int32_t co2Ppm = 0;
  std::int32_t temperatureCentiCelsius = 0;
  std::int32_t humidityCentiPercent = 0;
};

// The nine bytes a read_measurement returns: three words, each followed by its
// CRC. Every word is checked before any of them is used — a reading with one
// good word and two corrupt ones is not two thirds of a reading.
//
// Conversions are the datasheet's, in integer arithmetic so that a sensor task,
// a renderer and a test cannot disagree about rounding:
//   CO2 is the raw word, in ppm.
//   T  = -45 + 175 * raw / 2^16, in degrees Celsius.
//   RH = 100 * raw / 2^16, in percent.
// Both are scaled by 100 here, because the screen renders one decimal and
// centi-units keep that exact without a float anywhere near the path.
inline Scd4xMeasurement scd4xDecodeMeasurement(const std::uint8_t* bytes,
                                               std::size_t length) {
  Scd4xMeasurement measurement;
  if (bytes == nullptr || length < 9) return measurement;
  for (std::size_t word = 0; word < 3; ++word) {
    const std::uint8_t* start = bytes + word * 3;
    if (scd4xCrc(start, 2) != start[2]) return measurement;
  }
  const auto raw = [bytes](std::size_t word) {
    return static_cast<std::uint32_t>(bytes[word * 3] << 8 | bytes[word * 3 + 1]);
  };
  measurement.co2Ppm = static_cast<std::int32_t>(raw(0));
  // -45 + 175 * raw / 65536, times 100. The multiply is done before the divide
  // and in 64 bits, so 175 * 65535 * 100 cannot overflow on the way through.
  measurement.temperatureCentiCelsius = static_cast<std::int32_t>(
      -4500 + static_cast<std::int64_t>(175) * 100 * raw(1) / 65536);
  measurement.humidityCentiPercent = static_cast<std::int32_t>(
      static_cast<std::int64_t>(100) * 100 * raw(2) / 65536);
  measurement.valid = true;
  return measurement;
}

// A reading the sensor cannot have produced. The SCD4x tops out at 40,000 ppm
// and cannot measure below -10 degrees or above 60; anything outside that is a
// bus fault dressed as data, and this project would rather show nothing than a
// number it does not believe.
inline bool scd4xPlausible(const Scd4xMeasurement& measurement) {
  return measurement.valid && measurement.co2Ppm > 0 &&
         measurement.co2Ppm <= 40000 &&
         measurement.temperatureCentiCelsius >= -1000 &&
         measurement.temperatureCentiCelsius <= 6000 &&
         measurement.humidityCentiPercent >= 0 &&
         measurement.humidityCentiPercent <= 10000;
}

// Data-ready returns a word whose low eleven bits are zero when nothing is
// ready. Reading a measurement before then returns the previous one, silently,
// which is how a sensor appears to be answering while telling you nothing new.
inline bool scd4xDataReady(const std::uint8_t* bytes, std::size_t length) {
  if (bytes == nullptr || length < 3) return false;
  if (scd4xCrc(bytes, 2) != bytes[2]) return false;
  const std::uint16_t word =
      static_cast<std::uint16_t>(bytes[0] << 8 | bytes[1]);
  return (word & 0x07FFU) != 0U;
}

// Automatic self-calibration assumes the sensor sees roughly 400 ppm of fresh
// air within each window of about a week. A cube on a bedroom shelf may never
// see it, and the sensor then walks its own baseline down — reading low by a
// hundred ppm or more, with no error and no symptom (docs/125 §3). For a device
// whose whole claim is that a reading is informational, silently drifting low
// is the worst available failure: it under-reports exactly the condition a
// person would act on. So it ships off, and recalibration is an explicit act.
inline std::array<std::uint8_t, 5> scd4xDisableAutomaticSelfCalibration() {
  return scd4xFrameWithArgument(Scd4xCommand::SetAutomaticSelfCalibration, 0);
}

// Temperature offset, which is how the part is meant to be corrected for the
// enclosure it lives in — and correcting it HERE rather than in our own
// arithmetic matters, because the offset also feeds the sensor's humidity
// compensation. Subtracting a few degrees on the way to the screen would fix
// the temperature and leave the humidity wrong.
//
// Measured on this project's hardware 2026-07-29: with the factory default of
// 4 degrees, a room the owner measured at 23.4-24 read 25.3 once thermally
// settled, and 29 before it was. The settling matters — an offset computed
// from a warm sensor bakes in half a transient.
//
// The register takes ticks: offset_celsius * 65536 / 175.
inline std::array<std::uint8_t, 5> scd4xSetTemperatureOffset(std::int32_t centiCelsius) {
  const std::int32_t ticks = (centiCelsius * 65536) / 17500;
  const std::uint16_t clamped = ticks < 0 ? 0
                              : ticks > 0xFFFF ? 0xFFFF
                                               : static_cast<std::uint16_t>(ticks);
  return scd4xFrameWithArgument(Scd4xCommand::SetTemperatureOffset, clamped);
}

inline std::int32_t scd4xDecodeTemperatureOffset(const std::uint8_t* bytes, std::size_t length) {
  if (bytes == nullptr || length < 3 || scd4xCrc(bytes, 2) != bytes[2]) return INT32_MIN;
  const std::uint32_t ticks = static_cast<std::uint32_t>(bytes[0] << 8 | bytes[1]);
  return static_cast<std::int32_t>((static_cast<std::int64_t>(ticks) * 17500) / 65536);
}

// Forced recalibration: tell the sensor what the air around it actually is,
// after several minutes in it. The reference goes in as ppm.
inline std::array<std::uint8_t, 5> scd4xForcedRecalibration(std::uint16_t referencePpm) {
  return scd4xFrameWithArgument(Scd4xCommand::PerformForcedRecalibration,
                                referencePpm);
}

}  // namespace open_radio
