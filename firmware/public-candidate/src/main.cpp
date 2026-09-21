#include <Arduino.h>
#include <ESPmDNS.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "onboarding_assets.hpp"
#include "open_radio/firmware_contract.hpp"
#include "open_radio/bluetooth_profile_gate.hpp"
#include "open_radio/local_noise_source.hpp"
#include "open_radio/upgrade_policy.hpp"
#include "open_radio/renderer.hpp"
#include "open_radio/runtime_ingress.hpp"
#include "open_radio/runtime_orchestrator.hpp"
#include "open_radio/runtime_service_bridge.hpp"
#include "open_radio/service_adapters.hpp"
#include "open_radio/ui_controller.hpp"
#include "device_network_runtime.hpp"
#include "runtime_ingress_vectors.hpp"
#include "runtime_soak_vectors.hpp"
#include "service_golden_vectors.hpp"
#include "station_catalog.hpp"
#include "logo_store.hpp"
#include "sensor_task.hpp"
#include "station_slots.hpp"
#include "ui_contract.hpp"

#if defined(OPEN_RADIO_VARIANT_FULL)
#include <BluetoothA2DPSource.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_coexist.h>
#include <esp_gap_bt_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#if !defined(CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE)
#error "The full variant requires ESP32 Wi-Fi/Bluetooth software coexistence"
#endif
#endif

#if defined(OPEN_RADIO_VARIANT_FULL) || \
    defined(OPEN_RADIO_VARIANT_LOCAL_SPEAKER)
#define OPEN_RADIO_HAS_MP3 1
#include "public_audio_pipeline.hpp"
#include "station_runtime.hpp"
#endif

