#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "open_radio/service_contracts.hpp"

namespace open_radio {

// Capacity of the on-screen now-playing title, sanitised from ICY metadata.
constexpr std::size_t kNowPlayingTitleBytes = 193;
// The panel, so hit boxes that must reach an edge can say so instead of
// repeating 320 and drifting from it.
constexpr std::int16_t kScreenWidthPx = 320;
constexpr std::size_t kBluetoothNameBytes = 33;

struct UiControllerUpdate {
  bool dirty = false;
  bool configChanged = false;
  bool stationChanged = false;
  bool wifiPortalToggleRequested = false;
  bool wifiPortalRestartRequested = false;
  bool bluetoothScanRequested = false;
  bool consoleSessionRequested = false;
  bool consoleSessionEndRequested = false;
  bool secureResetRequested = false;
  bool noiseModeRequested = false;
  bool radioModeRequested = false;
  bool noisePlaybackChanged = false;
  bool noiseColorChanged = false;
};

class UiController {
 public:
  UiController() = default;

  void begin(const DeviceConfigDto& config, RuntimeState runtime,
             OutputKind output, std::uint64_t nowMs) {
    config_ = config;
    runtime_ = runtime;
    output_ = output;
    screen_ = !config.onboardingComplete ||
                      runtime == RuntimeState::ConfigRequired
                  ? Screen::Wifi
                  : runtime == RuntimeState::SafeMode ? Screen::SafeMode
                                                      : Screen::NowPlaying;
    returnScreen_ = Screen::NowPlaying;
    lastInteractionMs_ = nowMs;
    lastAnimationMs_ = nowMs;
    pending_ = {};
    wifiOnline_ = config.onboardingComplete;
    wifiPortalActive_ = false;
    bluetoothState_ = config.bluetoothRemembered
                          ? BluetoothUiState::Connected
                          : BluetoothUiState::Idle;
    bluetoothCandidate_.fill(0);
    noiseMode_ = false;
    noisePlaying_ = false;
    noiseColor_ = NoiseColor::White;
    pending_.dirty = true;
  }

  const DeviceConfigDto& config() const { return config_; }
  Screen screen() const { return screen_; }
  RuntimeState runtime() const { return runtime_; }
  OutputKind output() const { return output_; }
  std::uint8_t stationIndex() const { return config_.preferredStationIndex; }
  std::uint8_t animationFrame() const { return animationFrame_; }
  bool confirmDelete() const { return confirmDelete_; }
  const char* nowPlayingTitle() const { return metadata_.data(); }
  bool metadataAvailable() const { return metadata_[0] != '\0'; }
  bool wifiOnline() const { return wifiOnline_; }
  bool wifiPortalActive() const { return wifiPortalActive_; }
  BluetoothUiState bluetoothState() const { return bluetoothState_; }
  const char* bluetoothCandidate() const { return bluetoothCandidate_.data(); }
  bool noiseMode() const { return noiseMode_; }
  bool noisePlaying() const { return noisePlaying_; }
  NoiseColor noiseColor() const { return noiseColor_; }

  void restoreNoiseState(NoiseColor color, bool resumeMode) {
    noiseColor_ = validNoiseColor(color) ? color : NoiseColor::White;
    noiseMode_ = resumeMode && config_.onboardingComplete;
    noisePlaying_ = noiseMode_;
    if (noiseMode_) {
      screen_ = Screen::Noise;
      returnScreen_ = Screen::NowPlaying;
    }
    markDirty();
  }
  void setWifiPortalActive(bool active) {
    if (wifiPortalActive_ == active) return;
    wifiPortalActive_ = active;
    markDirty();
  }

  void setWifiOnline(bool online) {
    if (wifiOnline_ == online) return;
    wifiOnline_ = online;
    markDirty();
  }

  void setBluetoothState(BluetoothUiState state, const char* candidate = nullptr) {
    std::array<char, kBluetoothNameBytes> sanitized{};
    sanitizeBounded(candidate, sanitized);
    if (bluetoothState_ == state && bluetoothCandidate_ == sanitized) return;
    bluetoothState_ = state;
    bluetoothCandidate_ = sanitized;
    markDirty();
  }

  void completeNetworkOnboarding() {
    config_.wifiProfiles[0] = NetworkProfileRef::Home;
    config_.wifiProfileCount = 1;
    config_.onboardingComplete = true;
    pending_.configChanged = true;
    markDirty();
  }

  void requireNetworkOnboarding() {
    const bool configChanged = config_.onboardingComplete ||
                               config_.wifiProfileCount != 0;
    config_.wifiProfileCount = 0;
    config_.onboardingComplete = false;
    screen_ = Screen::Wifi;
    if (configChanged) pending_.configChanged = true;
    markDirty();
  }

  void rememberBluetooth() {
    if (config_.bluetoothRemembered) return;
    config_.bluetoothRemembered = true;
    pending_.configChanged = true;
    markDirty();
  }

