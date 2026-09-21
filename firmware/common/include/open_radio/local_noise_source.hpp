#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "open_radio/firmware_contract.hpp"
#include "open_radio/service_contracts.hpp"

namespace open_radio {

// Host-portable local PCM source. The caller owns the queue and supplies the
// seed, which lets firmware use esp_random() without coupling this class to
// Arduino or ESP-IDF. All colors target -14 dBFS RMS before the existing
// device-volume stage.
class LocalNoiseSource {
 public:
  static constexpr std::size_t kFramesPerOwnerLoop = 8192;
  static constexpr std::size_t kFadeInFrames = kPcmSampleRate / 4;
  static constexpr std::size_t kFadeOutFrames = kPcmSampleRate * 3 / 20;

  void start(NoiseColor color, std::uint32_t seed) {
    color_ = validColor(color) ? color : NoiseColor::White;
    pendingColor_ = color_;
    randomState_ = seed == 0 ? kFallbackSeed : seed;
    resetFilters();
    envelope_ = 0.0F;
    fadeStartEnvelope_ = 0.0F;
    envelopeFrame_ = 0;
    state_ = State::FadingIn;
  }

  void requestStop() {
    if (state_ == State::Stopped || state_ == State::FadingOutToStop) return;
    beginFadeOut(State::FadingOutToStop);
  }

  void setColor(NoiseColor color) {
    if (!validColor(color)) return;
    pendingColor_ = color;
    if (state_ == State::Stopped) {
      color_ = color;
      resetFilters();
      return;
    }
    if (color == color_ && state_ != State::FadingOutToColor) return;
    if (state_ != State::FadingOutToStop) {
      beginFadeOut(State::FadingOutToColor);
    }
  }

  template <typename PushFrame>
  std::size_t generate(PushFrame&& pushFrame,
                       std::size_t requestedFrames = kFramesPerOwnerLoop) {
    const std::size_t budget =
        std::min(requestedFrames, kFramesPerOwnerLoop);
    std::size_t generated = 0;
    while (generated < budget && state_ != State::Stopped) {
      const float sample = std::clamp(nextColoredSample() * nextEnvelope(),
                                      -1.0F, 1.0F);
      const auto pcm = static_cast<std::int16_t>(
          std::lround(sample * static_cast<float>(INT16_MAX)));
      if (!pushFrame(PcmFrame{pcm, pcm})) break;
      ++generated;
      completeEnvelopeFrame();
    }
    return generated;
  }

  bool active() const { return state_ != State::Stopped; }
  bool steady() const { return state_ == State::Playing; }
  NoiseColor color() const { return color_; }
  float envelope() const { return envelope_; }

 private:
  enum class State : std::uint8_t {
    Stopped,
    FadingIn,
    Playing,
    FadingOutToStop,
    FadingOutToColor
  };

  static constexpr std::uint32_t kFallbackSeed = 0x6d2b79f5U;
  static constexpr float kWhiteGain = 0.34555F;
  // Calibrated against the steady-state RMS of the compact three-pole Paul
  // Kellet approximation below. These are fixed synthesis constants, not an
  // adaptive leveler, so the audio loop has no hidden feedback or writes.
  static constexpr float kPinkGain = 0.11685F;
  static constexpr float kBrownInputGain = 0.020F;
  static constexpr float kBrownOutputGain = 1.3380F;
  static constexpr float kBrownLeak = 0.997F;

  static bool validColor(NoiseColor color) {
    return color == NoiseColor::White || color == NoiseColor::Pink ||
           color == NoiseColor::Brown;
  }

  void beginFadeOut(State destination) {
    fadeStartEnvelope_ = envelope_;
    envelopeFrame_ = 0;
    state_ = destination;
  }

  float nextEnvelope() {
    switch (state_) {
      case State::FadingIn:
        envelope_ = static_cast<float>(envelopeFrame_ + 1U) /
                    static_cast<float>(kFadeInFrames);
        break;
      case State::FadingOutToStop:
      case State::FadingOutToColor:
        envelope_ = fadeStartEnvelope_ *
                    (1.0F - static_cast<float>(envelopeFrame_ + 1U) /
                                static_cast<float>(kFadeOutFrames));
        break;
      case State::Playing:
        envelope_ = 1.0F;
        break;
      case State::Stopped:
        envelope_ = 0.0F;
        break;
    }
    return std::clamp(envelope_, 0.0F, 1.0F);
  }

  void completeEnvelopeFrame() {
    if (state_ == State::Playing || state_ == State::Stopped) return;
    ++envelopeFrame_;
    if (state_ == State::FadingIn && envelopeFrame_ >= kFadeInFrames) {
      envelope_ = 1.0F;
      envelopeFrame_ = 0;
      state_ = State::Playing;
      return;
    }
    if ((state_ == State::FadingOutToStop ||
         state_ == State::FadingOutToColor) &&
        envelopeFrame_ >= kFadeOutFrames) {
      envelope_ = 0.0F;
      envelopeFrame_ = 0;
      if (state_ == State::FadingOutToStop) {
        state_ = State::Stopped;
      } else {
        color_ = pendingColor_;
        resetFilters();
        state_ = State::FadingIn;
      }
    }
  }

  std::uint32_t nextRandom() {
    std::uint32_t value = randomState_;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    randomState_ = value;
    return value;
  }

  float nextWhite() {
    constexpr float kScale = 2.0F / 16777215.0F;
    return static_cast<float>(nextRandom() >> 8U) * kScale - 1.0F;
  }

  float nextColoredSample() {
    const float white = nextWhite();
    if (color_ == NoiseColor::White) return white * kWhiteGain;
    if (color_ == NoiseColor::Pink) {
      pink0_ = 0.99765F * pink0_ + 0.0990460F * white;
      pink1_ = 0.96300F * pink1_ + 0.2965164F * white;
      pink2_ = 0.57000F * pink2_ + 1.0526913F * white;
      return (pink0_ + pink1_ + pink2_ + 0.1848F * white) * kPinkGain;
    }
    brown_ = brown_ * kBrownLeak + white * kBrownInputGain;
    return brown_ * kBrownOutputGain;
  }

  void resetFilters() {
    pink0_ = 0.0F;
    pink1_ = 0.0F;
    pink2_ = 0.0F;
    brown_ = 0.0F;
  }

  std::uint32_t randomState_ = kFallbackSeed;
  NoiseColor color_ = NoiseColor::White;
  NoiseColor pendingColor_ = NoiseColor::White;
  State state_ = State::Stopped;
  std::size_t envelopeFrame_ = 0;
  float envelope_ = 0.0F;
  float fadeStartEnvelope_ = 0.0F;
  float pink0_ = 0.0F;
  float pink1_ = 0.0F;
  float pink2_ = 0.0F;
  float brown_ = 0.0F;
};

}  // namespace open_radio