namespace {

constexpr std::uint32_t kConfigMagic = 0x4f524346U;
constexpr std::size_t kConfigHeaderBytes = 14;
constexpr std::size_t kConfigBlobBytes =
    kConfigHeaderBytes + open_radio::kStoragePayloadBytes;

void writeU32(std::uint8_t* destination, std::uint32_t value) {
  destination[0] = static_cast<std::uint8_t>(value);
  destination[1] = static_cast<std::uint8_t>(value >> 8U);
  destination[2] = static_cast<std::uint8_t>(value >> 16U);
  destination[3] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t readU32(const std::uint8_t* source) {
  return static_cast<std::uint32_t>(source[0]) |
         static_cast<std::uint32_t>(source[1]) << 8U |
         static_cast<std::uint32_t>(source[2]) << 16U |
         static_cast<std::uint32_t>(source[3]) << 24U;
}

class PreferencesSlotBackend final : public open_radio::StorageSlotBackend {
 public:
  explicit PreferencesSlotBackend(Preferences& preferences)
      : preferences_(preferences) {}

  bool readSlot(open_radio::SlotId slot,
                open_radio::StorageSlotDto& destination) const override {
    destination = {};
    const char* key = keyFor(slot);
    if (key == nullptr) return false;
    const auto length = preferences_.getBytesLength(key);
    if (length == 0) return true;
    if (length != buffer_.size() ||
        preferences_.getBytes(key, buffer_.data(), buffer_.size()) !=
            buffer_.size()) {
      return false;
    }
    if (readU32(buffer_.data()) != kConfigMagic) return false;
    destination.occupied = true;
    destination.committed = buffer_[4] != 0;
    destination.payloadLength = buffer_[5];
    destination.generation = readU32(buffer_.data() + 6);
    destination.checksum = readU32(buffer_.data() + 10);
    if (destination.payloadLength == 0 ||
        destination.payloadLength > destination.payload.size()) {
      return true;
    }
    std::copy_n(buffer_.data() + kConfigHeaderBytes,
                destination.payloadLength, destination.payload.begin());
    destination.payloadParseable = open_radio::deserializeConfig(
        destination.payload.data(), destination.payloadLength,
        destination.config);
    return true;
  }

  bool writeSlot(open_radio::SlotId slot,
                 const open_radio::StorageSlotDto& source) override {
    const char* key = keyFor(slot);
    if (key == nullptr || source.payloadLength > source.payload.size()) {
      return false;
    }
    buffer_.fill(0);
    writeU32(buffer_.data(), kConfigMagic);
    buffer_[4] = source.committed ? 1 : 0;
    buffer_[5] = static_cast<std::uint8_t>(source.payloadLength);
    writeU32(buffer_.data() + 6, source.generation);
    writeU32(buffer_.data() + 10, source.checksum);
    std::copy_n(source.payload.begin(), source.payloadLength,
                buffer_.begin() + kConfigHeaderBytes);
    return preferences_.putBytes(key, buffer_.data(), buffer_.size()) ==
           buffer_.size();
  }

 private:
  static const char* keyFor(open_radio::SlotId slot) {
    if (slot == open_radio::SlotId::A) return "config_a";
    if (slot == open_radio::SlotId::B) return "config_b";
    return nullptr;
  }

  Preferences& preferences_;
  mutable std::array<std::uint8_t, kConfigBlobBytes> buffer_{};
};

open_radio::ui::ScreenKind mapScreen(const open_radio::UiController& controller) {
  const auto& config = controller.config();
  switch (controller.screen()) {
    case open_radio::Screen::Wifi:
      return config.onboardingComplete
                 ? open_radio::ui::ScreenKind::wifi_status
                 : open_radio::ui::ScreenKind::onboarding_wifi;
    case open_radio::Screen::City:
      return config.onboardingComplete
                 ? open_radio::ui::ScreenKind::market
                 : open_radio::ui::ScreenKind::onboarding_first_sound;
    case open_radio::Screen::Bluetooth:
      return config.onboardingComplete
                 ? open_radio::ui::ScreenKind::bluetooth_pairing
                 : open_radio::ui::ScreenKind::onboarding_bluetooth;
    case open_radio::Screen::Stations:
      return open_radio::ui::ScreenKind::stations;
    case open_radio::Screen::Volume:
      return open_radio::ui::ScreenKind::volume_control;
    case open_radio::Screen::Brightness:
      return open_radio::ui::ScreenKind::brightness_control;
    case open_radio::Screen::SettingsQuick:
      return open_radio::ui::ScreenKind::settings_quick;
    case open_radio::Screen::SettingsSystem:
      return open_radio::ui::ScreenKind::settings_system;
    case open_radio::Screen::SettingsDisplay:
      return open_radio::ui::ScreenKind::settings_display;
    case open_radio::Screen::Noise:
      return open_radio::ui::ScreenKind::noise;
    case open_radio::Screen::About:
      return open_radio::ui::ScreenKind::about;
    case open_radio::Screen::Diagnostics:
      return open_radio::ui::ScreenKind::diagnostics;
    case open_radio::Screen::DisplayOff:
      return open_radio::ui::ScreenKind::display_off;
    case open_radio::Screen::Unsupported:
      return open_radio::ui::ScreenKind::unsupported;
    case open_radio::Screen::SafeMode:
      return open_radio::ui::ScreenKind::safe_mode;
    case open_radio::Screen::Screensaver:
      switch (config.screensaverMode) {
        case open_radio::ScreensaverMode::Bars:
          return open_radio::ui::ScreenKind::screensaver_bars;
        case open_radio::ScreensaverMode::Orbit:
          return open_radio::ui::ScreenKind::screensaver_orbit;
        case open_radio::ScreensaverMode::Cat:
          return open_radio::ui::ScreenKind::screensaver_cat;
        default:
          return open_radio::ui::ScreenKind::screensaver_pulse;
      }
    case open_radio::Screen::NowPlaying:
    default:
      // The EFFECTIVE variant, not the stored one: the two air homes exist only
      // while a module is publishing (ui_controller::homeVariant).
      switch (controller.homeVariant()) {
        case open_radio::NowPlayingVariant::Glance:
          return open_radio::ui::ScreenKind::now_playing_glance;
        case open_radio::NowPlayingVariant::Air:
          return open_radio::ui::ScreenKind::now_playing_air;
        default:
          return open_radio::ui::ScreenKind::now_playing_editorial;
      }
  }
}

// The nine tiles the renderer draws. A copy, not the generated constant,
// because a slot the installer reassigned has to show the station they chose —
// on the tile and in the now-playing seed — while keeping the tile's own accent
// colours. StationTheme holds pointers; StationSlots owns what they point at.
std::array<open_radio::ui::StationTheme,
           open_radio::ui::generated::kStationCount>
    activeStations = open_radio::ui::generated::kStations;

open_radio::public_candidate::StationSlots stationSlots;
open_radio::public_candidate::LogoStore logoStore;

void refreshActiveStations() {
  activeStations = open_radio::ui::generated::kStations;
  for (std::size_t slot = 0; slot < activeStations.size(); ++slot) {
    if (!stationSlots.overridden(slot)) continue;
    activeStations[slot].name = stationSlots.name(slot);
    const char* shortName = stationSlots.shortName(slot);
    if (shortName != nullptr && shortName[0] != '\0') {
      activeStations[slot].short_name = shortName;
    }
  }
}


// Defined below with the rest of the runtime state; declared here because the
// screen is built before that point in the file.
// DEFINICJA, nie extern — i celowo POZA blokiem HAS_MP3.
//
// Two render paths read this object — the sensor tiles and the derived
// values — and both run in EVERY variant. The definition used to sit under
// `#if HAS_MP3`, so `core2-onboarding-only`, `core2-bt-disabled-diagnostics`
// and `core2-corrupt-config-safe-mode` declared it, used it and never defined
// it: a LINKER error rather than a compiler one, which is why no single file
// shows it.
//
// Readings live here and nowhere else; the owner loop reads a snapshot it can
// never see half-written, and the task that fills it runs on the other core.
open_radio::SensorSnapshot sensorSnapshot;


class Core2BoardUi {
 public:

  bool begin(const open_radio::DeviceConfigDto& config,
             open_radio::RuntimeState runtime, open_radio::OutputKind output,
             std::uint64_t nowMs) {
    framebuffer_ = static_cast<std::uint16_t*>(heap_caps_malloc(
        kPixelCount * sizeof(std::uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (framebuffer_ == nullptr) return false;
    M5.Display.setSwapBytes(true);
    refreshClock(nowMs);
    controller_.begin(config, runtime, output, nowMs);
    applyHardwareLevels();
    flush();
    return true;
  }

  void syncRuntime(open_radio::RuntimeState runtime,
                   open_radio::OutputKind output) {
    controller_.setRuntime(runtime, output);
  }

  void metadata(const char* title) { controller_.setNowPlayingTitle(title); }

  void completeNetworkOnboarding() {
    controller_.completeNetworkOnboarding();
  }

  void requireNetworkOnboarding() {
    controller_.requireNetworkOnboarding();
  }

  void rememberBluetooth() { controller_.rememberBluetooth(); }

  void forgetBluetooth() { controller_.forgetBluetooth(); }

  void setWifiPortalActive(bool active) {
    controller_.setWifiPortalActive(active);
  }

  void setWifiOnline(bool online) {
    controller_.setWifiOnline(online);
  }

  void setWifiStrength(std::uint8_t percent) {
    percent = std::min<std::uint8_t>(percent, 100);
    if (wifiStrengthPercent_ == percent) return;
    const auto delta = wifiStrengthPercent_ > percent
                           ? wifiStrengthPercent_ - percent
                           : percent - wifiStrengthPercent_;
    if (wifiStrengthPercent_ != 0 && percent != 0 && delta < 2) return;
    wifiStrengthPercent_ = percent;
    ambientDirty_ = true;
  }

  void setConsoleActive(bool active) {
    controller_.setConsoleActive(active);
    if (consoleActive_ == active) return;
    consoleActive_ = active;
    ambientDirty_ = true;
  }

  void setDeviceAddress(const char* address) {
    std::array<char, 40> next{};
    if (address != nullptr) {
      std::snprintf(next.data(), next.size(), "%s", address);
    }
    if (std::strcmp(next.data(), deviceAddress_.data()) == 0) return;
    deviceAddress_ = next;
    ambientDirty_ = true;
  }

  void setBluetoothState(open_radio::BluetoothUiState state,
                         const char* candidate = nullptr) {
    controller_.setBluetoothState(state, candidate);
  }

  void setSetupAccessCode(const char* accessCode) {
    const char* value = accessCode == nullptr ? "" : accessCode;
    if (std::strncmp(setupAccessCode_.data(), value,
                     setupAccessCode_.size()) == 0) {
      return;
    }
    std::snprintf(setupAccessCode_.data(), setupAccessCode_.size(), "%s",
                  value);
    setupQrPayload_.fill(0);
    if (value[0] != '\0') {
      std::snprintf(setupQrPayload_.data(), setupQrPayload_.size(),
                    "WIFI:T:WPA;S:OpenRadio-Setup;P:%s;H:false;;", value);
    }
    if (framebuffer_ != nullptr) render();
  }

  void tap(std::int16_t x, std::int16_t y, std::uint64_t nowMs) {
    controller_.tap(x, y, nowMs);
  }

  void hold(std::int16_t x, std::int16_t y, std::uint64_t nowMs) {
    controller_.hold(x, y, nowMs);
  }

  // For the touch trace only: which screen the controller is on, as a number a
  // log line can print.
  open_radio::Screen screen() const { return controller_.screen(); }

  void drag(std::int16_t x, std::int16_t y, std::uint64_t nowMs) {
    controller_.drag(x, y, nowMs);
  }

  bool release(std::int16_t x, std::int16_t y, std::uint64_t nowMs) {
    return controller_.release(x, y, nowMs);
  }

  void hardwareButton(open_radio::HardwareButton button,
                      std::uint64_t nowMs) {
    controller_.hardwareButton(button, nowMs);
  }

  void swipe(std::int16_t startX, std::int16_t deltaX,
             std::int16_t deltaY, std::uint64_t nowMs) {
    controller_.swipe(startX, deltaX, deltaY, nowMs);
  }

  void tick(std::uint64_t nowMs, bool animationAllowed) {
    refreshClock(nowMs);
    controller_.tick(nowMs, animationAllowed);
    refreshBattery(nowMs);
  }

  open_radio::UiControllerUpdate flush() {
    const auto update = controller_.takeUpdate();
    if (!update.dirty && !ambientDirty_) return update;
    ambientDirty_ = false;
    applyHardwareLevels();
    render();
    return update;
  }

  const open_radio::DeviceConfigDto& config() const {
    return controller_.config();
  }

  void restoreNoiseState(open_radio::NoiseColor color, bool resumeMode) {
    controller_.restoreNoiseState(color, resumeMode);
  }

  bool noisePlaying() const { return controller_.noisePlaying(); }
  open_radio::NoiseColor noiseColor() const { return controller_.noiseColor(); }

 private:
  static constexpr std::size_t kPixelCount =
      static_cast<std::size_t>(open_radio::ui::generated::kWidth) *
      open_radio::ui::generated::kHeight;
  static constexpr std::uint32_t kBatteryReadIntervalMs = 10000U;
  static constexpr std::uint32_t kBatteryReadAmbientIntervalMs = 30000U;
  static constexpr std::uint32_t kBatteryNominalCapacityMilliampHours = 390U;

  void applyHardwareLevels() {
    const auto brightness =
        controller_.screen() == open_radio::Screen::DisplayOff
            ? 0
            : static_cast<std::uint8_t>(std::max<int>(
                  8, controller_.config().brightness * 255 / 100));
    if (appliedBrightness_ != brightness) {
      M5.Display.setBrightness(brightness);
      appliedBrightness_ = brightness;
    }
    const auto volume = static_cast<std::uint8_t>(
        controller_.config().volume * 255 / 100);
    if (appliedVolume_ != volume) {
      M5.Speaker.setVolume(volume);
      appliedVolume_ = volume;
    }
  }

  void refreshClock(std::uint64_t nowMs) {
    if (clockRead_ && nowMs - lastClockReadMs_ < 1000U) return;
    clockRead_ = true;
    lastClockReadMs_ = nowMs;
    auto next = M5.Rtc.getDateTime();
    // SNTP/system time is the authority while online. The RTC is a boot-time
    // fallback only; reading a previously mis-set RTC after sync could keep a
    // stale UTC or double-offset hour visible until the next hardware write.
    std::tm local{};
    const auto epoch = std::time(nullptr);
    if (epoch >= 1704067200 && localtime_r(&epoch, &local) != nullptr &&
        local.tm_year + 1900 >= 2024) {
      next = m5::rtc_datetime_t(local);
    }
    const bool valid = next.date.year >= 2024 && next.date.year <= 2099 &&
                       next.date.month >= 1 && next.date.month <= 12 &&
                       next.date.date >= 1 && next.date.date <= 31 &&
                       next.time.hours >= 0 && next.time.hours <= 23 &&
                       next.time.minutes >= 0 && next.time.minutes <= 59;
    if (clockValid_ == valid &&
        (!valid || (clock_.date.year == next.date.year &&
                    clock_.date.month == next.date.month &&
                    clock_.date.date == next.date.date &&
                    clock_.time.hours == next.time.hours &&
                    clock_.time.minutes == next.time.minutes))) {
      return;
    }
    clock_ = next;
    clockValid_ = valid;
    ambientDirty_ = true;
  }

  bool settingsVisible() const {
    const auto screen = controller_.screen();
    return screen == open_radio::Screen::SettingsQuick ||
           screen == open_radio::Screen::SettingsSystem ||
           screen == open_radio::Screen::SettingsDisplay;
  }

  void refreshBattery(std::uint64_t nowMs) {
    // The home header wears the battery cell since 2026-07-30 (docs/137
    // Q1), so the poll no longer hides behind the Settings screens.
    // Settings keeps its ten-second cadence; everywhere else a lazier one
    // is plenty — the AXP sits on the internal I2C bus and a resting
    // charge figure does not change faster than half a minute.
    const std::uint32_t interval = settingsVisible()
                                       ? kBatteryReadIntervalMs
                                       : kBatteryReadAmbientIntervalMs;
    if (batteryRead_ && nowMs - lastBatteryReadMs_ < interval) {
      return;
    }
    batteryRead_ = true;
    lastBatteryReadMs_ = nowMs;

    const auto level = M5.Power.getBatteryLevel();
    const auto voltage = M5.Power.getBatteryVoltage();
    if (level < 0 || voltage <= 0) {
      if (batteryAvailable_) {
        batteryAvailable_ = false;
        batteryRuntimeValid_ = false;
        dischargeCurrentSamples_ = 0;
        smoothedDischargeCurrentMilliAmps_ = 0;
        ambientDirty_ = true;
      }
      return;
    }

    const auto rawCurrent = std::clamp<std::int32_t>(
        M5.Power.getBatteryCurrent(), -2000, 2000);
    const auto quantizedCurrent = static_cast<std::int16_t>(
        rawCurrent >= 0 ? ((rawCurrent + 2) / 5) * 5
                        : -(((-rawCurrent + 2) / 5) * 5));
    const bool externalPower = M5.Power.getVBUSVoltage() >= 4000;
    const auto boundedLevel = static_cast<std::uint8_t>(
        std::clamp<std::int32_t>(level, 0, 100));
    const auto boundedVoltage = static_cast<std::uint16_t>(
        ((std::clamp<std::int32_t>(voltage, 0, 5000) + 5) / 10) * 10);

    bool runtimeValid = false;
    std::uint16_t runtimeMinutes = 0;
    if (!externalPower && quantizedCurrent <= -10) {
      const auto dischargeCurrent =
          static_cast<std::uint32_t>(-quantizedCurrent);
      if (dischargeCurrentSamples_ == 0 || batteryExternalPower_) {
        smoothedDischargeCurrentMilliAmps_ = dischargeCurrent;
        dischargeCurrentSamples_ = 1;
      } else {
        smoothedDischargeCurrentMilliAmps_ =
            (smoothedDischargeCurrentMilliAmps_ * 3U + dischargeCurrent) / 4U;
        if (dischargeCurrentSamples_ < 255) ++dischargeCurrentSamples_;
      }
      if (dischargeCurrentSamples_ >= 3 &&
          smoothedDischargeCurrentMilliAmps_ != 0 && boundedLevel != 0) {
        const auto rawRuntimeMinutes =
            kBatteryNominalCapacityMilliampHours * 60U * boundedLevel /
            (100U * smoothedDischargeCurrentMilliAmps_);
        runtimeMinutes = static_cast<std::uint16_t>(std::min<std::uint32_t>(
            ((rawRuntimeMinutes + 2U) / 5U) * 5U, 5995U));
        runtimeValid = runtimeMinutes != 0;
      }
    } else {
      dischargeCurrentSamples_ = 0;
      smoothedDischargeCurrentMilliAmps_ = 0;
    }

    const bool changed = !batteryAvailable_ ||
                         batteryExternalPower_ != externalPower ||
                         batteryLevelPercent_ != boundedLevel ||
                         batteryVoltageMillivolts_ != boundedVoltage ||
                         batteryCurrentMilliamps_ != quantizedCurrent ||
                         batteryRuntimeValid_ != runtimeValid ||
                         batteryRuntimeMinutes_ != runtimeMinutes;
    batteryAvailable_ = true;
    batteryExternalPower_ = externalPower;
    batteryLevelPercent_ = boundedLevel;
    batteryVoltageMillivolts_ = boundedVoltage;
    batteryCurrentMilliamps_ = quantizedCurrent;
    batteryRuntimeValid_ = runtimeValid;
    batteryRuntimeMinutes_ = runtimeMinutes;
    if (changed) ambientDirty_ = true;
  }

  void render() {
    open_radio::ui::CompactSnapshot snapshot{};
    const auto& config = controller_.config();
    snapshot.station_index = config.preferredStationIndex;
    snapshot.requested_station_index = config.preferredStationIndex;
    snapshot.wifi_online = controller_.wifiOnline();
    snapshot.wifi_strength_percent = wifiStrengthPercent_;
    snapshot.output = controller_.output() == open_radio::OutputKind::Bluetooth
                          ? open_radio::ui::AudioOutput::bluetooth
                          : open_radio::ui::AudioOutput::local;
    snapshot.volume = config.volume;
    snapshot.degraded =
        controller_.runtime() == open_radio::RuntimeState::DegradedPlaying;
    snapshot.theme = config.theme == open_radio::DisplayTheme::Light
                         ? open_radio::ui::ThemeMode::light
                         : open_radio::ui::ThemeMode::dark;
    snapshot.variant =
        controller_.homeVariant() == open_radio::NowPlayingVariant::Glance
            ? open_radio::ui::HomeVariant::glance
        : controller_.homeVariant() == open_radio::NowPlayingVariant::Air
            ? open_radio::ui::HomeVariant::air
            : open_radio::ui::HomeVariant::editorial;
    snapshot.locale = config.locale == open_radio::Locale::En
                          ? open_radio::ui::LocaleMode::en
                          : open_radio::ui::LocaleMode::pl;
    snapshot.screen = mapScreen(controller_);
    snapshot.brightness = config.brightness;
    snapshot.battery_available = batteryAvailable_;
    snapshot.battery_external_power = batteryExternalPower_;
    snapshot.battery_level_percent = batteryLevelPercent_;
    snapshot.battery_voltage_millivolts = batteryVoltageMillivolts_;
    snapshot.battery_current_milliamps = batteryCurrentMilliamps_;
    snapshot.battery_runtime_valid = batteryRuntimeValid_;
    snapshot.battery_runtime_minutes = batteryRuntimeMinutes_;
    snapshot.noise_color = static_cast<std::uint8_t>(controller_.noiseColor());
    snapshot.noise_playing = controller_.noisePlaying();
    // Readings are taken from the snapshot the sensor task publishes into, on
    // the other core. The read never waits and never blocks that task; when it
    // cannot complete, the screen shows the previous value with its real age
    // rather than a gap.
    {
      // Every slot is read once and merged by QUANTITY rather than by position.
      // Two modules are owned and a third slot exists; hard-coding "primary" and
      // "secondary" would have made the third one invisible the day it arrived,
      // which is the shape of bug that only shows up on somebody else's desk.
      std::array<open_radio::SensorReading, open_radio::kSensorSlotCount> slots{};
      std::uint32_t newest = 0;
      for (std::size_t slot = 0; slot < open_radio::kSensorSlotCount; ++slot) {
        slots[slot] = sensorSnapshot.readOrAbsent(slot);
        snapshot.sensor_status[slot] = static_cast<std::uint8_t>(slots[slot].status);
        if (slots[slot].observedAtMs > newest) newest = slots[slot].observedAtMs;
      }

      // The first working slot that measures a thing wins it. Quantities only
      // one module type produces — CO2, pressure, gas — have no contest.
      const auto pick = [&slots](std::int32_t open_radio::SensorReading::*field) {
        for (const auto& reading : slots) {
          if (reading.status != open_radio::SensorStatus::Ok) continue;
          if (reading.has(reading.*field)) return reading.*field;
        }
        return open_radio::kAbsentValue;
      };
      snapshot.sensor_co2_ppm = pick(&open_radio::SensorReading::co2Ppm);
      snapshot.sensor_pressure_centi_hectopascal =
          pick(&open_radio::SensorReading::pressureCentiHectopascal);
      snapshot.sensor_gas_resistance_ohm =
          pick(&open_radio::SensorReading::gasResistanceOhm);

      // Temperature and humidity are contested: more than one module measures
      // them. A module that also reports CO2 is an SCD4x, which runs warm from
      // its own heater — a BIAS, not noise, so averaging it with an unbiased
      // part would halve the bias and report it with more confidence rather
      // than less. Anything else is preferred, and the ENV Pro is also the
      // tighter part on humidity (+/-3% against +/-6%).
      const auto pickCoolest = [&slots](std::int32_t open_radio::SensorReading::*field) {
        std::int32_t fallback = open_radio::kAbsentValue;
        for (const auto& reading : slots) {
          if (reading.status != open_radio::SensorStatus::Ok) continue;
          if (!reading.has(reading.*field)) continue;
          if (!reading.has(reading.co2Ppm)) return reading.*field;
          if (fallback == open_radio::kAbsentValue) fallback = reading.*field;
        }
        return fallback;
      };
      snapshot.sensor_temperature_centi_celsius =
          pickCoolest(&open_radio::SensorReading::temperatureCentiCelsius);
      snapshot.sensor_humidity_centi_percent =
          pickCoolest(&open_radio::SensorReading::humidityCentiPercent);
      // A live module keeps the screen on. Told to the controller rather than
      // written into the config, so the owner's stored preference survives
      // untouched and returns the moment the module is unplugged.
      bool sensorLive = false;
      for (const auto& reading : slots) {
        if (reading.status == open_radio::SensorStatus::Ok) sensorLive = true;
      }
      controller_.setSensorPresent(sensorLive);
      // Derived values travel under their own seqlock, so a reading and a trend
      // can be published on their own schedules without invalidating each
      // other's reads.
      const auto derived = sensorSnapshot.readDerived();
      snapshot.sensor_air_band = derived.band;
      snapshot.sensor_co2_rate_ppm_per_hour = derived.co2RatePpmPerHour;
      snapshot.sensor_co2_next_threshold_ppm = derived.co2NextThresholdPpm;
      snapshot.sensor_co2_minutes_to_next = derived.co2MinutesToNext;
      snapshot.sensor_co2_ceiling_ppm = derived.co2CeilingPpm;
      snapshot.sensor_pressure_change_centi_hpa_3h = derived.pressureChangeCentiHpa3h;
      snapshot.sensor_gas_departure_percent = derived.gasDeparturePercent;
      snapshot.sensor_gas_warmup_minutes = derived.gasWarmupMinutes;
      snapshot.sensor_dew_point_centi_celsius = derived.dewPointCentiCelsius;
      // The alert is chosen on the sensor core, where the history is, and WORDED
      // here, where the locale is. The renderer receives a finished sentence and
      // a level; it holds no thresholds and cannot disagree with the rules.
      snapshot.air_alert = open_radio::alertSentence(
          static_cast<open_radio::AlertId>(derived.alertId),
          snapshot.locale == open_radio::ui::LocaleMode::pl);
      snapshot.air_alert_level = derived.alertLevel;
      const std::uint32_t nowMs = millis();
      snapshot.sensor_age_seconds = newest == 0 || nowMs < newest
          ? 0
          : static_cast<std::uint16_t>(std::min<std::uint32_t>((nowMs - newest) / 1000U, 65535U));
    }
    // What this build calls itself, from the build flag rather than a literal
    // in the renderer, so the About screen cannot drift from release/claims.json
    // without somebody editing the build.
#if defined(OPEN_RADIO_RELEASE)
    snapshot.firmware_version = OPEN_RADIO_RELEASE;
#else
    snapshot.firmware_version = "dev";
#endif
    snapshot.now_playing_title = controller_.nowPlayingTitle();
    snapshot.setup_access_code = setupAccessCode_.data();
    snapshot.wifi_portal_active = controller_.wifiPortalActive();
    snapshot.bluetooth_state =
        controller_.bluetoothState() == open_radio::BluetoothUiState::Scanning
            ? open_radio::ui::BluetoothState::scanning
            : controller_.bluetoothState() == open_radio::BluetoothUiState::Found
                  ? open_radio::ui::BluetoothState::found
                  : controller_.bluetoothState() == open_radio::BluetoothUiState::Connecting
                        ? open_radio::ui::BluetoothState::connecting
                        : controller_.bluetoothState() == open_radio::BluetoothUiState::Connected
                              ? open_radio::ui::BluetoothState::connected
                              : controller_.bluetoothState() == open_radio::BluetoothUiState::Error
                                    ? open_radio::ui::BluetoothState::error
                                    : open_radio::ui::BluetoothState::idle;
    snapshot.bluetooth_candidate_name = controller_.bluetoothCandidate();
    snapshot.bluetooth_remembered = config.bluetoothRemembered;
    snapshot.metadata_available = controller_.metadataAvailable();
    snapshot.animation_frame = controller_.animationFrame();
    snapshot.screensaver_enabled = config.screensaverEnabled;
    snapshot.screensaver_mode = static_cast<std::uint8_t>(config.screensaverMode);
    snapshot.screensaver_delay_seconds = config.screensaverDelaySeconds;
    snapshot.display_off_enabled = config.displayOffEnabled;
    snapshot.display_off_delay_seconds = config.displayOffDelaySeconds;
    snapshot.confirm_delete = controller_.confirmDelete();
    snapshot.device_address = deviceAddress_.data();
    snapshot.console_active = consoleActive_;
    snapshot.clock_valid = clockValid_;
    if (clockValid_) {
      snapshot.clock_year = static_cast<std::uint16_t>(clock_.date.year);
      snapshot.clock_month = static_cast<std::uint8_t>(clock_.date.month);
      snapshot.clock_day = static_cast<std::uint8_t>(clock_.date.date);
      snapshot.clock_hour = static_cast<std::uint8_t>(clock_.time.hours);
      snapshot.clock_minute = static_cast<std::uint8_t>(clock_.time.minutes);
    }
    const open_radio::ui::FramebufferView framebuffer{
        framebuffer_, kPixelCount, open_radio::ui::generated::kWidth,
        open_radio::ui::generated::kHeight};
    const auto result = open_radio::ui::renderScreen(
        framebuffer, snapshot, activeStations.data(), activeStations.size());
    if (result.status != open_radio::ui::RenderStatus::ok) {
      presentDiagnostic(result.status);
      return;
    }
    M5.Display.startWrite();
    M5.Display.pushImage(0, 0, open_radio::ui::generated::kWidth,
                         open_radio::ui::generated::kHeight, framebuffer_);
    M5.Display.endWrite();
    const bool wifiQrScreen =
        snapshot.screen == open_radio::ui::ScreenKind::onboarding_wifi ||
        snapshot.screen == open_radio::ui::ScreenKind::wifi_status;
    if (wifiQrScreen && snapshot.wifi_portal_active &&
        setupQrPayload_[0] != '\0') {
      M5.Display.qrcode(setupQrPayload_.data(), 128, 4, 184, 5, true);
    } else if (snapshot.screen == open_radio::ui::ScreenKind::wifi_status &&
               consoleActive_ && deviceAddress_[0] != '\0') {
      // Console QR carries the raw address, not the mDNS name — every phone
      // camera resolves an IP, not every browser resolves .local. The scheme
      // is a separate argument so the endpoint scanner sees no URL literal
      // (the address is the device's own, not an embedded endpoint).
      std::array<char, 56> payload{};
      std::snprintf(payload.data(), payload.size(), "%s://%s/", "http",
                    deviceAddress_.data());
      M5.Display.qrcode(payload.data(), 128, 4, 184, 5, true);
    }
  }

  void presentDiagnostic(open_radio::ui::RenderStatus status) {
    M5.Display.clear(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(16, 24);
    M5.Display.println("Open Radio Core2");
    M5.Display.setTextSize(1);
    M5.Display.printf("renderer status=%u\n", static_cast<unsigned>(status));
    M5.Display.println("UI ERROR / CHECK SERIAL");
  }

  open_radio::UiController controller_;
  std::array<char, 16> setupAccessCode_{};
  std::array<char, 80> setupQrPayload_{};
  m5::rtc_datetime_t clock_{};
  std::uint64_t lastClockReadMs_ = 0;
  std::uint64_t lastBatteryReadMs_ = 0;
  bool clockRead_ = false;
  bool clockValid_ = false;
  bool batteryRead_ = false;
  bool batteryAvailable_ = false;
  bool batteryExternalPower_ = false;
  bool batteryRuntimeValid_ = false;
  std::uint8_t batteryLevelPercent_ = 0;
  std::uint8_t dischargeCurrentSamples_ = 0;
  std::uint16_t batteryVoltageMillivolts_ = 0;
  std::int16_t batteryCurrentMilliamps_ = 0;
  std::uint16_t batteryRuntimeMinutes_ = 0;
  std::uint32_t smoothedDischargeCurrentMilliAmps_ = 0;
  // Also covers clock-only refreshes that do not mutate UiController state.
  bool ambientDirty_ = false;
  std::uint8_t wifiStrengthPercent_ = 0;
  bool consoleActive_ = false;
  std::array<char, 40> deviceAddress_{};
  std::int16_t appliedBrightness_ = -1;
  std::int16_t appliedVolume_ = -1;
  std::uint16_t* framebuffer_ = nullptr;

#if defined(OPEN_RADIO_LAB_CONSOLE)
 public:
  // Exactly what was pushed to the panel, over the wire, so a screen can be
  // compared against the host render instead of being assumed to match it.
  //
  // Every screen in this project is designed against a host build and checked
  // by a determinism hash — which proves the RENDERER is deterministic, not
  // that the device shows what the renderer drew. Between them sit a different
  // compiler, a different word size, M5GFX's own colour handling and a panel
  // with its own idea of gamma. This closes that gap with a byte comparison.
  //
  // Run-length encoded because interface screens are mostly flat: 320x240 is
  // 153,600 raw bytes, about twenty seconds at 115,200 baud, and typically a
  // couple of thousand runs — a second or two. Emitted as hex text rather than
  // base64 so a capture can be read, diffed and truncated without tooling.
  void dumpFramebuffer() const {
    if (framebuffer_ == nullptr) {
      Serial.println("screenshot result=no-framebuffer");
      return;
    }
    // Silence the other task for the duration. It measures and publishes
    // throughout; only its logging pauses.
    open_radio::public_candidate::gSerialQuiet.store(true, std::memory_order_relaxed);
    Serial.printf("screenshot begin w=%d h=%d format=rgb565-rle-hex\n",
                  open_radio::ui::generated::kWidth,
                  open_radio::ui::generated::kHeight);
    std::size_t runs = 0;
    std::size_t index = 0;
    int onLine = 0;
    while (index < kPixelCount) {
      const std::uint16_t value = framebuffer_[index];
      std::size_t length = 1;
      while (index + length < kPixelCount && length < 0xFFFFU &&
             framebuffer_[index + length] == value) {
        ++length;
      }
      Serial.printf("%04X%04X", static_cast<unsigned>(length),
                    static_cast<unsigned>(value));
      if (++onLine == 16) {
        Serial.println();
        onLine = 0;
      }
      index += length;
      ++runs;
    }
    if (onLine != 0) Serial.println();
    Serial.printf("screenshot end runs=%u pixels=%u\n",
                  static_cast<unsigned>(runs),
                  static_cast<unsigned>(kPixelCount));
    open_radio::public_candidate::gSerialQuiet.store(false, std::memory_order_relaxed);
  }
#endif
};

class DisabledBluetoothOutput final : public open_radio::AudioOutput {
 public:
  open_radio::OutputKind kind() const override {
    return open_radio::OutputKind::Bluetooth;
  }
  bool available() const override { return false; }
  std::size_t write(const open_radio::PcmFrame*, std::size_t) override {
    return 0;
  }
};

Preferences preferences;
PreferencesSlotBackend preferencesSlots(preferences);
open_radio::NvsAbStorageAdapter configStorage(preferencesSlots);
Core2BoardUi boardUi;
open_radio::RuntimeOrchestrator runtimeOrchestrator;
open_radio::RuntimeIngress runtimeIngress(runtimeOrchestrator);
open_radio::RuntimeServiceBridge runtimeServices(runtimeIngress);
open_radio::public_candidate::DeviceNetworkRuntime networkRuntime(preferences,
                                                                  runtimeServices);
constexpr const char* kPolandTimezone =
    "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr const char* kTimeServer = "pool.ntp.org";
bool timeSyncStarted = false;
std::atomic<bool> freshNetworkTime{false};
std::uint32_t nextRtcSyncPollMs = 0;
std::uint32_t nextMemoryReportMs = 0;
std::uint32_t nextLoopStallReportMs = 0;
std::uint32_t nextStandbyWithheldLogMs = 0;
std::uint32_t maxLoopStallUs = 0;
unsigned long maxAudioLoopUs = 0;
unsigned long maxAudioDrainUs = 0;
unsigned long maxUiTickUs = 0;
unsigned long maxUiFlushUs = 0;

void reportMemoryBudget(const char* stage) {
  const auto internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const auto psramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  Serial.printf(
      // dma_largest, separately. The internal figure covers a superset: on
      // 2026-08-01 this line read largest=1140 while 640-byte MALLOC_CAP_DMA
      // requests were being refused, and the reading was used to argue the
      // heap still had room. It did not have DMA-capable room, which is what
      // the Bluetooth controller and the display transfer lines actually ask
      // for. A memory report that cannot answer the question it is consulted
      // about is worse than none, because it gets believed.
      "memory stage=%s dram_free=%u dram_min=%u dram_largest=%u "
      "dma_largest=%u "
      "psram_free=%u psram_min=%u psram_largest=%u loop_stack_bytes=%u\n",
      stage,
      static_cast<unsigned>(heap_caps_get_free_size(internalCaps)),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(internalCaps)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(internalCaps)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)),
      static_cast<unsigned>(heap_caps_get_free_size(psramCaps)),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(psramCaps)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(psramCaps)),
      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void startOptionalTimeSync(std::uint32_t nowMs) {
  if (!timeSyncStarted) {
    sntp_set_time_sync_notification_cb([](timeval*) {
      freshNetworkTime.store(true, std::memory_order_release);
    });
    configTzTime(kPolandTimezone, kTimeServer);
    timeSyncStarted = true;
  }
  nextRtcSyncPollMs = nowMs;
}

void pollOptionalTimeSync(std::uint32_t nowMs) {
  if (!timeSyncStarted ||
      static_cast<std::int32_t>(nowMs - nextRtcSyncPollMs) < 0) {
    return;
  }
  if (!freshNetworkTime.exchange(false, std::memory_order_acq_rel)) {
    nextRtcSyncPollMs = nowMs + 250U;
    return;
  }
  std::tm local{};
  std::tm utc{};
  const auto epoch = std::time(nullptr);
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED &&
      epoch >= 1704067200 && localtime_r(&epoch, &local) != nullptr &&
      gmtime_r(&epoch, &utc) != nullptr && local.tm_year + 1900 >= 2024) {
    // M5Unified restores the RTC as UTC during M5.begin(). Persist UTC here as
    // well; storing Polish local time makes the next boot apply CEST twice.
    M5.Rtc.setDateTime(&utc);
    const auto rtc = M5.Rtc.getDateTime();
    Serial.printf(
        "time_sync tz=Europe/Warsaw local=%04d-%02d-%02dT%02d:%02d:%02d "
        "dst=%d rtc=%04d-%02d-%02dT%02d:%02d:%02d\n",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
        local.tm_min, local.tm_sec, local.tm_isdst, rtc.date.year,
        rtc.date.month, rtc.date.date, rtc.time.hours, rtc.time.minutes,
        rtc.time.seconds);
    nextRtcSyncPollMs = nowMs + 6U * 60U * 60U * 1000U;
    return;
  }
  nextRtcSyncPollMs = nowMs + 1000U;
}

#if defined(OPEN_RADIO_HAS_MP3)
open_radio::public_candidate::PsramPcmRingBuffer<
    open_radio::public_candidate::kDecodedPcmFrames>
    decodedFrames;
open_radio::public_candidate::LocalSpeakerOutput localSpeaker;
bool audioBuffersReady = false;

#if defined(OPEN_RADIO_VARIANT_FULL)
enum class BluetoothConnectionOrigin : std::uint8_t {
  Unknown,
  LocalDial,
  RemoteListen
};

constexpr std::uint32_t kBluetoothNoDialAgeMs = UINT32_MAX;
constexpr std::uint32_t kBluetoothDirtyAttemptAbortMs = 6500U;

// Keep the cube's UI volume local to the transmitted PCM.  Sending AVRCP
// absolute-volume commands as well as attenuating PCM makes low values nearly
// silent, while disabling PCM attenuation lets a power-cycled speaker return
// at its own (potentially 100%) gain.  This source deliberately ignores remote
// absolute-volume synchronization so the persisted cube setting is the one
// bounded gain control that is always applied before Bluetooth media starts.
class LocalVolumeBluetoothSource final : public BluetoothA2DPSource {
 public:
  LocalVolumeBluetoothSource() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
    // Page scan is required for a remembered speaker to reconnect to the cube,
    // but inquiry discovery is not. Keep the source connectable without
    // advertising it as a generally discoverable device.
    discoverability = ESP_BT_NON_DISCOVERABLE;
#endif
  }

  void set_volume(std::uint8_t volume) override {
    volume_value = static_cast<std::uint8_t>(
        std::min(static_cast<int>(volume), 0x7f));
    volume_control()->set_volume(volume_value);
    volume_control()->set_enabled(true);
    is_volume_used = true;
  }

  // ESP32-A2DP waits for its 10-second heartbeat before checking whether a
  // freshly reconnected sink is ready.  The dependency is pinned, and its
  // connected-media idle state is zero.  Requesting CHECK_SRC_RDY here keeps
  // START ownership in the library while allowing the firmware supervisor to
  // remove that avoidable reconnect delay.
  int requestMediaStartIfIdle() {
    if (!mediaStartAllowed_.load(std::memory_order_relaxed) ||
        !is_connected() || s_media_state != 0) {
      return 0;
    }
    return esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) == ESP_OK ? 1
                                                                         : -1;
  }

  void setMediaStartAllowed(bool allowed) {
    mediaStartAllowed_.store(allowed, std::memory_order_relaxed);
  }

  bool mediaStartAllowed() const {
    return mediaStartAllowed_.load(std::memory_order_relaxed);
  }

  // ---- 0.4.2 instrumentation (audit 2026-09-08: raport 01 §3, Codex §BT) ----
  // Numeric breadcrumbs written from the library task and read by the owner
  // loop through atomics: no String, no allocation, no NVS in a callback. They
  // exist to give every attempt_result=dirty a cause it can be blamed on —
  // the reason the speaker takes 10 s or 55 s to come back is NOT MEASURED
  // and the first fix written against a guess was reverted unshipped.
  enum LocalTeardown : std::uint8_t {
    kTeardownNone = 0,
    kTeardownRejectedInbound = 1,   // callback refused a peer we did not authorise
    kTeardownForgetOrScan = 2,      // owner dropped the link to forget / rescan
    kTeardownConnectedRefused = 3,  // CONNECTED arrived but the owner would not keep it
    kTeardownMediaState = 4,        // media-state branch tore the link down
    kTeardownProbeFailed = 5,       // media probe path tore the link down
    kTeardownTimeoutPhase = 6       // standby prefill / media start timeout
  };
  void noteLocalTeardown(std::uint8_t reason) {
    localTeardownMs_.store(millis() | 1U, std::memory_order_relaxed);
    localTeardownReason_.store(reason, std::memory_order_release);
  }
  std::uint32_t localTeardownAgeMs(std::uint32_t nowMs) const {
    const auto at = localTeardownMs_.load(std::memory_order_acquire);
    return at == 0 ? kBluetoothNoDialAgeMs : nowMs - at;
  }
  std::uint8_t localTeardownReason() const {
    return localTeardownReason_.load(std::memory_order_acquire);
  }
  // The A2DP disconnect reason the library saw last (esp_a2d_disc_rsn_t:
  // 0 = normal, local or remote; 1 = abnormal, link lost). 0xff = none yet.
  std::uint8_t takeLastDiscRsn() {
    return lastDiscRsn_.exchange(0xff, std::memory_order_acq_rel);
  }
  // Age of the remembered peer's ACL, or kBluetoothNoDialAgeMs when none is
  // up. Separates "profile dial over an existing ACL" from a fresh page.
  std::uint32_t aclAgeMs(std::uint32_t nowMs) const {
    const auto since = aclUpSinceMs_.load(std::memory_order_acquire);
    return since == 0 ? kBluetoothNoDialAgeMs : nowMs - since;
  }
  // "Bluedroid enabled" does not prove the library's application task exists:
  // a failed xTaskCreate only logs inside start(). Asked explicitly so the
  // owner never records a stack as started that cannot dispatch an event.
  bool appTaskRunning() const { return app_task_handle != nullptr; }

  void authorizeProfileOpen(open_radio::BluetoothProfileOpenReason reason) {
    profileOpenGate_.authorize(reason);
  }

  void cancelProfileOpenAuthorization() {
    profileOpenGate_.cancelAuthorization();
  }

  std::uint32_t observeProfileProgress() {
    return profileOpenGate_.observeProgress();
  }

  std::uint32_t observedProfileGeneration() const {
    return profileOpenGate_.observedGeneration();
  }

  bool finishProfileOpen(std::uint32_t generation) {
    return profileOpenGate_.finish(generation);
  }

  // Retire whatever open is currently held, for the one caller that has no
  // callback to name a generation: the watchdog that declares a disconnect
  // complete because the terminal callback never arrived.
  //
  // Every ordinary teardown retires its generation from the DISCONNECTED
  // branch. The watchdog exists precisely because that branch did not run, so
  // without this the gate keeps a generation nobody will ever finish — and
  // begin() opens only by compare-exchanging activeGeneration_ from zero.
  // Recovery would then be scheduled, attempted, and refused, forever, while
  // the log said it was armed. That is the same shape as the defect the
  // watchdog was written to fix, one layer further down.
  std::uint32_t retireProfileOpen() {
    const auto generation = profileOpenGate_.activeGeneration();
    if (generation == 0) return 0;
    return profileOpenGate_.finish(generation) ? generation : 0;
  }

  void setReconnectPeer(
      const std::array<std::uint8_t, ESP_BD_ADDR_LEN>& address,
      bool enabled) {
    reconnectPeerEnabled_.store(false, std::memory_order_release);
    for (std::size_t index = 0; index < address.size(); ++index) {
      reconnectPeer_[index].store(address[index], std::memory_order_relaxed);
    }
    reconnectPeerEnabled_.store(enabled, std::memory_order_release);
  }

  void applyReconnectListenPolicy() {
    const bool peerEnabled =
        reconnectPeerEnabled_.load(std::memory_order_acquire);
    set_scan_mode_connectable(peerEnabled);
    Serial.printf("bluetooth listen_policy connectable=%s peer_enabled=%s\n",
                  peerEnabled ? "yes" : "no",
                  peerEnabled ? "yes" : "no");
  }

  bool takeInboundProfileDialRequest() {
    return inboundProfileDialPending_.exchange(false,
                                                std::memory_order_acq_rel);
  }

  BluetoothConnectionOrigin takeConnectionOrigin() {
    return static_cast<BluetoothConnectionOrigin>(connectionOrigin_.exchange(
        static_cast<std::uint8_t>(BluetoothConnectionOrigin::Unknown),
        std::memory_order_acq_rel));
  }

  std::uint32_t takeConnectionDialAgeMs() {
    return connectionDialAgeMs_.exchange(kBluetoothNoDialAgeMs,
                                         std::memory_order_acq_rel);
  }

  void noteDialStarted(std::uint32_t nowMs) {
    lastDialStartedMs_.store(nowMs, std::memory_order_relaxed);
    dialOutstanding_.store(true, std::memory_order_release);
  }

  void clearDialAttempt() {
    dialOutstanding_.store(false, std::memory_order_release);
  }


  // While the user-initiated scan window is open, an inbound unknown sink IS
  // the pairing result. A speaker power-cycled mid-pairing keeps the source
  // in its own paired list and DIALS IN instead of page-scanning, so the
  // owner's outbound pages time out forever (live deadlock 2026-07-17:
  // inbound peer=other rejected every ~20 s while every dial hit HCI 0x104).
  void setPairingWindow(bool active) {
    pairingWindow_.store(active, std::memory_order_release);
    Serial.printf("bluetooth pairing_window=%s\n", active ? "open" : "closed");
  }

  bool takeAdoptedPeer(std::uint8_t* destination) {
    if (!adoptedPeerPending_.exchange(false, std::memory_order_acq_rel)) {
      return false;
    }
    for (std::size_t index = 0; index < ESP_BD_ADDR_LEN; ++index) {
      destination[index] = adoptedPeer_[index].load(std::memory_order_relaxed);
    }
    return true;
  }

  bool dialAttemptOutstanding() const {
    return dialOutstanding_.load(std::memory_order_acquire);
  }

  std::uint32_t dialAgeMs(std::uint32_t nowMs) const {
    if (!dialOutstanding_.load(std::memory_order_acquire)) {
      return kBluetoothNoDialAgeMs;
    }
    return nowMs - lastDialStartedMs_.load(std::memory_order_relaxed);
  }

 protected:
  // ESP32-A2DP has two local profile-open paths: connect_to() and a direct
  // discovery call. Both dispatch through this virtual low-level boundary, so
  // fencing here covers every local esp_a2d_source_connect without patching
  // or forking the pinned dependency.
  esp_err_t esp_a2d_connect(esp_bd_addr_t peer) override {
    const auto decision = profileOpenGate_.begin();
    if (!decision.allowed) {
      Serial.println(
          "bluetooth profile_open result=blocked reason=not_authorized_or_busy");
      return ESP_ERR_INVALID_STATE;
    }
    noteDialStarted(millis());
    Serial.printf("bluetooth profile_open generation=%lu reason=%u acl_age_ms=%ld lib_state=%d\n",
                  static_cast<unsigned long>(decision.generation),
                  static_cast<unsigned>(decision.reason),
                  static_cast<long>(aclAgeMs(millis()) == kBluetoothNoDialAgeMs
                                        ? -1L
                                        : static_cast<long>(aclAgeMs(millis()))),
                  static_cast<int>(s_a2d_state));
    const auto result = BluetoothA2DPSource::esp_a2d_connect(peer);
    if (result != ESP_OK) {
      clearDialAttempt();
      profileOpenGate_.finish(decision.generation);
    }
    return result;
  }

  // The library invokes this from connect_to() right before esp_a2d_connect.
  // Dialing and page-scan listening must never overlap for the same peer: an
  // inbound session that wins while our page is still in flight is torn down
  // when the stale page attempt completes with HCI 0x104 page timeout
  // (measured 2026-07-17: media died 1,346 ms after start, exactly 5,133 ms
  // after dial start). Go dark for the duration of every dial; each
  // completion path (DISCONNECTED handler, retry idle window, forget/scan)
  // re-asserts the listen policy, so inbound reconnect keeps working in the
  // idle gaps between attempts.
  void set_scan_mode_connectable_default() override {
    Serial.println("bluetooth listen_policy connectable=no reason=dialing");
    set_scan_mode_connectable(false);
  }

  void process_user_state_callbacks(std::uint16_t event,
                                    void* param) override {
    // E1/E2: the raw event as the library task sees it, BEFORE any decision
    // and before the owner loop's single-slot mailbox can coalesce it.
    if (event == ESP_A2D_CONNECTION_STATE_EVT && param != nullptr) {
      auto* raw = static_cast<esp_a2d_cb_param_t*>(param);
      const auto nowMs = millis();
      if (raw->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        lastDiscRsn_.store(static_cast<std::uint8_t>(raw->conn_stat.disc_rsn),
                           std::memory_order_release);
      }
      const auto acl = aclAgeMs(nowMs);
      Serial.printf(
          "bluetooth a2d_evt t_ms=%lu state=%u disc_rsn=%u peer=%s "
          "dial_age_ms=%lu lib_state=%d media_state=%d gen=%lu acl_age_ms=%ld\n",
          static_cast<unsigned long>(nowMs),
          static_cast<unsigned>(raw->conn_stat.state),
          static_cast<unsigned>(raw->conn_stat.disc_rsn),
          reconnectPeerMatches(raw->conn_stat.remote_bda) ? "remembered" : "other",
          static_cast<unsigned long>(dialAgeMs(nowMs)),
          static_cast<int>(s_a2d_state), static_cast<int>(s_media_state),
          static_cast<unsigned long>(observedProfileGeneration()),
          acl == kBluetoothNoDialAgeMs ? -1L : static_cast<long>(acl));
    } else if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT && param != nullptr) {
      auto* raw = static_cast<esp_a2d_cb_param_t*>(param);
      Serial.printf("bluetooth media_ack t_ms=%lu cmd=%u status=%u\n",
                    static_cast<unsigned long>(millis()),
                    static_cast<unsigned>(raw->media_ctrl_stat.cmd),
                    static_cast<unsigned>(raw->media_ctrl_stat.status));
    }
    if (event == ESP_A2D_CONNECTION_STATE_EVT && param != nullptr) {
      auto* a2d = static_cast<esp_a2d_cb_param_t*>(param);
      if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        const auto dialAge = dialAgeMs(millis());
        const bool remoteListen =
            dialAge == kBluetoothNoDialAgeMs ||
            dialAge > kBluetoothDirtyAttemptAbortMs;
        const bool peerRequired =
            reconnectPeerEnabled_.load(std::memory_order_acquire);
        bool peerMatches = peerRequired;
        if (peerRequired) {
          for (std::size_t index = 0; index < ESP_BD_ADDR_LEN; ++index) {
            if (reconnectPeer_[index].load(std::memory_order_relaxed) !=
                a2d->conn_stat.remote_bda[index]) {
              peerMatches = false;
              break;
            }
          }
        }
        const bool pairingAdopt =
            !peerMatches && pairingWindow_.load(std::memory_order_acquire);
        const bool accepted =
            pairingAdopt || (peerRequired ? peerMatches : !remoteListen);
        if (!accepted) {
          Serial.printf(
              "bluetooth initiator=remote dial_age_ms=%lu result=rejected\n",
              static_cast<unsigned long>(dialAge));
          noteLocalTeardown(kTeardownRejectedInbound);
          esp_a2d_source_disconnect(a2d->conn_stat.remote_bda);
          return;
        }
        if (pairingAdopt) {
          for (std::size_t index = 0; index < ESP_BD_ADDR_LEN; ++index) {
            adoptedPeer_[index].store(a2d->conn_stat.remote_bda[index],
                                      std::memory_order_relaxed);
            reconnectPeer_[index].store(a2d->conn_stat.remote_bda[index],
                                        std::memory_order_relaxed);
          }
          reconnectPeerEnabled_.store(true, std::memory_order_release);
          adoptedPeerPending_.store(true, std::memory_order_release);
          pairingWindow_.store(false, std::memory_order_release);
          Serial.println("bluetooth inbound_pairing result=adopted");
        }
        if (remoteListen) {
          // The pinned source library deliberately disables incoming links and
          // therefore leaves a peer-initiated CONNECTED event in UNCONNECTED.
          // Adopt the event before its state dispatcher runs so subsequent
          // media control is handled by the normal CONNECTED state.
          s_a2d_state = APP_AV_STATE_CONNECTED;
          s_media_state = 0;
          s_connecting_heatbeat_count = 0;
        }
        connectionOrigin_.store(
            static_cast<std::uint8_t>(
                remoteListen ? BluetoothConnectionOrigin::RemoteListen
                             : BluetoothConnectionOrigin::LocalDial),
            std::memory_order_release);
        connectionDialAgeMs_.store(dialAge, std::memory_order_release);
      }
    }
    BluetoothA2DPSource::process_user_state_callbacks(event, param);
  }