  void forgetBluetooth() {
    if (config_.bluetoothRemembered) {
      config_.bluetoothRemembered = false;
      pending_.configChanged = true;
      markDirty();
    }
    setBluetoothState(BluetoothUiState::Idle);
  }


  void setConsoleActive(bool active) {
    if (consoleActive_ == active) return;
    consoleActive_ = active;
    markDirty();
  }

  void setRuntime(RuntimeState runtime, OutputKind output) {
    if (runtime_ == runtime && output_ == output) return;
    runtime_ = runtime;
    output_ = output;
    if (runtime == RuntimeState::SafeMode) screen_ = Screen::SafeMode;
    else if (runtime == RuntimeState::UnsupportedStation) screen_ = Screen::Unsupported;
    else if (runtime == RuntimeState::ConfigRequired) screen_ = Screen::Wifi;
    markDirty();
  }

  // Some stations put CONTROL MARKERS in StreamTitle rather than a track name.
  // RMF FM sends STOP_AD_BREAK, and it reached the screen under the heading
  // "Teraz gra" — found by capturing the cube's own framebuffer on 2026-07-29,
  // which is a defect nothing in this tree could previously have seen, because
  // every check reads a host render and the host was never handed that string.
  //
  // The rule is deliberately narrow: a marker is ALL CAPS with underscores and
  // no spaces. A real title has spaces and mixed case, so "AC_DC" would be the
  // only plausible casualty and it is not how anybody writes it. Anything
  // broader would start eating legitimate titles, and showing nothing is better
  // than showing a machine token but worse than showing a real name.
  static bool looksLikeControlMarker(const char* value) {
    if (value == nullptr || value[0] == '\0') return false;
    bool sawUnderscore = false;
    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
      const unsigned char character = static_cast<unsigned char>(*cursor);
      if (character == '_') { sawUnderscore = true; continue; }
      if (character >= 'A' && character <= 'Z') continue;
      if (character >= '0' && character <= '9') continue;
      return false;
    }
    return sawUnderscore;
  }

  void setNowPlayingTitle(const char* title) {
    std::array<char, kNowPlayingTitleBytes> sanitized{};
    if (!looksLikeControlMarker(title)) sanitize(title, sanitized);
    if (std::strncmp(metadata_.data(), sanitized.data(), metadata_.size()) == 0) {
      return;
    }
    metadata_ = sanitized;
    markDirty();
  }

  void tap(std::int16_t x, std::int16_t y, std::uint64_t nowMs) {
    if (wakeDisplay(nowMs)) return;
    // One contact, one tap — whatever the panel says.
    //
    // The FT6336 occasionally reports a single touch twice, and the click latch
    // can deliver its synthetic click and the library's own classification one
    // loop apart. Either way the controller receives two taps at the same spot
    // tens of milliseconds apart, and what that DOES depends entirely on what
    // the first tap opened. For a day the pair was survivable: the menu button
    // sat at y=34..89, so the ghost landed on a settings tile and the menu was
    // merely "confusing" — sometimes opening Bluetooth nobody asked for. On
    // 2026-07-30 the menu moved into the very corner, the ghost started landing
    // in the back shortcut below, and the menu began CLOSING THE INSTANT IT
    // OPENED. The owner's exact words, and the layout change did not create the
    // fault — it weaponised one that had been misread as "hit boxes" all day.
    //
    // 150 ms and 16 px: a double-report arrives within ~100 ms at the same
    // coordinates; the fastest deliberate human repeat-tap is slower and a
    // repeat on a stepper lands well past 150 ms. A real second intent is not
    // deduplicated, a ghost is.
    if (anyTap_ && nowMs - lastTapMs_ < kTapDedupeMs &&
        magnitude(x - lastTapX_) <= 16 && magnitude(y - lastTapY_) <= 16) {
      return;
    }
    anyTap_ = true;
    lastTapMs_ = nowMs;
    lastTapX_ = x;
    lastTapY_ = y;
    const Screen before = screen_;
    // ONE corner, 268..319 by 0..63, everywhere. It was y<48 while the menu
    // button that opens screens from home reached y<64, and the trace of
    // 2026-07-30 caught the owner's finger living exactly in the difference:
    // the SAME spot pressed twice — (319,50) opened the menu, (304,48) missed
    // both the y<48 shortcut and the 44-pixel close box under it, fell through
    // to the tile row, and opened Bluetooth nobody asked for. A corner that
    // means ARRIVE at y=50 must mean LEAVE at y=50, or aim itself becomes a
    // coin toss.
    if (x >= 268 && y < 64) {
      // The top-right corner is "leave this screen" on every screen but home —
      // and on home it is the menu button, in the same place. So a ghost that
      // survives the dedupe by drifting lands in the corner as LEAVE within
      // milliseconds of the corner having been ARRIVE. No person opens a screen
      // and closes it in a third of a second; a ghost does nothing else.
      if (anyScreenChange_ && nowMs - lastScreenChangeMs_ < kBackGraceMs) {
        return;
      }
      if (screen_ != Screen::NowPlaying) {
        back();
        noteScreenChange(before, nowMs);
        return;
      }
    }
    dispatchTap(x, y);
    noteScreenChange(before, nowMs);
  }

