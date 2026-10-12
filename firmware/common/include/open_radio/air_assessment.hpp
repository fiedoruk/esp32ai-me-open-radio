#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace open_radio {

// What the readings MEAN, kept apart from how they are drawn and from how they
// were obtained.
//
// The device says the number is high when it is high. Version 1 of the claims
// contract forbade that, by analogy with medical-device rules, and the analogy
// was wrong: an appliance that measures CO2 and refuses to say 1450 ppm is
// stale is not being careful, it is being useless. The line that survives is
// narrower — speak about the AIR and what to do with it, never about the
// person, and never impersonate a detector this hardware does not contain.
//
// Every threshold here comes from release/claims-vocabulary.json, where each
// one carries a published source, and `check:claims` refuses a threshold that
// does not. A limit this project invented would be exactly the failure the
// contract exists to prevent, reached from the other direction.
//
// One number is deliberately absent: an "index". The BME688's air-quality index
// and CO2 equivalent are outputs of Bosch's BSEC, a closed binary that cannot
// be combined with GPL-3.0. What is here is arithmetic anyone can check.

enum class AirBand : std::uint8_t {
  Unknown,
  // Outdoor-ish. Ventilation is winning.
  Fresh,
  // Ordinary occupied room.
  Normal,
  // 1000 ppm and up: the level at which cognitive test scores first measurably
  // fall (Satish 2012, Allen 2016). Air the room.
  Stale,
  // 1400 and up: half the performance on strategic thinking against a 550 ppm
  // control. This air has been recirculating for hours.
  VeryStale,
  // 2500 and up.
  High
};

inline AirBand co2Band(std::int32_t ppm) {
  if (ppm <= 0) return AirBand::Unknown;
  if (ppm < 800) return AirBand::Fresh;
  if (ppm < 1000) return AirBand::Normal;
  if (ppm < 1400) return AirBand::Stale;
  if (ppm < 2500) return AirBand::VeryStale;
  return AirBand::High;
}

enum class HumidityBand : std::uint8_t {
  Unknown,
  // Dry enough to irritate, sustained.
  Dry,
  Comfortable,
  // Above 60% sustained, any surface below the dew point will condense. That is
  // psychrometrics, not an opinion about anyone's health.
  Damp
};

inline HumidityBand humidityBand(std::int32_t centiPercent) {
  if (centiPercent < 0) return HumidityBand::Unknown;
  if (centiPercent < 3000) return HumidityBand::Dry;
  if (centiPercent <= 6000) return HumidityBand::Comfortable;
  return HumidityBand::Damp;
}

// Dew point by the Magnus formula, in centi-degrees. The temperature at which
// this air would start condensing: a window colder than this WILL run wet, and
// so will an outside wall behind a wardrobe. It costs one logarithm and turns
// two readings into something a person can act on.
inline std::int32_t dewPointCentiCelsius(std::int32_t temperatureCentiCelsius,
                                         std::int32_t humidityCentiPercent) {
  if (humidityCentiPercent <= 0) return INT32_MIN;
  const double temperature = temperatureCentiCelsius / 100.0;
  const double humidity = humidityCentiPercent / 100.0;
  const double alpha = std::log(humidity / 100.0) + (17.62 * temperature) / (243.12 + temperature);
  const double dewPoint = (243.12 * alpha) / (17.62 - alpha);
  return static_cast<std::int32_t>(std::lround(dewPoint * 100.0));
}

// Absolute humidity in centi-grams per cubic metre: how much water is actually
// in the air, which relative humidity does not tell you because it moves with
// temperature. Two rooms at 50% and different temperatures hold different
// amounts of water, and only this number says so.
inline std::int32_t absoluteHumidityCentiGramsPerCubicMetre(
    std::int32_t temperatureCentiCelsius, std::int32_t humidityCentiPercent) {
  if (humidityCentiPercent < 0) return INT32_MIN;
  const double temperature = temperatureCentiCelsius / 100.0;
  const double humidity = humidityCentiPercent / 100.0;
  const double saturation = 6.112 * std::exp((17.67 * temperature) / (temperature + 243.5));
  const double grams = (saturation * humidity * 2.1674) / (273.15 + temperature);
  return static_cast<std::int32_t>(std::lround(grams * 100.0));
}

