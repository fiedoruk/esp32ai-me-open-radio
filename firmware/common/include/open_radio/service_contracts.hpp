#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "open_radio/firmware_contract.hpp"

namespace open_radio {

constexpr std::size_t kMaxRememberedNetworks = 8;
constexpr std::size_t kMaxNetworkScans = 8;
constexpr std::size_t kStoragePayloadBytes = 64;
constexpr std::size_t kOnboardingRequestBytes = 4096;
constexpr std::size_t kOnboardingStaticAssetBytes = 65536;
constexpr std::size_t kDiscoveryResponseBytes = 16384;

enum class Locale : std::uint8_t { Pl, En, Invalid };
enum class DisplayTheme : std::uint8_t { Dark, Light, Invalid };
// Appended BEFORE Invalid so the two existing values keep their numbers. This
// field IS stored in device-config-v2, so a third value is a compatibility
// boundary: an older firmware reading it does not know what Air means. That is
// precisely why this arrives in 0.4 rather than as a point release.
enum class NowPlayingVariant : std::uint8_t { Editorial, Glance, Air, Invalid };
enum class ScreensaverMode : std::uint8_t { Pulse, Bars, Orbit, Cat, Invalid };
enum class NoiseColor : std::uint8_t { White, Pink, Brown, Invalid };
enum class BluetoothUiState : std::uint8_t {
  Idle,
  Scanning,
  Found,
  Connecting,
  Connected,
  Error
};
enum class NetworkProfileRef : std::uint8_t {
  None,
  Home,
  Travel,
  LegacyHome,
  Pending,
  Unknown
};
enum class NetworkSecurity : std::uint8_t { Open, Wpa2Psk, Wpa3Sae, Unknown };
enum class NetworkProfileState : std::uint8_t {
  Base,
  HomeQuarantined,
  HomeRecovered
};
enum class NetworkSelectionStatus : std::uint8_t {
  Selected,
  PromptRequired,
  RetryScheduled,
  InvalidInput
};
enum class PersistenceSetup : std::uint8_t {
  Empty,
  Baseline,
  LegacyOnly,
  FutureOnly,
  ChecksumCorruptNewest,
  ParseCorruptNewest
};
enum class WritePhase : std::uint8_t {
  None,
  BeforeWrite,
  DuringPayload,
  BeforeCommitMarker,
  AfterCommit
};
enum class SlotId : std::uint8_t { None, A, B };
enum class StorageStatus : std::uint8_t { Bootable, SafeMode };
enum class StorageRejectReason : std::uint8_t {
  None,
  Empty,
  Uncommitted,
  ChecksumMismatch,
  PayloadParseError,
  FutureSchemaUnsupported,
  InvalidConfig
};
enum class CandidateOutcome : std::uint8_t {
  Success,
  Timeout,
  Http404,
  Stale,
  ParseError,
  Invalid
};
enum class ResolverStatus : std::uint8_t {
  Playing,
  RetryScheduled,
  Unsupported,
  InvalidInput
};
enum class ResolverSelection : std::uint8_t {
  None,
  Primary,
  LastKnownGood
};
enum class DiscoveryParser : std::uint8_t {
  RmfonJson,
  EurozetJson,
  TubaJson,
  TokFmPlayer,
  ZprPlayer,
  Unknown
};
enum class ContentType : std::uint8_t {
  Html,
  Css,
  JavaScript,
  Json,
  FormUrlEncoded,
  Unknown
};
enum class HttpMethod : std::uint8_t { Get, Post };
enum class RequestValidation : std::uint8_t {
  Accepted,
  NotFound,
  MethodNotAllowed,
  UnsupportedContentType,
  EmptyBody,
  Oversized,
  InvalidConfig
};
enum class Screen : std::uint8_t {
  Wifi,
  City,
  Bluetooth,
  NowPlaying,
  Stations,
  Volume,
  Brightness,
  SettingsQuick,
  SettingsSystem,
  SettingsDisplay,
  Noise,
  About,
  Diagnostics,
  Screensaver,
  DisplayOff,
  Unsupported,
  SafeMode,
  // Environment readings. Appended, so every existing value keeps its number —
  // and this enum is runtime state, not stored: device-config-v2 keeps the
  // screensaver and display-off settings, never the current screen. Adding a
  // screen therefore costs nothing in rollback compatibility, which is what
  // separates it from NowPlayingVariant.
  //
  // Deliberately NOT a third audio application. Radio and Noise are exclusive
  // because both drive the speaker; readings drive nothing, so making them a
  // third value on that axis would mean looking at the air stops the music.
  // Two axes: what plays, and what the screen shows.
  Sensors
};
enum class TouchAction : std::uint8_t {
  None,
  Close,
  Settings,
  PreviousStation,
  Stations,
  NextStation,
  SelectItem,
  ActivateSetting,
  SettingsQuick,
  SettingsSystem,
  SettingsDisplay,
  Wake
};

enum class HardwareButton : std::uint8_t { A, B, C };

struct TouchActionDto {
  TouchAction action = TouchAction::None;
  std::uint8_t index = 0;
};

struct DeviceConfigDto {
  std::uint8_t schemaVersion = 2;
  Locale locale = Locale::Pl;
  std::uint8_t preferredStationIndex = 0;
  // 15 by owner decision 2026-07-21: a first boot in a quiet room should be
  // audible, not startling. Stored configs override this immediately.
  std::uint8_t volume = 15;
  std::uint8_t brightness = 75;
  DisplayTheme theme = DisplayTheme::Dark;
  // The daily home screen is a persisted user choice, not a build-time variant.
  // Out of the box it is the quiet one: a cube on a shelf should show what is
  // playing and nothing else until someone asks for more.
  // The full radio screen is what a cube shows out of the box, by owner
  // decision 2026-07-30. A device whose first screen is the stripped-down one
  // asks a new owner to discover that there is more; the reverse asks nothing.
  //
  // A DEFAULT, not a migration: a cube that already stored a choice keeps it.
  NowPlayingVariant nowPlayingVariant = NowPlayingVariant::Editorial;
  // Off by default, by owner decision 2026-07-21. A screensaver on a device
  // whose whole screen is one tile hides the only thing it has to say.
  bool screensaverEnabled = false;
  ScreensaverMode screensaverMode = ScreensaverMode::Pulse;
  std::uint16_t screensaverDelaySeconds = 120;
  // Also off by default, by owner decision 2026-07-29. The reason is the same
  // one that turned the screensaver off, and the environment module makes it
  // sharper: a device whose job is to show a reading has nothing to gain from
  // hiding it, and the cell is 500 mAh — this thing lives next to a socket.
  //
  // The panel is IPS, so there is no burn-in to protect it from, which is the
  // usual argument and does not apply here.
  //
  // Note this is a DEFAULT, not a migration: a cube that already stored
  // displayOffEnabled keeps what its owner had. Changing a stored choice under
  // someone is exactly what this release exists to stop doing. The toggle is in
  // Settings, Display.
  bool displayOffEnabled = false;
  std::uint16_t displayOffDelaySeconds = 1800;
  std::array<NetworkProfileRef, kMaxRememberedNetworks> wifiProfiles{};
  std::uint8_t wifiProfileCount = 0;
  bool onboardingComplete = false;
  bool bluetoothRemembered = false;
};

struct ConfigValidationDto {
  bool valid = false;
  bool schemaValid = false;
  bool localeValid = false;
  bool stationValid = false;
  bool volumeValid = false;
  bool brightnessValid = false;
  bool themeValid = false;
  bool nowPlayingVariantValid = false;
  bool displayValid = false;
  bool networksValid = false;
};

struct NetworkProfileHealthDto {
  NetworkProfileRef ref = NetworkProfileRef::Unknown;
  bool approved = false;
  std::uint8_t priority = 0;
  std::uint8_t healthScore = 0;
  std::uint8_t consecutiveFailures = 0;
  std::uint64_t quarantineUntilMs = 0;
  std::uint64_t lastSuccessMs = 0;
  bool hasLastSuccess = false;
  bool preferred = false;
};

struct NetworkScanDto {
  NetworkProfileRef profileRef = NetworkProfileRef::Unknown;
  NetworkSecurity security = NetworkSecurity::Unknown;
  std::int16_t rssi = -127;
  bool reachable = false;
  bool captivePortal = false;
};

struct NetworkSelectionDto {
  NetworkSelectionStatus status = NetworkSelectionStatus::InvalidInput;
  NetworkProfileRef selectedProfile = NetworkProfileRef::None;
  std::uint64_t retryAtMs = 0;
};

struct StorageSlotDto {
  bool occupied = false;
  bool committed = false;
  bool payloadParseable = false;
  std::uint32_t generation = 0;
  std::uint32_t checksum = 0;
  std::array<std::uint8_t, kStoragePayloadBytes> payload{};
  std::size_t payloadLength = 0;
  DeviceConfigDto config{};
};

struct StorageSelectionDto {
  StorageStatus status = StorageStatus::SafeMode;
  SlotId selectedSlot = SlotId::None;
  std::uint32_t generation = 0;
  std::uint8_t migratedFromVersion = 0;
  StorageRejectReason rejectionA = StorageRejectReason::None;
  StorageRejectReason rejectionB = StorageRejectReason::None;
  DeviceConfigDto config{};
};

struct ResolverInputDto {
  CapabilityClass capability = CapabilityClass::Mp3Icy;
  CandidateOutcome primaryOutcome = CandidateOutcome::Timeout;
  bool primaryReferenceOnly = false;
  bool hasLastKnownGood = false;
  CandidateOutcome lastKnownGoodOutcome = CandidateOutcome::Invalid;
  std::uint64_t nowMs = 0;
};

struct ResolverResultDto {
  ResolverStatus status = ResolverStatus::InvalidInput;
  ResolverSelection selected = ResolverSelection::None;
  std::uint64_t retryAtMs = 0;
  std::uint8_t scoreAfter = 100;
  std::uint64_t quarantineUntilMs = 0;
};

struct DiscoveryResponseDto {
  std::uint16_t statusCode = 0;
  ContentType contentType = ContentType::Unknown;
  std::size_t bodyBytes = 0;
  bool completeBody = false;
  bool expectedFieldPresent = false;
  bool endpointUsesHttps = false;
  std::size_t endpointBytes = 0;
};

struct DiscoveryParseDto {
  CandidateOutcome outcome = CandidateOutcome::Invalid;
  bool endpointAccepted = false;
};

struct OnboardingRouteDto {
  const char* path;
  HttpMethod method;
  ContentType contentType;
  std::size_t maximumBytes;
};

struct RedactedDiagnosticsDto {
  std::uint8_t schemaVersion = 1;
  std::uint8_t wifiProfileCount = 0;
  std::uint8_t preferredStationIndex = 0;
  Locale locale = Locale::Invalid;
  bool bluetoothRemembered = false;
  bool containsCredential = false;
};

struct UiSnapshotDto {
  std::uint8_t schemaVersion = 1;
  Screen screen = Screen::Wifi;
  RuntimeState systemState = RuntimeState::ConfigRequired;
  OutputKind output = OutputKind::LocalSpeaker;
  Locale locale = Locale::Pl;
  std::uint8_t stationIndex = 0;
  std::uint8_t requestedStationIndex = 0;
  std::uint8_t volume = 15;
  std::uint8_t brightness = 70;
  bool setupComplete = false;
};

struct GoldenNetworkVector {
  const char* scenarioId;
  NetworkProfileState profileState;
  std::uint64_t nowMs;
  std::array<NetworkScanDto, 2> scans;
  std::uint8_t scanCount;
  NetworkSelectionStatus expectedStatus;
  NetworkProfileRef expectedProfile;
  std::uint64_t expectedRetryAtMs;
};

struct GoldenPersistenceVector {
  const char* scenarioId;
  PersistenceSetup setup;
  WritePhase writePhase;
  StorageStatus expectedStatus;
  SlotId expectedSlot;
  std::uint32_t expectedGeneration;
  std::uint8_t expectedMigratedFromVersion;
};

struct GoldenResolverVector {
  const char* scenarioId;
  ResolverInputDto input;
  ResolverStatus expectedStatus;
  ResolverSelection expectedSelection;
  std::uint64_t expectedRetryAtMs;
};

}