  void hold(std::int16_t, std::int16_t, std::uint64_t nowMs) {
    // A long press only wakes the display; there is nothing to save any more.
    wakeDisplay(nowMs);
  }

  void drag(std::int16_t x, std::int16_t y, std::uint64_t nowMs) {
    lastInteractionMs_ = nowMs;
    if (!inside(x, y, 20, 78, 280, 72)) return;
    const int value = percentAt(x);
    if (screen_ == Screen::Volume) setVolume(value, false);
    else if (screen_ == Screen::Brightness) setBrightness(value, false);
  }

  bool release(std::int16_t x, std::int16_t y, std::uint64_t nowMs) {
    lastInteractionMs_ = nowMs;
    if (!inside(x, y, 20, 78, 280, 72)) return false;
    const int value = percentAt(x);
    if (screen_ == Screen::Volume) setVolume(value, true);
    else if (screen_ == Screen::Brightness) setBrightness(value, true);
    else return false;
    return true;
  }

  void hardwareButton(HardwareButton button, std::uint64_t nowMs) {
    if (wakeDisplay(nowMs)) return;
    if (screen_ == Screen::Noise) {
      if (button == HardwareButton::A) changeNoiseColor(-1);
      else if (button == HardwareButton::B) returnToRadio();
      else changeNoiseColor(1);
      return;
    }
    // The picker is touch-only: the projected tap at y=210 would land in the
    // third tile row and select a station the owner never pointed at.
    if (screen_ == Screen::Stations) return;
    const std::int16_t projectedX =
        button == HardwareButton::A ? 40
                                    : button == HardwareButton::B ? 160 : 280;
    // The three pads go to the transport on EVERY home screen. Only the full
    // home draws a transport bar, so on the other two the projected tap landed
    // at y=210 where nothing responds and the pads did nothing at all — which
    // is what the owner reported on 2026-07-30. A physical button that works on
    // one screen and not another is not a physical button.
    if (screen_ == Screen::NowPlaying) {
      activateTransport(projectedX);
      return;
    }
    const Screen before = screen_;
    dispatchTap(projectedX, 210);
    noteScreenChange(before, nowMs);
  }

  void swipe(std::int16_t startX, std::int16_t deltaX,
             std::int16_t deltaY, std::uint64_t nowMs) {
    if (wakeDisplay(nowMs)) return;
    if (std::abs(deltaX) < 28 || std::abs(deltaX) < std::abs(deltaY)) return;
    const Screen beforeSwipe = screen_;
    if (startX <= 24 && deltaX > 0) {
      back();
      noteScreenChange(beforeSwipe, nowMs);
      return;
    }
    if (screen_ == Screen::NowPlaying) {
      changeStation(deltaX < 0 ? 1 : -1);
    } else if (screen_ == Screen::SettingsQuick ||
               screen_ == Screen::SettingsSystem ||
               screen_ == Screen::SettingsDisplay) {
      changeSettingsPage(deltaX < 0 ? 1 : -1);
    }
    noteScreenChange(beforeSwipe, nowMs);
  }

  // A cube with a live environment module keeps its screen on, whatever the
  // stored preference says.
  //
  // This is a RULE, not a default, and the distinction is the point. 0.3 exists
  // partly to stop changing stored choices under people, so flipping somebody's
  // displayOffEnabled to false would be the exact thing that release forbids —
  // and a default change does nothing at all for a cube that already stored
  // "on", which is why the owner still watched his screen go dark after the
  // defaults were set to off.
  //
  // The rule holds only while a module is publishing. Unplug it and the stored
  // preference takes over again, untouched, which is what makes this different
  // from overwriting it.
  void setSensorPresent(bool present) { sensorPresent_ = present; }

  // The home screen the device is actually SHOWING, which is not always the one
  // stored.
  //
  // Two of the three homes exist to show the air. Without a module they are a
  // clock over an empty half-screen, and the owner's objection to that is the
  // right one: somebody who has not bought a sensor should never be able to
  // reach a panel whose whole subject is missing. So the two air homes are
  // available only while something is publishing.
  //
  // The stored preference is NOT rewritten — same rule as the display timeout
  // above, and for the same reason. Unplug the module and the cube shows the
  // full radio; plug it back in and the minimal screen the owner chose is
  // simply there again, because nothing ever overwrote his choice.
  NowPlayingVariant homeVariant() const {
    return sensorPresent_ ? config_.nowPlayingVariant : NowPlayingVariant::Editorial;
  }