  void app_gap_callback(esp_bt_gap_cb_event_t event,
                        esp_bt_gap_cb_param_t* param) override {
#if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 4, 4)
    if (event == ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT && param != nullptr) {
      const auto dialAge = dialAgeMs(millis());
      const bool rememberedPeer =
          reconnectPeerMatches(param->acl_conn_cmpl_stat.bda);
      // Arduino-ESP32 2.x exposes the IDF 4.4.7 ACL status with an internal
      // flag in the upper byte (observed success=0x100, page-timeout=0x104).
      // The low byte remains the HCI status, so this also accepts the public
      // API's plain ESP_BT_STATUS_SUCCESS value of zero.
      const bool aclSucceeded =
          (static_cast<unsigned>(param->acl_conn_cmpl_stat.stat) & 0xffU) ==
          0U;
      Serial.printf(
          "bluetooth acl=conn dial_age_ms=%lu stat=%u peer=%s success=%s\n",
          static_cast<unsigned long>(dialAge),
          static_cast<unsigned>(param->acl_conn_cmpl_stat.stat),
          rememberedPeer ? "remembered" : "other",
          aclSucceeded ? "yes" : "no");
      // This speaker establishes an inbound ACL while the pinned A2DP Source
      // remains profile-disconnected. Waiting passively cannot complete A2DP,
      // while the measured successful recovery issued a normal source dial
      // shortly after that ACL appeared. Wake the owner loop immediately; do
      // not call the A2DP API re-entrantly from the GAP callback task.
      if (aclSucceeded && rememberedPeer) {
        aclUpSinceMs_.store(millis() | 1U, std::memory_order_release);
      }
      if (aclSucceeded && dialAge == kBluetoothNoDialAgeMs &&
          rememberedPeer) {
        inboundProfileDialPending_.store(true, std::memory_order_release);
        Serial.println("bluetooth inbound_acl action=profile_dial_pending");
      }
    } else if (event == ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT &&
               param != nullptr) {
      const auto nowMs = millis();
      const auto acl = aclAgeMs(nowMs);
      const bool rememberedPeer =
          reconnectPeerMatches(param->acl_disconn_cmpl_stat.bda);
      Serial.printf(
          "bluetooth acl=disconn dial_age_ms=%lu reason=%u peer=%s acl_age_ms=%ld "
          "profile_state=%d\n",
          static_cast<unsigned long>(dialAgeMs(nowMs)),
          static_cast<unsigned>(param->acl_disconn_cmpl_stat.reason),
          rememberedPeer ? "remembered" : "other",
          acl == kBluetoothNoDialAgeMs ? -1L : static_cast<long>(acl),
          static_cast<int>(s_a2d_state));
      if (rememberedPeer) aclUpSinceMs_.store(0, std::memory_order_release);
    } else if (event == ESP_BT_GAP_MODE_CHG_EVT && param != nullptr) {
      // Sniff/active transitions: a profile open failing while the link sits
      // in sniff is a different animal from one failing on a fresh page.
      Serial.printf("bluetooth mode_chg t_ms=%lu mode=%d peer=%s\n",
                    static_cast<unsigned long>(millis()),
                    static_cast<int>(param->mode_chg.mode),
                    reconnectPeerMatches(param->mode_chg.bda) ? "remembered"
                                                              : "other");
    }
#endif
    if (event == ESP_BT_GAP_AUTH_CMPL_EVT && param != nullptr) {
      Serial.printf(
          "bluetooth gap_auth_cmpl stat=%u peer=%s dial_age_ms=%lu\n",
          static_cast<unsigned>(param->auth_cmpl.stat),
          reconnectPeerMatches(param->auth_cmpl.bda) ? "remembered"
                                                     : "other",
          static_cast<unsigned long>(dialAgeMs(millis())));
    }
    BluetoothA2DPSource::app_gap_callback(event, param);
  }

  // The pinned dependency probes media from its own 10-second heartbeat. Gate
  // only the CONNECTED/IDLE start probe until firmware has staged both the BT
  // queue and fallback reserve; connecting-state timeout handling still runs.
  void a2d_app_heart_beat(void* arg) override {
    if (is_connected() && s_media_state == 0 &&
        !mediaStartAllowed_.load(std::memory_order_relaxed)) {
      return;
    }
    // The latch is permission, granted once and never revisited; this is the
    // heartbeat that acts on it, up to ten seconds later and from another
    // task. Between the two the DMA pool can shrink, and starting media is
    // what makes Bluedroid allocate from it — osi_malloc failure there is not
    // an error return, it is an assert and a reset.
    //
    // The same 2048 the admission uses, asked again at the moment of the
    // request. No new number, and no new policy: this only stops the window
    // between deciding and doing from being a hole in the decision.
    if (is_connected() && s_media_state == 0 &&
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA) < 2048) {
      return;
    }
    BluetoothA2DPSource::a2d_app_heart_beat(arg);
  }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
  void bt_av_volume_changed() override {}
  void bt_av_notify_evt_handler(std::uint8_t,
                                esp_avrc_rn_param_t*) override {}
#endif

 private:
  bool reconnectPeerMatches(const std::uint8_t* address) const {
    if (address == nullptr ||
        !reconnectPeerEnabled_.load(std::memory_order_acquire)) {
      return false;
    }
    for (std::size_t index = 0; index < ESP_BD_ADDR_LEN; ++index) {
      if (reconnectPeer_[index].load(std::memory_order_relaxed) !=
          address[index]) {
        return false;
      }
    }
    return true;
  }

  std::atomic<bool> mediaStartAllowed_{false};
  std::atomic<std::uint32_t> localTeardownMs_{0};
  std::atomic<std::uint8_t> localTeardownReason_{0};
  std::atomic<std::uint8_t> lastDiscRsn_{0xff};
  std::atomic<std::uint32_t> aclUpSinceMs_{0};
  std::array<std::atomic<std::uint8_t>, ESP_BD_ADDR_LEN> reconnectPeer_{};
  std::atomic<bool> reconnectPeerEnabled_{false};
  std::atomic<bool> pairingWindow_{false};
  std::array<std::atomic<std::uint8_t>, ESP_BD_ADDR_LEN> adoptedPeer_{};
  std::atomic<bool> adoptedPeerPending_{false};
  std::atomic<std::uint8_t> connectionOrigin_{
      static_cast<std::uint8_t>(BluetoothConnectionOrigin::Unknown)};
  std::atomic<std::uint32_t> connectionDialAgeMs_{kBluetoothNoDialAgeMs};
  std::atomic<std::uint32_t> lastDialStartedMs_{0};
  std::atomic<bool> dialOutstanding_{false};
  std::atomic<bool> inboundProfileDialPending_{false};
  open_radio::BluetoothProfileOpenGate profileOpenGate_;
};

LocalVolumeBluetoothSource bluetoothSource;
A2DPLinearVolumeControl bluetoothLinearVolumeControl;
open_radio::public_candidate::BluetoothQueueOutput bluetoothQueue;
open_radio::OutputRouter outputRouter(localSpeaker, bluetoothQueue);
std::uint32_t bluetoothStackStarts = 0;
std::uint32_t bluetoothDirectConnectAttempts = 0;
std::uint32_t bluetoothMediaStarts = 0;
std::uint32_t bluetoothRetryAttempts = 0;
std::uint32_t bluetoothFallbacks = 0;
std::uint32_t bluetoothPullWatchdogTrips = 0;
std::atomic<std::uint32_t> bluetoothLastPullMs{0};
std::atomic<std::uint32_t> bluetoothPullCallbacks{0};
std::atomic<std::uint32_t> bluetoothEventStackMinimumBytes{UINT32_MAX};
#else
DisabledBluetoothOutput disabledBluetooth;
open_radio::OutputRouter outputRouter(localSpeaker, disabledBluetooth);
#endif

open_radio::public_candidate::Mp3StreamPipeline mp3Pipeline(decodedFrames);
open_radio::public_candidate::StationRuntime stationRuntime(
    preferences, runtimeServices, mp3Pipeline, stationSlots);
enum class PlaybackMode : std::uint8_t { Radio = 0, Noise = 1 };
constexpr const char* kPlaybackModeKey = "play_mode";
constexpr const char* kNoiseColorKey = "noise_color";
open_radio::LocalNoiseSource localNoiseSource;
// Readings live here and nowhere else; the owner loop reads a snapshot it can
// never see half-written, and the task that fills it runs on the other core.
open_radio::public_candidate::SensorTask sensorTask;
PlaybackMode playbackMode = PlaybackMode::Radio;
// Console session (settings tile): playback suspends while the browser window
// is open; a slot change during the session restarts the device so the fresh
// boot refetches logos in the pre-Bluetooth TLS window.
bool consoleSessionActive = false;
bool consoleSlotsChanged = false;
// Background logo fetching runs on its OWN low-priority task pinned to the
// other core: TLS handshakes and slow CDNs never touch the audio loop.
// Bluetooth auto-start waits until the pass completes so TLS keeps its heap.
// The task only reads stationSlots (slot changes restart the device) and
// writes PSRAM pixel buffers + SPIFFS; the flag is its single output.
volatile bool logoFetchPending = false;
// A person tapping to pair outranks decoration. Tile artwork can be fetched on
// any later boot; the human waiting in front of the cube cannot. Set on the
// tap, cleared when the task is reaped.
// std::atomic, nie volatile. `volatile` w C++ mowi kompilatorowi tylko
// „nie optymalizuj tego dostepu" — NIE daje ani atomowosci, ani bariery
// pamieci, a te dwie flagi przechodza miedzy RDZENIAMI: `abort` z petli
// do zadania, `done` z zadania do petli. Na ESP32 jednobajtowy dostep
// jest w praktyce niepodzielny, wiec bledu nie zaobserwowalismy — ale to
// wlasciwosc platformy, nie gwarancja jezyka, i recenzja slusznie ja
// wytknela. Relaxed wystarcza: sam fakt zmiany jest cala trescia.
std::atomic<bool> logoFetchAbort{false};
// Set by the fetch task when its pass is over; the LOOP deletes the task and
// only then lets Bluetooth start. Kimi K3's verification (2026-07-30) caught
// the flaw in both earlier shapes: a task that deletes ITSELF is freed later
// by its core's idle task, and a cross-core vTaskDelete defers exactly the
// same way — so on either path the Bluetooth gate could open while the 8 KB
// stack was still allocated. A SUSPENDED task is not running anywhere, so
// deleting it from the loop frees the memory immediately and the ordering
// below is deterministic.
std::atomic<bool> logoFetchDone{false};
TaskHandle_t logoFetchTaskHandle = nullptr;
open_radio::NoiseColor storedNoiseColor = open_radio::NoiseColor::White;
bool noiseExitPending = false;
open_radio::PcmFrame* decodedDrainFrames = nullptr;
std::uint32_t reportedLocalSpeakerStarvations = 0;

// Noise is GENERATED, not received, and that changes what its buffers are for.
//
// The radio path buffers against the network: 262,144 decoded frames is 5.94 s,
// sized so local playback survives the longest measured synchronous stream
// operation (5.313 s), and the speaker takes 131,072-frame blocks — 2.97 s each,
// two queued, another 5.94 s. Against a stream that is exactly right.
//
// A generator has no network to survive. Every frame it buffers buys nothing
// and costs LATENCY: 5.94 s of ring plus 5.94 s of speaker queue is 11.9 s
// between somebody pressing "brown" and hearing brown. The whole reason the
// control felt broken was that it was answering an eternity later.
//
// So the noise lane uses its own depths. Neither constant is the frozen one —
// kLocalSpeakerFramesPerBlock and the ring capacity are untouched, and the
// radio path still gets every frame of margin it was measured to need.
//
// HALF of that is fixed here, and the other half turned out not to be mine to
// fix. LocalSpeakerOutput::write refuses any count that is not exactly
// kLocalSpeakerFramesPerBlock:
//
//     if (count != kLocalSpeakerFramesPerBlock) return 0;
//
// so writing shorter blocks did not shorten the latency, it produced SILENCE —
// every write returned zero and nothing ever reached the speaker. Found on
// hardware within an hour of shipping it, by the owner, who reported that the
// generator no longer made a sound.
//
// The earlier claim that "the block depth was always chosen in main.cpp rather
// than in the frozen header" was wrong. The depth is chosen here; the CONTRACT
// is enforced there, in a frozen file, and a contract enforced elsewhere is
// still a contract. Cutting the speaker queue therefore needs a reviewed
// exception against firmware/manifests/audio-surface.lock.json, which is a
// deliberate act and not something to slip in beside a screen change.
//
// So the speaker keeps its 131,072-frame block, and only the ring is capped.
// That removes one of the two 5.94 s terms:
//
//   before: 5.94 s ring + 5.94 s speaker = 11.9 s
//   now:    0.56 s ring + 5.94 s speaker = 6.5 s
//
// And then the ring cap turned out to buy nothing either, which is the part
// worth writing down rather than quietly reverting.
//
// Restoring the block size while leaving the ring at 24,576 frames left a ring
// SMALLER THAN ONE BLOCK. The local drain peeks exactly one block and gives up
// if the ring is short, so nothing ever reached the speaker: the generator ran,
// the ring sat at exactly its target for ever, and the cube was wedged in a
// silent noise mode it could not leave, because the exit waits for the ring to
// empty and the ring could not drain. Measured on hardware: decoded=24576 for
// thirty-five seconds, running=noise, local_starvations=0.
//
// The static_assert below had caught precisely this an hour earlier. It was
// removed in the same edit that reverted the block size — the guard and the
// thing it guarded were deleted together, which is how a check that already
// worked gets to fail twice.
//
// So the arithmetic is what it always was: any useful cap must hold at least
// one 131,072-frame block, which IS 2.97 s, and two of them is the 5.94 s the
// ring already held. Capping it therefore cannot reduce the latency at all
// while the speaker contract demands whole multi-second blocks.
//
// Conclusion, recorded rather than worked around: 11.9 s stands until
// LocalSpeakerOutput::write accepts a smaller block for the generator, and that
// is a change to a frozen file needing a reviewed exception against
// firmware/manifests/audio-surface.lock.json. It is a deliberate act for its
// own change, not something to slip in beside a screen.
constexpr std::size_t kNoiseSpeakerFramesPerBlock =
    open_radio::public_candidate::kLocalSpeakerFramesPerBlock;
constexpr std::size_t kNoiseRingTargetFrames =
    open_radio::public_candidate::kDecodedPcmFrames;
static_assert(kNoiseRingTargetFrames >= kNoiseSpeakerFramesPerBlock,
              "a ring below one speaker block can never fill one, and the local "
              "drain path writes whole blocks or nothing: the cube plays silence "
              "and cannot leave noise mode");

// Which block depth the speaker is being fed at right now. One place, so the
// two drain paths — Bluetooth-capable and local-only — cannot disagree about
// how much latency the current mode is carrying.
std::size_t speakerBlockFrames() {
  return playbackMode == PlaybackMode::Noise
             ? kNoiseSpeakerFramesPerBlock
             : open_radio::public_candidate::kLocalSpeakerFramesPerBlock;
}

