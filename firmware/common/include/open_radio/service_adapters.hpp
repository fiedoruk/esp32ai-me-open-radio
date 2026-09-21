#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "open_radio/service_contracts.hpp"

namespace open_radio {

inline std::uint64_t boundedAdd(std::uint64_t value, std::uint64_t delta) {
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  return delta > maximum - value ? maximum : value + delta;
}

inline std::uint32_t crc32(const std::uint8_t* data, std::size_t length) {
  std::uint32_t crc = 0xffffffffU;
  for (std::size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return crc ^ 0xffffffffU;
}

inline ConfigValidationDto validateDeviceConfig(const DeviceConfigDto& config) {
  ConfigValidationDto result;
  result.schemaValid = config.schemaVersion == 2;
  result.localeValid = config.locale == Locale::Pl || config.locale == Locale::En;
  result.stationValid = config.preferredStationIndex < 9;
  result.volumeValid = config.volume <= 100;
  result.brightnessValid = config.brightness <= 100;
  result.themeValid = config.theme == DisplayTheme::Dark ||
                      config.theme == DisplayTheme::Light;
  // All three homes, because the controller can store all three. Air was
  // absent here while activateSystem() wrote it, so choosing the air home
  // failed the whole record and every later commit — volume, brightness,
  // theme, locale — went down with it, silently, for as long as Air was held.
  // The owner's rule is that a cube without a module shows one home and a cube
  // with one offers two more; a home you may select but may not keep is not
  // one of them.
  result.nowPlayingVariantValid =
      config.nowPlayingVariant == NowPlayingVariant::Editorial ||
      config.nowPlayingVariant == NowPlayingVariant::Glance ||
      config.nowPlayingVariant == NowPlayingVariant::Air;
  const bool screensaverDelayValid = config.screensaverDelaySeconds == 30 ||
                                     config.screensaverDelaySeconds == 60 ||
                                     config.screensaverDelaySeconds == 120 ||
                                     config.screensaverDelaySeconds == 300 ||
                                     config.screensaverDelaySeconds == 600;
  const bool displayOffDelayValid = config.displayOffDelaySeconds == 900 ||
                                    config.displayOffDelaySeconds == 1800 ||
                                    config.displayOffDelaySeconds == 3600;
  result.displayValid =
      (config.screensaverMode == ScreensaverMode::Pulse ||
       config.screensaverMode == ScreensaverMode::Bars ||
       config.screensaverMode == ScreensaverMode::Orbit ||
       config.screensaverMode == ScreensaverMode::Cat) &&
      screensaverDelayValid && displayOffDelayValid;
  result.networksValid = config.wifiProfileCount <= kMaxRememberedNetworks;
  for (std::size_t index = 0; index < config.wifiProfileCount && result.networksValid;
       ++index) {
    const auto ref = config.wifiProfiles[index];
    result.networksValid = ref != NetworkProfileRef::None &&
                           ref != NetworkProfileRef::Unknown;
  }
  result.valid = result.schemaValid && result.localeValid && result.stationValid &&
                 result.volumeValid && result.brightnessValid && result.themeValid &&
                 result.nowPlayingVariantValid && result.displayValid &&
                 result.networksValid;
  return result;
}

inline std::size_t serializeConfig(const DeviceConfigDto& config,
                                   std::array<std::uint8_t, kStoragePayloadBytes>& output) {
  output.fill(0);
  std::size_t cursor = 0;
  output[cursor++] = config.schemaVersion;
  output[cursor++] = static_cast<std::uint8_t>(config.locale);
  output[cursor++] = config.preferredStationIndex;
  output[cursor++] = config.volume;
  output[cursor++] = config.brightness;
  output[cursor++] = static_cast<std::uint8_t>(config.theme);
  output[cursor++] = static_cast<std::uint8_t>(config.nowPlayingVariant);
  output[cursor++] = config.screensaverEnabled ? 1 : 0;
  output[cursor++] = static_cast<std::uint8_t>(config.screensaverMode);
  output[cursor++] = static_cast<std::uint8_t>(config.screensaverDelaySeconds & 0xffU);
  output[cursor++] = static_cast<std::uint8_t>(config.screensaverDelaySeconds >> 8U);
  output[cursor++] = config.displayOffEnabled ? 1 : 0;
  output[cursor++] = static_cast<std::uint8_t>(config.displayOffDelaySeconds & 0xffU);
  output[cursor++] = static_cast<std::uint8_t>(config.displayOffDelaySeconds >> 8U);
  output[cursor++] = config.wifiProfileCount;
  for (std::size_t index = 0; index < config.wifiProfileCount; ++index) {
    output[cursor++] = static_cast<std::uint8_t>(config.wifiProfiles[index]);
  }
  output[cursor++] = config.onboardingComplete ? 1 : 0;
  output[cursor++] = config.bluetoothRemembered ? 1 : 0;
  return cursor;
}

inline bool deserializeConfig(const std::uint8_t* input, std::size_t length,
                              DeviceConfigDto& config) {
  if (input == nullptr || length < 17 || length > kStoragePayloadBytes) return false;
  std::size_t cursor = 0;
  DeviceConfigDto parsed;
  parsed.schemaVersion = input[cursor++];
  parsed.locale = static_cast<Locale>(input[cursor++]);
  parsed.preferredStationIndex = input[cursor++];
  parsed.volume = input[cursor++];
  parsed.brightness = input[cursor++];
  parsed.theme = static_cast<DisplayTheme>(input[cursor++]);
  parsed.nowPlayingVariant = static_cast<NowPlayingVariant>(input[cursor++]);
  parsed.screensaverEnabled = input[cursor++] != 0;
  parsed.screensaverMode = static_cast<ScreensaverMode>(input[cursor++]);
  parsed.screensaverDelaySeconds = static_cast<std::uint16_t>(input[cursor]) |
                                   static_cast<std::uint16_t>(input[cursor + 1]) << 8U;
  cursor += 2;
  parsed.displayOffEnabled = input[cursor++] != 0;
  parsed.displayOffDelaySeconds = static_cast<std::uint16_t>(input[cursor]) |
                                  static_cast<std::uint16_t>(input[cursor + 1]) << 8U;
  cursor += 2;
  parsed.wifiProfileCount = input[cursor++];
  if (parsed.wifiProfileCount > kMaxRememberedNetworks ||
      cursor + parsed.wifiProfileCount + 2 > length) {
    return false;
  }
  for (std::size_t index = 0; index < parsed.wifiProfileCount; ++index) {
    parsed.wifiProfiles[index] = static_cast<NetworkProfileRef>(input[cursor++]);
  }
  parsed.onboardingComplete = input[cursor++] != 0;
  parsed.bluetoothRemembered = input[cursor++] != 0;
  if (cursor != length) return false;
  config = parsed;
  return true;
}

inline DeviceConfigDto currentConfigA() {
  DeviceConfigDto config;
  config.locale = Locale::Pl;
  config.preferredStationIndex = 0;
  config.volume = 42;
  config.brightness = 75;
  config.theme = DisplayTheme::Dark;
  config.nowPlayingVariant = NowPlayingVariant::Editorial;
  config.screensaverEnabled = true;
  config.screensaverMode = ScreensaverMode::Pulse;
  config.screensaverDelaySeconds = 120;
  config.displayOffEnabled = true;
  config.displayOffDelaySeconds = 1800;
  config.wifiProfiles[0] = NetworkProfileRef::Home;
  config.wifiProfileCount = 1;
  config.onboardingComplete = true;
  config.bluetoothRemembered = true;
  return config;
}

inline DeviceConfigDto currentConfigB() {
  DeviceConfigDto config;
  config.locale = Locale::En;
  config.preferredStationIndex = 5;
  config.volume = 58;
  config.brightness = 50;
  config.theme = DisplayTheme::Light;
  config.nowPlayingVariant = NowPlayingVariant::Glance;
  config.screensaverEnabled = false;
  config.screensaverMode = ScreensaverMode::Orbit;
  config.screensaverDelaySeconds = 300;
  config.displayOffEnabled = true;
  config.displayOffDelaySeconds = 3600;
  config.wifiProfiles[0] = NetworkProfileRef::Home;
  config.wifiProfiles[1] = NetworkProfileRef::Travel;
  config.wifiProfileCount = 2;
  config.onboardingComplete = true;
  return config;
}

inline StorageSlotDto makeCommittedSlot(std::uint32_t generation,
                                        const DeviceConfigDto& config) {
  StorageSlotDto slot;
  slot.occupied = true;
  slot.committed = true;
  slot.payloadParseable = true;
  slot.generation = generation;
  slot.config = config;
  slot.payloadLength = serializeConfig(config, slot.payload);
  slot.checksum = crc32(slot.payload.data(), slot.payloadLength);
  return slot;
}

class StorageSlotBackend {
 public:
  virtual ~StorageSlotBackend() = default;
  virtual bool readSlot(SlotId slot, StorageSlotDto& destination) const = 0;
  virtual bool writeSlot(SlotId slot, const StorageSlotDto& source) = 0;
};

class HostSlotBackend final : public StorageSlotBackend {
 public:
  bool readSlot(SlotId slot, StorageSlotDto& destination) const override {
    if (slot == SlotId::A) destination = slots_[0];
    else if (slot == SlotId::B) destination = slots_[1];
    else return false;
    return true;
  }

  bool writeSlot(SlotId slot, const StorageSlotDto& source) override {
    if (slot == SlotId::A) slots_[0] = source;
    else if (slot == SlotId::B) slots_[1] = source;
    else return false;
    return true;
  }

  void clear() { slots_ = {}; }

 private:
  std::array<StorageSlotDto, 2> slots_{};
};

class NvsAbStorageAdapter {
 public:
  explicit NvsAbStorageAdapter(StorageSlotBackend& backend) : backend_(backend) {}

  StorageSelectionDto selectBoot() const {
    StorageSlotDto slotA;
    StorageSlotDto slotB;
    backend_.readSlot(SlotId::A, slotA);
    backend_.readSlot(SlotId::B, slotB);
    const auto inspectionA = inspect(slotA);
    const auto inspectionB = inspect(slotB);
    StorageSelectionDto result;
    result.rejectionA = inspectionA.reason;
    result.rejectionB = inspectionB.reason;
    const StorageSlotDto* selected = nullptr;
    SlotId selectedId = SlotId::None;
    if (inspectionA.valid) {
      selected = &slotA;
      selectedId = SlotId::A;
    }
    if (inspectionB.valid &&
        (selected == nullptr || slotB.generation > selected->generation)) {
      selected = &slotB;
      selectedId = SlotId::B;
    }
    if (selected == nullptr) return result;
    result.status = StorageStatus::Bootable;
    result.selectedSlot = selectedId;
    result.generation = selected->generation;
    result.config = selected->config;
    if (result.config.schemaVersion == 1) {
      result.migratedFromVersion = 1;
      result.config.schemaVersion = 2;
    }
    return result;
  }

  bool commit(const DeviceConfigDto& config, WritePhase phase) {
    if (!validateDeviceConfig(config).valid || phase == WritePhase::None) return false;
    const auto current = selectBoot();
    const SlotId target = current.selectedSlot == SlotId::A ? SlotId::B : SlotId::A;
    const std::uint32_t generation = current.generation + 1;
    if (phase == WritePhase::BeforeWrite) return false;
    auto slot = makeCommittedSlot(generation, config);
    if (phase == WritePhase::DuringPayload) {
      slot.payloadLength /= 2;
      slot.committed = false;
    } else if (phase == WritePhase::BeforeCommitMarker) {
      slot.committed = false;
    }
    // The backend says whether the blob was written in full; a caller that is
    // told "committed" while NVS refused the write assumes success and skips
    // its failure log (Codex F13, audit 2026-09-08).
    const bool written = backend_.writeSlot(target, slot);
    return written && phase == WritePhase::AfterCommit;
  }

 private:
  struct Inspection {
    bool valid;
    StorageRejectReason reason;
  };

  static Inspection inspect(const StorageSlotDto& slot) {
    if (!slot.occupied) return {false, StorageRejectReason::Empty};
    if (!slot.committed) return {false, StorageRejectReason::Uncommitted};
    if (slot.payloadLength == 0 || slot.payloadLength > slot.payload.size() ||
        crc32(slot.payload.data(), slot.payloadLength) != slot.checksum) {
      return {false, StorageRejectReason::ChecksumMismatch};
    }
    if (!slot.payloadParseable) {
      return {false, StorageRejectReason::PayloadParseError};
    }
    if (slot.config.schemaVersion > 2) {
      return {false, StorageRejectReason::FutureSchemaUnsupported};
    }
    DeviceConfigDto migrated = slot.config;
    if (migrated.schemaVersion == 1) migrated.schemaVersion = 2;
    if (!validateDeviceConfig(migrated).valid) {
      return {false, StorageRejectReason::InvalidConfig};
    }
    return {true, StorageRejectReason::None};
  }

  StorageSlotBackend& backend_;
};

inline void seedPersistenceSetup(HostSlotBackend& backend, PersistenceSetup setup) {
  backend.clear();
  if (setup == PersistenceSetup::Empty) return;
  DeviceConfigDto base = currentConfigA();
  if (setup == PersistenceSetup::LegacyOnly) base.schemaVersion = 1;
  if (setup == PersistenceSetup::FutureOnly) base.schemaVersion = 3;
  backend.writeSlot(SlotId::A, makeCommittedSlot(1, base));
  if (setup == PersistenceSetup::ChecksumCorruptNewest) {
    auto newest = makeCommittedSlot(2, currentConfigB());
    newest.checksum = 0;
    backend.writeSlot(SlotId::B, newest);
  } else if (setup == PersistenceSetup::ParseCorruptNewest) {
    auto newest = makeCommittedSlot(2, currentConfigB());
    newest.payloadLength -= 2;
    newest.checksum = crc32(newest.payload.data(), newest.payloadLength);
    newest.payloadParseable = false;
    backend.writeSlot(SlotId::B, newest);
  }
}

inline NetworkProfileHealthDto baseProfile(NetworkProfileRef ref) {
  if (ref == NetworkProfileRef::Home) {
    return {ref, true, 90, 95, 0, 0, 2900000, true, true};
  }
  if (ref == NetworkProfileRef::Travel) {
    return {ref, true, 70, 85, 0, 0, 2800000, true, false};
  }
  if (ref == NetworkProfileRef::LegacyHome) {
    return {ref, true, 50, 60, 1, 0, 0, false, false};
  }
  if (ref == NetworkProfileRef::Pending) {
    return {ref, false, 80, 100, 0, 0, 0, false, false};
  }
  return {};
}

class WifiSelectionAdapter {
 public:
  NetworkSelectionDto select(NetworkProfileState state,
                             const NetworkScanDto* scans,
                             std::size_t scanCount,
                             std::uint64_t nowMs) const {
    if (scans == nullptr || scanCount == 0 || scanCount > kMaxNetworkScans) {
      return {};
    }
    bool promptRequired = false;
    bool hasCandidate = false;
    std::int32_t bestScore = std::numeric_limits<std::int32_t>::min();
    NetworkProfileRef bestRef = NetworkProfileRef::None;
    std::uint64_t earliestQuarantine = 0;
    for (std::size_t index = 0; index < scanCount; ++index) {
      const auto& scan = scans[index];
      if (!scan.reachable) continue;
      auto profile = baseProfile(scan.profileRef);
      if (scan.profileRef == NetworkProfileRef::Home &&
          state == NetworkProfileState::HomeQuarantined) {
        profile.healthScore = 45;
        profile.consecutiveFailures = 2;
        profile.quarantineUntilMs = boundedAdd(nowMs, 30000);
      } else if (scan.profileRef == NetworkProfileRef::Home &&
                 state == NetworkProfileState::HomeRecovered) {
        profile.healthScore = 100;
        profile.consecutiveFailures = 0;
        profile.quarantineUntilMs = 0;
        profile.lastSuccessMs = nowMs - 1000;
      }
      const bool secured = scan.security == NetworkSecurity::Wpa2Psk ||
                           scan.security == NetworkSecurity::Wpa3Sae;
      if (scan.security == NetworkSecurity::Open || scan.captivePortal ||
          !secured || scan.profileRef == NetworkProfileRef::None ||
          scan.profileRef == NetworkProfileRef::Unknown || !profile.approved) {
        promptRequired = true;
        continue;
      }
      if (profile.quarantineUntilMs > nowMs) {
        if (earliestQuarantine == 0 ||
            profile.quarantineUntilMs < earliestQuarantine) {
          earliestQuarantine = profile.quarantineUntilMs;
        }
        continue;
      }
      const auto signalScore = std::max<std::int32_t>(
          0, std::min<std::int32_t>(100, scan.rssi + 100));
      const bool recent = profile.hasLastSuccess && nowMs >= profile.lastSuccessMs &&
                          nowMs - profile.lastSuccessMs <= 86400000;
      const std::int32_t score = profile.priority + profile.healthScore +
                                 signalScore + (profile.preferred ? 30 : 0) +
                                 (recent ? 10 : 0) -
                                 profile.consecutiveFailures * 15;
      if (!hasCandidate || score > bestScore ||
          (score == bestScore && static_cast<std::uint8_t>(profile.ref) <
                                     static_cast<std::uint8_t>(bestRef))) {
        hasCandidate = true;
        bestScore = score;
        bestRef = profile.ref;
      }
    }
    if (hasCandidate) {
      return {NetworkSelectionStatus::Selected, bestRef, 0};
    }
    if (promptRequired) {
      return {NetworkSelectionStatus::PromptRequired, NetworkProfileRef::None, 0};
    }
    return {NetworkSelectionStatus::RetryScheduled, NetworkProfileRef::None,
            earliestQuarantine == 0 ? boundedAdd(nowMs, 5000)
                                    : earliestQuarantine};
  }
};

class Mp3ResolverAdapter {
 public:
  ResolverResultDto resolve(const ResolverInputDto& input) const {
    if (input.capability == CapabilityClass::HlsHeAac) {
      return {ResolverStatus::Unsupported, ResolverSelection::None, 0, 100, 0};
    }
    if (input.primaryOutcome == CandidateOutcome::Invalid ||
        (input.hasLastKnownGood &&
         input.lastKnownGoodOutcome == CandidateOutcome::Invalid)) {
      return {};
    }
    std::uint8_t score = 100;
    std::uint64_t quarantine = 0;
    if (!input.primaryReferenceOnly) {
      if (input.primaryOutcome == CandidateOutcome::Success) {
        return {ResolverStatus::Playing, ResolverSelection::Primary, 0, 100, 0};
      }
      const std::int16_t delta = input.primaryOutcome == CandidateOutcome::Http404
                                     ? -50
                                 : input.primaryOutcome == CandidateOutcome::ParseError
                                     ? -35
                                 : input.primaryOutcome == CandidateOutcome::Timeout
                                     ? -25
                                     : -20;
      score = static_cast<std::uint8_t>(std::max<std::int16_t>(0, 100 + delta));
      if (score <= 50) quarantine = boundedAdd(input.nowMs, 30000);
    }
    if (input.hasLastKnownGood &&
        input.lastKnownGoodOutcome == CandidateOutcome::Success) {
      return {ResolverStatus::Playing, ResolverSelection::LastKnownGood, 0,
              score, quarantine};
    }
    return {ResolverStatus::RetryScheduled, ResolverSelection::None,
            quarantine == 0 ? boundedAdd(input.nowMs, 5000) : quarantine,
            score, quarantine};
  }
};

inline DiscoveryParseDto parseDiscoveryResponse(DiscoveryParser parser,
                                                 const DiscoveryResponseDto& response) {
  if (parser == DiscoveryParser::Unknown || response.bodyBytes == 0 ||
      !response.completeBody || response.bodyBytes > kDiscoveryResponseBytes) {
    return {CandidateOutcome::ParseError, false};
  }
  if (response.statusCode == 404) return {CandidateOutcome::Http404, false};
  if (response.statusCode < 200 || response.statusCode >= 300) {
    return {CandidateOutcome::Stale, false};
  }
  const bool jsonParser = parser == DiscoveryParser::RmfonJson ||
                          parser == DiscoveryParser::EurozetJson ||
                          parser == DiscoveryParser::TubaJson;
  if ((jsonParser && response.contentType != ContentType::Json) ||
      (!jsonParser && response.contentType != ContentType::Html) ||
      !response.expectedFieldPresent || !response.endpointUsesHttps ||
      response.endpointBytes == 0 || response.endpointBytes >= 128) {
    return {CandidateOutcome::ParseError, false};
  }
  return {CandidateOutcome::Success, true};
}

inline constexpr std::array<OnboardingRouteDto, 9> kOnboardingRoutes{{
    {"/", HttpMethod::Get, ContentType::Html, kOnboardingStaticAssetBytes},
    {"/network-onboarding/index.html", HttpMethod::Get, ContentType::Html,
     kOnboardingStaticAssetBytes},
    {"/network-onboarding/styles.css", HttpMethod::Get, ContentType::Css,
     kOnboardingStaticAssetBytes},
    {"/network-onboarding/state-machine.mjs", HttpMethod::Get,
     ContentType::JavaScript, kOnboardingStaticAssetBytes},
    {"/network-onboarding/app.mjs", HttpMethod::Get, ContentType::JavaScript,
     kOnboardingStaticAssetBytes},
    {"/api/config", HttpMethod::Post, ContentType::Json,
     kOnboardingRequestBytes},
    {"/api/config-form", HttpMethod::Post, ContentType::FormUrlEncoded,
     kOnboardingRequestBytes},
    // The directory the installer picks from and the nine slots they fill.
    // Both live on the access point, before Wi-Fi exists, because that is the
    // only moment a phone can reach the device at all.
    {"/api/directory", HttpMethod::Get, ContentType::Json,
     kOnboardingStaticAssetBytes},
    {"/api/slots", HttpMethod::Post, ContentType::Json,
     kOnboardingRequestBytes}
}};

inline const OnboardingRouteDto* findOnboardingRoute(const char* path,
                                                      HttpMethod method) {
  if (path == nullptr) return nullptr;
  for (const auto& route : kOnboardingRoutes) {
    if (route.method == method && std::strcmp(route.path, path) == 0) {
      return &route;
    }
  }
  return nullptr;
}

inline RequestValidation validateConfigurationRequest(
    const char* path, HttpMethod method, ContentType contentType,
    std::size_t bodyBytes, const DeviceConfigDto* config) {
  const auto* route = findOnboardingRoute(path, method);
  if (route == nullptr) return RequestValidation::NotFound;
  if (method != HttpMethod::Post) return RequestValidation::MethodNotAllowed;
  if (contentType != route->contentType) {
    return RequestValidation::UnsupportedContentType;
  }
  if (bodyBytes == 0) return RequestValidation::EmptyBody;
  if (bodyBytes > route->maximumBytes) return RequestValidation::Oversized;
  if (config == nullptr || !validateDeviceConfig(*config).valid) {
    return RequestValidation::InvalidConfig;
  }
  return RequestValidation::Accepted;
}

inline RedactedDiagnosticsDto redactConfig(const DeviceConfigDto& config) {
  return {1, config.wifiProfileCount, config.preferredStationIndex,
          config.locale, config.bluetoothRemembered, false};
}

class UiProjectionAdapter {
 public:
  UiSnapshotDto project(RuntimeState state, std::uint8_t stationIndex,
                        OutputKind output, Locale locale = Locale::Pl) const {
    UiSnapshotDto snapshot;
    snapshot.systemState = state;
    snapshot.output = output;
    snapshot.locale = locale;
    snapshot.stationIndex = stationIndex < 9 ? stationIndex : 0;
    snapshot.requestedStationIndex = snapshot.stationIndex;
    snapshot.setupComplete = state != RuntimeState::ConfigRequired;
    snapshot.screen = state == RuntimeState::ConfigRequired ? Screen::Wifi
                                                            : Screen::NowPlaying;
    if (state == RuntimeState::SafeMode) snapshot.screen = Screen::Diagnostics;
    return snapshot;
  }

  TouchActionDto hitTestNowPlaying(std::int16_t x, std::int16_t y,
                                   NowPlayingVariant variant) const {
    if (inside(x, y, 268, variant == NowPlayingVariant::Glance ? 8 : 34, 44,
               44)) {
      return {TouchAction::Settings, 0};
    }
    if (inside(x, y, 8, 184, 56, 56)) return {TouchAction::PreviousStation, 0};
    if (inside(x, y, 64, 184, 192, 56)) return {TouchAction::Stations, 0};
    if (inside(x, y, 256, 184, 64, 56)) return {TouchAction::NextStation, 0};
    // The title is no longer actionable: saving the current track is gone, so
    // a hit there falls through to TouchAction::None.
    return {};
  }

 private:
  static bool inside(std::int16_t x, std::int16_t y, std::int16_t left,
                     std::int16_t top, std::int16_t width,
                     std::int16_t height) {
    return x >= left && y >= top && x < left + width && y < top + height;
  }
};

}
