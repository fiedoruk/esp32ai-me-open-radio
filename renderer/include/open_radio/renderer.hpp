#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace open_radio::ui {

struct FramebufferView {
  std::uint16_t* pixels;
  std::size_t pixel_count;
  int width;
  int height;
};

struct Rect {
  int x;
  int y;
  int width;
  int height;
};

struct StationTheme {
  const char* id;
  const char* name;
  const char* short_name;
  std::uint16_t accent;
  std::uint16_t accent_alt;
  std::uint16_t on_accent;
};

struct ThemeTokens {
  std::uint16_t canvas;
  std::uint16_t surface;
  std::uint16_t raised;
  std::uint16_t text;
  std::uint16_t muted;
  std::uint16_t border;
  std::uint16_t success;
  std::uint16_t warning;
  std::uint16_t danger;
  std::uint16_t shadow;
  std::uint16_t brand;
  std::uint16_t on_brand;
  // Brand teal for small text: darker than brand in the light theme, where
  // brand on canvas is about 4.2:1. Fills, icons and large glyphs use brand.
  std::uint16_t brand_text;
  // Edge of a control on a raised or surface fill: raised and surface sit at
  // about 1.1:1 on the light canvas, the edge carries the control's outline.
  std::uint16_t control_edge;
};

struct IconMask24 {
  std::array<std::uint32_t, 24> rows;
};

struct FontGlyph {
  std::uint32_t codepoint;
  std::uint32_t bitmap_offset;
  std::uint8_t width;
  std::uint8_t height;
  std::int8_t x_offset;
  std::int8_t y_offset;
  std::uint8_t advance;
};

struct FontFace {
  const FontGlyph* glyphs;
  std::size_t glyph_count;
  const std::uint8_t* bitmap;
  std::size_t bitmap_size;
  std::uint8_t ascent;
  std::uint8_t descent;
};

enum class FontSize : std::uint8_t {
  caption,
  body,
  title,
  display
};

struct TextRenderResult {
  bool valid;
  bool truncated;
  int width;
};

enum class ThemeMode : std::uint8_t {
  dark,
  light
};

enum class AudioOutput : std::uint8_t {
  local,
  bluetooth
};

enum class HomeVariant : std::uint8_t {
  editorial,
  glance,
  air
};

enum class LocaleMode : std::uint8_t {
  pl,
  en
};

enum class BluetoothState : std::uint8_t {
  idle,
  scanning,
  found,
  connecting,
  connected,
  error
};

// Sentinel for a quantity the attached module does not measure. Mirrors
// open_radio::kAbsentValue; kept separate so the renderer does not depend on
// the firmware headers.
inline constexpr std::int32_t kSensorAbsent = INT32_MIN;

enum class ScreenKind : std::uint8_t {
  now_playing_editorial,
  now_playing_glance,
  now_playing_air,
  screensaver_pulse,
  screensaver_bars,
  screensaver_orbit,
  screensaver_cat,
  display_off,
  stations,
  volume_control,
  brightness_control,
  settings_quick,
  settings_system,
  settings_display,
  noise,
  sensors,
  wifi_status,
  wifi_recovery,
  bluetooth_fallback,
  unsupported,
  safe_mode,
  onboarding_wifi,
  onboarding_first_sound,
  onboarding_bluetooth,
  bluetooth_pairing,
  diagnostics,
  about,
  market
};

