// SPDX-License-Identifier: Apache-2.0
// app/ui/i18n/Strings.cpp — Compile-time multilingual string catalog (EN, IT, DE, FR) (ADR-019)
#include "Strings.h"

namespace faro::ui::i18n {

namespace {
struct Row {
  const char* en;
  const char* it;
  const char* de;
  const char* fr;
};

static const Row kCatalog[size_t(StrId::_Count)] = {
  // TabChats
  {"Chats", "Chat", "Chats", "Discussions"},
  // TabChannels
  {"Channels", "Canali", "Kanäle", "Canaux"},
  // TabDiscover
  {"Discover", "Scopri", "Entdecken", "Découvrir"},
  // TabMap
  {"Map", "Mappa", "Karte", "Carte"},
  // TabSettings
  {"Settings", "Impostaz.", "Optionen", "Réglages"},
  // HomeDashboard
  {"Faro Mesh Dashboard", "Cruscotto Rete Faro", "Faro Mesh Übersicht", "Tableau de bord Faro"},
  // DirectMessages
  {"Direct Messages", "Messaggi Diretti", "Direktnachrichten", "Messages directs"},
  // GroupChannels
  {"Group Channels", "Canali di Gruppo", "Gruppenkanäle", "Canaux de groupe"},
  // DiscoverMesh
  {"Discover nodes", "Scopri nodi", "Knoten suchen", "Découvrir les nœuds"},
  // ContactsDir
  {"Contacts Directory", "Rubrica Contatti", "Kontaktverzeichnis", "Répertoire contacts"},
  // ContactDetails
  {"Contact & QR Share", "Contatto & QR", "Kontakt & QR-Code", "Contact & QR"},
  // TracePathTitle
  {"Multi-Hop Trace Path", "Tracciamento Percorso", "Multi-Hop Routenpfad", "Trace du chemin"},
  // OfflineMapTitle
  {"Offline Alpine Map", "Mappa Alpina Offline", "Offline Alpenkarte", "Carte alpine hors-ligne"},
  // DiagnosticsTitle
  {"Radio & System Diagnostics", "Diagnostica Radio & Sistema", "Funk- & Systemdiagnose", "Diagnostic radio"},
  // BootWizardTitle
  {"Welcome to Faro — Setup", "Benvenuto in Faro — Config.", "Willkommen bei Faro", "Bienvenue sur Faro"},
  // BtnZeroHopAdvert
  {"0-Hop Advert", "Annuncio 0-Hop", "0-Hop Bake", "Annonce 0-saut"},
  // BtnFloodAdvert
  {"Flood Advert", "Annuncio Flood", "Flood-Bake", "Annonce Flood"},
  // BtnSend
  {"Send", "Invia", "Senden", "Envoyer"},
  // BtnCanned
  {"Canned", "Rapidi", "Vorlagen", "Rapides"},
  // BtnBack
  {"Back", "Indietro", "Zurück", "Retour"},
  // BtnContacts
  {"Contacts", "Contatti", "Kontakte", "Contacts"},
  // BtnDiagnostics
  {"Diagnostics", "Diagnostica", "Diagnose", "Diagnostic"},
  // BtnResetPath
  {"Reset Path (Flood)", "Reset Percorso", "Pfad zurücksetzen", "Réinit. chemin"},
  // BtnTracePath
  {"Trace Path", "Traccia", "Route prüfen", "Tracer"},
  // BtnTelemetry
  {"Telemetry", "Telemetria", "Telemetrie", "Télémétrie"},
  // RepeatRoamPreferred
  {"Strongest repeater", "Ripetitore più forte", "Stärkster Repeater", "Répéteur le plus fort"},
  // NoGpsFix
  {"No GPS Fix", "Nessun Fix GPS", "Kein GPS-Fix", "Pas de signal GPS"},
  // ThemeDark
  {"Dark Tactical", "Scuro Tattico", "Dunkel Taktisch", "Sombre tactique"},
  // ThemeOutdoor
  {"Google Light", "Google Chiaro", "Google Hell", "Google Clair"},
  // LanguageLabel
  {"Language / Lingua / Sprache", "Lingua Interfaccia", "Sprache / Language", "Langue"},
  // RadioPresetLabel
  {"LoRa Region Preset", "Profilo Radio LoRa", "LoRa Funkprofil", "Profil radio LoRa"},
  // DutyCycleLabel
  {"Duty cycle limit 10 %", "Limite duty cycle 10 %", "Sendezeit-Limit 10 %", "Limite cycle 10 %"},
  // FinishWizard
  {"Finish & Start Mesh", "Completa e Avvia", "Fertig & Starten", "Terminer & Démarrer"}
};
}  // namespace

const char* tr(StrId id, core::Language lang) {
  size_t idx = size_t(id);
  if (idx >= size_t(StrId::_Count)) return "";
  switch (lang) {
    case core::Language::IT: return kCatalog[idx].it;
    case core::Language::DE: return kCatalog[idx].de;
    case core::Language::FR: return kCatalog[idx].fr;
    case core::Language::EN:
    default:                 return kCatalog[idx].en;
  }
}

const char* languageCode(core::Language lang) {
  switch (lang) {
    case core::Language::IT: return "IT";
    case core::Language::DE: return "DE";
    case core::Language::FR: return "FR";
    case core::Language::EN:
    default:                 return "EN";
  }
}

const char* languageName(core::Language lang) {
  switch (lang) {
    case core::Language::IT: return "Italiano";
    case core::Language::DE: return "Deutsch";
    case core::Language::FR: return "Français";
    case core::Language::EN:
    default:                 return "English";
  }
}

}  // namespace faro::ui::i18n