// A short history, in RAM and never in NVS. NVS here is five pages and the
// Arduino core erases the whole partition when it runs out of free ones
// (docs/125 §6); a value that changes every few seconds has no business near
// it. Sized for three hours at one sample a minute, which is what a pressure
// trend needs and more than a CO2 rate does.
// N+1 slots hold N minutes of lookback: with exactly 180 the oldest sample was
// 179 minutes old, so a sample "at least 180 minutes old" never existed and the
// three-hour tendency stayed absent on every cube (review of 2026-09-08).
inline constexpr std::size_t kAirHistorySamples = 181;

class AirHistory {
 public:
  void record(std::uint32_t minuteStamp, std::int32_t co2Ppm,
              std::int32_t pressureCentiHectopascal, std::int32_t gasResistanceOhm) {
    if (count_ != 0 && samples_[newest_].minute == minuteStamp) return;
    newest_ = (newest_ + 1) % kAirHistorySamples;
    samples_[newest_] = {minuteStamp, co2Ppm, pressureCentiHectopascal, gasResistanceOhm};
    if (count_ < kAirHistorySamples) ++count_;
  }

  std::size_t count() const { return count_; }

  // Change per minute over the last `minutes`, or kNoTrend when there is not
  // enough history to say. Deliberately not a smoothed derivative: two points a
  // known distance apart is what a person means by "how fast is it rising", and
  // a filter would blur exactly the sharp events worth noticing.
  static constexpr std::int32_t kNoTrend = INT32_MIN;

  std::int32_t co2RatePpmPerMinute(std::uint32_t minutes) const {
    return ratePerMinute(minutes, [](const Sample& sample) { return sample.co2Ppm; });
  }

  std::int32_t pressureChangeCentiHectopascal(std::uint32_t minutes) const {
    const Sample* past = sampleBefore(minutes);
    if (past == nullptr || past->pressureCentiHectopascal == INT32_MIN) return kNoTrend;
    const Sample& now = samples_[newest_];
    if (now.pressureCentiHectopascal == INT32_MIN) return kNoTrend;
    return now.pressureCentiHectopascal - past->pressureCentiHectopascal;
  }

  // A metal-oxide element drifts, so its absolute resistance means nothing. The
  // signal is the DEPARTURE from where it has been sitting — which is why the
  // baseline is the median of the history rather than any single reading.
  std::int32_t gasBaselineOhm() const {
    if (count_ == 0) return INT32_MIN;
    std::array<std::int32_t, kAirHistorySamples> values{};
    std::size_t valid = 0;
    for (std::size_t index = 0; index < count_; ++index) {
      const std::int32_t value = samples_[index].gasResistanceOhm;
      if (value != INT32_MIN) values[valid++] = value;
    }
    if (valid == 0) return INT32_MIN;
    // Partial selection is enough for a median and avoids dragging in <algorithm>
    // sorting on a device where this runs once a minute.
    const std::size_t middle = valid / 2;
    for (std::size_t outer = 0; outer <= middle; ++outer) {
      std::size_t smallest = outer;
      for (std::size_t inner = outer + 1; inner < valid; ++inner) {
        if (values[inner] < values[smallest]) smallest = inner;
      }
      const std::int32_t swap = values[outer];
      values[outer] = values[smallest];
      values[smallest] = swap;
    }
    return values[middle];
  }

 private:
  struct Sample {
    std::uint32_t minute = 0;
    std::int32_t co2Ppm = INT32_MIN;
    std::int32_t pressureCentiHectopascal = INT32_MIN;
    std::int32_t gasResistanceOhm = INT32_MIN;
  };

  const Sample* sampleBefore(std::uint32_t minutes) const {
    if (count_ < 2) return nullptr;
    // No sample can be `minutes` old before `minutes` have passed — and the
    // earlier clamp-to-zero happily returned the FIRST sample instead, so a
    // "three-hour change" six minutes after boot was a five-minute change in a
    // three-hour costume. Caught on the device 2026-07-30: a tendency arrow
    // confidently level at six minutes of history.
    if (samples_[newest_].minute < minutes) return nullptr;
    const std::uint32_t target = samples_[newest_].minute - minutes;
    const Sample* best = nullptr;
    for (std::size_t step = 1; step < count_; ++step) {
      const std::size_t index = (newest_ + kAirHistorySamples - step) % kAirHistorySamples;
      if (samples_[index].minute <= target) {
        best = &samples_[index];
        break;
      }
    }
    return best;
  }