struct CompactSnapshot {
  std::size_t station_index;
  std::size_t requested_station_index;
  bool wifi_online;
  AudioOutput output;
  std::uint8_t volume;
  bool degraded;
  ThemeMode theme;
  std::uint8_t wifi_strength_percent = 0;
  HomeVariant variant = HomeVariant::editorial;
  LocaleMode locale = LocaleMode::pl;
  ScreenKind screen = ScreenKind::now_playing_editorial;
  std::uint8_t brightness = 75;
  bool battery_available = false;
  bool battery_external_power = false;
  std::uint8_t battery_level_percent = 0;
  std::uint16_t battery_voltage_millivolts = 0;
  std::int16_t battery_current_milliamps = 0;
  bool battery_runtime_valid = false;
  std::uint16_t battery_runtime_minutes = 0;
  std::uint8_t noise_color = 0;
  bool noise_playing = false;
  // Environment readings, in the smallest unit the screen renders, with
  // kSensorAbsent for a quantity the attached module does not measure — as
  // opposed to one it measures as zero. The SCD41 has no pressure sensor and
  // the BME688 has no CO2 sensor at all, so the screen has to be able to say
  // "this module does not do that" without inventing a dash from a zero.
  //
  // Gas resistance is the raw value and nothing derived from it: the BME688's
  // index and CO2 equivalent come from a closed Bosch binary whose licence
  // cannot be combined with GPL-3.0.
  // One entry per module slot, mirroring open_radio::kSensorSlotCount: 0 absent,
  // 1 warming, 2 ok, 3 fault, 4 quarantined. Two seats for three supported
  // parts — the SCD40 and SCD41 share an address, so they share a slot; see the
  // slot-count note in firmware/common/include/open_radio/sensor_snapshot.hpp.
  // The screen counts how many say Ok; it never asks which one a value came
  // from, because the merge that chose between them already happened.
  static constexpr std::size_t kSensorSlots = 2;
  std::array<std::uint8_t, kSensorSlots> sensor_status{};
  std::int32_t sensor_co2_ppm = kSensorAbsent;
  std::int32_t sensor_temperature_centi_celsius = kSensorAbsent;
  std::int32_t sensor_humidity_centi_percent = kSensorAbsent;
  std::int32_t sensor_pressure_centi_hectopascal = kSensorAbsent;
  std::int32_t sensor_gas_resistance_ohm = kSensorAbsent;
  // The band the firmware placed the CO2 reading in, as
  // open_radio::AirBand: 0 unknown, 1 fresh, 2 normal, 3 stale, 4 very stale,
  // 5 high. The renderer does NOT re-derive it from the ppm value, because the
  // thresholds carry published sources and belong in exactly one file
  // (firmware/common/include/open_radio/air_assessment.hpp). A screen that
  // recomputed them would be a second, silent copy that could disagree.
  std::uint8_t sensor_air_band = 0;
  // Change over the last hour, and how long until the next threshold at the
  // present rate. Both kSensorAbsent until there is enough history to say —
  // "no answer yet" is a different thing from "steady", and the screen must not
  // render the first as the second.
  std::int32_t sensor_co2_rate_ppm_per_hour = kSensorAbsent;
  std::int32_t sensor_co2_minutes_to_next = kSensorAbsent;
  std::int32_t sensor_co2_next_threshold_ppm = kSensorAbsent;
  // The highest CO2 the attached module can measure: 5,000 on an SCD41, 2,000
  // on an SCD40, and the lower figure whenever the part could not be
  // identified. A reading sitting at this value is the sensor out of scale, not
  // a measurement of the room, and the screen says so rather than printing it
  // as if it were one.
  std::int32_t sensor_co2_ceiling_ppm = kSensorAbsent;
  // Three-hour pressure change in centi-hectopascals, and the gas element's
  // departure from its own running median as a percentage of it. Both
  // kSensorAbsent until there is enough history to say.
  //
  // "No answer yet" matters more here than anywhere else on the screen: an
  // arrow drawn level because three hours have not passed is not cautious, it
  // is a false statement in the shape people trust most. The screen draws
  // nothing until the history is there.
  std::int32_t sensor_pressure_change_centi_hpa_3h = kSensorAbsent;
  std::int32_t sensor_gas_departure_percent = kSensorAbsent;
  // Present only during the gas element's burn-in: minutes since power-on,
  // for the calibration line on the bar.
  std::int32_t sensor_gas_warmup_minutes = kSensorAbsent;
  // The dew point, in centi-degrees, computed by the firmware from temperature
  // and humidity. Carried rather than re-derived: the Magnus formula lives in
  // one file and a screen with its own copy would be a second answer.
  std::int32_t sensor_dew_point_centi_celsius = kSensorAbsent;
  // The one alert the device has decided to speak, already worded in the
  // owner's language, and how loudly (open_radio::AlertLevel as a byte).
  //
  // Worded upstream on purpose. The renderer draws what it is given and does
  // not know what 1000 ppm means — the thresholds carry published sources and
  // belong in one file (firmware/common/include/open_radio/alert_engine.hpp),
  // and a screen that re-derived them would be a second, silent copy free to
  // disagree. It is also the reason nothing here is a threshold: this is a
  // drawing layer.
  const char* air_alert = nullptr;
  std::uint8_t air_alert_level = 0;
  // How old the newest reading is. The screen says it, because a number with no
  // age silently claims to be now.
  std::uint16_t sensor_age_seconds = 0;
  // What this build calls itself. An About screen without one is an About
  // screen missing its only reason to exist.
  const char* firmware_version = nullptr;
  const char* now_playing_title = nullptr;
  const char* setup_access_code = nullptr;
  bool wifi_portal_active = false;
  BluetoothState bluetooth_state = BluetoothState::idle;
  const char* bluetooth_candidate_name = nullptr;
  bool bluetooth_remembered = false;
  bool metadata_available = false;
  std::uint8_t animation_frame = 0;
  bool screensaver_enabled = true;
  std::uint8_t screensaver_mode = 0;
  std::uint16_t screensaver_delay_seconds = 120;
  bool display_off_enabled = true;
  std::uint16_t display_off_delay_seconds = 1800;
  bool confirm_delete = false;
  bool clock_valid = false;
  std::uint16_t clock_year = 0;
  std::uint8_t clock_month = 0;
  std::uint8_t clock_day = 0;
  std::uint8_t clock_hour = 0;
  std::uint8_t clock_minute = 0;
  // Dotted-quad address of the device on the home network (nullptr or empty
  // when unknown). Shown on the Wi-Fi screen next to open-radio.local so the
  // owner can reach the device console from a browser.
  const char* device_address = nullptr;
  // True while the browser console session is open; the settings tile then
  // offers closing it instead of opening another.
  bool console_active = false;
};

