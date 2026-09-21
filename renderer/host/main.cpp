#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "fixture_now_playing.hpp"
#include "open_radio/renderer.hpp"
#include "ui_contract.hpp"

// The host harness — not the renderer library — reaches into the firmware for
// the CO2 band, so the sourced thresholds exist once. On the device the same
// function fills the same field; here it stands in for the sensor task. The
// renderer itself still depends on nothing outside renderer/.
#include "open_radio/alert_engine.hpp"
#include "open_radio/air_assessment.hpp"

namespace {

std::uint8_t expand5(std::uint16_t value) {
  return static_cast<std::uint8_t>((value * 255U + 15U) / 31U);
}

std::uint8_t expand6(std::uint16_t value) {
  return static_cast<std::uint8_t>((value * 255U + 31U) / 63U);
}

bool writePpm(const std::string& path, const std::vector<std::uint16_t>& pixels) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  output << "P6\n" << open_radio::ui::generated::kWidth << ' '
         << open_radio::ui::generated::kHeight << "\n255\n";
  for (const auto pixel : pixels) {
    const char rgb[] = {
        static_cast<char>(expand5((pixel >> 11U) & 0x1fU)),
        static_cast<char>(expand6((pixel >> 5U) & 0x3fU)),
        static_cast<char>(expand5(pixel & 0x1fU)),
    };
    output.write(rgb, sizeof(rgb));
  }
  return output.good();
}