  template <typename Field>
  std::int32_t ratePerMinute(std::uint32_t minutes, Field field) const {
    const Sample* past = sampleBefore(minutes);
    if (past == nullptr) return kNoTrend;
    const Sample& now = samples_[newest_];
    const std::int32_t elapsed = static_cast<std::int32_t>(now.minute - past->minute);
    if (elapsed <= 0) return kNoTrend;
    if (field(now) == INT32_MIN || field(*past) == INT32_MIN) return kNoTrend;
    return (field(now) - field(*past)) / elapsed;
  }

  std::array<Sample, kAirHistorySamples> samples_{};
  std::size_t newest_ = kAirHistorySamples - 1;
  std::size_t count_ = 0;
};

// How long until the air crosses a level, at the rate it is moving now. This is
// the sentence worth showing instead of a warning light: "at this rate you pass
// 1400 in eighteen minutes" tells a person what to do and when, which no
// coloured dot ever has. Negative or zero rate means it is not heading there.
inline std::int32_t minutesToCo2Level(std::int32_t currentPpm, std::int32_t ratePpmPerMinute,
                                      std::int32_t targetPpm) {
  if (ratePpmPerMinute == AirHistory::kNoTrend || ratePpmPerMinute <= 0) return -1;
  if (currentPpm >= targetPpm) return 0;
  return (targetPpm - currentPpm) / ratePpmPerMinute;
}

// Things worth telling someone about that no single reading shows.
enum class AirEvent : std::uint8_t {
  None,
  // CO2 falling fast with a temperature step: somebody opened a window. Worth
  // saying because it confirms the thing the device just asked for.
  Airing,
  // The gas element departed sharply from its own baseline while CO2 stayed
  // put. Whatever it is, it is not people breathing — cooking, a solvent, a
  // cleaning product, something spilt. The device reports the departure and
  // does NOT name the substance, because a non-selective element cannot.
  SomethingChanged,
  // CO2, temperature and the gas element all moving hard in the same direction
  // at once. That is the signature of combustion, and it is the strongest thing
  // this hardware can honestly say — still as "check what is happening", never
  // as a fire or carbon-monoxide alarm, which need sensors this device has not
  // got and a certification it does not hold.
  CombustionLike,
  // Two modules that should agree, disagreeing by more than their combined
  // tolerance. One of them is wrong and neither can say which.
  ModulesDisagree
};

struct AirEventInputs {
  std::int32_t co2RatePpmPerMinute = AirHistory::kNoTrend;
  std::int32_t temperatureRateCentiPerMinute = AirHistory::kNoTrend;
  std::int32_t gasResistanceOhm = INT32_MIN;
  std::int32_t gasBaselineOhm = INT32_MIN;
  std::int32_t primaryTemperatureCentiCelsius = INT32_MIN;
  std::int32_t secondaryTemperatureCentiCelsius = INT32_MIN;
};

inline AirEvent detectAirEvent(const AirEventInputs& inputs) {
  const bool gasKnown = inputs.gasResistanceOhm != INT32_MIN &&
                        inputs.gasBaselineOhm != INT32_MIN && inputs.gasBaselineOhm > 0;
  // A halving against the baseline. Metal-oxide resistance falls as reducing
  // gas rises, and it drifts slowly, so a factor of two inside minutes is an
  // event rather than ageing.
  const bool gasCollapsed = gasKnown && inputs.gasResistanceOhm * 2 < inputs.gasBaselineOhm;
  const bool co2Rising = inputs.co2RatePpmPerMinute != AirHistory::kNoTrend &&
                         inputs.co2RatePpmPerMinute > 30;
  const bool warmingFast = inputs.temperatureRateCentiPerMinute != AirHistory::kNoTrend &&
                           inputs.temperatureRateCentiPerMinute > 50;

  if (gasCollapsed && co2Rising && warmingFast) return AirEvent::CombustionLike;

  if (inputs.primaryTemperatureCentiCelsius != INT32_MIN &&
      inputs.secondaryTemperatureCentiCelsius != INT32_MIN) {
    const std::int32_t difference =
        inputs.primaryTemperatureCentiCelsius - inputs.secondaryTemperatureCentiCelsius;
    // Combined tolerance of the two parts is about 1.3 degrees; 3 degrees is
    // well past anything they can honestly differ by, even with the SCD4x
    // running warm.
    if (difference > 300 || difference < -300) return AirEvent::ModulesDisagree;
  }

  if (gasCollapsed && !co2Rising) return AirEvent::SomethingChanged;

  if (inputs.co2RatePpmPerMinute != AirHistory::kNoTrend &&
      inputs.co2RatePpmPerMinute < -40) {
    return AirEvent::Airing;
  }
  return AirEvent::None;
}

}  // namespace open_radio
