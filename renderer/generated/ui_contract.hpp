#pragma once

#include <array>

#include "open_radio/renderer.hpp"

namespace open_radio::ui::generated {

inline constexpr int kWidth = 320;
inline constexpr int kHeight = 240;
inline constexpr int kStatusBarHeight = 28;
inline constexpr int kControlBarHeight = 56;
inline constexpr std::size_t kStationCount = 9;

inline constexpr ThemeTokens kDarkTheme{0x0882, 0x10c4, 0x1926, 0xf7df, 0xadb8, 0x29c9, 0x56b4, 0xfdaa, 0xfbef, 0x0000, 0x46d5, 0x08a2, 0x46d5, 0x1926};
inline constexpr ThemeTokens kLightTheme{0xf7be, 0xffff, 0xe77d, 0x10c4, 0x530d, 0xceba, 0x0bca, 0x9a40, 0xb105, 0x2185, 0x1c2c, 0xffff, 0x13aa, 0x7c71};
inline constexpr ThemeMode kDefaultTheme = ThemeMode::dark;

constexpr const ThemeTokens& themeFor(ThemeMode mode) {
  return mode == ThemeMode::light ? kLightTheme : kDarkTheme;
}

inline constexpr std::array<StationTheme, kStationCount> kStations{{
    {"rmf-fm", "RMF FM", "RMF", 0x46d5, 0xfdac, 0x08a2},
    {"radio-zet", "Radio ZET", "ZET", 0xacdf, 0x56b9, 0x1064},
    {"tok-fm", "TOK FM", "TOK", 0x8ed7, 0xd539, 0x08a1},
    {"antyradio", "Antyradio", "ANTY", 0xfbab, 0xf58b, 0x1840},
    {"radio-eska", "Radio ESKA", "ESKA", 0xfccc, 0x7ebe, 0x1860},
    {"rmf-maxx", "RMF MAXX", "MAXX", 0x46fa, 0xfbd5, 0x00a2},
    {"radio-plus", "Radio Plus", "PLUS", 0xc25f, 0x3f38, 0x1044},
    {"zlote-przeboje", "Złote Przeboje", "ZŁOTE", 0xf54d, 0x8654, 0x1860},
    {"meloradio", "Meloradio", "MELO", 0xbcff, 0x6eda, 0x1044},
}};

}