bool parseScreen(const std::string& value, open_radio::ui::ScreenKind& screen) {
  using open_radio::ui::ScreenKind;
  if (value == "now-playing-editorial") screen = ScreenKind::now_playing_editorial;
  else if (value == "now-playing-glance") screen = ScreenKind::now_playing_glance;
  else if (value == "screensaver-pulse") screen = ScreenKind::screensaver_pulse;
  else if (value == "screensaver-bars") screen = ScreenKind::screensaver_bars;
  else if (value == "screensaver-orbit") screen = ScreenKind::screensaver_orbit;
  else if (value == "screensaver-cat") screen = ScreenKind::screensaver_cat;
  else if (value == "display-off") screen = ScreenKind::display_off;
  else if (value == "stations") screen = ScreenKind::stations;
  else if (value == "volume-control") screen = ScreenKind::volume_control;
  else if (value == "brightness-control") screen = ScreenKind::brightness_control;
  else if (value == "settings-quick") screen = ScreenKind::settings_quick;
  else if (value == "settings-system") screen = ScreenKind::settings_system;
  else if (value == "settings-display") screen = ScreenKind::settings_display;
  else if (value == "noise") screen = ScreenKind::noise;
  else if (value == "wifi-status") screen = ScreenKind::wifi_status;
  else if (value == "wifi-recovery") screen = ScreenKind::wifi_recovery;
  else if (value == "bluetooth-fallback") screen = ScreenKind::bluetooth_fallback;
  else if (value == "unsupported") screen = ScreenKind::unsupported;
  else if (value == "safe-mode") screen = ScreenKind::safe_mode;
  else if (value == "onboarding-wifi") screen = ScreenKind::onboarding_wifi;
  else if (value == "onboarding-first-sound") screen = ScreenKind::onboarding_first_sound;
  else if (value == "onboarding-bluetooth") screen = ScreenKind::onboarding_bluetooth;
  else if (value == "bluetooth-pairing") screen = ScreenKind::bluetooth_pairing;
  else if (value == "diagnostics") screen = ScreenKind::diagnostics;
  else if (value == "about") screen = ScreenKind::about;
  else if (value == "market") screen = ScreenKind::market;
  else if (value == "sensors") screen = ScreenKind::sensors;
  else return false;
  return true;
}

}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: open-radio-render <output.ppm> [--screen id] [--theme dark|light] [--locale pl|en] [--variant editorial|glance] [--clock YYYY-MM-DDTHH:MM] [--station 0..8] [--volume 0..100] [--brightness 0..100] [--output local|bluetooth] [--degraded false|true] [--noise-color white|pink|brown] [--noise-playing false|true] [--track text] [--setup-code text] [--wifi-online false|true] [--wifi-strength 0..100] [--wifi-portal false|true] [--address dotted-quad] [--bluetooth-state idle|scanning|found|connecting|connected|error] [--bluetooth-candidate text] [--bluetooth-remembered false|true] [--battery-level 0..100] [--battery-mv 0..5000] [--battery-ma -2000..2000] [--battery-runtime-min 0..5995] [--battery-external false|true] [--frame 0..255] [--co2 ppm] [--temperature-centi] [--humidity-centi] [--pressure-centi] [--gas-ohm] [--pressure-change centi-hPa/3h] [--gas-departure percent] [--sensor-age s] [--sensor-status 0..4]\n";
    return 2;
  }
  auto snapshot = open_radio::ui::generated::kNowPlayingFixture;
  snapshot.firmware_version = "0.4";
  snapshot.now_playing_title = "Męskie Granie Orkiestra — Supermoce";
  snapshot.metadata_available = true;
  for (int index = 2; index + 1 < argc; index += 2) {
    const std::string option = argv[index];
    const std::string value = argv[index + 1];
    if (option == "--theme") {
      if (value == "dark") snapshot.theme = open_radio::ui::ThemeMode::dark;
      else if (value == "light") snapshot.theme = open_radio::ui::ThemeMode::light;
      else return 2;
    } else if (option == "--variant") {
      if (value == "editorial") {
        snapshot.variant = open_radio::ui::HomeVariant::editorial;
        snapshot.screen = open_radio::ui::ScreenKind::now_playing_editorial;
      } else if (value == "glance") {
        snapshot.variant = open_radio::ui::HomeVariant::glance;
        snapshot.screen = open_radio::ui::ScreenKind::now_playing_glance;
      } else if (value == "air") {
        snapshot.variant = open_radio::ui::HomeVariant::air;
        snapshot.screen = open_radio::ui::ScreenKind::now_playing_air;
      }
      else return 2;
    } else if (option == "--co2") {
      snapshot.sensor_co2_ppm = std::atoi(value.c_str());
      snapshot.sensor_status[0] = 2;
      snapshot.sensor_air_band =
          static_cast<std::uint8_t>(open_radio::co2Band(snapshot.sensor_co2_ppm));
    } else if (option == "--temperature-centi") {
      snapshot.sensor_temperature_centi_celsius = std::atoi(value.c_str());
    } else if (option == "--humidity-centi") {
      snapshot.sensor_humidity_centi_percent = std::atoi(value.c_str());
    } else if (option == "--pressure-centi") {
      snapshot.sensor_pressure_centi_hectopascal = std::atoi(value.c_str());
      snapshot.sensor_status[1] = 2;
    } else if (option == "--gas-ohm") {
      snapshot.sensor_gas_resistance_ohm = std::atoi(value.c_str());
      snapshot.sensor_status[1] = 2;
    } else if (option == "--pressure-change") {
      snapshot.sensor_pressure_change_centi_hpa_3h = std::atoi(value.c_str());
    } else if (option == "--gas-departure") {
      snapshot.sensor_gas_departure_percent = std::atoi(value.c_str());
    } else if (option == "--gas-warmup") {
      snapshot.sensor_gas_warmup_minutes = std::atoi(value.c_str());
    } else if (option == "--dew-point-centi") {
      snapshot.sensor_dew_point_centi_celsius = std::atoi(value.c_str());
    } else if (option == "--co2-rate") {
      snapshot.sensor_co2_rate_ppm_per_hour = std::atoi(value.c_str());
    } else if (option == "--co2-minutes") {
      snapshot.sensor_co2_minutes_to_next = std::atoi(value.c_str());
    } else if (option == "--co2-ceiling") {
      snapshot.sensor_co2_ceiling_ppm = std::atoi(value.c_str());
    } else if (option == "--co2-next") {
      snapshot.sensor_co2_next_threshold_ppm = std::atoi(value.c_str());
    } else if (option == "--sensor-age") {
      snapshot.sensor_age_seconds = static_cast<std::uint16_t>(std::atoi(value.c_str()));
    } else if (option == "--sensor-status") {
      // 0 absent, 1 warming, 2 ok, 3 fault, 4 quarantined
      snapshot.sensor_status[0] = static_cast<std::uint8_t>(std::atoi(value.c_str()));
    } else if (option == "--screen") {
      if (!parseScreen(value, snapshot.screen)) return 2;
    } else if (option == "--locale") {
      if (value == "pl") snapshot.locale = open_radio::ui::LocaleMode::pl;
      else if (value == "en") snapshot.locale = open_radio::ui::LocaleMode::en;
      else return 2;
    } else if (option == "--clock") {
      unsigned year = 0;
      unsigned month = 0;
      unsigned day = 0;
      unsigned hour = 0;
      unsigned minute = 0;
      if (std::sscanf(value.c_str(), "%u-%u-%uT%u:%u", &year, &month, &day,
                      &hour, &minute) != 5 || year < 2024 || year > 2099 ||
          month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
          minute > 59) {
        return 2;
      }
      snapshot.clock_valid = true;
      snapshot.clock_year = static_cast<std::uint16_t>(year);
      snapshot.clock_month = static_cast<std::uint8_t>(month);
      snapshot.clock_day = static_cast<std::uint8_t>(day);
      snapshot.clock_hour = static_cast<std::uint8_t>(hour);
      snapshot.clock_minute = static_cast<std::uint8_t>(minute);
    } else if (option == "--station") {
      const auto station = std::strtoul(value.c_str(), nullptr, 10);
      if (station >= open_radio::ui::generated::kStations.size()) return 2;
      snapshot.station_index = station;
      snapshot.requested_station_index = station;
    } else if (option == "--volume") {
      const auto volume = std::strtoul(value.c_str(), nullptr, 10);
      if (volume > 100) return 2;
      snapshot.volume = static_cast<std::uint8_t>(volume);
    } else if (option == "--brightness") {
      const auto brightness = std::strtoul(value.c_str(), nullptr, 10);
      if (brightness > 100) return 2;
      snapshot.brightness = static_cast<std::uint8_t>(brightness);
    } else if (option == "--output") {
      if (value == "local") snapshot.output = open_radio::ui::AudioOutput::local;
      else if (value == "bluetooth") snapshot.output = open_radio::ui::AudioOutput::bluetooth;
      else return 2;
    } else if (option == "--degraded") {
      if (value == "false") snapshot.degraded = false;
      else if (value == "true") snapshot.degraded = true;
      else return 2;
    } else if (option == "--noise-color") {
      if (value == "white") snapshot.noise_color = 0;
      else if (value == "pink") snapshot.noise_color = 1;
      else if (value == "brown") snapshot.noise_color = 2;
      else return 2;
    } else if (option == "--noise-playing") {
      if (value == "false") snapshot.noise_playing = false;
      else if (value == "true") snapshot.noise_playing = true;
      else return 2;
    } else if (option == "--track") {
      snapshot.now_playing_title = argv[index + 1];
      snapshot.metadata_available = !value.empty();
    } else if (option == "--setup-code") {
      snapshot.setup_access_code = argv[index + 1];
    } else if (option == "--wifi-online") {
      if (value == "false") snapshot.wifi_online = false;
      else if (value == "true") snapshot.wifi_online = true;
      else return 2;
    } else if (option == "--wifi-strength") {
      const auto strength = std::strtoul(value.c_str(), nullptr, 10);
      if (strength > 100) return 2;
      snapshot.wifi_strength_percent = static_cast<std::uint8_t>(strength);
    } else if (option == "--wifi-portal") {
      if (value == "false") snapshot.wifi_portal_active = false;
      else if (value == "true") snapshot.wifi_portal_active = true;
      else return 2;
    } else if (option == "--address") {
      snapshot.device_address = argv[index + 1];
    } else if (option == "--console-active") {
      if (value == "false") snapshot.console_active = false;
      else if (value == "true") snapshot.console_active = true;
      else return 2;
    } else if (option == "--bluetooth-state") {
      using open_radio::ui::BluetoothState;
      if (value == "idle") snapshot.bluetooth_state = BluetoothState::idle;
      else if (value == "scanning") snapshot.bluetooth_state = BluetoothState::scanning;
      else if (value == "found") snapshot.bluetooth_state = BluetoothState::found;
      else if (value == "connecting") snapshot.bluetooth_state = BluetoothState::connecting;
      else if (value == "connected") snapshot.bluetooth_state = BluetoothState::connected;
      else if (value == "error") snapshot.bluetooth_state = BluetoothState::error;
      else return 2;
    } else if (option == "--bluetooth-candidate") {
      snapshot.bluetooth_candidate_name = argv[index + 1];
    } else if (option == "--bluetooth-remembered") {
      if (value == "false") snapshot.bluetooth_remembered = false;
      else if (value == "true") snapshot.bluetooth_remembered = true;
      else return 2;
    } else if (option == "--battery-level") {
      const auto level = std::strtoul(value.c_str(), nullptr, 10);
      if (level > 100) return 2;
      snapshot.battery_available = true;
      snapshot.battery_level_percent = static_cast<std::uint8_t>(level);
    } else if (option == "--battery-mv") {
      const auto millivolts = std::strtoul(value.c_str(), nullptr, 10);
      if (millivolts > 5000) return 2;
      snapshot.battery_voltage_millivolts =
          static_cast<std::uint16_t>(millivolts);
    } else if (option == "--battery-ma") {
      const auto milliamps = std::strtol(value.c_str(), nullptr, 10);
      if (milliamps < -2000 || milliamps > 2000) return 2;
      snapshot.battery_current_milliamps =
          static_cast<std::int16_t>(milliamps);
    } else if (option == "--battery-runtime-min") {
      const auto minutes = std::strtoul(value.c_str(), nullptr, 10);
      if (minutes > 5995) return 2;
      snapshot.battery_runtime_valid = minutes != 0;
      snapshot.battery_runtime_minutes = static_cast<std::uint16_t>(minutes);
    } else if (option == "--battery-external") {
      if (value == "false") snapshot.battery_external_power = false;
      else if (value == "true") snapshot.battery_external_power = true;
      else return 2;
    } else if (option == "--frame") {
      const auto frame = std::strtoul(value.c_str(), nullptr, 10);
      if (frame > 255) return 2;
      snapshot.animation_frame = static_cast<std::uint8_t>(frame);
    } else if (option == "--saver-mode") {
      const auto mode = std::strtoul(value.c_str(), nullptr, 10);
      if (mode > 3) return 2;
      snapshot.screensaver_mode = static_cast<std::uint8_t>(mode);
    } else if (option == "--confirm-delete") {
      if (value == "false") snapshot.confirm_delete = false;
      else if (value == "true") snapshot.confirm_delete = true;
      else return 2;
    } else {
      return 2;
    }
  }
  // The alert is decided by the REAL engine, not hand-fed as a string. A test
  // harness that let a caller type any sentence onto the screen would be able
  // to render a picture the device cannot produce, and every audit downstream
  // would be measuring that picture.
  {
    open_radio::AlertEngine engine;
    open_radio::AlertInputs inputs;
    inputs.co2Ppm = snapshot.sensor_co2_ppm;
    inputs.co2CeilingPpm = snapshot.sensor_co2_ceiling_ppm;
    inputs.co2MinutesToNext = snapshot.sensor_co2_minutes_to_next;
    inputs.temperatureCentiCelsius = snapshot.sensor_temperature_centi_celsius;
    inputs.humidityCentiPercent = snapshot.sensor_humidity_centi_percent;
    inputs.pressureChangeCentiHpa3h = snapshot.sensor_pressure_change_centi_hpa_3h;
    inputs.readingAgeSeconds = snapshot.sensor_age_seconds;
    if (snapshot.sensor_co2_rate_ppm_per_hour != open_radio::ui::kSensorAbsent) {
      inputs.co2RatePpmPerMinute = snapshot.sensor_co2_rate_ppm_per_hour / 60;
    }
    const open_radio::AlertPolicy policy;
    open_radio::AlertVerdict verdict;
    // Long enough for the longest dwell in the table, so a still picture shows
    // what the device settles on rather than what it shows in the first second.
    for (int step = 0; step < 40; ++step) {
      verdict = engine.evaluate(inputs, policy, 1000U + static_cast<std::uint32_t>(step) * 5U);
    }
    snapshot.air_alert = open_radio::alertSentence(
        verdict.id, snapshot.locale == open_radio::ui::LocaleMode::pl);
    snapshot.air_alert_level = static_cast<std::uint8_t>(verdict.level);
  }
  std::vector<std::uint16_t> pixels(
      static_cast<std::size_t>(open_radio::ui::generated::kWidth) *
      static_cast<std::size_t>(open_radio::ui::generated::kHeight));
  open_radio::ui::FramebufferView framebuffer{
      pixels.data(), pixels.size(), open_radio::ui::generated::kWidth,
      open_radio::ui::generated::kHeight};
  const auto result = open_radio::ui::renderScreen(
      framebuffer, snapshot,
      open_radio::ui::generated::kStations.data(),
      open_radio::ui::generated::kStations.size());
  if (result.status != open_radio::ui::RenderStatus::ok) {
    std::cerr << "render failed status=" << static_cast<int>(result.status)
              << '\n';
    return 3;
  }
  if (!writePpm(argv[1], pixels)) {
    std::cerr << "failed to write PPM: " << argv[1] << '\n';
    return 4;
  }
  std::cout << "fixture=" << open_radio::ui::generated::kFixtureId << " hash="
            << std::hex << std::setw(16) << std::setfill('0')
            << result.framebuffer_hash << std::dec << " pixels=" << pixels.size()
            << " truncated=" << result.truncated_text_count
            << " variant="
            << (snapshot.variant == open_radio::ui::HomeVariant::glance ? "glance"
                : snapshot.variant == open_radio::ui::HomeVariant::air ? "air"
                                                                       : "editorial")
            << " ppm=" << argv[1] << '\n';
  return 0;
}