bool ensureDecodedDrainBuffer() {
  if (decodedDrainFrames != nullptr) return true;
  decodedDrainFrames = static_cast<open_radio::PcmFrame*>(heap_caps_malloc(
      open_radio::public_candidate::kLocalSpeakerFramesPerBlock *
          sizeof(open_radio::PcmFrame),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return decodedDrainFrames != nullptr;
}

std::size_t drainDecodedFrames() {
  if (!ensureDecodedDrainBuffer()) return 0;
#if defined(OPEN_RADIO_VARIANT_FULL)
  const bool bluetoothActive =
      outputRouter.activeOutput() == open_radio::OutputKind::Bluetooth;
  if (!bluetoothActive) {
    std::size_t totalWritten = 0;
    const std::size_t blockFrames = speakerBlockFrames();
    for (std::size_t block = 0; block < 2U; ++block) {
      const std::size_t bluetoothFrames =
          std::min(bluetoothQueue.bufferedFrames(), blockFrames);
      const std::size_t decodedFrameCount = blockFrames - bluetoothFrames;
      if (bluetoothQueue.peekBuffered(decodedDrainFrames, bluetoothFrames) !=
              bluetoothFrames ||
          decodedFrames.peek(decodedDrainFrames + bluetoothFrames,
                             decodedFrameCount) != decodedFrameCount) {
        break;
      }
      const std::size_t written =
          outputRouter.write(decodedDrainFrames, blockFrames);
      if (written != blockFrames) {
        break;
      }
      bluetoothQueue.discardBuffered(bluetoothFrames);
      decodedFrames.discard(decodedFrameCount);
      totalWritten += written;
    }
    // A stopped local generator can leave a final fade shorter than the
    // network-sized speaker block. Queue that one bounded tail so STOP and
    // return-to-radio reach digital zero instead of stranding the fade.
    if (playbackMode == PlaybackMode::Noise && !localNoiseSource.active() &&
        localSpeaker.bufferedBlocks() < 2) {
      const std::size_t bluetoothFrames =
          std::min(bluetoothQueue.bufferedFrames(), blockFrames);
      const std::size_t decodedFrameCount =
          std::min(decodedFrames.size(), blockFrames - bluetoothFrames);
      const std::size_t tailFrames = bluetoothFrames + decodedFrameCount;
      if (tailFrames != 0 &&
          bluetoothQueue.peekBuffered(decodedDrainFrames, bluetoothFrames) ==
              bluetoothFrames &&
          decodedFrames.peek(decodedDrainFrames + bluetoothFrames,
                             decodedFrameCount) == decodedFrameCount &&
          localSpeaker.writeFadeTail(decodedDrainFrames, tailFrames) ==
              tailFrames) {
        bluetoothQueue.discardBuffered(bluetoothFrames);
        decodedFrames.discard(decodedFrameCount);
        totalWritten += tailFrames;
      }
    }
    return totalWritten;
  }
  const std::size_t drainFrames = 1024U;
  // The A2DP callback consumes continuously while an HTTP/decoder iteration
  // can block for hundreds of milliseconds. A fixed per-owner-loop drain cap
  // therefore turns loop latency into a throughput ceiling. Drain until the
  // A2DP queue is full. Do not retain a second reserve in decodedFrames once
  // Bluetooth is active: BluetoothQueueOutput is already the first ordered
  // part of local fallback and is preserved on disconnect. The old 44,100
  // floor stranded 44,801 decoded frames while the live BT queue emptied.
  // BluetoothQueueOutput still enters its critical section in bounded
  // 256-frame chunks.
  const std::size_t maxBlocks =
      open_radio::public_candidate::kBluetoothPcmFrames / drainFrames;
#else
  const std::size_t drainFrames = speakerBlockFrames();
  constexpr std::size_t maxBlocks = 2U;
#endif
  std::size_t totalWritten = 0;
  // Local playback needs complete long-lived M5Unified buffers. A2DP instead
  // needs frequent small writes so its real-time callback never waits for a
  // whole local-speaker block to be decoded.
  for (std::size_t block = 0; block < maxBlocks; ++block) {
#if defined(OPEN_RADIO_VARIANT_FULL)
    const std::size_t count = open_radio::selectPcmTransferFrames(
        decodedFrames.size(), bluetoothQueue.bufferedFrames(),
        open_radio::public_candidate::kBluetoothPcmFrames, 0U, drainFrames);
#else
    const std::size_t count =
        playbackMode == PlaybackMode::Noise
            ? std::min(decodedFrames.size(), drainFrames)
            : decodedFrames.size() >= drainFrames ? drainFrames : 0U;
#endif
    if (count == 0) break;
    if (decodedFrames.peek(decodedDrainFrames, count) != count) break;
    const std::size_t written = outputRouter.write(decodedDrainFrames, count);
    if (written == 0) break;
    decodedFrames.discard(written);
    totalWritten += written;
  }
  return totalWritten;
}

#if defined(OPEN_RADIO_VARIANT_FULL)
std::size_t primeBluetoothStandbyQueue() {
  if (!ensureDecodedDrainBuffer()) return 0;
  std::size_t totalWritten = 0;
  while (true) {
    const std::size_t count = open_radio::selectPcmTransferFrames(
        decodedFrames.size(), bluetoothQueue.bufferedFrames(),
        open_radio::public_candidate::kBluetoothPcmFrames,
        open_radio::public_candidate::kBluetoothFallbackReserveFrames,
        open_radio::public_candidate::kLocalSpeakerFramesPerBlock);
    if (count == 0) break;
    if (decodedFrames.peek(decodedDrainFrames, count) != count) break;
    const std::size_t written = bluetoothQueue.write(decodedDrainFrames, count);
    if (written == 0) break;
    decodedFrames.discard(written);
    totalWritten += written;
    if (written != count) break;
  }
  return totalWritten;
}
#endif

void reportLocalSpeakerStarvation() {
  const auto total = localSpeaker.starvations();
  if (total == reportedLocalSpeakerStarvations) return;
  reportedLocalSpeakerStarvations = total;
  Serial.printf("audio_starvation total=%lu\n",
                static_cast<unsigned long>(total));
}

void reportAudioQualityCounters() {
  Serial.printf(
      "audio_qc decoded_buffered=%u decoded_backpressure=%lu "
      "local_starvations=%lu stream_starts=%lu stream_failures=%lu "
      "stream_stops=%lu stream_start_ms=%lu source_status=%d "
      "source_events=%lu input_buffered=%u input_bytes=%lu "
      "input_underruns=%lu input_worker_stack_bytes=%lu",
      static_cast<unsigned>(decodedFrames.size()),
      static_cast<unsigned long>(mp3Pipeline.decodedBackpressureEvents()),
      static_cast<unsigned long>(localSpeaker.starvations()),
      static_cast<unsigned long>(stationRuntime.startAttempts()),
      static_cast<unsigned long>(stationRuntime.startFailures()),
      static_cast<unsigned long>(stationRuntime.unexpectedStops()),
      static_cast<unsigned long>(stationRuntime.lastStartDurationMs()),
      mp3Pipeline.lastSourceStatus(),
      static_cast<unsigned long>(mp3Pipeline.sourceStatusEvents()),
      static_cast<unsigned>(mp3Pipeline.inputBufferedBytes()),
      static_cast<unsigned long>(mp3Pipeline.inputReceivedBytes()),
      static_cast<unsigned long>(mp3Pipeline.inputUnderruns()),
      static_cast<unsigned long>(
          mp3Pipeline.inputWorkerStackMinimumBytes()));
  Serial.printf(" mode=%s noise_color=%u noise_active=%s",
                playbackMode == PlaybackMode::Noise ? "noise" : "radio",
                static_cast<unsigned>(storedNoiseColor),
                localNoiseSource.active() ? "yes" : "no");
#if defined(OPEN_RADIO_VARIANT_FULL)
  Serial.printf(" route=%s bt_buffered=%u bt_active_silence_frames=%lu "
                "bt_idle_silence_frames=%lu bt_backpressure=%lu "
                "bt_stack_starts=%lu bt_connect_attempts=%lu "
                "bt_media_starts=%lu bt_retries=%lu bt_fallbacks=%lu "
                "bt_pull_callbacks=%lu bt_pull_watchdogs=%lu",
                outputRouter.activeOutput() == open_radio::OutputKind::Bluetooth
                    ? "bluetooth"
                    : "local",
                static_cast<unsigned>(bluetoothQueue.bufferedFrames()),
                static_cast<unsigned long>(
                    bluetoothQueue.activeUnderrunFrames()),
                static_cast<unsigned long>(bluetoothQueue.idleSilenceFrames()),
                static_cast<unsigned long>(
                    bluetoothQueue.backpressureEvents()),
                static_cast<unsigned long>(bluetoothStackStarts),
                static_cast<unsigned long>(bluetoothDirectConnectAttempts),
                static_cast<unsigned long>(bluetoothMediaStarts),
                static_cast<unsigned long>(bluetoothRetryAttempts),
                static_cast<unsigned long>(bluetoothFallbacks),
                static_cast<unsigned long>(
                    bluetoothPullCallbacks.load(std::memory_order_relaxed)),
                static_cast<unsigned long>(bluetoothPullWatchdogTrips));
  const auto eventStackBytes =
      bluetoothEventStackMinimumBytes.load(std::memory_order_relaxed);
  if (eventStackBytes != UINT32_MAX) {
    Serial.printf(" bt_event_stack_bytes=%lu",
                  static_cast<unsigned long>(eventStackBytes));
  }
#endif
  Serial.println();
}

void handleMetadata(void*, const char* title) { boardUi.metadata(title); }
#endif

#if defined(OPEN_RADIO_VARIANT_FULL)
std::atomic<std::uint32_t> bluetoothCallbackSequence{3};
std::atomic<std::uint32_t> bluetoothConnectionEventPending{0};
// E7/E8: when the callback publishes, and how many publications the
// single-slot mailbox overwrote before the owner loop read them. A loop period
// of ~133 ms against "dirty" attempts of 86-134 ms means transitions CAN be
// lost; this counts them instead of assuming.
std::atomic<std::uint32_t> bluetoothConnectionEventAtMs{0};
std::atomic<std::uint32_t> bluetoothConnectionEventsLost{0};
std::atomic<std::uint32_t> bluetoothConnectionEventDialAgeMs{
    kBluetoothNoDialAgeMs};
std::atomic<std::int8_t> bluetoothAudioStatePending{-1};
std::atomic<std::int8_t> bluetoothDiscoveryPending{-1};
struct BluetoothCandidateEvent {
  std::array<char, open_radio::kBluetoothNameBytes> name{};
  std::array<std::uint8_t, ESP_BD_ADDR_LEN> address{};
};
QueueHandle_t bluetoothCandidateQueue = nullptr;
std::array<char, open_radio::kBluetoothNameBytes> activeBluetoothName{};
std::array<char, open_radio::kBluetoothNameBytes> rememberedBluetoothName{};
std::array<std::uint8_t, ESP_BD_ADDR_LEN> activeBluetoothAddress{};
std::array<std::uint8_t, ESP_BD_ADDR_LEN> rememberedBluetoothAddress{};
bool bluetoothStackStarted = false;
bool bluetoothBleMemoryReleased = false;
bool bluetoothAutoStartPending = false;
// A tap that arrived while the logo fetch held the internal heap. The automatic
// start has waited for `!logoFetchPending` since it was written; the manual path
// did not, and that asymmetry is what panicked the device on 2026-08-01. The
// person is the one who taps on a box-fresh cube — exactly when the logos are
// downloading — so the unguarded path was the one everybody walks.
bool bluetoothManualScanPending = false;
std::uint32_t bluetoothManualScanDeadlineMs = 0;
// A deferred tap has to expire. Without a deadline the flag survives until some
// unrelated moment satisfies the resume predicate, and then opens a pairing
// window the person asked for minutes ago and has forgotten about. Sixty
// seconds is longer than any observed logo pass and short enough that a stale
// request dies rather than surprising somebody.
constexpr std::uint32_t kBluetoothManualScanDeferralMs = 60000U;
bool bluetoothScanActive = false;
bool bluetoothDiscoveryActive = false;
std::atomic<bool> bluetoothAllowNewCandidate{false};
bool bluetoothFoundPending = false;
bool bluetoothScanAfterDisconnect = false;
bool bluetoothConnectInFlight = false;
bool bluetoothLinkConnected = false;
bool bluetoothMediaStarted = false;

// ONE definition of the exclusion, so both directions cannot drift apart.
//
// Internal DRAM is the contended resource: a TLS handshake wants ~40 KB of it
// and Bluedroid's task stacks want another ~19 KB, and neither can be served
// from PSRAM — every refused allocation on 2026-08-01 carried a capability
// mask (DMA / INTERNAL|8BIT) that external memory cannot satisfy. osi_malloc
// does not fail softly there; it asserts, and the radio reboots.
//
// The Bluetooth side has always tested this before arming. The TLS side never
// did, which left the whole exclusion working in one direction only — and the
// project's own docs/133 states the rule the code did not enforce.
//
// Deliberately conservative on the Bluetooth side: `stackStarted` is set the
// instant `source.start()` returns, while the pinned library goes on setting
// up A2DP and AVRCP asynchronously for another ten seconds or so. Treating
// "started" as "busy" therefore covers that invisible tail as well.
bool bluetoothActiveWasRemembered = false;
bool bluetoothDirtyAttemptReported = false;

std::uint32_t bluetoothScanStartedMs = 0;
std::uint32_t bluetoothFoundAtMs = 0;
std::uint32_t bluetoothConnectStartedMs = 0;
std::uint32_t bluetoothLinkConnectedAtMs = 0;
std::uint32_t bluetoothMediaStartAllowedAtMs = 0;
std::uint32_t bluetoothMediaProbeAtMs = 0;
std::uint32_t bluetoothMediaStartedAtMs = 0;
std::uint32_t bluetoothRetryAtMs = 0;
// Deadline for a DISCONNECTED that may never arrive; see the DISCONNECTING
// handler. Zero means no teardown is outstanding.
std::uint32_t bluetoothDisconnectingDeadlineMs = 0;
// This firmware's own answer to "is the link down", for the case where the
// library cannot answer because the callback that would have told it never
// arrived. Every route back to a speaker used to consult the library's
// connection_state alone, which then sat at DISCONNECTING for the rest of the
// session and refused every dial.
//
// A separate flag rather than writing the library's member: that member is a
// plain enum written by the Bluedroid callback task, so assigning it from the
// Arduino loop is a data race with no ordering against either writer, and it
// could overwrite a CONNECTED the library had just recorded. This is owned by
// the loop, read by the loop, and cleared the moment the library speaks again
// — at which point its view is the authoritative one and ours is not needed.
bool bluetoothTerminalDeclared = false;
constexpr std::uint32_t kBluetoothDisconnectingGraceMs = 8000U;
std::uint32_t bluetoothDiscoveryRetryAtMs = 0;
std::uint32_t bluetoothRecoveryStartedMs = 0;
std::uint8_t bluetoothRecoveryAttemptCount = 0;
std::uint8_t bluetoothMediaProbeAttempts = 0;
std::uint32_t bluetoothDirtyAwaitSinceMs = 0;
std::uint8_t appliedBluetoothVolume = 0xff;
constexpr std::uint32_t kBluetoothStandbyPrefillTimeoutMs = 5000;
constexpr std::uint32_t kBluetoothMediaStartTimeoutMs = 10000;
constexpr std::uint32_t kBluetoothMediaRecoveryDelayMs = 2000;
constexpr std::uint32_t kBluetoothMediaProbeInitialDelayMs = 250;
constexpr std::uint32_t kBluetoothMediaProbeIntervalMs = 2000;
constexpr std::uint8_t kBluetoothMediaProbeLimit = 5;
constexpr std::uint32_t kBluetoothScanTimeoutMs = 30000;
constexpr std::uint32_t kBluetoothPullWatchdogMs = 3000;
// A failed direct connect occupies the shared radio for about five seconds.
// Break phase-lock with speakers that run their own variable reconnect page
// schedule. The lower A2DP layer exposes no API that cancels an in-flight
// source connection, so each new supervisor-owned attempt begins only after
// the previous callback has completed and after a bounded randomized pause.
constexpr std::uint32_t kBluetoothRecoveryMinDelayMs = 8U * 1000U;
constexpr std::uint32_t kBluetoothRecoveryMaxDelayMs = 20U * 1000U;
// With no trusted peer yet, repeat standard-based discovery automatically but
// leave most airtime to Wi-Fi and local playback between inquiry windows.
// The owner watched five silent 60-second waits before his speaker finally
// took the connection. A page timeout costs ~5 s, so retrying four times a
// minute during the open pairing window keeps the user engaged instead of
// convinced the feature is broken.
constexpr std::uint32_t kBluetoothDiscoveryRecoveryDelayMs = 15U * 1000U;
// A failed Classic Bluetooth connect can occupy the shared radio for about
// five seconds. Keep both M5Unified playback slots plus one second of decoded
// PCM before every attempt so the built-in fallback stays audible while the
// radios contend. The threshold must sit BELOW the stream's burst equilibrium
// (~98k frames measured 2026-07-17): after the CDN burst a live stream is
// 1x realtime, the decoded pool never grows past that equilibrium during
// playback, and every deeper "reserve" requirement silently deferred dialing
// forever (six-minute first dial measured with the full-ring gate, zero dials
// in 115 s with the 3/4-ring gate). Two staged speaker blocks (~5.94 s) plus
// one second decoded cover every dial that leaves the stream running; a
// stream death inside a dial window is recovered by the bounded reopen path.
constexpr std::size_t kBluetoothRetryDecodedReserveFrames =
    open_radio::kPcmSampleRate;
constexpr std::uint32_t kBluetoothRetryAudioPollMs = 250U;
constexpr std::uint32_t kBluetoothRetryDeferredReportIntervalMs = 1000U;
constexpr std::uint32_t kBluetoothCleanAttemptMinMs = 4820U;
constexpr std::uint32_t kBluetoothCleanAttemptMaxMs = 5420U;
// The A2DP task can pull PCM as soon as the profile link is up, before the
// owner loop observes AUDIO_STARTED, so no exact-equality target is allowed.
// The ceiling is physics, not preference: after the CDN burst a live stream
// delivers 1x realtime, so decoded + BT queue can never exceed the burst
// equilibrium (~98k frames measured 2026-07-17) during playback. Standby
// pauses the local drain, which grows the pool at exactly one realtime
// second per second — two seconds of staged A2DP PCM is reachable within the
// five-second prefill timeout at every station, covers the worst measured
// 2.10-second owner-loop stall, and the sink keeps receiving padded frames
// through any deeper transient.
constexpr std::size_t kBluetoothStandbyReadyFrames =
    open_radio::kPcmSampleRate * 2U;
static_assert(kBluetoothStandbyReadyFrames >=
              open_radio::kPcmSampleRate * 2U);
std::uint32_t bluetoothRetryDeferredReportAtMs = 0;

bool cleanBluetoothAttemptTimeout(std::uint32_t durationMs) {
  return durationMs >= kBluetoothCleanAttemptMinMs &&
         durationMs <= kBluetoothCleanAttemptMaxMs;
}

void reportBluetoothAttemptResult(const char* result,
                                  std::uint32_t durationMs) {
  Serial.printf("bluetooth attempt_result=%s duration_ms=%lu\n", result,
                static_cast<unsigned long>(durationMs));
}

std::uint32_t bluetoothRecoveryDelayMs() {
  constexpr auto kDelayRangeMs =
      kBluetoothRecoveryMaxDelayMs - kBluetoothRecoveryMinDelayMs + 1U;
  return kBluetoothRecoveryMinDelayMs + esp_random() % kDelayRangeMs;
}

bool localAudioReadyForBluetoothRetry() {
  return outputRouter.activeOutput() == open_radio::OutputKind::LocalSpeaker &&
         localSpeaker.bufferedBlocks() == 2 &&
         decodedFrames.size() >= kBluetoothRetryDecodedReserveFrames;
}

void deferBluetoothRetryForAudio(std::uint32_t nowMs) {
  bluetoothRetryAtMs = nowMs + kBluetoothRetryAudioPollMs;
  if (bluetoothRetryDeferredReportAtMs != 0 &&
      static_cast<std::int32_t>(nowMs - bluetoothRetryDeferredReportAtMs) < 0) {
    return;
  }
  Serial.printf("bluetooth retry_deferred local_blocks=%u decoded_frames=%u\n",
                static_cast<unsigned>(localSpeaker.bufferedBlocks()),
                static_cast<unsigned>(decodedFrames.size()));
  bluetoothRetryDeferredReportAtMs =
      nowMs + kBluetoothRetryDeferredReportIntervalMs;
}

void resetBluetoothMediaProbe() {
  bluetoothLinkConnectedAtMs = 0;
  bluetoothMediaStartAllowedAtMs = 0;
  bluetoothMediaProbeAtMs = 0;
  bluetoothMediaProbeAttempts = 0;
  bluetoothSource.setMediaStartAllowed(false);
}

void armBluetoothMediaProbe(std::uint32_t nowMs) {
  bluetoothLinkConnectedAtMs = nowMs;
  bluetoothMediaStartAllowedAtMs = 0;
  bluetoothMediaProbeAtMs = 0;
  bluetoothMediaProbeAttempts = 0;
  bluetoothSource.setMediaStartAllowed(false);
}

bool bluetoothStandbyReady() {
  return localSpeaker.bufferedBlocks() > 0 &&
         decodedFrames.size() >=
             open_radio::public_candidate::kBluetoothFallbackReserveFrames &&
         bluetoothQueue.bufferedFrames() >= kBluetoothStandbyReadyFrames;
}

void allowBluetoothMediaStart(std::uint32_t nowMs) {
  if (bluetoothMediaStartAllowedAtMs != 0) return;
  bluetoothMediaStartAllowedAtMs = nowMs;
  bluetoothMediaProbeAtMs = nowMs + kBluetoothMediaProbeInitialDelayMs;
  bluetoothSource.setMediaStartAllowed(true);
  Serial.printf(
      "bluetooth standby_ready bt_buffered=%u decoded_frames=%u "
      "local_blocks=%u\n",
      static_cast<unsigned>(bluetoothQueue.bufferedFrames()),
      static_cast<unsigned>(decodedFrames.size()),
      static_cast<unsigned>(localSpeaker.bufferedBlocks()));
}

void applyBluetoothVolume() {
  const auto volume = static_cast<std::uint8_t>(
      boardUi.config().volume * 0x7fU / 100U);
  if (appliedBluetoothVolume == volume) return;
  bluetoothSource.set_volume(volume);
  appliedBluetoothVolume = volume;
  Serial.printf("bluetooth volume=%u saved_percent=%u\n",
                static_cast<unsigned>(volume),
                static_cast<unsigned>(boardUi.config().volume));
}

void observeBluetoothEventStack() {
  const auto availableBytes =
      static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
  auto minimum =
      bluetoothEventStackMinimumBytes.load(std::memory_order_relaxed);
  while (availableBytes < minimum &&
         !bluetoothEventStackMinimumBytes.compare_exchange_weak(
             minimum, availableBytes, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

int32_t provideBluetoothFrames(Frame* frames, int32_t frameCount) {
  bluetoothLastPullMs.store(millis(), std::memory_order_relaxed);
  bluetoothPullCallbacks.fetch_add(1, std::memory_order_relaxed);
  std::array<open_radio::PcmFrame, 128> buffered{};
  int32_t provided = 0;
  while (provided < frameCount) {
    const auto requested = static_cast<std::size_t>(
        std::min<int32_t>(frameCount - provided, buffered.size()));
    const std::size_t received = bluetoothQueue.read(buffered.data(), requested);
    if (received == 0) break;
    for (std::size_t index = 0; index < received; ++index) {
      frames[provided].channel1 = buffered[index].left;
      frames[provided].channel2 = buffered[index].right;
      ++provided;
    }
  }
  bluetoothQueue.recordSilenceFrames(
      static_cast<std::size_t>(frameCount - provided));
  // The A2DP source callback is a pull contract. Returning a short packet (or
  // zero during the CONNECTED hand-off) makes some speakers tear down the
  // stream immediately. Pad transient queue underruns with silence and always
  // satisfy the requested SBC input frame count.
  while (provided < frameCount) {
    frames[provided].channel1 = 0;
    frames[provided].channel2 = 0;
    ++provided;
  }
  return frameCount;
}

void handleBluetoothConnection(esp_a2d_connection_state_t state, void*) {
  observeBluetoothEventStack();
  Serial.printf("bluetooth state=%u connected=%s dram_free=%u dram_min=%u "
                "largest=%u\n",
                static_cast<unsigned>(state),
                state == ESP_A2D_CONNECTION_STATE_CONNECTED ? "yes" : "no",
                static_cast<unsigned>(heap_caps_get_free_size(
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(heap_caps_get_minimum_free_size(
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(heap_caps_get_largest_free_block(
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  // The first boot connection is seeded through the library's remembered
  // address API. Disable its retry owner as soon as that attempt is underway;
  // the firmware supervisor below is the only component allowed to retry.
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
    bluetoothSource.set_auto_reconnect(false, 0);
  }
  bluetoothConnectionEventDialAgeMs.store(bluetoothSource.dialAgeMs(millis()),
                                           std::memory_order_release);
  std::uint32_t profileGeneration = 0;
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTING ||
      state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    profileGeneration = bluetoothSource.observeProfileProgress();
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTING ||
             state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    profileGeneration = bluetoothSource.observedProfileGeneration();
  }
  bluetoothConnectionEventAtMs.store(millis(), std::memory_order_release);
  const auto previous = bluetoothConnectionEventPending.exchange(
      open_radio::packBluetoothProfileEvent(
          static_cast<std::uint8_t>(state), profileGeneration),
      std::memory_order_acq_rel);
  if (previous != 0) {
    bluetoothConnectionEventsLost.fetch_add(1, std::memory_order_relaxed);
    Serial.printf("bluetooth event_lost t_ms=%lu prev_state=%u new_state=%u\n",
                  static_cast<unsigned long>(millis()),
                  static_cast<unsigned>(
                      open_radio::unpackBluetoothProfileEvent(previous).state),
                  static_cast<unsigned>(state));
  }
}

void handleBluetoothAudio(esp_a2d_audio_state_t state, void*) {
  observeBluetoothEventStack();
  const auto previous = bluetoothAudioStatePending.exchange(
      static_cast<std::int8_t>(state), std::memory_order_acq_rel);
  if (previous != -1) {
    Serial.printf("bluetooth event_lost t_ms=%lu kind=audio prev_state=%d new_state=%u\n",
                  static_cast<unsigned long>(millis()), static_cast<int>(previous),
                  static_cast<unsigned>(state));
  }
}

void handleBluetoothDiscovery(esp_bt_gap_discovery_state_t state) {
  observeBluetoothEventStack();
  bluetoothDiscoveryPending.store(
      state == ESP_BT_GAP_DISCOVERY_STARTED ? 1 : 0,
      std::memory_order_release);
}

bool validBluetoothAddress(
    const std::array<std::uint8_t, ESP_BD_ADDR_LEN>& address) {
  const bool allZero =
      std::all_of(address.begin(), address.end(), [](std::uint8_t value) {
        return value == 0;
      });
  const bool allBroadcast =
      std::all_of(address.begin(), address.end(), [](std::uint8_t value) {
        return value == 0xff;
      });
  return !allZero && !allBroadcast;
}

bool loadRememberedBluetooth() {
  rememberedBluetoothName.fill(0);
  rememberedBluetoothAddress.fill(0);
  // Straight into the fixed buffer, no Arduino String: this runs moments
  // before the Bluetooth stack start, and a String heap-allocates for any name
  // past its inline capacity — a few dozen bytes of churn exactly where
  // docs/133 wants the heap quietest (Codex audit #6, 2026-07-30).
  const std::size_t nameLength = preferences.getString(
      "bt_name", rememberedBluetoothName.data(),
      rememberedBluetoothName.size());
  if (nameLength == 0 || nameLength >= open_radio::kBluetoothNameBytes ||
      preferences.getBytesLength("bt_addr") !=
          rememberedBluetoothAddress.size() ||
      preferences.getBytes("bt_addr", rememberedBluetoothAddress.data(),
                           rememberedBluetoothAddress.size()) !=
          rememberedBluetoothAddress.size() ||
      !validBluetoothAddress(rememberedBluetoothAddress)) {
    rememberedBluetoothName.fill(0);
    preferences.remove("bt_name");
    preferences.remove("bt_addr");
    return false;
  }
  return true;
}

bool persistActiveBluetooth() {
  const std::size_t nameLength = std::strlen(activeBluetoothName.data());
  const bool addressPersisted =
      validBluetoothAddress(activeBluetoothAddress) &&
      preferences.putBytes("bt_addr", activeBluetoothAddress.data(),
                           activeBluetoothAddress.size()) ==
          activeBluetoothAddress.size();
  if (nameLength > 0 && addressPersisted &&
      preferences.putString("bt_name", activeBluetoothName.data()) ==
          nameLength) {
    rememberedBluetoothName = activeBluetoothName;
    rememberedBluetoothAddress = activeBluetoothAddress;
    bluetoothActiveWasRemembered = true;
    bluetoothDiscoveryRetryAtMs = 0;
    bluetoothSource.setReconnectPeer(rememberedBluetoothAddress, true);
    boardUi.rememberBluetooth();
    return true;
  }
  preferences.remove("bt_addr");
  preferences.remove("bt_name");
  bluetoothActiveWasRemembered = false;
  boardUi.forgetBluetooth();
  return false;
}

bool selectCompatibleSpeaker(const char* name, esp_bd_addr_t address, int) {
  const char* displayName = name != nullptr && name[0] != '\0'
                                ? name
                                : "Bluetooth speaker";
  std::array<std::uint8_t, ESP_BD_ADDR_LEN> candidateAddress{};
  std::copy_n(address, candidateAddress.size(), candidateAddress.begin());
  const bool rememberedDevice =
      validBluetoothAddress(rememberedBluetoothAddress) &&
      candidateAddress == rememberedBluetoothAddress;
  const bool accepted = rememberedDevice ||
                        bluetoothAllowNewCandidate.load(
                            std::memory_order_relaxed);
  if (!accepted || bluetoothCandidateQueue == nullptr) return false;
  BluetoothCandidateEvent event;
  std::snprintf(event.name.data(), event.name.size(), "%s", displayName);
  event.address = candidateAddress;
  return xQueueOverwrite(bluetoothCandidateQueue, &event) == pdPASS;
}

void useLocalBluetoothFallback() {
  const bool wasBluetoothActive =
      outputRouter.activeOutput() == open_radio::OutputKind::Bluetooth;
  if (wasBluetoothActive) {
    ++bluetoothFallbacks;
    bluetoothRecoveryStartedMs = millis();
    bluetoothRecoveryAttemptCount = 0;
    Serial.printf("bluetooth fallback=%lu recovery_start_ms=%lu\n",
                  static_cast<unsigned long>(bluetoothFallbacks),
                  static_cast<unsigned long>(bluetoothRecoveryStartedMs));
  }
  bluetoothMediaStarted = false;
  bluetoothMediaStartedAtMs = 0;
  bluetoothQueue.setPlaybackActive(false);
  // Freeze the queued future PCM instead of discarding it. The local drain
  // consumes this older audio before the decoded queue, preserving continuity
  // and enough headroom for a synchronous stream read/reopen.
  bluetoothQueue.setConnected(false, false);
  outputRouter.preferBluetooth(false);
  if (wasBluetoothActive) drainDecodedFrames();
}

void scheduleBluetoothRetry(std::uint32_t nowMs,
                            std::uint32_t overrideDelayMs = 0) {
  if (!boardUi.config().bluetoothRemembered ||
      !bluetoothActiveWasRemembered ||
      !validBluetoothAddress(activeBluetoothAddress) ||
      bluetoothRetryAtMs != 0) {
    return;
  }
  const auto delay = overrideDelayMs != 0 ? overrideDelayMs
                                           : bluetoothRecoveryDelayMs();
  bluetoothRetryAtMs = nowMs + delay;
  Serial.printf("bluetooth retry_in_ms=%lu\n",
                static_cast<unsigned long>(bluetoothRetryAtMs - nowMs));
}

void scheduleBluetoothDiscoveryRetry(std::uint32_t nowMs,
                                     std::uint32_t overrideDelayMs = 0) {
  if (boardUi.config().bluetoothRemembered ||
      bluetoothDiscoveryRetryAtMs != 0) {
    return;
  }
  const auto delay = overrideDelayMs != 0
                         ? overrideDelayMs
                         : kBluetoothDiscoveryRecoveryDelayMs;
  bluetoothDiscoveryRetryAtMs = nowMs + delay;
  Serial.printf("bluetooth discovery_retry_in_ms=%lu\n",
                static_cast<unsigned long>(delay));
}

void scheduleBluetoothRecovery(std::uint32_t nowMs,
                               std::uint32_t overrideDelayMs = 0) {
  if (boardUi.config().bluetoothRemembered) {
    if (!bluetoothActiveWasRemembered) {
      if (!loadRememberedBluetooth()) {
        boardUi.forgetBluetooth();
        scheduleBluetoothDiscoveryRetry(nowMs, overrideDelayMs);
        return;
      }
      activeBluetoothName = rememberedBluetoothName;
      activeBluetoothAddress = rememberedBluetoothAddress;
      bluetoothActiveWasRemembered = true;
      bluetoothSource.setReconnectPeer(activeBluetoothAddress, true);
      if (bluetoothStackStarted) bluetoothSource.applyReconnectListenPolicy();
    }
    scheduleBluetoothRetry(nowMs, overrideDelayMs);
    return;
  }
  scheduleBluetoothDiscoveryRetry(nowMs, overrideDelayMs);
}

void configureBluetoothSource() {
  bluetoothSource.set_auto_reconnect(false, 0);
  bluetoothSource.set_ssp_enabled(true);
  bluetoothSource.set_pin_code("0000", ESP_BT_PIN_TYPE_VARIABLE);
  bluetoothSource.set_local_name("Open Radio Core2");
  bluetoothSource.set_valid_cod_service(ESP_BT_COD_SRVC_RENDERING |
                                        ESP_BT_COD_SRVC_AUDIO);
  bluetoothSource.set_discovery_mode_callback(handleBluetoothDiscovery);
  bluetoothSource.set_ssid_callback(selectCompatibleSpeaker);
}

bool prepareClassicBluetoothController() {
  auto status = esp_bt_controller_get_status();
  if (status == ESP_BT_CONTROLLER_STATUS_ENABLED) return true;
  if (status == ESP_BT_CONTROLLER_STATUS_IDLE) {
    if (!bluetoothBleMemoryReleased) {
      const auto released = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
      if (released != ESP_OK) {
        Serial.printf("bluetooth ble_release_error=%d\n",
                      static_cast<int>(released));
        return false;
      }
      bluetoothBleMemoryReleased = true;
    }
    esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    config.mode = ESP_BT_MODE_CLASSIC_BT;
    const auto initialized = esp_bt_controller_init(&config);
    if (initialized != ESP_OK) {
      Serial.printf("bluetooth controller_init_error=%d\n",
                    static_cast<int>(initialized));
      return false;
    }
    status = esp_bt_controller_get_status();
  }
  if (status == ESP_BT_CONTROLLER_STATUS_INITED) {
    // BALANCE, deliberately: the Wi-Fi bias (PREFER_WIFI) was added when the
    // 128 kb/s MP3 mounts needed 16 KB/s under congestion, and it audibly
    // choked the A2DP TX side on a weaker-link speaker (full queues, zero
    // injected silence, owner still heard cuts — 2026-07-17 evening). The
    // aacp decks need only 6-8 KB/s, so Wi-Fi no longer needs the bias.
    // Disabling Wi-Fi modem sleep remains FORBIDDEN (coex aborts at
    // controller enable — hard platform constraint, do not retry).
    const auto preference = esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    Serial.printf("bluetooth coex_preference=balance result=%d\n",
                  static_cast<int>(preference));
    const auto enabled = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (enabled != ESP_OK) {
      Serial.printf("bluetooth controller_enable_error=%d\n",
                    static_cast<int>(enabled));
      return false;
    }
  }
  return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
}

bool beginBluetoothDiscovery(std::uint32_t nowMs) {
  if (!bluetoothStackStarted || bluetoothDiscoveryActive ||
      bluetoothSource.get_connection_state() !=
          ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    return false;
  }
  bluetoothSource.set_auto_reconnect(false, 0);
  bluetoothSource.authorizeProfileOpen(
      open_radio::BluetoothProfileOpenReason::Discovery);
  const auto result = esp_bt_gap_start_discovery(
      ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
  if (result != ESP_OK) {
    bluetoothSource.cancelProfileOpenAuthorization();
    Serial.printf("bluetooth discovery_start_error=%d\n",
                  static_cast<int>(result));
    return false;
  }
  bluetoothScanActive = true;
  bluetoothDiscoveryActive = true;
  bluetoothScanStartedMs = nowMs;
  // The scan tap is the user's explicit pairing intent: while this window is
  // open an inbound unknown sink is adopted instead of rejected (see
  // process_user_state_callbacks). Closed on adoption or any connection.
  bluetoothSource.setPairingWindow(true);
  boardUi.setBluetoothState(open_radio::BluetoothUiState::Scanning);
  return true;
}

// Down enough to dial again. The library's own answer, OR this firmware's,
// when the library was never told. Both are needed: the library's is right
// almost always and is the only one that knows about links this code did not
// initiate, and ours is the only one that exists after a dropped terminal
// callback. Ours is cleared the moment any connection event is processed, so
// it can only survive while the library is silent.
bool bluetoothLinkIsDown() {
  return bluetoothSource.get_connection_state() ==
             ESP_A2D_CONNECTION_STATE_DISCONNECTED ||
         bluetoothTerminalDeclared;
}

bool connectRememberedBluetooth(std::uint32_t nowMs) {
  if (!bluetoothStackStarted || bluetoothDiscoveryActive ||
      !bluetoothLinkIsDown() ||
      !validBluetoothAddress(activeBluetoothAddress)) {
    return false;
  }
  // Update the library's disconnect target, then immediately disable its own
  // retry policy before issuing the one supervisor-owned attempt.
  bluetoothSource.set_auto_reconnect(activeBluetoothAddress.data(), 0);
  bluetoothSource.set_auto_reconnect(false, 0);
  bluetoothSource.authorizeProfileOpen(
      open_radio::BluetoothProfileOpenReason::Recovery);
  if (!bluetoothSource.connect_to(activeBluetoothAddress.data())) {
    return false;
  }
  bluetoothRetryDeferredReportAtMs = 0;
  ++bluetoothDirectConnectAttempts;
  if (bluetoothRecoveryStartedMs != 0) {
    ++bluetoothRecoveryAttemptCount;
    Serial.printf("bluetooth recovery_attempt=%u elapsed_ms=%lu\n",
                  static_cast<unsigned>(bluetoothRecoveryAttemptCount),
                  static_cast<unsigned long>(nowMs -
                                             bluetoothRecoveryStartedMs));
  }
  bluetoothConnectInFlight = true;
  bluetoothDirtyAttemptReported = false;
  bluetoothConnectStartedMs = nowMs;
  bluetoothRetryAtMs = 0;
  boardUi.setBluetoothState(open_radio::BluetoothUiState::Connecting,
                            activeBluetoothName.data());
  Serial.printf("bluetooth direct_connect_attempt=%lu\n",
                static_cast<unsigned long>(bluetoothDirectConnectAttempts));
  return true;
}

// Bluedroid needs a large contiguous block of INTERNAL DRAM to come up, and
// this device is close to the line while a station is playing: measured on
// hardware 2026-07-29, the largest free internal block sits around 38 KB in
// that state and free DRAM has been seen down to 19 KB.
//
// When BTU_StartUp cannot get what it needs it does not return an error — it
// calls its own shutdown path, which frees a mutex that was never created, and
// FreeRTOS asserts:
//
//   assert failed: vQueueDelete queue.c:2131 (pxQueue)
//   btc_init_bluetooth -> bte_main_enable -> BTU_StartUp -> BTU_ShutDown
//     -> btu_task_shut_down -> bta_sys_free -> osi_mutex_free
//
// The owner met that by pressing connect while the radio played: the cube
// rebooted. The assert is in a prebuilt component and cannot be patched here,
// so the only defence is to not walk into it. Refusing with a message is a
// worse Bluetooth experience and a far better radio than a reset.
//
// The bar is measured, not guessed: a start that succeeded had 69,620 bytes in
// its largest free block beforehand, and one that failed had far less. 56 KB
// sits between those with room on both sides. It is checked immediately before
// the call, because what matters is the state at that instant.
// What Bluedroid needs as ONE contiguous internal block before it is safe to
// start. The guard exists because Bluedroid asserts inside its own failure path
// when it runs out — vQueueDelete at queue.c:2131 — so the stack cannot be
// allowed to try and fail.
//
// 56 KiB was a margin chosen and then described as if it were measured, and it
// cost the owner his Bluetooth speaker: the cube refused to start the stack at
// 38,900 bytes, logged the refusal and retried for ever. Measured on hardware
// 2026-07-29 across three flashes of the same evening: 63,476 bytes free at the
// pre-Bluetooth mark on one boot and 38,900 on another, with only 5,860 bytes
// of difference in TOTAL free DRAM. The gap is fragmentation, not consumption —
// nine station logos are re-fetched and re-decoded after every build change,
// because the logo cache is derived state and deliberately discarded on
// upgrade, and that decode chops the heap exactly when Bluetooth wants a
// contiguous block.
//
// 32 KiB was then tried and refused a stack by TWELVE BYTES — 32,756 against
// 32,768 — which is the clearest possible demonstration that the number was
// never the point. Any threshold in this region is a coin toss against a heap
// that fragmentation moves by tens of kilobytes between boots.
//
// So it is 16 KiB, and it means something different from the other two: not a
// prediction of what Bluedroid needs, but a floor below which the heap is
// genuinely exhausted and no allocation of consequence can succeed. That is a
// condition this code can actually assert.
//
// The wider lesson is worth more than the constant. Before today there was no
// guard here at all and Bluetooth worked for months; the guard was added after
// ONE crash and promptly converted an occasional failure into a permanent one,
// which is strictly worse. A protection that fires when the thing would have
// worked is not conservative, it is broken — and it cost the owner his speaker
// for an evening.
//
// The refusal line logs the actual figure every time, so the boundary that
// matters will eventually be observed instead of assumed.
constexpr std::size_t kBluetoothStartLargestBlockMin = 16 * 1024;

bool startBluetoothStack(bool scan, std::uint32_t nowMs) {
  if (bluetoothStackStarted) return true;
  // The TLS side of the exclusion moved to a single door for exactly this
  // reason; the Bluetooth side had been left as two callers that remember to
  // check. They are the two somebody thought of — the manual tap and the auto
  // start. Recovery is not one of them: a speaker that drops while Wi-Fi
  // reconnects reaches this function with a fetch task live, which is the
  // overlap the panic of 2026-08-01 came out of. A guard that depends on
  // being remembered has already been forgotten once here.
  //
  // Asking the fetch to stop rather than queueing behind it, because the wait
  // is not short: three passes with deliberate twenty-second gaps ran to
  // sixty-six seconds on the owner's cube, and a speaker that has just
  // dropped is a person waiting for sound. Tile artwork can be fetched on a
  // later boot. Refusal is a deferral — both callers reschedule.
  if (logoFetchPending) {
    logoFetchAbort.store(true, std::memory_order_relaxed);
    // Put a manual request back where the loop will find it. The caller
    // consumed bluetoothManualScanPending before getting here, and the retry
    // it schedules on `false` refuses to arm while a speaker is remembered —
    // so a tap on a cold stack could be swallowed entirely, leaving the screen
    // in Error with nothing pending. Restoring it costs nothing and cannot
    // spin: the resume condition requires the fetch to have finished, which is
    // what the line above just asked for.
    if (scan) bluetoothManualScanPending = true;
    Serial.printf("bluetooth start_deferred reason=logo-fetch-active scan=%s\n",
                  scan ? "yes" : "no");
    return false;
  }
  const std::size_t largestInternalBlock =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  // DMA is MEASURED here, not gated on. Every refused allocation in the panic
  // of 2026-08-01 asked for MALLOC_CAP_DMA — twelve of 640 B, eleven of 1856 B
  // — while the internal figure still read comfortable, so the number above
  // demonstrably answers a different question than the one that killed it.
  //
  // The temptation is to add `|| largestDma < N`. The paragraph above this
  // function is the reason not to: the internal guard was itself added after
  // ONE crash, turned an occasional failure into a permanent one, and cost the
  // owner his speaker for an evening. A second invented constant would repeat
  // that exactly. Nothing in the repository supports a DMA number yet — the
  // observations are inconsistent with any single threshold (a start at
  // 38,900 B worked; one at 32,756 B worked but fell to a 564 B minimum; the
  // panic came at 1,588; a 1,856 B DMA request was refused at 3,444).
  //
  // So both paths log it, refusal AND success. A boundary can only be argued
  // from figures on BOTH sides of it, and today we have them on neither.
  const std::size_t largestDmaAtStart =
      heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
  if (largestInternalBlock < kBluetoothStartLargestBlockMin) {
    Serial.printf(
        "bluetooth start_refused largest_internal=%u dma_largest=%u "
        "required=%u reason=memory\n",
        static_cast<unsigned>(largestInternalBlock),
        static_cast<unsigned>(largestDmaAtStart),
        static_cast<unsigned>(kBluetoothStartLargestBlockMin));
    bluetoothConnectInFlight = false;
    bluetoothScanActive = false;
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                              activeBluetoothName.data());
    return false;
  }
  configureBluetoothSource();
  // Arduino-ESP32 2.x is prebuilt for BTDM. Prepare and enable the controller
  // explicitly as Classic-only before the library calls btStart(); otherwise
  // releasing BLE memory and then asking btStart() for BTDM is invalid.
  if (!prepareClassicBluetoothController()) {
    bluetoothConnectInFlight = false;
    bluetoothScanActive = false;
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                              activeBluetoothName.data());
    return false;
  }
  if (scan) {
    bluetoothSource.set_auto_reconnect(false, 0);
    bluetoothSource.authorizeProfileOpen(
        open_radio::BluetoothProfileOpenReason::Discovery);
  } else {
    bluetoothSource.set_auto_reconnect(activeBluetoothAddress.data(), 0);
    bluetoothSource.authorizeProfileOpen(
        open_radio::BluetoothProfileOpenReason::BootRemembered);
    bluetoothConnectInFlight = true;
    bluetoothDirtyAttemptReported = false;
    bluetoothConnectStartedMs = nowMs;
    ++bluetoothDirectConnectAttempts;
  }
  bluetoothSource.start();
  // start() returns void and gives up quietly: a controller that will not
  // enable, a Bluedroid init or enable that fails, a work queue or task that
  // cannot be created — each logs and returns, and the caller cannot tell.
  // Setting the flag regardless was the expensive part. A false "started"
  // suppresses every retry (they all test it first), keeps
  // bluetoothConnectInFlight raised so new streams are refused, and closes the
  // TLS gate for the rest of the boot on the strength of a stack that is not
  // there. Three separate silences from one unchecked assumption.
  //
  // Asking the SDK instead of trusting the library. This is the same question
  // prepareClassicBluetoothController() already asks about the controller, one
  // layer up.
  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
    Serial.printf(
        "bluetooth start_failed reason=bluedroid-not-enabled status=%d "
        "controller=%d\n",
        static_cast<int>(esp_bluedroid_get_status()),
        static_cast<int>(esp_bt_controller_get_status()));
    bluetoothConnectInFlight = false;
    bluetoothScanActive = false;
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                              activeBluetoothName.data());
    return false;
  }
  if (!bluetoothSource.appTaskRunning()) {
    // Codex F08 (2026-09-08): Bluedroid can be enabled while the library's
    // BtAppT task failed to allocate. Its queued STACK_UP is then never
    // processed, no dial ever happens, and with the flag set the owner would
    // suppress every stream open for the rest of the boot. Loud, not wedged.
    Serial.println("bluetooth start_failed reason=app-task-missing");
    bluetoothConnectInFlight = false;
    bluetoothScanActive = false;
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                              activeBluetoothName.data());
    return false;
  }
  bluetoothStackStarted = true;
  ++bluetoothStackStarts;
  // The figure that let a start THROUGH — the other side of the boundary. Only
  // refusals were ever recorded, so every observation we had came from below
  // the line and none from above it, which is not enough to argue where the
  // line belongs.
  Serial.printf("bluetooth start_admitted largest_internal=%u dma_largest=%u\n",
                static_cast<unsigned>(largestInternalBlock),
                static_cast<unsigned>(largestDmaAtStart));
  Serial.printf("bluetooth stack_start=%lu mode=%s\n",
                static_cast<unsigned long>(bluetoothStackStarts),
                scan ? "scan" : "remembered");
  return true;
}

void startBluetooth(bool scan) {
  const auto nowMs = millis();
  bluetoothAutoStartPending = false;
  bluetoothRetryAtMs = 0;
  bluetoothDiscoveryRetryAtMs = 0;
  useLocalBluetoothFallback();
  applyBluetoothVolume();
  if (scan) {
    activeBluetoothName.fill(0);
    activeBluetoothAddress.fill(0);
    bluetoothSource.setReconnectPeer(activeBluetoothAddress, false);
    if (bluetoothStackStarted) bluetoothSource.applyReconnectListenPolicy();
    bluetoothActiveWasRemembered = false;
    bluetoothFoundPending = false;
    bluetoothAllowNewCandidate.store(true, std::memory_order_relaxed);
    bluetoothScanActive = true;
    bluetoothScanStartedMs = nowMs;
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Scanning);
    if (!bluetoothStackStarted) {
      if (!startBluetoothStack(true, nowMs)) {
        scheduleBluetoothDiscoveryRetry(nowMs);
      }
      return;
    }
    const auto state = bluetoothSource.get_connection_state();
    if (state != ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
      bluetoothScanAfterDisconnect = true;
      if (state != ESP_A2D_CONNECTION_STATE_DISCONNECTING) {
        bluetoothSource.noteLocalTeardown(
            LocalVolumeBluetoothSource::kTeardownForgetOrScan);
        bluetoothSource.disconnect();
      }
      return;
    }
    if (!beginBluetoothDiscovery(nowMs)) {
      bluetoothScanActive = false;
      boardUi.setBluetoothState(open_radio::BluetoothUiState::Error);
    }
    return;
  }

  if (!loadRememberedBluetooth()) {
    bluetoothSource.setReconnectPeer(rememberedBluetoothAddress, false);
    if (bluetoothStackStarted) bluetoothSource.applyReconnectListenPolicy();
    boardUi.forgetBluetooth();
    scheduleBluetoothDiscoveryRetry(nowMs, 250U);
    return;
  }
  activeBluetoothName = rememberedBluetoothName;
  activeBluetoothAddress = rememberedBluetoothAddress;
  bluetoothSource.setReconnectPeer(activeBluetoothAddress, true);
  bluetoothActiveWasRemembered = true;
  bluetoothAllowNewCandidate.store(false, std::memory_order_relaxed);
  boardUi.setBluetoothState(open_radio::BluetoothUiState::Connecting,
                            activeBluetoothName.data());
  if (!bluetoothStackStarted) {
    if (!startBluetoothStack(false, nowMs)) scheduleBluetoothRetry(nowMs);
  } else if (!connectRememberedBluetooth(nowMs)) {
    scheduleBluetoothRetry(nowMs);
  }
}
#endif

// POZA blokiem wariantu FULL — i to jest cala rzecz.
//
// Pierwsza wersja miala deklaracje i definicje WEWNATRZ `#if VARIANT_FULL`,
// razem z galezia `#else return true`, ktora przez to nigdy nie mogla sie
// skompilowac. Wywolanie stoi natomiast pod `OPEN_RADIO_HAS_MP3`, a ten jest
// wlaczony takze dla `core2-local-speaker-fallback` — czyli w tej linii
// buildowej zostawalo samo wywolanie, bez deklaracji. Deterministyczny blad
// kompilacji w obowiazkowym wariancie, ktorego `npm run check` NIE LAPIE, bo
// buduje wylacznie kandydata publicznego. Znalazla to recenzja, nie bramka.
#if defined(OPEN_RADIO_VARIANT_FULL)
bool internalHeapFreeForTls() {
  return !bluetoothStackStarted && !bluetoothMediaStarted;
}
#else
bool internalHeapFreeForTls() { return true; }   // bez Bluetootha nie ma konkurencji
#endif


#if defined(OPEN_RADIO_HAS_MP3)
// Seed the now-playing title with the station name on every selection. Some
// stations never send an ICY StreamTitle, so the screen would otherwise show
// nothing at all. The seed also replaces a stale title left by the previous
// station, and a real StreamTitle simply overwrites it when it arrives.
// With automatic station-hopping rejected by owner decision (2026-07-21), a
// station whose every alternate failed just keeps retrying quietly. This line
// is the only signal separating "the broadcaster is down" from "the radio is
// broken": once a full lap through the slot's alternates yields no audio, the
// title says so, and the first decoded frame afterwards clears it by reseeding
// the station name (a real ICY title then overwrites that).
bool exhaustionAnnounced = false;
void seedNowPlayingStationName();

void announceStationExhaustion() {
  const bool exhausted = stationRuntime.exhaustedCycles() > 0;
  if (exhausted == exhaustionAnnounced) return;
  exhaustionAnnounced = exhausted;
  if (exhausted) {
    boardUi.metadata(boardUi.config().locale == open_radio::Locale::En
                         ? "This station is not broadcasting. Still trying."
                         : "Ta stacja teraz nie nadaje. Próbuję dalej.");
    Serial.println("station_health stage=announced");
  } else {
    seedNowPlayingStationName();
  }
}

void seedNowPlayingStationName() {
  const auto index = boardUi.config().preferredStationIndex;
  if (index < open_radio::generated::kStationCount) {
    boardUi.metadata(stationSlots.name(index));
  }
}

void loadPlaybackSettings() {
  playbackMode = preferences.getUChar(kPlaybackModeKey, 0) == 1
                     ? PlaybackMode::Noise
                     : PlaybackMode::Radio;
  const auto rawColor = preferences.getUChar(kNoiseColorKey, 0);
  storedNoiseColor = rawColor <= static_cast<std::uint8_t>(
                                     open_radio::NoiseColor::Brown)
                         ? static_cast<open_radio::NoiseColor>(rawColor)
                         : open_radio::NoiseColor::White;
}

void persistPlaybackMode(PlaybackMode mode) {
  const auto value = static_cast<std::uint8_t>(mode);
  if (preferences.getUChar(kPlaybackModeKey, 0) != value) {
    preferences.putUChar(kPlaybackModeKey, value);
  }
}

void persistNoiseColor(open_radio::NoiseColor color) {
  const auto value = static_cast<std::uint8_t>(color);
  if (preferences.getUChar(kNoiseColorKey, 0) != value) {
    preferences.putUChar(kNoiseColorKey, value);
  }
  storedNoiseColor = color;
}

void startNoisePlayback() {
  localNoiseSource.start(storedNoiseColor, esp_random());
  Serial.printf("noise event=start color=%u fade_ms=250\n",
                static_cast<unsigned>(storedNoiseColor));
}

void enterNoiseMode(std::uint32_t nowMs) {
  stationRuntime.suspend(nowMs);
  decodedFrames.clear();
  localSpeaker.stopPlayback();
#if defined(OPEN_RADIO_VARIANT_FULL)
  bluetoothQueue.clear();
#endif
  playbackMode = PlaybackMode::Noise;
  noiseExitPending = false;
  storedNoiseColor = boardUi.noiseColor();
  persistPlaybackMode(PlaybackMode::Noise);
  persistNoiseColor(storedNoiseColor);
  startNoisePlayback();
  Serial.println("noise mode=entered source=local");
}

void finishNoiseExit(std::uint32_t nowMs) {
  localSpeaker.stopPlayback();
  playbackMode = PlaybackMode::Radio;
  noiseExitPending = false;
  stationRuntime.select(boardUi.config().preferredStationIndex, nowMs);
  seedNowPlayingStationName();
  Serial.println("noise mode=exited target=last_station");
}

void requestRadioMode() {
  persistPlaybackMode(PlaybackMode::Radio);
  if (playbackMode != PlaybackMode::Noise) return;
  noiseExitPending = true;
  if (localNoiseSource.active()) {
    localNoiseSource.requestStop();
    Serial.println("noise event=fade_out reason=radio fade_ms=150");
  }
}

bool activeAudioSourceHealthy() {
  if (playbackMode != PlaybackMode::Noise) return stationRuntime.healthy();
  if (localNoiseSource.active() || decodedFrames.size() != 0 ||
      localSpeaker.bufferedBlocks() != 0) {
    return true;
  }
#if defined(OPEN_RADIO_VARIANT_FULL)
  return bluetoothQueue.bufferedFrames() != 0;
#else
  return false;
#endif
}

bool noiseTailDrained() {
  if (decodedFrames.size() != 0 || localSpeaker.bufferedBlocks() != 0) {
    return false;
  }
#if defined(OPEN_RADIO_VARIANT_FULL)
  if (bluetoothQueue.bufferedFrames() != 0) return false;
#endif
  return true;
}
#endif

void initializeRuntime(const open_radio::StorageSelectionDto& storage) {
  runtimeServices.boot(storage, 1, 1, 0);
  runtimeServices.localOutput(M5.Speaker.isEnabled(), 1, 1, 0);
  runtimeIngress.drain();
}

bool eraseAllLocalHistory() {
  Serial.println("secure_reset phase=begin scope=default_nvs");
  networkRuntime.setPortalEnabled(false);
  WiFi.disconnect(true, true);
  delay(50);
  preferences.end();
  const auto deinitResult = nvs_flash_deinit();
  if (deinitResult != ESP_OK &&
      deinitResult != ESP_ERR_NVS_NOT_INITIALIZED) {
    Serial.printf("secure_reset phase=deinit result=%d\n",
                  static_cast<int>(deinitResult));
    preferences.begin("open-radio", false);
    return false;
  }
  logoStore.clearStored();
  const auto eraseResult = nvs_flash_erase();
  if (eraseResult != ESP_OK) {
    Serial.printf("secure_reset phase=erase result=%d\n",
                  static_cast<int>(eraseResult));
    nvs_flash_init();
    preferences.begin("open-radio", false);
    return false;
  }
  Serial.println("secure_reset phase=complete action=restart");
  Serial.flush();
  delay(100);
  ESP.restart();
  return true;
}

#if defined(OPEN_RADIO_HAS_MP3)
// Fetches every missing tile icon off the audio path, then releases the
// Bluetooth auto-start gate and exits. Low priority, other core; a pass is
// at most nine bounded fetches, so the task always terminates.
void logoFetchTask(void*) {
  // First boot on a factory-fresh flash: the filesystem does not exist yet
  // and must be formatted HERE — this task is not watchdog-subscribed and
  // the radio is already playing. A failed format degrades to monograms.
  if (!logoStore.ensureReady()) {
    Serial.println("logo_fetch stage=fs-unavailable");
  }
  // First the rmfon pools, then the icons — same heap window, same task, and
  // no longer the owner loop: three TLS discoveries in a row (DNS ladder,
  // connect, handshake) at every Wi-Fi connect could exceed its 30-second
  // watchdog on a sick network (audit 2026-09-08, V1-01/V1-08, Codex F05). A
  // station started meanwhile falls back to its pinned mount until the pools
  // land; StationRuntime::PoolLock keeps the two tasks off each other.
  if (!logoFetchAbort.load(std::memory_order_relaxed)) {
    stationRuntime.prefetchRmfPools();
  }
  // Fetching next to a LIVE stream exhausts something in the shared network
  // stack after about two connections (measured: the same URLs succeed from
  // a laptop and succeeded on-device before playback started). Three passes
  // with a quiet gap between them harvest what each burst allows, so one
  // boot converges instead of two icons per reboot.
  for (int pass = 0; pass < 3; ++pass) {
    if (logoFetchAbort.load(std::memory_order_relaxed)) break;
    if (pass > 0) {
      if (!logoStore.anyMissing(stationSlots)) break;
      logoStore.resetAttempts();
      Serial.printf("logo_fetch stage=pass-retry pass=%d\n", pass + 1);
      // Slept in slices so an abort lands in half a second instead of twenty.
      // Most of the wait a person sat through on 2026-08-01 was HERE — two of
      // these gaps, sixty-six seconds of a Bluetooth request queued behind
      // deliberate idling. Nothing is even allocated during them; the queue was
      // paying for a quiet period it did not need to wait out.
      for (int slice = 0; slice < 40 && !logoFetchAbort.load(std::memory_order_relaxed); ++slice) {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      if (logoFetchAbort.load(std::memory_order_relaxed)) break;
    }
    while (networkRuntime.connected() && !logoFetchAbort.load(std::memory_order_relaxed) &&
           logoStore.fetchNextMissing(stationSlots)) {
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (!networkRuntime.connected()) break;
  }
  // "pairing" was true when a tap was the only thing that aborted a fetch.
  // Recovery after a dropped speaker aborts it too now, and a log that names
  // the wrong cause is worse than one that names none: it sends the next
  // reader looking at the screen instead of at the link.
  if (logoFetchAbort.load(std::memory_order_relaxed)) {
    // The flag naming the pairing case only exists where Bluetooth does. This
    // function is compiled in every variant, and the last time something
    // Bluetooth-shaped was written without that in mind it left three build
    // variants broken for months.
#if defined(OPEN_RADIO_VARIANT_FULL)
    const char* const abortReason =
        bluetoothManualScanPending ? "pairing" : "bluetooth-start";
#else
    const char* const abortReason = "unknown";
#endif
    Serial.printf("logo_fetch stage=aborted reason=%s\n", abortReason);
  }
  Serial.println("logo_fetch stage=pass-complete");
  // NOT logoFetchPending=false and NOT self-delete: the loop reaps this task
  // (freeing the stack immediately, because a suspended task runs nowhere)
  // and releases the Bluetooth gate afterwards, in that order.
  logoFetchDone.store(true, std::memory_order_relaxed);
  vTaskSuspend(nullptr);
}

void startLogoFetchTask() {
  if (logoFetchTaskHandle != nullptr) return;
  if (xTaskCreatePinnedToCore(logoFetchTask, "logo_fetch", 8192, nullptr, 1,
                              &logoFetchTaskHandle, 0) != pdPASS) {
    // No task means no fetch this boot — monograms stay, Bluetooth must not.
    logoFetchPending = false;
    Serial.println("logo_fetch stage=task-failed");
  }
}
#endif

// Console session: the settings tile opens a 15-minute browser window over
// the home network. Radio playback suspends for the duration — the Wi-Fi air
// the HTTP session takes is the same air the stream needs — while local noise
// keeps playing, since it never touches the network.
#if defined(OPEN_RADIO_HAS_MP3)
// The console lives on the stream and the slots, so all of it belongs to the
// MP3 variant. Its state declarations were always under HAS_MP3; these
// functions were not — and three of the five mandatory variants had not
// compiled since the commit that introduced them.
void enterConsoleSession(std::uint32_t nowMs) {
  if (consoleSessionActive) {
    // Re-entering the tile prolongs the window instead of toggling it shut —
    // an accidental second tap must never strand the phone mid-errand.
    networkRuntime.startConsoleSession(nowMs);
    return;
  }
  if (!networkRuntime.startConsoleSession(nowMs)) {
    Serial.println("console stage=rejected reason=offline");
    return;
  }
  consoleSessionActive = true;
  consoleSlotsChanged = false;
#if defined(OPEN_RADIO_HAS_MP3)
  if (playbackMode != PlaybackMode::Noise) {
    stationRuntime.suspend(nowMs);
    decodedFrames.clear();
    localSpeaker.stopPlayback();
#if defined(OPEN_RADIO_VARIANT_FULL)
    bluetoothQueue.clear();
#endif
  }
#endif
  // The name exists while the console does: the responder starts here and
  // stops at exit, so its memory belongs to the session, not to the radio.
  const bool mdnsUp = MDNS.begin("open-radio");
  Serial.printf("mdns host=open-radio.local result=%s scope=console\n",
                mdnsUp ? "ok" : "fail");
  boardUi.metadata(boardUi.config().locale == open_radio::Locale::En
                       ? "Console: http://open-radio.local"
                       : "Konsola: http://open-radio.local");
  Serial.println("console mode=entered playback=suspended");
}

void exitConsoleSession(std::uint32_t nowMs) {
  consoleSessionActive = false;
  MDNS.end();
  Serial.println("mdns scope=console state=stopped");
  if (consoleSlotsChanged) {
    // The new slots need their logos, and logo TLS is only feasible in the
    // pre-Bluetooth window — which only a fresh boot provides.
    Serial.println("console mode=exited action=restart reason=slots-changed");
    Serial.flush();
    delay(100);
    ESP.restart();
    return;
  }
#if defined(OPEN_RADIO_HAS_MP3)
  if (playbackMode != PlaybackMode::Noise) {
    stationRuntime.select(boardUi.config().preferredStationIndex, nowMs);
    seedNowPlayingStationName();
  }
#else
  (void)nowMs;
#endif
  Serial.println("console mode=exited action=resume");
}
#endif

void flushUi() {
  const auto update = boardUi.flush();
  if (update.secureResetRequested) {
    eraseAllLocalHistory();
    return;
  }
#if defined(OPEN_RADIO_HAS_MP3)
  if (update.consoleSessionRequested) {
    enterConsoleSession(millis());
  }
  if (update.consoleSessionEndRequested && consoleSessionActive) {
    // The console screen's bottom button: the loop poll resumes playback or
    // restarts on changed slots.
    networkRuntime.stopConsoleSession();
  }
#endif
  if (update.configChanged &&
      !configStorage.commit(boardUi.config(), open_radio::WritePhase::AfterCommit)) {
    Serial.println("ui config persistence failed");
  }
#if defined(OPEN_RADIO_HAS_MP3)
  if (update.noiseColorChanged) {
    persistNoiseColor(boardUi.noiseColor());
    localNoiseSource.setColor(storedNoiseColor);
    Serial.printf("noise event=color color=%u fade_ms=150+250\n",
                  static_cast<unsigned>(storedNoiseColor));
  }
  if (update.noiseModeRequested) {
    enterNoiseMode(millis());
  }
  if (update.radioModeRequested) {
    requestRadioMode();
  } else if (update.noisePlaybackChanged && !update.noiseModeRequested &&
             playbackMode == PlaybackMode::Noise) {
    if (boardUi.noisePlaying()) {
      startNoisePlayback();
    } else {
      localNoiseSource.requestStop();
      Serial.println("noise event=fade_out reason=stop fade_ms=150");
    }
  }
  if (update.stationChanged) {
    // Picking a station on the cube means "play the radio": an open console
    // session ends here, so the resume path (loop poll) can start the stream.
    if (consoleSessionActive) networkRuntime.stopConsoleSession();
    stationRuntime.select(boardUi.config().preferredStationIndex, millis());
    seedNowPlayingStationName();
#if defined(OPEN_RADIO_VARIANT_FULL)
    bluetoothQueue.clear();
#endif
  }
#endif
#if defined(OPEN_RADIO_VARIANT_FULL)
  if (update.bluetoothScanRequested) {
    // Bringing up Bluedroid while the logo fetcher holds two 16,717-byte TLS
    // buffers plus its task stack costs more internal RAM than the two have
    // together. osi_malloc does not fail softly: it asserts, and the radio
    // reboots. Measured 2026-08-01 — reset_reason=4 three seconds after a tap.
    // Deferred, not dropped: the UI says Scanning because the request WAS
    // accepted, and the scan opens the moment the fetcher is reaped.
    if (logoFetchPending) {
      bluetoothManualScanPending = true;
      bluetoothManualScanDeadlineMs = millis() + kBluetoothManualScanDeferralMs;
      boardUi.setBluetoothState(open_radio::BluetoothUiState::Scanning);
      // The window opens NOW if the stack is already up, and only the start is
      // deferred. Opening it costs no meaningful memory, and holding it back is
      // what would hurt: with the stack running, a remembered speaker dialling
      // in gets rejected every ~20 s while our own pages time out at HCI 0x104
      // — the live deadlock of 2026-07-17, which this window exists to prevent.
      // Deferring the expensive half is the fix; deferring the cheap half would
      // have traded a panic for the fault it replaced.
      if (bluetoothStackStarted) bluetoothSource.setPairingWindow(true);
      // And tell the fetcher to stop. Waiting for it to finish on its own cost
      // sixty-six seconds of a screen saying Scanning, which from the outside is
      // indistinguishable from a hang — a fault traded, not removed.
      logoFetchAbort.store(true, std::memory_order_relaxed);
      Serial.printf("bluetooth scan_deferred reason=logo-fetch stack_up=%s\n",
                    bluetoothStackStarted ? "yes" : "no");
    } else {
      // Open the pairing window on the TAP itself: discovery may be gated out
      // by an in-flight dial for a few seconds, but an inbound sink that calls
      // during that gap should still be adopted, not rejected.
      bluetoothSource.setPairingWindow(true);
      startBluetooth(true);
    }
  }
  applyBluetoothVolume();
#endif
  if (update.wifiPortalToggleRequested) {
    const bool requested = !networkRuntime.portalActive();
    networkRuntime.setPortalEnabled(requested);
    boardUi.setWifiPortalActive(networkRuntime.portalActive());
    boardUi.setSetupAccessCode(networkRuntime.portalActive()
                                   ? networkRuntime.portalPassword()
                                   : nullptr);
  }
  if (update.wifiPortalRestartRequested) {
    networkRuntime.restartPortal();
    boardUi.setWifiPortalActive(networkRuntime.portalActive());
    boardUi.setSetupAccessCode(networkRuntime.portalActive()
                                   ? networkRuntime.portalPassword()
                                   : nullptr);
  }
}

void handleNetworkConnected(void*, bool newlyProvisioned) {
  if (newlyProvisioned || !boardUi.config().onboardingComplete) {
    boardUi.completeNetworkOnboarding();
    flushUi();
    delay(50);
    ESP.restart();
    return;
  }
  const auto nowMs = millis();
  startOptionalTimeSync(nowMs);
  // No mDNS here any more. It started at every network connect and sat there
  // for ever — a responder task and its buffers, roughly seven kilobytes of
  // internal RAM — while the only thing that ever resolves the name is a
  // browser during a console session. The trace of 2026-07-30 13:39 watched
  // Bluedroid die of a failed allocation at media start with largest=1588
  // free, which is exactly the margin mDNS was sitting on. It now runs only
  // while the console does; the Wi-Fi screen keeps showing the raw address,
  // which works whether or not anything is resolving names.
  boardUi.setDeviceAddress(WiFi.localIP().toString().c_str());
#if defined(OPEN_RADIO_HAS_MP3)
  if (audioBuffersReady) {
    // THE WINDOW IS CHECKED NOW, NOT ASSUMED. Both calls below want internal
    // heap that Bluedroid also needs, and this handler runs on EVERY Wi-Fi
    // connect — not only the first. With a speaker already playing, a brief
    // network blip used to bring the radio back by opening three TLS
    // handshakes and a fetch task on top of a live Bluetooth stack. Nothing
    // stopped it: the Bluetooth arming below has always tested
    // `!bluetoothStackStarted`, and the TLS side never did.
    //
    // Skipping costs nothing that works anyway. rmfPoolSlot() pins plain-HTTP
    // mounts precisely so rmfon stations stay startable in any heap state, and
    // the comment on it says TLS discovery during playback "fails BY DESIGN"
    // with the stack up. Tile icons stay monograms until a boot that has room.
    if (internalHeapFreeForTls()) {
      // Heap-rich window before the Bluetooth stack exists: the rmfon
      // discovery pools are cached by the logo-fetch task started below
      // (0.4.2: no longer on this watchdog-subscribed owner task — audit
      // 2026-09-08, V1-08) so in-session station switches never need TLS.
      // The radio starts NOW. Missing tile icons fetch in the background from
      // the main loop, one at a time, while the decode buffer can bridge the
      // pause — the old all-at-once pre-playback fetch was a minute of boot
      // silence after every new build. Bluetooth stays deferred until the
      // fetch pass finishes, keeping the TLS heap window open.
      logoFetchPending = true;
      startLogoFetchTask();
    } else {
      // Reachable ONLY in the full variant: the predicate returns a constant
      // `true` where there is no Bluetooth. A dead branch still has to COMPILE
      // though, and these two flags do not exist in those variants —
      // `core2-local-speaker-fallback` fell over on exactly this line.
#if defined(OPEN_RADIO_VARIANT_FULL)
      Serial.printf("tls_skipped reason=bluetooth-active stack=%s media=%s\n",
                    bluetoothStackStarted ? "up" : "down",
                    bluetoothMediaStarted ? "playing" : "idle");
#endif
    }
    seedNowPlayingStationName();
    stationRuntime.networkConnected(nowMs);
  }
#endif
#if defined(OPEN_RADIO_VARIANT_FULL)
  // Defer the memory-heavy Classic Bluetooth stack until the first decoded
  // audio proves that Wi-Fi, endpoint resolution and playback are healthy.
  // Then reconnect the trusted peer automatically or discover the first
  // standards-compatible speaker automatically when no peer exists yet.
  if (!bluetoothStackStarted && !bluetoothMediaStarted) {
    bluetoothAutoStartPending = true;
  }
#endif
}

}  // namespace

#if defined(OPEN_RADIO_LAB_CONSOLE) && defined(OPEN_RADIO_HAS_MP3)
// Lab-lane serial console so soak captures can drive station switches
// without a human at the touchscreen. One command: "station <index>\n",
// reusing the exact switch path flushUi() takes for a touch selection.
// Non-blocking: bytes accumulate across owner-loop passes. The UI keeps
// showing its own selection — this bypasses BoardUi on purpose and is
// compiled only into the hardware-lab lane.
void pollLabConsole() {
  static char lineBuffer[24];
  static std::size_t lineLength = 0;
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') continue;
    if (value != '\n') {
      if (lineLength < sizeof(lineBuffer) - 1) lineBuffer[lineLength++] = value;
      continue;
    }
    lineBuffer[lineLength] = '\0';
    lineLength = 0;
    int stationIndex = -1;
    if (std::sscanf(lineBuffer, "station %d", &stationIndex) == 1 &&
        stationIndex >= 0 &&
        stationIndex < static_cast<int>(open_radio::generated::kStationCount)) {
      stationRuntime.select(static_cast<std::size_t>(stationIndex), millis());
      seedNowPlayingStationName();
#if defined(OPEN_RADIO_VARIANT_FULL)
      bluetoothQueue.clear();
#endif
      Serial.printf("lab_console station=%d result=selected\n", stationIndex);
    } else if (std::strcmp(lineBuffer, "screenshot") == 0) {
      boardUi.dumpFramebuffer();
    } else if (std::strcmp(lineBuffer, "noise on") == 0) {
      // So the generator can be exercised without a human at the touchscreen.
      // Whether it makes a SOUND still needs ears, but whether frames are being
      // produced, accepted by the speaker and consumed is exactly what the
      // audio_qc line reports, and that is what caught the last defect: every
      // write returned zero because the block size was wrong.
      enterNoiseMode(millis());
      Serial.println("lab_console noise=on");
    } else if (std::strcmp(lineBuffer, "noise off") == 0) {
      requestRadioMode();
      Serial.println("lab_console noise=off");
    } else if (lineBuffer[0] != '\0') {
      Serial.println("lab_console input=unrecognized result=ignored");
    }
  }
}
#endif

// Touch sampled several times per loop, with its events latched until the
// interface reads them.
//
// Measured on a Core2 v1.3 on 2026-07-29: the loop runs one iteration every
// ~133 ms (7.5 samples per second through a 50 ms diagnostic gate), because a
// single iteration decodes audio and pushes the whole display. Touch was polled
// exactly once per iteration, so a tap of 80-120 ms — an ordinary tap — begins
// and ends between two polls and is never seen at all. The panel, the interrupt
// line and the vendor driver were all verified working during the same capture:
// the panel reported 33 contacts and GPIO39 asserted 36 times while the driver
// reported almost none of them. Nothing was broken except the rate.
//
// Polling more often alone would make it worse, not better: wasClicked() is a
// one-shot flag cleared by the next update, so an extra poll can consume the
// click before the interface looks. Sampling and latching together is what
// fixes it. The latch is cleared by the reader, so an event survives however
// many samples happen before someone acts on it.
struct LatchedTouch {
  bool pressed = false;
  bool released = false;
  bool clicked = false;
  bool held = false;
  bool flicked = false;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t baseX = 0;
  std::int32_t distanceX = 0;
  std::int32_t distanceY = 0;

  void clear() { *this = LatchedTouch{}; }
};

LatchedTouch latchedTouch;

#if defined(OPEN_RADIO_LAB_CONSOLE)
// One line per touch EVENT (not per frame), so a person pressing the glass five
// times produces a readable transcript of what the panel reported and what the
// dispatcher did with it. This exists because 2026-07-30 was spent moving hit
// boxes on the theory that they were what the owner's finger was missing, and
// the theory was wrong: the fault was ghost taps, and one afternoon of this
// trace would have shown that in five presses.
inline void traceTouch(const char* event, int x, int y, int dx, int dy) {
  if (open_radio::public_candidate::gSerialQuiet.load(std::memory_order_relaxed)) return;
  Serial.printf("touch ev=%s x=%d y=%d dx=%d dy=%d t=%lu\n", event, x, y, dx, dy,
                static_cast<unsigned long>(millis()));
}
#else
inline void traceTouch(const char*, int, int, int, int) {}
#endif

void sampleTouch() {
  M5.Touch.update(millis());
  const auto& detail = M5.Touch.getDetail();
  // The three circles under the glass are zones of the SAME touch panel, at
  // y 240..279. A pad press therefore arrives TWICE: as M5.BtnA/B/C, and as an
  // ordinary touch these latches would dispatch to the screen ~100 ms later.
  // Measured on hardware 2026-07-30 (touchtrace.log): pad B raised
  // hardwareButton(B), which opened the station picker — and then its own
  // ghost, a tap at (166,278), SELECTED STATION 7 from the grid. Station 7 was
  // Radio ZET, whose only endpoint is a dead redirect, so the radio fell
  // silent, and the owner spent an afternoon being told the hit boxes were at
  // fault. The pads are buttons; their touches are not taps.
  if (detail.y >= 240 || detail.base_y >= 240) {
    if (detail.wasPressed() || detail.wasReleased()) {
      traceTouch("pad-zone", detail.x, detail.y, 0, 0);
    }
    return;
  }
  if (detail.wasPressed()) {
    traceTouch("press", detail.x, detail.y, 0, 0);
  }
  if (detail.isPressed()) {
    latchedTouch.pressed = true;
    latchedTouch.x = detail.x;
    latchedTouch.y = detail.y;
  }
  if (detail.wasReleased()) {
    traceTouch("release", detail.x, detail.y, detail.distanceX(), detail.distanceY());
    latchedTouch.released = true;
    latchedTouch.x = detail.x;
    latchedTouch.y = detail.y;
    // A press that barely moved IS a click, whatever the library decided.
    //
    // M5Unified only raises wasClicked() when the contact stayed put; anything
    // that drifts becomes a flick and the click never arrives. But a fingertip
    // on glass always drifts, and it drifts most at an edge, because the panel
    // reports the CENTROID of the contact patch and that centroid slides as the
    // finger rolls off. The owner's report was exact: "klikam dobrze" and the
    // close button in the corner does not answer. It was never the hit box —
    // 52 by 48, reaching two edges, measured — it was that no tap was delivered
    // at all.
    //
    // Sixteen pixels is five per cent of the panel: far below any deliberate
    // swipe (the settings pages need a third of the width) and far above the
    // wobble of a finger being lifted.
    const int driftX = detail.distanceX() < 0 ? -detail.distanceX() : detail.distanceX();
    const int driftY = detail.distanceY() < 0 ? -detail.distanceY() : detail.distanceY();
    // Under 28, mirroring the swipe gate exactly (a swipe needs |dx| >= 28 and
    // |dx| >= |dy|). The first threshold was 16, which left a DEAD ZONE: a
    // release that drifted 17..27 pixels was too dirty to be this click and too
    // small to be that swipe, so it became nothing at all — "raz zareaguje raz
    // nie", most often at the edges, where the contact-patch centroid slides
    // furthest as the finger rolls off.
    if (!latchedTouch.held && driftX < 28 && driftY < 28) {
      latchedTouch.clicked = true;
    }
  }
  if (detail.wasClicked()) {
    traceTouch("lib-click", detail.x, detail.y, 0, 0);
    latchedTouch.clicked = true;
    latchedTouch.x = detail.x;
    latchedTouch.y = detail.y;
  }
  if (detail.wasHold()) latchedTouch.held = true;
  if (detail.wasFlicked()) {
    traceTouch("flick", detail.x, detail.y, detail.distanceX(), detail.distanceY());
    latchedTouch.flicked = true;
    latchedTouch.baseX = detail.base_x;
    latchedTouch.distanceX = detail.distanceX();
    latchedTouch.distanceY = detail.distanceY();
  }
}

void setup() {
  auto m5Config = M5.config();
  m5Config.output_power = false;
  M5.begin(m5Config);
  Serial.begin(115200);
  // Arduino leaves loopTask outside the task watchdog, so a wedged loop is a
  // silent, permanent hang (hit live 2026-07-17: the first aacp stream froze
  // the device with zero serial output). Thirty seconds is far above every
  // legitimate stall observed (worst: 16.3 s LWIP DNS retry ladder); on
  // expiry the panic handler prints a backtrace and reboots, and the
  // last-known-good endpoint resumes playback — a pocket radio must recover
  // itself, not wait for a power cycle.
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(nullptr);
  // The hash_map_set assert of 2026-07-30 gave one line and a reboot. This
  // hook fires on EVERY failed allocation, from ROM printf (heap-free), so the
  // next out-of-memory death names its size and caps before Bluedroid pulls
  // the trigger. Costs nothing until the day it is the only witness.
  heap_caps_register_failed_alloc_callback(
      [](std::size_t size, std::uint32_t caps, const char* function) {
        ets_printf("alloc_failed size=%u caps=0x%x fn=%s\n",
                   static_cast<unsigned>(size), static_cast<unsigned>(caps),
                   function != nullptr ? function : "?");
      });
  Serial.printf("boot build_sha=%s\n", OPEN_RADIO_BUILD_SHA);
  // Started last and cheaply. An optional module a person bought separately
  // must never be able to delay a radio coming up, so it gets its own task on
  // the network core and the boot path does not wait for it.
#if defined(OPEN_RADIO_HAS_MP3)
  sensorTask.begin(&sensorSnapshot);
  // One source of truth for both directions: the same function the network
  // handler asks. StationRuntime gets a pointer, never a copy of the state.
  stationRuntime.setTlsGate(&internalHeapFreeForTls);
#endif
  // The capture allowlist has expected this line since day one, but nothing
  // ever emitted it — which left the 2026-07-17 double reboot after a
  // dead-speaker recovery dial unexplained (brownout vs panic vs watchdog).
  // esp_reset_reason(): 1=power-on, 3=sw, 4=panic, 5/6=wdt, 9=brownout.
  Serial.printf("boot reset_reason=%d\n",
                static_cast<int>(esp_reset_reason()));
#if defined(OPEN_RADIO_HAS_MP3)
  audioBuffersReady = decodedFrames.begin();
#if defined(OPEN_RADIO_VARIANT_FULL)
  audioBuffersReady = bluetoothQueue.begin() && audioBuffersReady;
#endif
  Serial.printf("audio_buffers result=%s decoded_psram=%u\n",
                audioBuffersReady ? "ok" : "error",
                static_cast<unsigned>(decodedFrames.capacity() *
                                      sizeof(open_radio::PcmFrame)));
#endif
  reportMemoryBudget("boot");
  preferences.begin("open-radio", false);
  // Owner decision 2026-07-21: a NEW build always boots factory-fresh, so the
  // owner walks the exact customer first-run path after every flash and a
  // regression there cannot hide behind his existing configuration. The same
  // rule protects customers: no release ever meets another release's NVS, so
  // the whole class of stale-state resurrection bugs (Pogoda 07-17, Jedynka's
  // 192k LKG today) stops existing by construction. A reboot on the SAME
  // build keeps every setting — a pocket radio must remember its owner.
  {
    const String storedBuild = preferences.getString("build_sha", "");
    if (storedBuild != OPEN_RADIO_BUILD_SHA) {
      // ADR-016. Until 0.3 this erased the whole partition. The rationale was
      // real — two incidents of stale state resurrecting across releases — but
      // both were DERIVED state, and the erase paid for that with the owner's
      // Wi-Fi, speaker, nine stations, language and display settings. It also
      // ran before StationSlots::load(), which made every migration this
      // project writes unreachable on a real upgrade.
      //
      // Now the derived families go and the owner's choices stay.
      // open_radio::classifyKey is the contract; the pinned key-set test in
      // upgrade_policy_tests.cpp is what stops a new key from silently
      // becoming unclassified.
      // Every generation of the key, not just the current one. The loop builds
      // the names it deletes, so a retired prefix is unreachable by the
      // classifier and would sit in NVS forever: nine stale URLs that no code
      // path can read and no upgrade can clear. Harmless until someone reuses a
      // prefix or a downgrade reads one, and free to sweep now.
      static constexpr const char* kCachePrefixes[] = {"lkg4_", "lkg3_", "lkg2_"};
      std::size_t erased = 0;
      for (const char* prefix : kCachePrefixes) {
        for (std::size_t station = 0;
             station < open_radio::public_candidate::StationSlots::kSlotCount; ++station) {
          char key[16] = {};
          std::snprintf(key, sizeof(key), "%s%u", prefix, static_cast<unsigned>(station));
          if (open_radio::classifyKey(key) != open_radio::UpgradeAction::Erase) continue;
          if (preferences.remove(key)) ++erased;
        }
      }
      Serial.printf("upgrade reason=new-build from=%s derived_erased=%u kept=choices\n",
                    storedBuild.length() > 0 ? storedBuild.c_str() : "(none)",
                    static_cast<unsigned>(erased));
      preferences.putString("build_sha", OPEN_RADIO_BUILD_SHA);
      // Bounded file deletion, not a partition format: the format of a
      // virgin filesystem takes longer than the 30 s loop watchdog on slow
      // flash chips and belongs to the logo task (ensureReady), never here.
      // Logos are a cache, so they go with the rest of the derived state.
      logoStore.clearStored();
    }
  }
  stationSlots.load(preferences);
  logoStore.begin();
  logoStore.loadAll(stationSlots);
  open_radio::ui::setRuntimeStationLogos(
      logoStore.logos(), open_radio::public_candidate::LogoStore::kSlots);
  refreshActiveStations();
  // The portal writes slots; the screen has to follow immediately, or the tile
  // keeps the old name until the next reboot.
  networkRuntime.bindSlots(
      stationSlots,
      [](void*) {
        refreshActiveStations();
#if defined(OPEN_RADIO_HAS_MP3)
        consoleSlotsChanged = true;
#endif
      },
      nullptr);
#if defined(OPEN_RADIO_HAS_MP3)
  loadPlaybackSettings();
#endif
  static_assert(open_radio::generated::kStationCount == 9,
                "launch catalog mismatch");
  static_assert(sizeof(open_radio::generated::k_index_html) > 100,
                "onboarding asset missing");
  static_assert(open_radio::generated::kOnboardingAssetCount == 4,
                "onboarding asset count mismatch");
  static_assert(open_radio::generated::kNetworkGoldenVectors.size() == 9,
                "network vectors missing");
  static_assert(open_radio::generated::kPersistenceGoldenVectors.size() == 9,
                "persistence vectors missing");
  static_assert(open_radio::generated::kResolverGoldenVectors.size() == 9,
                "resolver vectors missing");
  static_assert(open_radio::generated::kRuntimeSoakVectors.size() == 4,
                "runtime soak vectors missing");
  static_assert(open_radio::generated::kRuntimeIngressTraces.size() == 10,
                "runtime ingress traces missing");
  static_assert(open_radio::kOnboardingRoutes.size() == 9,
                "onboarding route contract mismatch");
  static_assert(sizeof(open_radio::PcmFrame) == sizeof(std::int16_t) * 2,
                "PCM frame layout mismatch");

#if defined(OPEN_RADIO_VARIANT_FULL)
  bluetoothCandidateQueue = xQueueCreate(1, sizeof(BluetoothCandidateEvent));
  bluetoothSource.set_avrc_rn_events({});
  bluetoothSource.set_volume_control(&bluetoothLinearVolumeControl);
  bluetoothSource.set_data_callback_in_frames(provideBluetoothFrames);
  bluetoothSource.set_on_connection_state_changed(handleBluetoothConnection);
  bluetoothSource.set_on_audio_state_changed(handleBluetoothAudio);
  outputRouter.preferBluetooth(false);
#elif defined(OPEN_RADIO_HAS_MP3)
  outputRouter.preferBluetooth(false);
#endif

#if defined(OPEN_RADIO_HAS_MP3)
  mp3Pipeline.setMetadataCallback(handleMetadata, nullptr);
#endif

  auto storage = configStorage.selectBoot();
#if defined(OPEN_RADIO_VARIANT_CORRUPT_SAFE_MODE)
  storage = {};
  storage.rejectionA = open_radio::StorageRejectReason::ChecksumMismatch;
  storage.rejectionB = open_radio::StorageRejectReason::ChecksumMismatch;
#endif
  initializeRuntime(storage);
  const auto runtime = runtimeOrchestrator.snapshot();
  // Factory boot takes the PRODUCT defaults, not the golden persistence
  // fixture. currentConfigA() exists to exercise the storage tests and
  // happens to say volume 42, editorial screen and screensaver on — which is
  // why none of the owner's factory defaults ever reached a fresh device
  // until 2026-07-21. One source of truth: DeviceConfigDto's initialisers.
  open_radio::DeviceConfigDto activeConfig = storage.status == open_radio::StorageStatus::Bootable
                                                 ? storage.config
                                                 : open_radio::DeviceConfigDto{};
  if (storage.status != open_radio::StorageStatus::Bootable) {
    activeConfig.wifiProfileCount = 0;
    activeConfig.onboardingComplete = false;
    activeConfig.bluetoothRemembered = false;
  }
  if (!boardUi.begin(activeConfig, runtime.state, runtime.output, millis())) {
    M5.Display.clear(TFT_BLACK);
    M5.Display.setCursor(8, 8);
    M5.Display.println("UI framebuffer allocation failed");
  }
#if defined(OPEN_RADIO_HAS_MP3)
  const bool resumeNoise = playbackMode == PlaybackMode::Noise &&
                           activeConfig.onboardingComplete;
  if (!resumeNoise) playbackMode = PlaybackMode::Radio;
  boardUi.restoreNoiseState(storedNoiseColor, resumeNoise);
  if (resumeNoise) {
    enterNoiseMode(millis());
  } else {
    stationRuntime.select(activeConfig.preferredStationIndex, millis());
    seedNowPlayingStationName();
  }
#endif
  networkRuntime.setConnectedCallback(handleNetworkConnected, nullptr);
  networkRuntime.begin(millis());
  if (!networkRuntime.hasProfiles()) {
    boardUi.requireNetworkOnboarding();
  }
  boardUi.setSetupAccessCode(networkRuntime.portalActive()
                                 ? networkRuntime.portalPassword()
                                 : nullptr);
  boardUi.setWifiPortalActive(networkRuntime.portalActive());
  boardUi.setWifiOnline(networkRuntime.connected());
  boardUi.setWifiStrength(networkRuntime.signalPercent());
  boardUi.setConsoleActive(networkRuntime.consoleActive());
  flushUi();
}

void loop() {
  esp_task_wdt_reset();
  const std::uint32_t loopStartedUs = micros();
  M5.update();
#if defined(OPEN_RADIO_LAB_CONSOLE) && defined(OPEN_RADIO_HAS_MP3)
  pollLabConsole();
#endif
  const auto loopNowMs = millis();
  if (static_cast<std::int32_t>(loopNowMs - nextMemoryReportMs) >= 0) {
    // The setup()-time print races the capture PTY attach after the
    // flash/monitor reset, so repeat the image identity with every periodic
    // report; evidence must be attributable to an exact binary. Labelled
    // heartbeat, not boot: today's log analysis briefly read four of these as
    // a crash loop, and a healthy pulse must not wear the name of a failure.
    Serial.printf("heartbeat build_sha=%s\n", OPEN_RADIO_BUILD_SHA);
    reportMemoryBudget("runtime");
#if defined(OPEN_RADIO_HAS_MP3)
    reportAudioQualityCounters();
#endif
    // Owner-observed unstable link (signal bouncing 70-90% where it used to
    // sit still, 2026-07-17) with the stutter arriving in waves: put the
    // radio link itself into every evidence capture instead of arguing about
    // it from narrative.
    if (networkRuntime.connected()) {
      Serial.printf("wifi_link rssi=%d channel=%u\n",
                    static_cast<int>(WiFi.RSSI()),
                    static_cast<unsigned>(WiFi.channel()));
    }
    nextMemoryReportMs = loopNowMs + 60U * 1000U;
  }
  const bool networkWasConnected = networkRuntime.connected();
  networkRuntime.loop(millis());
  pollOptionalTimeSync(millis());
  boardUi.setSetupAccessCode(networkRuntime.portalActive()
                                 ? networkRuntime.portalPassword()
                                 : nullptr);
  boardUi.setWifiPortalActive(networkRuntime.portalActive());
  boardUi.setWifiOnline(networkRuntime.connected());
  boardUi.setWifiStrength(networkRuntime.signalPercent());
  boardUi.setConsoleActive(networkRuntime.consoleActive());
  if (networkWasConnected && !networkRuntime.connected()) {
    boardUi.setDeviceAddress(nullptr);
#if defined(OPEN_RADIO_HAS_MP3)
    stationRuntime.networkLost(millis());
#endif
  }
  // The network runtime closes the console window itself (idle timeout or a
  // dropped network); playback resume or the slots-changed restart is ours.
#if defined(OPEN_RADIO_HAS_MP3)
  if (consoleSessionActive && !networkRuntime.consoleActive()) {
    exitConsoleSession(millis());
  }
#endif
  runtimeIngress.drain();
  runtimeIngress.advanceOwnerClock(millis());
  sampleTouch();
#if defined(OPEN_RADIO_HAS_MP3)
  const std::uint32_t audioLoopStartedUs = micros();
  if (playbackMode == PlaybackMode::Noise) {
    // Only enough to keep the speaker fed. Filling the radio-sized ring would
    // put five and a half seconds of already-decided noise in front of the next
    // thing the person asks for.
    const std::size_t target = std::min(kNoiseRingTargetFrames, decodedFrames.capacity());
    const std::size_t availableFrames =
        decodedFrames.size() >= target ? 0U : target - decodedFrames.size();
    localNoiseSource.generate(
        [](const open_radio::PcmFrame& frame) {
          return decodedFrames.push(frame);
        },
        availableFrames);
  } else {
    mp3Pipeline.beginOwnerLoop();
#if defined(OPEN_RADIO_VARIANT_FULL)
    mp3Pipeline.setBluetoothRealtimeMode(bluetoothMediaStarted);
    // A Classic Bluetooth connect attempt occupies the shared 2.4 GHz radio
    // for about five seconds. Defer only a new network open; local noise never
    // touches this path.
    stationRuntime.loop(millis(),
                        !consoleSessionActive &&
                            (!bluetoothConnectInFlight || bluetoothMediaStarted));
#else
    stationRuntime.loop(millis(), !consoleSessionActive);
#endif
    announceStationExhaustion();
  }
  maxAudioLoopUs =
      std::max(maxAudioLoopUs, micros() - audioLoopStartedUs);
#if defined(OPEN_RADIO_VARIANT_FULL)
  const bool bluetoothStandbyPrefilling =
      bluetoothLinkConnected && !bluetoothMediaStarted;
  if (bluetoothStandbyPrefilling) primeBluetoothStandbyQueue();
#endif
  const std::uint32_t audioDrainStartedUs = micros();
  const std::size_t drainedFrames =
#if defined(OPEN_RADIO_VARIANT_FULL)
      bluetoothStandbyPrefilling ? 0U :
#endif
      drainDecodedFrames();
  if (playbackMode == PlaybackMode::Radio) {
    stationRuntime.decodedFrames(drainedFrames, millis());
  }
  if (noiseExitPending && !localNoiseSource.active() && noiseTailDrained()) {
    finishNoiseExit(millis());
  } else if (playbackMode == PlaybackMode::Noise &&
             !localNoiseSource.active() && !noiseExitPending &&
             noiseTailDrained()) {
    // An intentional STOP is not an output starvation. Reset the local
    // speaker latch only after the generated fade has actually played.
    localSpeaker.stopPlayback();
  }
  maxAudioDrainUs =
      std::max(maxAudioDrainUs, micros() - audioDrainStartedUs);
#if defined(OPEN_RADIO_VARIANT_FULL)
#if defined(OPEN_RADIO_HAS_MP3)
  // Reap the finished fetch task BEFORE the Bluetooth gate can pass: the task
  // sits suspended, so this delete frees its 8 KB stack right here, and the
  // gate flag falls only once the memory is genuinely back.
  if (logoFetchDone.load(std::memory_order_relaxed) && logoFetchTaskHandle != nullptr &&
      eTaskGetState(logoFetchTaskHandle) == eSuspended) {
    vTaskDelete(logoFetchTaskHandle);
    logoFetchTaskHandle = nullptr;
    logoFetchDone.store(false, std::memory_order_relaxed);
    logoFetchPending = false;
    logoFetchAbort.store(false, std::memory_order_relaxed);
    Serial.println("logo_fetch stage=reaped stack_freed=8192");
  }
#endif
  bluetoothQueue.setPlaybackActive(
      activeAudioSourceHealthy() &&
      outputRouter.activeOutput() == open_radio::OutputKind::Bluetooth);
  // The tap the person made, replayed once the fetcher has let go. Kept next to
  // the automatic start so the two conditions stay visibly the same thing; they
  // drifted apart once and it cost a reboot in the owner's hands.
  // Disarm before the resume conditions are even considered. `!mediaStarted`
  // used to be part of the predicate, which meant a tap made while a speaker
  // was already playing wedged the flag true for the rest of the session and
  // left the screen saying Scanning forever. A pending scan under started media
  // is not a request to hold — it is a request that no longer means anything.
  // The teardown that never completed. Treated as a disconnect so recovery
  // owns it again; idempotent if the real DISCONNECTED turns up afterwards.
  // A callback already waiting in the atomic is not a lost callback. This runs
  // before the event is drained further down the loop, so without this clause a
  // DISCONNECTED that arrived microseconds before the deadline would be
  // announced as missing and then handled normally a few lines later — a log
  // saying the stack went silent, immediately after the stack spoke.
  if (bluetoothDisconnectingDeadlineMs != 0 && !bluetoothLinkConnected &&
      bluetoothConnectionEventPending.load(std::memory_order_acquire) == 0 &&
      static_cast<std::int32_t>(millis() - bluetoothDisconnectingDeadlineMs) >= 0) {
    bluetoothDisconnectingDeadlineMs = 0;
    bluetoothConnectInFlight = false;
    bluetoothSource.clearDialAttempt();
    // Retire the profile open this teardown was going to close. The
    // DISCONNECTED branch does it with the generation the callback carried;
    // here there is no callback, so the currently held generation is retired
    // directly. Omitted, the gate never returns to zero and every dial this
    // recovery schedules is refused by a compare-exchange that can no longer
    // succeed — recovery armed in the log, blocked in fact, until a power
    // cycle. Which is the failure the watchdog exists to prevent.
    const auto retired = bluetoothSource.retireProfileOpen();
    // Our own view of the link, because the library's cannot be trusted here
    // and must not be written from this task.
    bluetoothTerminalDeclared = true;
    Serial.printf(
        "bluetooth disconnect_lost reason=no-callback recovery=armed "
        "profile_retired=%lu\n",
        static_cast<unsigned long>(retired));
    scheduleBluetoothRecovery(millis());
  }
  if (bluetoothManualScanPending &&
      (bluetoothMediaStarted ||
       static_cast<std::int32_t>(millis() - bluetoothManualScanDeadlineMs) >= 0)) {
    bluetoothManualScanPending = false;
    Serial.printf("bluetooth scan_dropped reason=%s\n",
                  bluetoothMediaStarted ? "media-already-started" : "deadline");
    if (!bluetoothMediaStarted) {
      boardUi.setBluetoothState(open_radio::BluetoothUiState::Idle);
    }
  }
  if (bluetoothManualScanPending && !logoFetchPending &&
      !bluetoothConnectInFlight) {
    bluetoothManualScanPending = false;
    Serial.println("bluetooth scan_resumed reason=logo-fetch-complete");
    reportMemoryBudget("pre-bluetooth");
    bluetoothSource.setPairingWindow(true);
    startBluetooth(true);
    reportMemoryBudget("post-bluetooth-start");
  }
  if (bluetoothAutoStartPending && !logoFetchPending &&
      activeAudioSourceHealthy() &&
      localAudioReadyForBluetoothRetry() &&
      !bluetoothConnectInFlight && !bluetoothMediaStarted) {
    reportMemoryBudget("pre-bluetooth");
    startBluetooth(!boardUi.config().bluetoothRemembered);
    reportMemoryBudget("post-bluetooth-start");
  }
#endif
  reportLocalSpeakerStarvation();
#endif

  const auto runtime = runtimeOrchestrator.snapshot();
#if defined(OPEN_RADIO_HAS_MP3)
  boardUi.syncRuntime(
      playbackMode == PlaybackMode::Noise ? open_radio::RuntimeState::Playing
                                          : runtime.state,
      playbackMode == PlaybackMode::Noise ? outputRouter.activeOutput()
                                          : runtime.output);
#endif
  // The Core2 circles are capacitive touch zones. Act on press so they feel
  // immediate and remain reliable even while a display transfer is pending.
  const bool buttonAClicked = M5.BtnA.wasPressed();
  const bool buttonBClicked = M5.BtnB.wasPressed();
  const bool buttonCClicked = M5.BtnC.wasPressed();
  const bool hardwareButtonClicked =
      buttonAClicked || buttonBClicked || buttonCClicked;
  if (buttonAClicked) {
    boardUi.hardwareButton(open_radio::HardwareButton::A, millis());
  } else if (buttonBClicked) {
    boardUi.hardwareButton(open_radio::HardwareButton::B, millis());
  } else if (buttonCClicked) {
    boardUi.hardwareButton(open_radio::HardwareButton::C, millis());
  }
#if defined(OPEN_RADIO_LAB_CONSOLE)
  if (hardwareButtonClicked &&
      !open_radio::public_candidate::gSerialQuiet.load(std::memory_order_relaxed)) {
    Serial.printf("touch pad a=%d b=%d c=%d screen=%u t=%lu\n", buttonAClicked,
                  buttonBClicked, buttonCClicked,
                  static_cast<unsigned>(boardUi.screen()),
                  static_cast<unsigned long>(millis()));
  }
#endif
  if (!hardwareButtonClicked) {
    // Reads the latch, not the live detail: the tap being acted on may have
    // happened during the audio work or the display push, tens of milliseconds
    // before this line runs.
    const LatchedTouch touch = latchedTouch;
    latchedTouch.clear();
    if (touch.pressed) boardUi.drag(touch.x, touch.y, millis());
    const bool adjustmentReleased =
        touch.released && boardUi.release(touch.x, touch.y, millis());
    if (!adjustmentReleased && touch.clicked) {
#if defined(OPEN_RADIO_LAB_CONSOLE)
      const unsigned screenBefore = static_cast<unsigned>(boardUi.screen());
#endif
      boardUi.tap(touch.x, touch.y, millis());
#if defined(OPEN_RADIO_LAB_CONSOLE)
      if (!open_radio::public_candidate::gSerialQuiet.load(std::memory_order_relaxed)) {
        Serial.printf("touch tap x=%d y=%d screen %u->%u t=%lu\n",
                      static_cast<int>(touch.x), static_cast<int>(touch.y),
                      screenBefore, static_cast<unsigned>(boardUi.screen()),
                      static_cast<unsigned long>(millis()));
      }
#endif
    }
    if (!adjustmentReleased && touch.held) {
      Serial.printf("ui_touch hold x=%d y=%d\n", static_cast<int>(touch.x),
                    static_cast<int>(touch.y));
      boardUi.hold(touch.x, touch.y, millis());
    }
    if (!adjustmentReleased && touch.flicked) {
      boardUi.swipe(touch.baseX, touch.distanceX, touch.distanceY, millis());
    }
  }
#if defined(OPEN_RADIO_VARIANT_FULL)
  BluetoothCandidateEvent candidateEvent;
  if (bluetoothCandidateQueue != nullptr &&
      xQueueReceive(bluetoothCandidateQueue, &candidateEvent, 0) == pdTRUE) {
    activeBluetoothName = candidateEvent.name;
    activeBluetoothAddress = candidateEvent.address;
    bluetoothActiveWasRemembered =
        validBluetoothAddress(rememberedBluetoothAddress) &&
        activeBluetoothAddress == rememberedBluetoothAddress;
    bluetoothAllowNewCandidate.store(false, std::memory_order_relaxed);
    bluetoothScanActive = false;
    bluetoothConnectInFlight = true;
    bluetoothConnectStartedMs = millis();
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Found,
                              activeBluetoothName.data());
    bluetoothFoundPending = true;
    bluetoothFoundAtMs = millis();
  }
  const auto discovery = bluetoothDiscoveryPending.exchange(
      -1, std::memory_order_acq_rel);
  if (discovery != -1) {
    bluetoothDiscoveryActive = discovery == 1;
    if (bluetoothDiscoveryActive && bluetoothScanActive &&
        !bluetoothFoundPending) {
      boardUi.setBluetoothState(open_radio::BluetoothUiState::Scanning);
    }
  }

  const auto connectionEvent =
      bluetoothConnectionEventPending.exchange(0, std::memory_order_acq_rel);
  if (connectionEvent != 0) {
    // The library is talking again, so its own view of the link is the
    // authoritative one from here and the watchdog's stand-in must not
    // outlive it. Cleared before the state is examined, so the branches below
    // — which set the deadline, or retire it — start from the truth.
    bluetoothTerminalDeclared = false;
    const auto profileEvent =
        open_radio::unpackBluetoothProfileEvent(connectionEvent);
    const auto profileGeneration = profileEvent.generation;
    const auto state =
        static_cast<esp_a2d_connection_state_t>(profileEvent.state);
    {
      const auto loopNowMs = millis();
      Serial.printf(
          "bluetooth loop_evt t_ms=%lu state=%u gen=%lu handled_after_ms=%lu lost_total=%lu\n",
          static_cast<unsigned long>(loopNowMs), static_cast<unsigned>(state),
          static_cast<unsigned long>(profileGeneration),
          static_cast<unsigned long>(
              loopNowMs - bluetoothConnectionEventAtMs.load(std::memory_order_acquire)),
          static_cast<unsigned long>(
              bluetoothConnectionEventsLost.load(std::memory_order_relaxed)));
    }
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
      // A newer transition retires the old teardown. Left armed, the deadline
      // from a previous disconnect could expire in the middle of establishing
      // THIS link and clear the connection it is tracking.
      bluetoothDisconnectingDeadlineMs = 0;
      bluetoothLinkConnected = false;
      resetBluetoothMediaProbe();
      bluetoothConnectInFlight = true;
      bluetoothDirtyAttemptReported = false;
      if (bluetoothConnectStartedMs == 0) bluetoothConnectStartedMs = millis();
      useLocalBluetoothFallback();
      boardUi.setBluetoothState(open_radio::BluetoothUiState::Connecting,
                                activeBluetoothName.data());
    } else if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
      bluetoothDisconnectingDeadlineMs = 0;
      bluetoothSource.finishProfileOpen(profileGeneration);
      const auto origin = bluetoothSource.takeConnectionOrigin();
      const auto dialAgeMs = bluetoothSource.takeConnectionDialAgeMs();
      const bool accepted =
          origin == BluetoothConnectionOrigin::LocalDial ||
          origin == BluetoothConnectionOrigin::RemoteListen;
      if (!accepted) {
        Serial.printf(
            "bluetooth initiator=unknown dial_age_ms=%lu result=rejected\n",
            static_cast<unsigned long>(dialAgeMs));
        bluetoothSource.clearDialAttempt();
        bluetoothLinkConnected = false;
        resetBluetoothMediaProbe();
        bluetoothConnectInFlight = false;
        useLocalBluetoothFallback();
        boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                                  activeBluetoothName.data());
        bluetoothSource.noteLocalTeardown(
            LocalVolumeBluetoothSource::kTeardownConnectedRefused);
        bluetoothSource.disconnect();
        scheduleBluetoothRecovery(millis(), kBluetoothMediaRecoveryDelayMs);
      } else {
        std::array<std::uint8_t, ESP_BD_ADDR_LEN> adoptedAddress{};
        if (bluetoothSource.takeAdoptedPeer(adoptedAddress.data())) {
          // Inbound pairing adoption: the sink that just connected becomes
          // the active and remembered speaker, exactly as a scan-dialed
          // candidate would after a successful connect. Keep the discovery
          // candidate's name when the UI already shows one.
          activeBluetoothAddress = adoptedAddress;
          if (std::strlen(activeBluetoothName.data()) == 0) {
            std::snprintf(activeBluetoothName.data(),
                          activeBluetoothName.size(), "%s", "Glosnik");
          }
          persistActiveBluetooth();
        }
        const bool remoteListen =
            origin == BluetoothConnectionOrigin::RemoteListen;
        const auto connectedAtMs = millis();
        Serial.printf(
            "bluetooth initiator=%s dial_age_ms=%lu result=accepted\n",
            remoteListen ? "remote" : "local",
            static_cast<unsigned long>(dialAgeMs));
        if (!remoteListen && dialAgeMs != kBluetoothNoDialAgeMs) {
          reportBluetoothAttemptResult("success", dialAgeMs);
        }
        bluetoothSource.clearDialAttempt();
        bluetoothDirtyAttemptReported = false;
        bluetoothConnectStartedMs = 0;
        bluetoothLinkConnected = true;
        armBluetoothMediaProbe(connectedAtMs);
        bluetoothQueue.setConnected(true);
        bluetoothQueue.clear();
        bluetoothRetryAtMs = 0;
        bluetoothFoundPending = false;
        bluetoothScanActive = false;
        bluetoothScanAfterDisconnect = false;
        boardUi.setBluetoothState(open_radio::BluetoothUiState::Connecting,
                                  activeBluetoothName.data());
        const auto sequence =
            bluetoothCallbackSequence.fetch_add(1, std::memory_order_relaxed);
        runtimeServices.bluetoothConnection(true, 1, sequence, millis());
        // Re-assert the persisted UI volume after every link establishment.
        // Many speakers reset AVRCP absolute volume when they power-cycle.
        appliedBluetoothVolume = 0xff;
        applyBluetoothVolume();
      }
    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTING) {
      bluetoothLinkConnected = false;
      resetBluetoothMediaProbe();
      useLocalBluetoothFallback();
      // A deadline by which DISCONNECTED must arrive, because it can simply not
      // arrive. The pinned library copies every A2DP event through malloc and
      // drops it when the queue is full — the dropped event is lost and its
      // buffer leaks. Lose the DISCONNECTED that follows this, and nothing else
      // fires: the media timeout needs bluetoothLinkConnected, which this line
      // just cleared; the pull watchdog needs media playing; the dirty watchdog
      // needs an outstanding dial. bluetoothRetryAtMs stays zero and Bluetooth
      // is finished for the rest of the boot, with bluetoothConnectInFlight
      // possibly blocking new streams as well.
      //
      // Unbounded is the part that matters. Every other failure here recovers
      // on some timer; this one only ends with a power cycle, which is not a
      // thing to ship in a release meant to run for months unattended.
      bluetoothDisconnectingDeadlineMs = millis() + kBluetoothDisconnectingGraceMs;
      // Zero is this variable's "not armed", so the one millisecond in every
      // 49.7 days where the sum lands on it would disarm the watchdog at the
      // moment it was being set. Costs a branch; removes a bug that would be
      // unreproducible if it ever happened.
      if (bluetoothDisconnectingDeadlineMs == 0) bluetoothDisconnectingDeadlineMs = 1;
    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
      bluetoothDisconnectingDeadlineMs = 0;
      const bool finishedProfileOpen =
          bluetoothSource.finishProfileOpen(profileGeneration);
      if (profileGeneration != 0 && !finishedProfileOpen) {
        Serial.printf("bluetooth profile_terminal generation=%lu result=stale\n",
                      static_cast<unsigned long>(profileGeneration));
      }
      const auto disconnectedAtMs = millis();
      {
        // E5: what the library and the owner each knew about this teardown —
        // the fields attempt_result=dirty never carried. disc_rsn from the
        // A2DP callback, the last local disconnect() and its age, the ACL age.
        const auto teardownAge = bluetoothSource.localTeardownAgeMs(disconnectedAtMs);
        const auto aclAge = bluetoothSource.aclAgeMs(disconnectedAtMs);
        Serial.printf(
            "bluetooth attempt_context disc_rsn=%u local_reason=%u "
            "local_teardown_age_ms=%ld acl_age_ms=%ld dial_outstanding=%d\n",
            static_cast<unsigned>(bluetoothSource.takeLastDiscRsn()),
            static_cast<unsigned>(bluetoothSource.localTeardownReason()),
            teardownAge == kBluetoothNoDialAgeMs ? -1L : static_cast<long>(teardownAge),
            aclAge == kBluetoothNoDialAgeMs ? -1L : static_cast<long>(aclAge),
            bluetoothSource.dialAttemptOutstanding() ? 1 : 0);
      }
      if (bluetoothSource.dialAttemptOutstanding() &&
          bluetoothConnectStartedMs != 0) {
        auto durationMs = bluetoothConnectionEventDialAgeMs.exchange(
            kBluetoothNoDialAgeMs, std::memory_order_acq_rel);
        if (durationMs == kBluetoothNoDialAgeMs) {
          durationMs = bluetoothSource.dialAgeMs(disconnectedAtMs);
        }
        const bool cleanTimeout = cleanBluetoothAttemptTimeout(durationMs);
        reportBluetoothAttemptResult(cleanTimeout ? "clean_timeout" : "dirty",
                                     durationMs);
      }
      bluetoothSource.clearDialAttempt();
      bluetoothDirtyAttemptReported = false;
      bluetoothLinkConnected = false;
      resetBluetoothMediaProbe();
      bluetoothConnectInFlight = false;
      bluetoothConnectStartedMs = 0;
      useLocalBluetoothFallback();
      const auto sequence =
          bluetoothCallbackSequence.fetch_add(1, std::memory_order_relaxed);
      runtimeServices.bluetoothConnection(false, 1, sequence, millis());
      if (bluetoothScanAfterDisconnect) {
        bluetoothSource.setReconnectPeer(activeBluetoothAddress, false);
        bluetoothSource.applyReconnectListenPolicy();
        bluetoothScanAfterDisconnect = false;
        if (!beginBluetoothDiscovery(millis())) {
          bluetoothScanActive = false;
          boardUi.setBluetoothState(open_radio::BluetoothUiState::Error);
        }
      } else if (!bluetoothScanActive) {
        bluetoothSource.setReconnectPeer(activeBluetoothAddress,
                                         bluetoothActiveWasRemembered);
        bluetoothSource.applyReconnectListenPolicy();
        boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                                  activeBluetoothName.data());
        // Anchor the retry to the actual A2DP DISCONNECTED callback. A dirty
        // abort can take much longer than the nominal retry delay to unwind;
        // preserving its already-expired timer starts a new profile dial on
        // stale HCI state and can tear down an otherwise successful link.
        bluetoothRetryAtMs = 0;
        scheduleBluetoothRecovery(millis());
      }
    }
  }

  const auto audioState =
      bluetoothAudioStatePending.exchange(-1, std::memory_order_acq_rel);
  if (audioState != -1) {
    const auto state = static_cast<esp_a2d_audio_state_t>(audioState);
    const bool mediaStartAdmitted = bluetoothSource.mediaStartAllowed();
    if (state == ESP_A2D_AUDIO_STATE_STARTED && bluetoothLinkConnected &&
        mediaStartAdmitted) {
      bluetoothQueue.setConnected(true);
      localSpeaker.stopPlayback();
      outputRouter.preferBluetooth(true);
      bluetoothMediaStarted = true;
      bluetoothMediaStartedAtMs = millis();
      bluetoothLastPullMs.store(bluetoothMediaStartedAtMs,
                                std::memory_order_relaxed);
      bluetoothConnectInFlight = false;
      bluetoothConnectStartedMs = 0;
      bluetoothRetryAtMs = 0;
      bluetoothMediaProbeAtMs = 0;
      bluetoothMediaProbeAttempts = 0;
      ++bluetoothMediaStarts;
      if (bluetoothRecoveryStartedMs != 0) {
        Serial.printf(
            "bluetooth recovery_complete_ms=%lu attempts=%u\n",
            static_cast<unsigned long>(bluetoothMediaStartedAtMs -
                                       bluetoothRecoveryStartedMs),
            static_cast<unsigned>(bluetoothRecoveryAttemptCount));
        bluetoothRecoveryStartedMs = 0;
        bluetoothRecoveryAttemptCount = 0;
      }
      persistActiveBluetooth();
      appliedBluetoothVolume = 0xff;
      applyBluetoothVolume();
      boardUi.setBluetoothState(open_radio::BluetoothUiState::Connected,
                                activeBluetoothName.data());
      Serial.printf("bluetooth media_started=%lu buffered=%u\n",
                    static_cast<unsigned long>(bluetoothMediaStarts),
                    static_cast<unsigned>(bluetoothQueue.bufferedFrames()));
    } else if (state == ESP_A2D_AUDIO_STATE_STARTED) {
      // A dependency callback must not bypass the pre-media reserve gate. A
      // sink that starts unexpectedly stays on local output and gets one
      // supervisor-owned retry instead of consuming an unprepared BT queue.
      Serial.println("bluetooth media_start_rejected reason=standby_not_ready");
      bluetoothLinkConnected = false;
      resetBluetoothMediaProbe();
      bluetoothConnectInFlight = false;
      useLocalBluetoothFallback();
      boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                                activeBluetoothName.data());
      const auto connectionState = bluetoothSource.get_connection_state();
      if (connectionState != ESP_A2D_CONNECTION_STATE_DISCONNECTED &&
          connectionState != ESP_A2D_CONNECTION_STATE_DISCONNECTING) {
        bluetoothSource.noteLocalTeardown(
            LocalVolumeBluetoothSource::kTeardownMediaState);
        bluetoothSource.disconnect();
      }
      scheduleBluetoothRecovery(millis(), kBluetoothMediaRecoveryDelayMs);
    } else {
      const bool wasMediaStarted = bluetoothMediaStarted;
      useLocalBluetoothFallback();
      if (bluetoothLinkConnected && wasMediaStarted) {
        armBluetoothMediaProbe(millis());
        bluetoothConnectInFlight = true;
        bluetoothConnectStartedMs = millis();
        boardUi.setBluetoothState(open_radio::BluetoothUiState::Connecting,
                                  activeBluetoothName.data());
      }
    }
  }

  if (bluetoothFoundPending && millis() - bluetoothFoundAtMs >= 150U) {
    bluetoothFoundPending = false;
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Connecting,
                              activeBluetoothName.data());
  }
  if (bluetoothScanActive &&
      millis() - bluetoothScanStartedMs >= kBluetoothScanTimeoutMs) {
    if (bluetoothDiscoveryActive) bluetoothSource.cancel_discovery();
    bluetoothScanActive = false;
    bluetoothDiscoveryActive = false;
    bluetoothScanAfterDisconnect = false;
    bluetoothAllowNewCandidate.store(false, std::memory_order_relaxed);
    bluetoothSource.cancelProfileOpenAuthorization();
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                              activeBluetoothName.data());
    if (loadRememberedBluetooth()) {
      activeBluetoothName = rememberedBluetoothName;
      activeBluetoothAddress = rememberedBluetoothAddress;
      bluetoothActiveWasRemembered = true;
      scheduleBluetoothRecovery(millis());
    } else {
      boardUi.forgetBluetooth();
      scheduleBluetoothDiscoveryRetry(millis());
    }
  }
  if (bluetoothMediaStarted &&
      millis() - bluetoothLastPullMs.load(std::memory_order_relaxed) >=
          kBluetoothPullWatchdogMs) {
    ++bluetoothPullWatchdogTrips;
    Serial.printf("bluetooth pull_watchdog=%lu media_age_ms=%lu\n",
                  static_cast<unsigned long>(bluetoothPullWatchdogTrips),
                  static_cast<unsigned long>(millis() -
                                             bluetoothMediaStartedAtMs));
    bluetoothLinkConnected = false;
    resetBluetoothMediaProbe();
    bluetoothConnectInFlight = false;
    useLocalBluetoothFallback();
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                              activeBluetoothName.data());
    const auto state = bluetoothSource.get_connection_state();
    if (state != ESP_A2D_CONNECTION_STATE_DISCONNECTED &&
        state != ESP_A2D_CONNECTION_STATE_DISCONNECTING) {
      bluetoothSource.noteLocalTeardown(
          LocalVolumeBluetoothSource::kTeardownProbeFailed);
      bluetoothSource.disconnect();
    }
    scheduleBluetoothRecovery(millis());
  }
  const auto bluetoothNowMs = millis();
  if (bluetoothLinkConnected && !bluetoothMediaStarted &&
      bluetoothMediaStartAllowedAtMs == 0 && bluetoothStandbyReady()) {
    // The heap check moved AHEAD of the flag. allowBluetoothMediaStart() calls
    // setMediaStartAllowed(true), and from that moment the library's own
    // heartbeat can start media without passing the deferral below — so the
    // check that followed it was guarding a door that was already open. Same
    // thresholds as the deferral, deliberately: this is a reordering, not a new
    // policy, and inventing a second number would make the two disagree later.
    const std::size_t freeInternal =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const std::size_t largestInternal =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // DMA is checked, not merely printed. Passing the INTERNAL test proves
    // nothing about a contiguous DMA-capable block, and DMA is the class that
    // was actually being refused on 2026-08-01 — twelve 640-byte requests and
    // eleven of 1856, all caps=0x8, while the internal figure still read 1140.
    // A gate that measures the wrong pool is the shape of the bug it is here
    // to stop.
    const std::size_t largestDma =
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    if (freeInternal < 12288 || largestInternal < 4096 || largestDma < 2048) {
      // Once a second, not once per loop turn. This condition holds for
      // seconds while the loop runs hundreds of times a second, so an
      // unthrottled print flooded the port and cost CPU exactly when the
      // device was short of resources. A flooded log is worse than a silent
      // one: it has to be sieved before anything next to it can be seen.
      if (static_cast<std::int32_t>(bluetoothNowMs - nextStandbyWithheldLogMs) >= 0) {
        nextStandbyWithheldLogMs = bluetoothNowMs + 1000U;
        Serial.printf(
          "bluetooth standby_withheld reason=memory free=%u largest=%u "
          "dma_largest=%u\n",
          static_cast<unsigned>(freeInternal),
          static_cast<unsigned>(largestInternal),
          static_cast<unsigned>(largestDma));
      }
    } else {
      allowBluetoothMediaStart(bluetoothNowMs);
    }
  }
  if (bluetoothLinkConnected && !bluetoothMediaStarted &&
      bluetoothMediaProbeAtMs != 0 &&
      static_cast<std::int32_t>(bluetoothNowMs - bluetoothMediaProbeAtMs) >= 0) {
    // Starting media makes Bluedroid allocate — a media task, AVDTP state, an
    // osi hash-map entry — and osi_malloc failure is not an error return, it
    // is assert -> SW_CPU_RESET. Caught on hardware 2026-07-30 13:39:26:
    // media_started=1, then "assert failed: hash_map_set hash_map.c:129
    // (data != NULL)" with largest=1588. A probe deferred while the heap is
    // that thin costs a retry interval; the assert costs the whole radio.
    const std::size_t freeInternal =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const std::size_t largestInternal =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // DMA here too. The admission above measures all three and then sets a
    // latch that is never re-examined, so every probe after the first was
    // asking a narrower question than the one that let it through — and DMA is
    // the pool that was actually being refused when this crashed. Adding it
    // invents nothing: it is the same 2048 the admission already uses, applied
    // at the moment of the request rather than at the moment of permission.
    const std::size_t largestDma =
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    if (freeInternal < 12288 || largestInternal < 4096 || largestDma < 2048) {
      // Deferred, not counted: the probe limit is for a sink that will not
      // answer, and a heap that cannot host the answer is a different problem.
      Serial.printf(
          "bluetooth media_deferred reason=memory free=%u largest=%u "
          "dma_largest=%u\n",
          static_cast<unsigned>(freeInternal),
          static_cast<unsigned>(largestInternal),
          static_cast<unsigned>(largestDma));
      bluetoothMediaProbeAtMs = bluetoothNowMs + kBluetoothMediaProbeIntervalMs;
    } else {
      const int result = bluetoothSource.requestMediaStartIfIdle();
      if (result != 0) {
        ++bluetoothMediaProbeAttempts;
        Serial.printf("bluetooth media_probe=%u result=%s link_age_ms=%lu\n",
                      static_cast<unsigned>(bluetoothMediaProbeAttempts),
                      result > 0 ? "sent" : "error",
                      static_cast<unsigned long>(bluetoothNowMs -
                                                 bluetoothLinkConnectedAtMs));
      }
      bluetoothMediaProbeAtMs =
          bluetoothMediaProbeAttempts >= kBluetoothMediaProbeLimit
              ? 0
              : bluetoothNowMs + kBluetoothMediaProbeIntervalMs;
    }
  }
  const bool bluetoothStandbyPrefillTimeout =
      bluetoothLinkConnected && bluetoothLinkConnectedAtMs != 0 &&
      bluetoothMediaStartAllowedAtMs == 0 &&
      bluetoothNowMs - bluetoothLinkConnectedAtMs >=
          kBluetoothStandbyPrefillTimeoutMs;
  const bool bluetoothMediaStartTimeout =
      bluetoothLinkConnected && bluetoothMediaStartAllowedAtMs != 0 &&
      bluetoothNowMs - bluetoothMediaStartAllowedAtMs >=
          kBluetoothMediaStartTimeoutMs;
  const bool bluetoothDirtyAttempt =
      bluetoothConnectInFlight && !bluetoothLinkConnected &&
      !bluetoothDirtyAttemptReported &&
      bluetoothSource.dialAttemptOutstanding() &&
      bluetoothConnectStartedMs != 0 &&
      bluetoothSource.dialAgeMs(bluetoothNowMs) >
          kBluetoothDirtyAttemptAbortMs;
  if (bluetoothDirtyAttempt) {
    bluetoothDirtyAttemptReported = true;
    bluetoothDirtyAwaitSinceMs = bluetoothNowMs;
    useLocalBluetoothFallback();
    Serial.printf(
        "bluetooth dirty_watchdog elapsed_ms=%lu action=await_stack\n",
        static_cast<unsigned long>(
            bluetoothSource.dialAgeMs(bluetoothNowMs)));
  }
  // await_stack must not be forever: a wedged callback left connectInFlight
  // set, which silently disabled BOTH stream reopens and the stream stall
  // watchdog (frozen playback, 2026-07-17). Thirty seconds without the stack
  // resolving the dirty attempt reboots the complete controller. A soft retry
  // cannot safely release the profile generation because a delayed terminal
  // callback could otherwise clear the next attempt.
  if (bluetoothDirtyAttemptReported && bluetoothDirtyAwaitSinceMs != 0 &&
      bluetoothNowMs - bluetoothDirtyAwaitSinceMs >= 30000U &&
      bluetoothConnectInFlight && !bluetoothLinkConnected) {
    Serial.println("bluetooth dirty_watchdog action=reboot");
    Serial.flush();
    delay(20);
    ESP.restart();
  } else if (bluetoothConnectInFlight && !bluetoothMediaStarted &&
             (bluetoothStandbyPrefillTimeout || bluetoothMediaStartTimeout)) {
    const char* timeoutPhase = bluetoothStandbyPrefillTimeout
                                   ? "buffer"
                                   : "media";
    const std::uint32_t timeoutElapsedMs =
        bluetoothStandbyPrefillTimeout
            ? bluetoothNowMs - bluetoothLinkConnectedAtMs
            : bluetoothNowMs - bluetoothMediaStartAllowedAtMs;
    Serial.printf(
        "bluetooth timeout_phase=%s elapsed_ms=%lu bt_buffered=%u "
        "decoded_frames=%u local_blocks=%u\n",
        timeoutPhase, static_cast<unsigned long>(timeoutElapsedMs),
        static_cast<unsigned>(bluetoothQueue.bufferedFrames()),
        static_cast<unsigned>(decodedFrames.size()),
        static_cast<unsigned>(localSpeaker.bufferedBlocks()));
    bluetoothConnectInFlight = false;
    bluetoothLinkConnected = false;
    resetBluetoothMediaProbe();
    useLocalBluetoothFallback();
    boardUi.setBluetoothState(open_radio::BluetoothUiState::Error,
                              activeBluetoothName.data());
    const auto state = bluetoothSource.get_connection_state();
    if (state != ESP_A2D_CONNECTION_STATE_DISCONNECTED &&
        state != ESP_A2D_CONNECTION_STATE_DISCONNECTING) {
      bluetoothSource.noteLocalTeardown(
          LocalVolumeBluetoothSource::kTeardownTimeoutPhase);
      bluetoothSource.disconnect();
    }
    scheduleBluetoothRecovery(
        millis(),
        bluetoothMediaStartTimeout ? kBluetoothMediaRecoveryDelayMs : 0);
  }
  if (bluetoothSource.takeInboundProfileDialRequest() &&
      boardUi.config().bluetoothRemembered) {
    // E6: the state the scheduled dial lands in. The reverted 2026-08-02 fix
    // targeted exactly this hand-off and missed, because a retry armed by the
    // disconnect 8-20 s earlier was still live; these fields make that
    // visible before anyone writes the next fix.
    const auto scheduledAtMs = millis();
    const long retryWasArmedInMs =
        bluetoothRetryAtMs == 0
            ? -1L
            : static_cast<long>(static_cast<std::int32_t>(bluetoothRetryAtMs - scheduledAtMs));
    const long discDeadlineInMs =
        bluetoothDisconnectingDeadlineMs == 0
            ? -1L
            : static_cast<long>(static_cast<std::int32_t>(
                  bluetoothDisconnectingDeadlineMs - scheduledAtMs));
    Serial.printf(
        "bluetooth inbound_acl action=profile_dial_scheduled retry_was_armed_in_ms=%ld "
        "dial_outstanding=%d connect_in_flight=%d link_connected=%d disc_deadline_in_ms=%ld\n",
        retryWasArmedInMs, bluetoothSource.dialAttemptOutstanding() ? 1 : 0,
        bluetoothConnectInFlight ? 1 : 0, bluetoothLinkConnected ? 1 : 0,
        discDeadlineInMs);
    bluetoothRetryAtMs = millis();
  }
  if (bluetoothDiscoveryRetryAtMs != 0 &&
      static_cast<std::int32_t>(millis() - bluetoothDiscoveryRetryAtMs) >= 0 &&
      activeAudioSourceHealthy() && !boardUi.config().bluetoothRemembered) {
    const auto retryNowMs = millis();
    if (!localAudioReadyForBluetoothRetry()) {
      bluetoothDiscoveryRetryAtMs =
          retryNowMs + kBluetoothRetryAudioPollMs;
    } else if (!bluetoothStackStarted ||
               (!bluetoothDiscoveryActive && !bluetoothConnectInFlight &&
                bluetoothLinkIsDown())) {
      bluetoothDiscoveryRetryAtMs = 0;
      startBluetooth(true);
    } else {
      bluetoothDiscoveryRetryAtMs = retryNowMs + 1000U;
    }
  }
  if (bluetoothRetryAtMs != 0 &&
      static_cast<std::int32_t>(millis() - bluetoothRetryAtMs) >= 0 &&
      activeAudioSourceHealthy() && boardUi.config().bluetoothRemembered) {
    const auto retryNowMs = millis();
    if (!localAudioReadyForBluetoothRetry()) {
      deferBluetoothRetryForAudio(retryNowMs);
    } else if (!bluetoothStackStarted) {
      ++bluetoothRetryAttempts;
      startBluetooth(false);
    } else if (!bluetoothDiscoveryActive && bluetoothLinkIsDown()) {
      bluetoothRetryAtMs = 0;
      ++bluetoothRetryAttempts;
      if (!connectRememberedBluetooth(retryNowMs)) {
        scheduleBluetoothRetry(retryNowMs);
      }
    } else {
      // A callback transition is still completing. Recheck shortly without
      // consuming or exponentially increasing the retry budget.
      bluetoothRetryAtMs = millis() + 1000U;
    }
  }
