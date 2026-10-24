// SPDX-License-Identifier: Apache-2.0
// app/ui/i18n/Strings.h — Compile-time multilingual string catalog (EN, IT, DE, FR) (ADR-019)
#pragma once

#include "core/Types.h"

namespace faro::ui::i18n {

enum class StrId : uint8_t {
  TabChats = 0,
  TabChannels,
  TabDiscover,
  TabMap,
  TabSettings,
  HomeDashboard,
  DirectMessages,
  GroupChannels,
  DiscoverMesh,
  ContactsDir,
  ContactDetails,
  TracePathTitle,
  OfflineMapTitle,
  DiagnosticsTitle,
  BootWizardTitle,
  BtnZeroHopAdvert,
  BtnFloodAdvert,
  BtnSend,
  BtnCanned,
  BtnBack,
  BtnContacts,
  BtnDiagnostics,
  BtnResetPath,
  BtnTracePath,
  BtnTelemetry,
  RepeatRoamPreferred,
  NoGpsFix,
  ThemeDark,
  ThemeOutdoor,
  LanguageLabel,
  RadioPresetLabel,
  DutyCycleLabel,
  FinishWizard,
  _Count
};

const char* tr(StrId id, core::Language lang);
const char* languageCode(core::Language lang);
const char* languageName(core::Language lang);

}  // namespace faro::ui::i18n
