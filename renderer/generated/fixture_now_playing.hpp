#pragma once

#include "open_radio/renderer.hpp"

namespace open_radio::ui::generated {

inline constexpr const char* kFixtureId = "now-playing-rmf-local";
inline constexpr CompactSnapshot kNowPlayingFixture{
    0,
    0,
    true,
    AudioOutput::local,
    42,
    false,
    ThemeMode::dark,
};

}