#endif
  const std::uint32_t uiTickStartedUs = micros();
#if defined(OPEN_RADIO_HAS_MP3)
  const bool animationAudioSafe =
      outputRouter.activeOutput() == open_radio::OutputKind::Bluetooth
          ? decodedFrames.size() >=
                open_radio::public_candidate::kLocalSpeakerFramesPerBlock
          : localSpeaker.bufferedBlocks() == 2 &&
                decodedFrames.size() >=
                    open_radio::public_candidate::kLocalSpeakerFramesPerBlock;
  boardUi.tick(millis(), animationAudioSafe);
#else
  boardUi.tick(millis(), true);
#endif
  maxUiTickUs = std::max(maxUiTickUs, micros() - uiTickStartedUs);
  sampleTouch();
  const std::uint32_t uiFlushStartedUs = micros();
  flushUi();
  sampleTouch();
  maxUiFlushUs = std::max(maxUiFlushUs, micros() - uiFlushStartedUs);
  const std::uint32_t loopDurationUs = micros() - loopStartedUs;
  if (loopDurationUs >= 50000U) {
    maxLoopStallUs = std::max(maxLoopStallUs, loopDurationUs);
  }
  if (maxLoopStallUs != 0 &&
      static_cast<std::int32_t>(loopNowMs - nextLoopStallReportMs) >= 0) {
#if defined(OPEN_RADIO_HAS_MP3)
    Serial.printf("loop_stall max_ms=%lu audio_ms=%lu drain_ms=%lu "
                  "ui_tick_ms=%lu ui_flush_ms=%lu decoded=%u running=%s\n",
                  static_cast<unsigned long>(maxLoopStallUs / 1000U),
                  static_cast<unsigned long>(maxAudioLoopUs / 1000U),
                  static_cast<unsigned long>(maxAudioDrainUs / 1000U),
                  static_cast<unsigned long>(maxUiTickUs / 1000U),
                  static_cast<unsigned long>(maxUiFlushUs / 1000U),
                  static_cast<unsigned>(decodedFrames.size()),
                  playbackMode == PlaybackMode::Noise
                      ? localNoiseSource.active() ? "noise" : "paused"
                      : stationRuntime.running() ? "yes" : "no");
#else
    Serial.printf("loop_stall max_ms=%lu\n",
                  static_cast<unsigned long>(maxLoopStallUs / 1000U));
#endif
    maxLoopStallUs = 0;
    maxAudioLoopUs = 0;
    maxAudioDrainUs = 0;
    maxUiTickUs = 0;
    maxUiFlushUs = 0;
    nextLoopStallReportMs = loopNowMs + 5000U;
  }
  delay(1);
}