  void tick(std::uint64_t nowMs, bool animationAllowed) {
    const std::uint64_t idle = nowMs >= lastInteractionMs_ ? nowMs - lastInteractionMs_ : 0;
    if (screen_ != Screen::DisplayOff && !sensorPresent_ && config_.displayOffEnabled &&
        idle >= static_cast<std::uint64_t>(config_.displayOffDelaySeconds) * 1000U) {
      screen_ = Screen::DisplayOff;
      previewScreensaver_ = false;
      markDirty();
      return;
    }
    if (screen_ != Screen::Screensaver && screen_ != Screen::DisplayOff &&
        !sensorPresent_ && config_.screensaverEnabled &&
        idle >= static_cast<std::uint64_t>(config_.screensaverDelaySeconds) * 1000U) {
      screen_ = Screen::Screensaver;
      previewScreensaver_ = false;
      lastAnimationMs_ = nowMs;
      markDirty();
      return;
    }
    if (screen_ == Screen::Screensaver && animationAllowed &&
        nowMs - lastAnimationMs_ >= 83U) {
      lastAnimationMs_ = nowMs;
      ++animationFrame_;
      markDirty();
    }
  }

  UiControllerUpdate takeUpdate() {
    const auto update = pending_;
    pending_ = {};
    return update;
  }

 private:
  static bool inside(std::int16_t x, std::int16_t y, int left, int top,
                     int width, int height) {
    return x >= left && y >= top && x < left + width && y < top + height;
  }

  static bool validNoiseColor(NoiseColor color) {
    return color == NoiseColor::White || color == NoiseColor::Pink ||
           color == NoiseColor::Brown;
  }

  static void sanitize(const char* source,
                       std::array<char, kNowPlayingTitleBytes>& destination) {
    destination.fill(0);
    if (source == nullptr) return;
    std::size_t written = 0;
    bool previousSpace = true;
    for (const auto* cursor = reinterpret_cast<const unsigned char*>(source);
         *cursor != 0U && written + 1 < destination.size(); ++cursor) {
      unsigned char value = *cursor;
      if (value < 0x20U || value == 0x7fU) value = ' ';
      if (value == ' ') {
        if (previousSpace) continue;
        previousSpace = true;
      } else {
        previousSpace = false;
      }
      destination[written++] = static_cast<char>(value);
    }
    while (written > 0 && destination[written - 1] == ' ') --written;
    std::size_t continuation_start = written;
    while (continuation_start > 0 &&
           (static_cast<unsigned char>(destination[continuation_start - 1]) &
            0xc0U) == 0x80U) {
      --continuation_start;
    }
    if (continuation_start < written && continuation_start > 0) {
      const auto lead = static_cast<unsigned char>(destination[continuation_start - 1]);
      const std::size_t expected = (lead & 0xe0U) == 0xc0U
                                       ? 2
                                       : (lead & 0xf0U) == 0xe0U
                                             ? 3
                                             : (lead & 0xf8U) == 0xf0U ? 4 : 1;
      const std::size_t actual = written - (continuation_start - 1);
      if (actual < expected) written = continuation_start - 1;
    }
    destination[written] = '\0';
  }

  template <std::size_t Size>
  static void sanitizeBounded(const char* source,
                              std::array<char, Size>& destination) {
    destination.fill(0);
    if (source == nullptr) return;
    std::size_t written = 0;
    for (const auto* cursor = reinterpret_cast<const unsigned char*>(source);
         *cursor != 0U && written + 1 < destination.size(); ++cursor) {
      const unsigned char value = *cursor;
      destination[written++] = value >= 0x20U && value != 0x7fU
                                   ? static_cast<char>(value)
                                   : ' ';
    }
    destination[written] = '\0';
  }

  bool wakeDisplay(std::uint64_t nowMs) {
    lastInteractionMs_ = nowMs;
    if (screen_ != Screen::DisplayOff && screen_ != Screen::Screensaver) {
      return false;
    }
    setScreen(previewScreensaver_ ? Screen::SettingsDisplay
                                  : noiseMode_ ? Screen::Noise
                                               : Screen::NowPlaying);
    previewScreensaver_ = false;
    return true;
  }

  void markDirty() { pending_.dirty = true; }

  static std::int16_t magnitude(std::int16_t value) {
    return value < 0 ? static_cast<std::int16_t>(-value) : value;
  }

  // Records WHEN the visible screen last changed, which is what the corner
  // grace in tap() measures against. Called by every input path that can
  // change it, with the screen as it was before the input ran.
  void noteScreenChange(Screen before, std::uint64_t nowMs) {
    if (screen_ == before) return;
    anyScreenChange_ = true;
    lastScreenChangeMs_ = nowMs;
  }

  void setScreen(Screen screen) {
    if (screen_ == screen) return;
    if (screen != Screen::Screensaver && screen != Screen::DisplayOff) {
      returnScreen_ = screen_;
    }
    screen_ = screen;
    confirmDelete_ = false;
    markDirty();
  }