enum class RenderStatus : std::uint8_t {
  ok,
  invalid_buffer,
  invalid_dimensions,
  invalid_station
};

struct RenderResult {
  RenderStatus status;
  std::uint64_t framebuffer_hash;
  std::uint16_t truncated_text_count = 0;
};

constexpr std::uint16_t rgb565(std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue) {
  return static_cast<std::uint16_t>(((red >> 3U) << 11U) |
                                    ((green >> 2U) << 5U) | (blue >> 3U));
}

bool isValid(FramebufferView framebuffer);
bool fill(FramebufferView framebuffer, std::uint16_t color);
bool fillRect(FramebufferView framebuffer, Rect rect, std::uint16_t color);
bool fillRoundRect(FramebufferView framebuffer, Rect rect, int radius,
                   std::uint16_t color);
bool drawIconMask24(FramebufferView framebuffer, int x, int y,
                    const IconMask24& mask, std::uint16_t color);
int measureText(const char* value, FontSize size);
TextRenderResult drawText(FramebufferView framebuffer, int x, int y,
                          const char* value, FontSize size,
                          std::uint16_t color, int maximum_width,
                          bool ellipsize = true);
std::uint64_t hashFramebuffer(FramebufferView framebuffer);
RenderResult renderNowPlaying(FramebufferView framebuffer,
                              const CompactSnapshot& snapshot,
                              const StationTheme* stations,
                              std::size_t station_count);
// A station logo delivered at runtime — fetched by the device itself at first
// configuration, decoded to RGB565 and stored locally. The renderer treats it
// as an overlay source: when a slot has one, it wins over the baked pack;
// when it has none, the baked pack (owner/lab lanes) or the monogram applies.
// Pixels are borrowed, not owned; the caller keeps them alive for as long as
// rendering may happen.
struct RuntimeStationLogo {
  const std::uint16_t* pixels = nullptr;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
};

// Registers the runtime logo table (index-aligned with the station table).
// Passing nullptr or zero count clears it. Not part of the deterministic
// render contract: host tests never register one, so golden hashes are
// computed with this feature dormant.
void setRuntimeStationLogos(const RuntimeStationLogo* logos, std::size_t count);

RenderResult renderScreen(FramebufferView framebuffer,
                          const CompactSnapshot& snapshot,
                          const StationTheme* stations,
                          std::size_t station_count);

}
