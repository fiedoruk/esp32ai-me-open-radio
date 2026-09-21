#pragma once

#include <array>
#include <cstdint>

#include "open_radio/air_assessment.hpp"

namespace open_radio {

// When the device speaks, and how loudly. The rules are docs/131; this is the
// only implementation of them.
//
// Before this existed every condition was evaluated where it was used: the CO2
// band coloured a bar in the renderer, the gas event was decided in the sensor
// task, humidity was a comparison inside a draw call. Each was defensible and
// together they had no hysteresis, no dwell, no rate limit and no notion of one
// speaking at a time — so a reading sitting on 1000 ppm toggled an instruction
// on and off all afternoon, and nothing in the design said it should not.
//
// The point of gathering them is not tidiness. It is that a threshold, its
// release value, how long it must hold and how long before it may repeat are
// FOUR numbers that only mean anything together, and no call site that owns one
// of them owns the other three.

enum class AlertLevel : std::uint8_t {
  // Recolour something already on the screen. Nothing else.
  Ambient,
  // One line of text in the advice band.
  Notice,
  // A line, and permission to wake a blanked display.
  Alert,
  // A line, a wake, and one sound. Exactly one member, deliberately.
  ActNow,
};

// Numbered as in docs/130 so the catalogue, the rules and the code cannot drift
// into three different orders. The ORDER here is also the priority order, which
// is why None is first and the CO2 family precedes the rest.
enum class AlertId : std::uint8_t {
  None,
  Co2High,            // A3
  Co2VeryStale,       // A2
  Co2Stale,           // A1
  Co2Forecast,        // A4
  Airing,             // A5
  DewPointRisk,       // B2
  HumidityDamp,       // B3
  HumidityDry,        // B4
  PressureStorm,      // C2
  PressureChanging,   // C1
  PressureClearing,   // C3
  GasCombustionLike,  // D3
  GasNotPeople,       // D2
  GasDeparture,       // D1
  ModulesDisagree,    // E1
  ReadingStale,
};

inline constexpr std::size_t kAlertCount =
    static_cast<std::size_t>(AlertId::ReadingStale) + 1;

// What a single settings toggle mutes. Families are muted whole: an owner who
// turns off "humidity" knows what they did, and an owner picking through
// fourteen individual switches does not know what is on.
enum class AlertFamily : std::uint8_t { Core, Co2, Humidity, Pressure, Gas };

struct AlertInputs {
  std::int32_t co2Ppm = INT32_MIN;
  std::int32_t co2CeilingPpm = INT32_MIN;
  std::int32_t co2RatePpmPerMinute = AirHistory::kNoTrend;
  std::int32_t co2MinutesToNext = INT32_MIN;
  std::int32_t temperatureCentiCelsius = INT32_MIN;
  std::int32_t temperatureRateCentiPerMinute = AirHistory::kNoTrend;
  std::int32_t humidityCentiPercent = INT32_MIN;
  std::int32_t dewPointCentiCelsius = INT32_MIN;
  std::int32_t pressureChangeCentiHpa3h = INT32_MIN;
  std::int32_t gasResistanceOhm = INT32_MIN;
  std::int32_t gasBaselineOhm = INT32_MIN;
  std::int32_t secondaryTemperatureCentiCelsius = INT32_MIN;
  std::uint16_t readingAgeSeconds = 0;
};

// What the owner is allowed to change. Note what is NOT here: not one threshold.
// Every trigger in this file carries a published source, and a slider that moved
// one would produce a device asserting that 2500 ppm is fine because somebody
// set it so — the exact claim release/claims-vocabulary.json exists to prevent.
struct AlertPolicy {
  // The ceiling on what any alert may do, in the owner's own words: off, screen
  // only, screen and wake, or sound as well.
  AlertLevel highest = AlertLevel::Alert;
  bool enabled = true;
  bool muteHumidity = false;
  bool mutePressure = false;
  bool muteGas = false;
  // Quiet hours lower the level by one step rather than suppressing anything.
  // Inactive when the clock is unknown: inferring the hour would be guessing,
  // and guessing wrong here means silence at the moment it mattered.
  bool quietHoursActive = false;
  bool allowActNowWhenQuiet = true;
};

struct AlertVerdict {
  AlertId id = AlertId::None;
  AlertLevel level = AlertLevel::Ambient;
  bool wake = false;
  bool sound = false;
};

// The four numbers, together, once.
struct AlertRule {
  AlertId id;
  AlertLevel level;
  AlertFamily family;
  // Consecutive evaluations the condition must hold before it fires. The sensor
  // answers every five seconds, so one reading is not an event: somebody
  // breathing on the cube reaches 3000 ppm in two seconds.
  std::uint8_t dwell;
  // Minutes after clearing before it may fire again.
  std::uint16_t rearmMinutes;
};

inline constexpr std::array<AlertRule, kAlertCount> kAlertRules{{
    {AlertId::None, AlertLevel::Ambient, AlertFamily::Core, 0, 0},
    {AlertId::Co2High, AlertLevel::Alert, AlertFamily::Co2, 3, 15},
    {AlertId::Co2VeryStale, AlertLevel::Notice, AlertFamily::Co2, 3, 10},
    {AlertId::Co2Stale, AlertLevel::Notice, AlertFamily::Co2, 3, 10},
    {AlertId::Co2Forecast, AlertLevel::Notice, AlertFamily::Co2, 6, 20},
    {AlertId::Airing, AlertLevel::Ambient, AlertFamily::Co2, 2, 5},
    {AlertId::DewPointRisk, AlertLevel::Alert, AlertFamily::Humidity, 6, 30},
    {AlertId::HumidityDamp, AlertLevel::Notice, AlertFamily::Humidity, 24, 60},
    {AlertId::HumidityDry, AlertLevel::Notice, AlertFamily::Humidity, 24, 60},
    {AlertId::PressureStorm, AlertLevel::Notice, AlertFamily::Pressure, 1, 60},
    {AlertId::PressureChanging, AlertLevel::Ambient, AlertFamily::Pressure, 1, 60},
    {AlertId::PressureClearing, AlertLevel::Ambient, AlertFamily::Pressure, 1, 60},
    {AlertId::GasCombustionLike, AlertLevel::ActNow, AlertFamily::Gas, 4, 30},
    {AlertId::GasNotPeople, AlertLevel::Alert, AlertFamily::Gas, 4, 20},
    {AlertId::GasDeparture, AlertLevel::Alert, AlertFamily::Gas, 4, 20},
    {AlertId::ModulesDisagree, AlertLevel::Notice, AlertFamily::Core, 12, 360},
    {AlertId::ReadingStale, AlertLevel::Notice, AlertFamily::Core, 1, 5},
}};

// Whether a condition is met, given whether it is currently held. The second
// argument is the whole of the hysteresis: a rule answers with its TRIGGER when
// it is off and its RELEASE when it is on, and the two are never the same
// number.
inline bool alertConditionHolds(AlertId id, const AlertInputs& inputs, bool held) {
  const bool hasCo2 = inputs.co2Ppm != INT32_MIN;
  // A reading sitting AT the module's ceiling is the sensor out of scale, not a
  // measurement of the room, and nothing may be claimed from it.
  const bool co2Usable = hasCo2 && (inputs.co2CeilingPpm == INT32_MIN ||
                                    inputs.co2Ppm < inputs.co2CeilingPpm);
  const bool gasKnown = inputs.gasResistanceOhm != INT32_MIN &&
                        inputs.gasBaselineOhm != INT32_MIN &&
                        inputs.gasBaselineOhm > 0;
  // Halved against its own median to fire; back above seven tenths to clear.
  const bool gasDeparted =
      gasKnown && (held ? inputs.gasResistanceOhm * 10 < inputs.gasBaselineOhm * 7
                        : inputs.gasResistanceOhm * 2 < inputs.gasBaselineOhm);
  switch (id) {
    case AlertId::None:
      return false;
    case AlertId::Co2High:
      return co2Usable && inputs.co2Ppm >= (held ? 2350 : 2500);
    case AlertId::Co2VeryStale:
      return co2Usable && inputs.co2Ppm >= (held ? 1320 : 1400);
    case AlertId::Co2Stale:
      return co2Usable && inputs.co2Ppm >= (held ? 950 : 1000);
    case AlertId::Co2Forecast:
      return co2Usable && inputs.co2MinutesToNext != INT32_MIN &&
             inputs.co2MinutesToNext >= 0 &&
             inputs.co2MinutesToNext <= (held ? 30 : 20);
    case AlertId::Airing:
      return inputs.co2RatePpmPerMinute != AirHistory::kNoTrend &&
             inputs.co2RatePpmPerMinute <= (held ? -20 : -40);
    case AlertId::DewPointRisk:
      // Condensation starts where a surface is at or below the dew point. Two
      // degrees of margin, because the coldest surface in a room is colder than
      // the air the module is sitting in — a window, or the wall behind a
      // wardrobe.
      return inputs.dewPointCentiCelsius != INT32_MIN &&
             inputs.temperatureCentiCelsius != INT32_MIN &&
             inputs.dewPointCentiCelsius >=
                 inputs.temperatureCentiCelsius - (held ? 300 : 200);
    case AlertId::HumidityDamp:
      return inputs.humidityCentiPercent != INT32_MIN &&
             inputs.humidityCentiPercent > (held ? 5700 : 6000);
    case AlertId::HumidityDry:
      return inputs.humidityCentiPercent != INT32_MIN &&
             inputs.humidityCentiPercent < (held ? 3300 : 3000);
    case AlertId::PressureStorm:
      return inputs.pressureChangeCentiHpa3h != INT32_MIN &&
             inputs.pressureChangeCentiHpa3h <= (held ? -500 : -600);
    case AlertId::PressureChanging:
      return inputs.pressureChangeCentiHpa3h != INT32_MIN &&
             inputs.pressureChangeCentiHpa3h <= (held ? -300 : -350);
    case AlertId::PressureClearing:
      return inputs.pressureChangeCentiHpa3h != INT32_MIN &&
             inputs.pressureChangeCentiHpa3h >= (held ? 300 : 350);
    case AlertId::GasCombustionLike:
      return gasDeparted &&
             inputs.co2RatePpmPerMinute != AirHistory::kNoTrend &&
             inputs.co2RatePpmPerMinute > 30 &&
             inputs.temperatureRateCentiPerMinute != AirHistory::kNoTrend &&
             inputs.temperatureRateCentiPerMinute > 50;
    case AlertId::GasNotPeople:
      return gasDeparted && inputs.co2RatePpmPerMinute != AirHistory::kNoTrend &&
             inputs.co2RatePpmPerMinute < 10;
    case AlertId::GasDeparture:
      return gasDeparted;
    case AlertId::ModulesDisagree: {
      if (inputs.temperatureCentiCelsius == INT32_MIN) return false;
      if (inputs.secondaryTemperatureCentiCelsius == INT32_MIN) return false;
      const std::int32_t difference =
          inputs.temperatureCentiCelsius - inputs.secondaryTemperatureCentiCelsius;
      const std::int32_t magnitude = difference < 0 ? -difference : difference;
      return magnitude > (held ? 200 : 300);
    }
    case AlertId::ReadingStale:
      return inputs.readingAgeSeconds > (held ? 30 : 60);
  }
  return false;
}

inline AlertFamily alertFamily(AlertId id) {
  return kAlertRules[static_cast<std::size_t>(id)].family;
}

class AlertEngine {
 public:
  // One evaluation per sensor publication, which is every five seconds. Time is
  // the device's own monotonic seconds and never the wall clock: a cube with no
  // network has correct intervals and no date, and an alert that needed the date
  // would be disabled exactly where it is most useful.
  AlertVerdict evaluate(const AlertInputs& inputs, const AlertPolicy& policy,
                        std::uint32_t nowSeconds) {
    AlertVerdict verdict;
    if (!policy.enabled) {
      for (auto& state : states_) state = State{};
      return verdict;
    }
    // A stale reading silences everything derived from it. This is checked
    // first, and separately, because every other rule below is a statement about
    // air that the device is no longer measuring.
    const bool stale = inputs.readingAgeSeconds > 60;

    for (std::size_t index = 1; index < kAlertCount; ++index) {
      const AlertRule& rule = kAlertRules[index];
      State& state = states_[index];
      const bool suppressed =
          stale && rule.id != AlertId::ReadingStale;
      const bool holds =
          !suppressed && alertConditionHolds(rule.id, inputs, state.active);

      if (!holds) {
        if (state.active) {
          state.active = false;
          state.clearedAtSeconds = nowSeconds;
          state.everCleared = true;
        }
        state.dwell = 0;
        continue;
      }
      if (state.active) continue;
      // Cooling: the same air balancing near a threshold must not produce a run
      // of identical alerts.
      if (state.everCleared &&
          nowSeconds - state.clearedAtSeconds < rule.rearmMinutes * 60U) {
        state.dwell = 0;
        continue;
      }
      if (state.dwell < 255) ++state.dwell;
      if (state.dwell >= rule.dwell) state.active = true;
    }

    // One at a time, by level and then by catalogue order. The rest stay active
    // and appear when this one clears; there is no queue and no rotation,
    // because an alert that changes every three seconds is not read.
    for (std::size_t index = 1; index < kAlertCount; ++index) {
      const AlertRule& rule = kAlertRules[index];
      if (!states_[index].active) continue;
      if (muted(rule.family, policy)) continue;
      if (verdict.id != AlertId::None && rule.level <= verdict.level) continue;
      verdict.id = rule.id;
      verdict.level = rule.level;
    }
    if (verdict.id == AlertId::None) return verdict;

    verdict.level = applyPolicy(verdict.level, policy);
    verdict.wake = verdict.level >= AlertLevel::Alert;
    verdict.sound = verdict.level == AlertLevel::ActNow;
    return verdict;
  }