  void back() {
    if (screen_ == Screen::NowPlaying) return;
    if (screen_ == Screen::Noise) {
      returnToRadio();
      return;
    }
    if (screen_ == Screen::Stations ||
        screen_ == Screen::SettingsQuick || screen_ == Screen::SettingsSystem ||
        screen_ == Screen::SettingsDisplay) {
      setScreen(Screen::NowPlaying);
    } else {
      setScreen(returnScreen_ == screen_ ? Screen::NowPlaying : returnScreen_);
    }
  }


  void dispatchTap(std::int16_t x, std::int16_t y) {
    switch (screen_) {
      case Screen::NowPlaying:
        tapNowPlaying(x, y);
        break;
      case Screen::Stations:
        tapStations(x, y);
        break;
      case Screen::Volume:
        tapVolume(x, y);
        break;
      case Screen::Brightness:
        tapBrightness(x, y);
        break;
      case Screen::SettingsQuick:
      case Screen::SettingsSystem:
      case Screen::SettingsDisplay:
        tapSettings(x, y);
        break;
      case Screen::Noise:
        tapNoise(x, y);
        break;
      case Screen::Sensors:
        // UNREACHABLE — MARKED FOR REMOVAL (2026-07-31, before the 0.4 freeze).
        // Nothing in the firmware calls setScreen(Screen::Sensors); the air
        // homes carry the readings instead. It costs nothing at runtime, which
        // is the only reason it was left in a release being frozen: taking it
        // out touches ten files including the contract enum in
        // service_contracts.hpp, both i18n dictionaries, text-slots.json,
        // check-claims, the geometry gate's screen list and the renderer
        // goldens. Remove it in 0.5 as one deliberate change, or route to it —
        // but do not leave it half-removed.
        //
        // Read-only by design anyway: the close box in the top right is handled by the
        // shared rule above, and there is nothing on this screen to press.
        // Calibration belongs to the console, where an explicit act can be
        // explained; a tile that silently rebaselined a sensor would be the
        // opposite of what the ASC decision was for.
        break;
      case Screen::Wifi:
        if (consoleActive_ && !wifiPortalActive_ && y >= 184) {
          // The console screen's bottom button closes the session. With the
          // portal active the renderer shows the PORTAL controls, so the tap
          // must keep meaning what the screen shows.
          pending_.consoleSessionEndRequested = true;
          markDirty();
          break;
        }
        if (config_.onboardingComplete && y >= 184) {
          if (!wifiPortalActive_) {
            wifiPortalActive_ = true;
            pending_.wifiPortalToggleRequested = true;
          } else if (x < 160) {
            pending_.wifiPortalRestartRequested = true;
          } else if (wifiOnline_) {
            wifiPortalActive_ = false;
            pending_.wifiPortalToggleRequested = true;
          }
          markDirty();
        } else if (y >= 184 && x >= 104) {
          // Wi-Fi is the only mandatory onboarding gate. Do not allow touch or
          // the projected B/C hardware buttons to skip it while offline.
          if (wifiOnline_) setScreen(Screen::City);
        } else if (y >= 184) {
          toggleLocale();
        }
        break;
      case Screen::City:
        if (y >= 184 && x < 104) setScreen(Screen::Wifi);
        else if (y >= 184) setScreen(Screen::Bluetooth);
        break;
      case Screen::Bluetooth:
        if (config_.onboardingComplete) {
          if (y >= 184 && bluetoothState_ != BluetoothUiState::Scanning &&
              bluetoothState_ != BluetoothUiState::Connecting) {
            setBluetoothState(BluetoothUiState::Scanning);
            pending_.bluetoothScanRequested = true;
          }
        } else if (y >= 184 && x >= 104) {
          config_.onboardingComplete = true;
          configChanged();
          setBluetoothState(BluetoothUiState::Scanning);
          pending_.bluetoothScanRequested = true;
        } else if (y >= 184) {
          config_.onboardingComplete = true;
          config_.bluetoothRemembered = false;
          configChanged();
          setScreen(Screen::NowPlaying);
        }
        break;
      case Screen::About:
        if (y >= 184) setScreen(Screen::Diagnostics);
        break;
      case Screen::Diagnostics:
      case Screen::Unsupported:
      case Screen::SafeMode:
        if (y >= 184) setScreen(screen_ == Screen::Unsupported
                                    ? Screen::Stations
                                    : screen_ == Screen::SafeMode
                                          ? Screen::Diagnostics
                                          : Screen::NowPlaying);
        break;
      default:
        if (y >= 184) setScreen(Screen::NowPlaying);
        break;
    }
  }

  void activateTransport(std::int16_t x) {
    if (x < 64) changeStation(-1);
    else if (x < 256) setScreen(Screen::Stations);
    else changeStation(1);
  }

