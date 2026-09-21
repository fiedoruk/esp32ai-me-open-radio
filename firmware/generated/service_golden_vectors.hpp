#pragma once

#include <array>

#include "open_radio/service_contracts.hpp"

namespace open_radio {
namespace generated {

inline constexpr std::array<GoldenNetworkVector, 9> kNetworkGoldenVectors{{
  {"preferred-home-selected", NetworkProfileState::Base, 3000000ULL, {{{NetworkProfileRef::Home, NetworkSecurity::Wpa2Psk, -62, true, false}, {NetworkProfileRef::Travel, NetworkSecurity::Wpa2Psk, -35, true, false}}}, 2, NetworkSelectionStatus::Selected, NetworkProfileRef::Home, 0ULL},
  {"fallback-travel-selected", NetworkProfileState::Base, 3001000ULL, {{{NetworkProfileRef::Home, NetworkSecurity::Wpa2Psk, -90, false, false}, {NetworkProfileRef::Travel, NetworkSecurity::Wpa2Psk, -48, true, false}}}, 2, NetworkSelectionStatus::Selected, NetworkProfileRef::Travel, 0ULL},
  {"unknown-secured-prompts", NetworkProfileState::Base, 3002000ULL, {{{NetworkProfileRef::None, NetworkSecurity::Wpa2Psk, -45, true, false}, {}}}, 1, NetworkSelectionStatus::PromptRequired, NetworkProfileRef::None, 0ULL},
  {"open-network-prompts", NetworkProfileState::Base, 3003000ULL, {{{NetworkProfileRef::None, NetworkSecurity::Open, -30, true, false}, {}}}, 1, NetworkSelectionStatus::PromptRequired, NetworkProfileRef::None, 0ULL},
  {"captive-network-prompts", NetworkProfileState::Base, 3004000ULL, {{{NetworkProfileRef::None, NetworkSecurity::Wpa2Psk, -40, true, true}, {}}}, 1, NetworkSelectionStatus::PromptRequired, NetworkProfileRef::None, 0ULL},
  {"unapproved-profile-prompts", NetworkProfileState::Base, 3005000ULL, {{{NetworkProfileRef::Pending, NetworkSecurity::Wpa2Psk, -40, true, false}, {}}}, 1, NetworkSelectionStatus::PromptRequired, NetworkProfileRef::None, 0ULL},
  {"no-reachable-network-retries", NetworkProfileState::Base, 3006000ULL, {{{NetworkProfileRef::Home, NetworkSecurity::Wpa2Psk, -95, false, false}, {}}}, 1, NetworkSelectionStatus::RetryScheduled, NetworkProfileRef::None, 3011000ULL},
  {"quarantined-home-falls-back", NetworkProfileState::HomeQuarantined, 3007000ULL, {{{NetworkProfileRef::Home, NetworkSecurity::Wpa2Psk, -40, true, false}, {NetworkProfileRef::Travel, NetworkSecurity::Wpa2Psk, -55, true, false}}}, 2, NetworkSelectionStatus::Selected, NetworkProfileRef::Travel, 0ULL},
  {"preferred-home-returns", NetworkProfileState::HomeRecovered, 3008000ULL, {{{NetworkProfileRef::Home, NetworkSecurity::Wpa2Psk, -55, true, false}, {NetworkProfileRef::Travel, NetworkSecurity::Wpa2Psk, -42, true, false}}}, 2, NetworkSelectionStatus::Selected, NetworkProfileRef::Home, 0ULL}
}};

inline constexpr std::array<GoldenPersistenceVector, 9> kPersistenceGoldenVectors{{
  {"commit-selects-new-slot", PersistenceSetup::Baseline, WritePhase::AfterCommit, StorageStatus::Bootable, SlotId::B, 2, 0},
  {"power-loss-before-write", PersistenceSetup::Baseline, WritePhase::BeforeWrite, StorageStatus::Bootable, SlotId::A, 1, 0},
  {"power-loss-during-payload", PersistenceSetup::Baseline, WritePhase::DuringPayload, StorageStatus::Bootable, SlotId::A, 1, 0},
  {"power-loss-before-marker", PersistenceSetup::Baseline, WritePhase::BeforeCommitMarker, StorageStatus::Bootable, SlotId::A, 1, 0},
  {"checksum-fallback", PersistenceSetup::ChecksumCorruptNewest, WritePhase::None, StorageStatus::Bootable, SlotId::A, 1, 0},
  {"parse-fallback", PersistenceSetup::ParseCorruptNewest, WritePhase::None, StorageStatus::Bootable, SlotId::A, 1, 0},
  {"legacy-migrates", PersistenceSetup::LegacyOnly, WritePhase::None, StorageStatus::Bootable, SlotId::A, 1, 1},
  {"future-schema-safe-mode", PersistenceSetup::FutureOnly, WritePhase::None, StorageStatus::SafeMode, SlotId::None, 0, 0},
  {"empty-storage-safe-mode", PersistenceSetup::Empty, WritePhase::None, StorageStatus::SafeMode, SlotId::None, 0, 0}
}};

inline constexpr std::array<GoldenResolverVector, 9> kResolverGoldenVectors{{
  {"rmf-fm-timeout-lkg", {CapabilityClass::Mp3Icy, CandidateOutcome::Timeout, false, true, CandidateOutcome::Success, 1000000ULL}, ResolverStatus::Playing, ResolverSelection::LastKnownGood, 0ULL},
  {"radio-zet-404-lkg", {CapabilityClass::Mp3Icy, CandidateOutcome::Http404, false, true, CandidateOutcome::Success, 1001000ULL}, ResolverStatus::Playing, ResolverSelection::LastKnownGood, 0ULL},
  {"tok-fm-primary-success", {CapabilityClass::Mp3Icy, CandidateOutcome::Success, false, false, CandidateOutcome::Invalid, 1002000ULL}, ResolverStatus::Playing, ResolverSelection::Primary, 0ULL},
  {"antyradio-primary-success", {CapabilityClass::Mp3Icy, CandidateOutcome::Success, false, false, CandidateOutcome::Invalid, 1003000ULL}, ResolverStatus::Playing, ResolverSelection::Primary, 0ULL},
  {"eska-mp3-success", {CapabilityClass::Mp3Icy, CandidateOutcome::Success, false, false, CandidateOutcome::Invalid, 1004000ULL}, ResolverStatus::Playing, ResolverSelection::Primary, 0ULL},
  {"rmf-maxx-bounded-retry", {CapabilityClass::Mp3Icy, CandidateOutcome::Timeout, false, false, CandidateOutcome::Invalid, 1005000ULL}, ResolverStatus::RetryScheduled, ResolverSelection::None, 1010000ULL},
  {"radio-plus-lkg", {CapabilityClass::Mp3Icy, CandidateOutcome::ParseError, true, true, CandidateOutcome::Success, 1006000ULL}, ResolverStatus::Playing, ResolverSelection::LastKnownGood, 0ULL},
  {"zlote-stale-lkg", {CapabilityClass::Mp3Icy, CandidateOutcome::Stale, false, true, CandidateOutcome::Success, 1007000ULL}, ResolverStatus::Playing, ResolverSelection::LastKnownGood, 0ULL},
  {"meloradio-primary-success", {CapabilityClass::Mp3Icy, CandidateOutcome::Success, false, false, CandidateOutcome::Invalid, 1008000ULL}, ResolverStatus::Playing, ResolverSelection::Primary, 0ULL}
}};

}
}
