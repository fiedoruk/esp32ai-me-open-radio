#pragma once

#include <cstddef>
#include <cstdint>

namespace open_radio {

enum class WifiPortalRequest : std::uint8_t { Automatic, User };

// A provisioned appliance recovers as a station. Disconnect reasons and retry
// counts do not express a user's intent to change credentials. Keep automatic
// setup for missing profiles and unfinished onboarding: that screen has no
// manual portal control. No persisted "worked once" bit or timeout is needed.
constexpr bool allowWifiPortalStart(std::size_t savedProfiles,
                                   bool onboardingComplete,
                                   WifiPortalRequest request) {
  return request == WifiPortalRequest::User || savedProfiles == 0 ||
         !onboardingComplete;
}

}  // namespace open_radio