  void tapNowPlaying(std::int16_t x, std::int16_t y) {
    // The menu reaches the right edge of the glass and a little further down
    // than its icon.
    //
    // It used to be a 44x44 box at x 268, which stops at 311 on a 320-wide
    // screen: eight dead columns along the very edge, in the corner, under the
    // one control people reach for by feel. Measured on hardware 2026-07-29,
    // the owner's taps came back as 308,47 — inside by one pixel — and 319,56,
    // outside by eight columns and nothing else. It read as an unreliable
    // touchscreen and was a hit box that did not reach the corner it lived in.
    //
    // A capacitive panel reports the centroid of the contact patch, which sits
    // lower and further out than where a person believes they are pointing, so
    // a target in a corner has to extend TO the corner. Widened to the edge and
    // given more height; the top edge is unchanged, so nothing above it moves.
    // Every box below corresponds to something DRAWN, and the correspondence is
    // the whole point. On 2026-07-30 the full home's layout changed — the menu
    // moved up, volume became a ring in the opposite corner, the "playing
    // reliably" chip was deleted — and these boxes stayed where the old
    // elements had been. The result on the device: a press at the top of the
    // menu button did nothing, a press on empty canvas below it opened the
    // menu, and a patch of the metadata card silently opened Bluetooth. Three
    // invisible controls, which is the worst kind, because they fire on a tap
    // aimed at something else.
    //
    // Neither audit could see it. The geometry audit knows where ink is; the
    // touch audit knows where targets are; nothing compared the two. That gap
    // is now its own check (testEveryHomeTargetHasSomethingDrawn).
    if (inside(x, y, 268, 0, 52, 64)) {
      // Reaches the top-right corner on all three homes, because all three draw
      // the menu button there now. It began at y=8 and the touch audit reported
      // eight rows of dead glass above the control people press most often — a
      // capacitive panel reports the centroid of a contact patch, so a target
      // in a corner has to extend TO the corner.
      setScreen(Screen::SettingsQuick);
    } else if (homeVariant() == NowPlayingVariant::Editorial &&
               inside(x, y, 104, 0, 128, 56)) {
      // The miniature volume scale (ticks 112..194, the figure to about
      // 224) that replaced the ring on 2026-07-30. The box reaches the top
      // edge for the centroid reason; left it overlaps the clock's empty
      // clip, right it stops exactly where the battery cell begins at 232 —
      // the cell reports, it does not invite.
      setScreen(Screen::Volume);
    } else if (y >= 184 &&
               homeVariant() == NowPlayingVariant::Editorial) {
      // The transport strip belongs to the FULL home, which is the only variant
      // that draws it. On the minimal and air screens the bottom of the panel
      // was still changing station with nothing drawn there at all — an
      // invisible control, and the worst kind, because it fires on a tap aimed
      // at something else.
      activateTransport(x);
    }
  }

  void tapVolume(std::int16_t x, std::int16_t y) {
    if (inside(x, y, 20, 78, 280, 72)) {
      setVolume(percentAt(x), true);
    } else if (y >= 184 && x < 96) {
      setVolume(static_cast<int>(config_.volume) - 10);
    } else if (y >= 184 && x >= 224) {
      setVolume(static_cast<int>(config_.volume) + 10);
    } else if (y >= 184) {
      back();
    }
  }

  void tapBrightness(std::int16_t x, std::int16_t y) {
    if (inside(x, y, 20, 78, 280, 72)) {
      setBrightness(percentAt(x), true);
    } else if (y >= 184 && x < 96) {
      setBrightness(static_cast<int>(config_.brightness) - 10, true);
    } else if (y >= 184 && x >= 224) {
      setBrightness(static_cast<int>(config_.brightness) + 10, true);
    } else if (y >= 184) {
      back();
    }
  }

  void tapStations(std::int16_t x, std::int16_t y) {
    // The picker is a centred 3x3 of 64-pixel tiles, but its TARGETS are the
    // whole panel divided between them.
    //
    // It used to accept only the grid itself — x from 52 to 267, y from 12 to
    // 227 — which left fifty-two dead columns down each side and a dead strip
    // top and bottom. Aiming at the leftmost tile from the left, which is what
    // a thumb does, hit nothing at all. Reported as "ciężko się klika wybór
    // stacji", and it was: a quarter of the glass did nothing.
    //
    // The boundaries sit at the midpoints between neighbouring tiles, so every
    // pixel belongs to whichever tile it is nearest and each target is about
    // 107 by 80 instead of 72 by 72.
    const int column = x < 124 ? 0 : x < 196 ? 1 : 2;
    const int row = y < 84 ? 0 : y < 156 ? 1 : 2;
    const int index = row * 3 + column;
    if (index >= 0 && index < 9) {
      config_.preferredStationIndex = static_cast<std::uint8_t>(index);
      // ICY metadata belongs to the stream that emitted it. Never keep the
      // previous station's title visible while the new stream connects.
      metadata_.fill(0);
      pending_.configChanged = true;
      pending_.stationChanged = true;
      markDirty();
      setScreen(Screen::NowPlaying);
    }
  }