  bool isActive(AlertId id) const {
    return states_[static_cast<std::size_t>(id)].active;
  }

 private:
  struct State {
    std::uint8_t dwell = 0;
    bool active = false;
    bool everCleared = false;
    std::uint32_t clearedAtSeconds = 0;
  };

  static bool muted(AlertFamily family, const AlertPolicy& policy) {
    switch (family) {
      case AlertFamily::Humidity: return policy.muteHumidity;
      case AlertFamily::Pressure: return policy.mutePressure;
      case AlertFamily::Gas: return policy.muteGas;
      case AlertFamily::Co2:
      case AlertFamily::Core: return false;
    }
    return false;
  }

  static AlertLevel applyPolicy(AlertLevel level, const AlertPolicy& policy) {
    if (policy.quietHoursActive) {
      const bool exempt =
          policy.allowActNowWhenQuiet && level == AlertLevel::ActNow;
      if (!exempt && level > AlertLevel::Ambient) {
        level = static_cast<AlertLevel>(static_cast<std::uint8_t>(level) - 1);
      }
    }
    if (level > policy.highest) level = policy.highest;
    return level;
  }

  std::array<State, kAlertCount> states_{};
};

// The sentence itself, in both languages, beside the rule it belongs to rather
// than in the renderer. A wording that lived at the draw site could be changed
// without anybody reading the threshold it is making a claim about.
inline const char* alertSentence(AlertId id, bool polish) {
  switch (id) {
    case AlertId::None: return nullptr;
    case AlertId::Co2High: return polish ? "Wywietrz teraz" : "Air it now";
    case AlertId::Co2VeryStale:
      return polish ? "Powietrze krąży tu od godzin"
                    : "This air has been recirculating";
    case AlertId::Co2Stale: return polish ? "Wywietrz pokój" : "Air the room";
    case AlertId::Co2Forecast: return polish ? "Zaraz będzie duszno" : "It is heading for stale";
    case AlertId::Airing: return polish ? "Wietrzenie" : "Airing";
    case AlertId::DewPointRisk:
      return polish ? "Zimne ściany teraz mokre" : "Cold walls are wet now";
    case AlertId::HumidityDamp: return polish ? "Wilgotno od godzin" : "Damp for hours";
    case AlertId::HumidityDry: return polish ? "Bardzo sucho" : "Very dry";
    case AlertId::PressureStorm: return polish ? "Możliwa burza" : "A storm is possible";
    case AlertId::PressureChanging: return polish ? "Pogoda się zmienia" : "The weather is changing";
    case AlertId::PressureClearing: return polish ? "Rozpogadza się" : "It is clearing";
    case AlertId::GasCombustionLike:
      return polish ? "Sprawdź, co się dzieje" : "Check what is happening";
    case AlertId::GasNotPeople: return polish ? "To nie ludzie" : "This is not people";
    case AlertId::GasDeparture:
      return polish ? "Coś się zmieniło w powietrzu" : "Something changed in the air";
    case AlertId::ModulesDisagree:
      return polish ? "Moduły się nie zgadzają" : "The modules disagree";
    case AlertId::ReadingStale: return polish ? "Odczyt nieaktualny" : "Reading is stale";
  }
  return nullptr;
}

}  // namespace open_radio