  void tapSettings(std::int16_t x, std::int16_t y) {
    if (inside(x, y, 216, 0, 44, 48)) {
      openNoise();
      return;
    }
    // The readings used to have their own button here. Removed by owner decision
    // 2026-07-29: air belongs on the HOME screens, not behind a settings icon.
    // A person does not go looking for the air in a menu; they see it while
    // they are looking at something else, or they never see it at all. The
    // Sensors screen itself still exists and is still rendered — it is reached
    // by cycling the home variant, which is where every other display choice
    // already lives.
    if (y >= 184) {
      const int page = std::clamp<int>((x - 8) / 104, 0, 2);
      setScreen(page == 0 ? Screen::SettingsQuick
                          : page == 1 ? Screen::SettingsSystem
                                      : Screen::SettingsDisplay);
      return;
    }
    if (y < 48 || y >= 180) return;
    const int column = x < 160 ? 0 : 1;
    const int row = (y - 48) / 44;
    const int index = row * 2 + column;
    if (screen_ == Screen::SettingsQuick) activateQuick(index);
    else if (screen_ == Screen::SettingsSystem) activateSystem(index);
    else activateDisplay(index);
  }

  void tapNoise(std::int16_t x, std::int16_t y) {
    if (inside(x, y, 108, 50, 104, 104)) {
      noisePlaying_ = !noisePlaying_;
      pending_.noisePlaybackChanged = true;
      markDirty();
      return;
    }
    // The three colours tile the FULL width with no gaps and no dead columns.
    // White began at x=8 and was reported as harder to hit than the other two —
    // the same defect as the menu button this morning, and for the same reason:
    // a capacitive panel reports the centroid of the contact patch, which sits
    // further out than where a person believes they are pointing, so a target
    // at an edge has to extend TO the edge. Eight columns of glass were not
    // reaching anything.
    //
    // Taller too, 54 against 40. These are the controls of the screen; nothing
    // else on it competes for the space.
    if (inside(x, y, 0, 160, 106, 54)) selectNoiseColor(NoiseColor::White);
    else if (inside(x, y, 106, 160, 108, 54)) {
      selectNoiseColor(NoiseColor::Pink);
    } else if (inside(x, y, 214, 160, 106, 54)) {
      selectNoiseColor(NoiseColor::Brown);
    }
  }

  void openNoise() {
    noiseMode_ = true;
    noisePlaying_ = true;
    pending_.noiseModeRequested = true;
    pending_.noisePlaybackChanged = true;
    setScreen(Screen::Noise);
  }

  void returnToRadio() {
    noiseMode_ = false;
    noisePlaying_ = false;
    pending_.radioModeRequested = true;
    pending_.noisePlaybackChanged = true;
    setScreen(Screen::NowPlaying);
  }

  void selectNoiseColor(NoiseColor color) {
    if (!validNoiseColor(color) || noiseColor_ == color) return;
    noiseColor_ = color;
    pending_.noiseColorChanged = true;
    markDirty();
  }

  void changeNoiseColor(int delta) {
    const int current = static_cast<int>(noiseColor_);
    selectNoiseColor(static_cast<NoiseColor>((current + delta + 3) % 3));
  }

  void activateQuick(int index) {
    if (index == 0) setScreen(Screen::Wifi);
    else if (index == 1) setScreen(Screen::Bluetooth);
    else if (index == 2) setScreen(Screen::Volume);
    else if (index == 3) setScreen(Screen::Brightness);
    else if (index == 4) config_.theme = config_.theme == DisplayTheme::Dark
                                             ? DisplayTheme::Light
                                             : DisplayTheme::Dark;
    else if (index == 5) toggleLocale();
    if (index >= 4) configChanged();
  }

  void activateSystem(int index) {
    if (index == 0) {
      // Three home screens, cycled in order: the radio, the radio minimal with
      // the air beneath it, and the air with the radio reduced to a mark.
      //
      // This toggled between TWO for a day after the third was written, which
      // is why the owner reported it missing: every pixel of it existed, the
      // renderer drew it, the tests covered it, and no sequence of taps on the
      // device could reach it. A screen nothing routes to is not a screen.
      // Nothing to cycle through without a module: the other two homes are
      // about the air, and there is none to show. The tile says so rather than
      // becoming a control that does nothing when pressed.
      if (!sensorPresent_) return;
      config_.nowPlayingVariant =
          homeVariant() == NowPlayingVariant::Editorial
              ? NowPlayingVariant::Glance
          : homeVariant() == NowPlayingVariant::Glance
              ? NowPlayingVariant::Air
              : NowPlayingVariant::Editorial;
      configChanged();
    } else if (index == 1) setScreen(Screen::Wifi);
    else if (index == 2) setScreen(Screen::About);
    else if (index == 3) setScreen(Screen::Diagnostics);
    else if (index == 4) {
      if (!confirmDelete_) {
        confirmDelete_ = true;
        markDirty();
      } else {
        pending_.secureResetRequested = true;
        markDirty();
      }
    } else if (index == 5) {
      // Console session: the firmware opens the 15-minute browser window and
      // the Wi-Fi screen shows the address to type.
      pending_.consoleSessionRequested = true;
      setScreen(Screen::Wifi);
    }
  }

  void activateDisplay(int index) {
    if (index == 0) config_.screensaverEnabled = !config_.screensaverEnabled;
    else if (index == 1) {
      constexpr std::array<std::uint16_t, 5> values{{30, 60, 120, 300, 600}};
      config_.screensaverDelaySeconds = nextValue(values, config_.screensaverDelaySeconds);
    } else if (index == 2) {
      config_.screensaverMode =
          config_.screensaverMode == ScreensaverMode::Pulse
              ? ScreensaverMode::Bars
              : config_.screensaverMode == ScreensaverMode::Bars
                    ? ScreensaverMode::Orbit
                    : config_.screensaverMode == ScreensaverMode::Orbit
                          ? ScreensaverMode::Cat
                          : ScreensaverMode::Pulse;
    } else if (index == 3) config_.displayOffEnabled = !config_.displayOffEnabled;
    else if (index == 4) {
      constexpr std::array<std::uint16_t, 3> values{{900, 1800, 3600}};
      config_.displayOffDelaySeconds = nextValue(values, config_.displayOffDelaySeconds);
    } else if (index == 5) {
      previewScreensaver_ = true;
      screen_ = Screen::Screensaver;
      animationFrame_ = 0;
      markDirty();
      return;
    }
    configChanged();
  }

  void changeStation(int delta) {
    const int current = config_.preferredStationIndex;
    config_.preferredStationIndex = static_cast<std::uint8_t>((current + delta + 9) % 9);
    metadata_.fill(0);
    pending_.configChanged = true;
    pending_.stationChanged = true;
    markDirty();
  }

  void changeSettingsPage(int delta) {
    int page = screen_ == Screen::SettingsQuick
                   ? 0
                   : screen_ == Screen::SettingsSystem ? 1 : 2;
    page = (page + delta + 3) % 3;
    setScreen(page == 0 ? Screen::SettingsQuick
                        : page == 1 ? Screen::SettingsSystem
                                    : Screen::SettingsDisplay);
  }


  void toggleLocale() {
    config_.locale = config_.locale == Locale::Pl ? Locale::En : Locale::Pl;
    configChanged();
  }

  void configChanged() {
    pending_.configChanged = true;
    markDirty();
  }

  static int percentAt(std::int16_t x) {
    return std::clamp<int>((x - 24) * 100 / 272, 0, 100);
  }

  void setVolume(int value, bool persist = true) {
    const auto clamped = static_cast<std::uint8_t>(std::clamp(value, 0, 100));
    if (config_.volume != clamped) {
      config_.volume = clamped;
      markDirty();
    }
    if (persist) configChanged();
  }

  void setBrightness(int value, bool persist = true) {
    const auto clamped = static_cast<std::uint8_t>(std::clamp(value, 0, 100));
    if (config_.brightness != clamped) {
      config_.brightness = clamped;
      markDirty();
    }
    if (persist) configChanged();
  }




  template <std::size_t Size>
  static std::uint16_t nextValue(const std::array<std::uint16_t, Size>& values,
                                 std::uint16_t current) {
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (values[index] == current) return values[(index + 1) % values.size()];
    }
    return values[0];
  }

  DeviceConfigDto config_{};
  RuntimeState runtime_ = RuntimeState::ConfigRequired;
  OutputKind output_ = OutputKind::LocalSpeaker;
  Screen screen_ = Screen::Wifi;
  Screen returnScreen_ = Screen::NowPlaying;
  std::array<char, kNowPlayingTitleBytes> metadata_{};
  std::uint8_t animationFrame_ = 0;
  bool confirmDelete_ = false;
  bool previewScreensaver_ = false;
  bool sensorPresent_ = false;
  // Ghost-tap suppression (see tap()). anyTap_/anyScreenChange_ exist so the
  // FIRST tap of a session can never be deduplicated against millisecond zero.
  static constexpr std::uint64_t kTapDedupeMs = 150;
  static constexpr std::uint64_t kBackGraceMs = 350;
  bool anyTap_ = false;
  std::uint64_t lastTapMs_ = 0;
  std::int16_t lastTapX_ = 0;
  std::int16_t lastTapY_ = 0;
  bool anyScreenChange_ = false;
  std::uint64_t lastScreenChangeMs_ = 0;
  bool wifiOnline_ = false;
  bool wifiPortalActive_ = false;
  bool consoleActive_ = false;
  BluetoothUiState bluetoothState_ = BluetoothUiState::Idle;
  std::array<char, kBluetoothNameBytes> bluetoothCandidate_{};
  NoiseColor noiseColor_ = NoiseColor::White;
  bool noiseMode_ = false;
  bool noisePlaying_ = false;
  std::uint64_t lastInteractionMs_ = 0;
  std::uint64_t lastAnimationMs_ = 0;
  UiControllerUpdate pending_{};
};

}  // namespace open_radio
