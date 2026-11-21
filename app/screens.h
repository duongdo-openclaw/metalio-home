#pragma once
#include <string>
#include <vector>

#include "lvgl.h"
#include "settings.h"

namespace screens {

void OpenLauncher();
bool HomeActive();          // the home screen is showing (Kệ sách tab again → big shelf)
void OpenSmartHome();
void OpenDriveLibrary();
void OpenTouchTest();
void OpenLibrary();
void OpenReader(const std::string& path);
void OpenMusic();
void OpenRecorder();
void OpenApps();
void OpenOwner();
void OpenSettings();
void OpenTransfer();
void OpenTransferFor(const std::string& focus);   // "ai": Wi-Fi page opened at the AI settings
void OpenGoals();                       // goals_screen.cc: daily minutes, yearly books
void OpenDictionary(const std::string& word = "");   // dictionary_screen.cc
// Fills `box` (a column container) with the definitions of `word` from every dictionary, plus
// "+ Từ vựng" (adds to Từ của tôi, `example` = the sentence from the book) and "Mở từ điển".
// Shared by the dictionary app and the reader's lookup sheet. Returns false when nothing was found.
bool FillLookup(lv_obj_t* box, const std::string& word, const std::string& example, bool compact);
void OpenVocab();
// assistant_screen.cc: AI assistant (ChatGPT, Gemini, Claude…). `selection` / `question` optional;
// listen = start recording right away (AI key held), stopped by AssistantKeyUp().
void OpenAssistant(const std::string& selection = "", const std::string& question = "", bool listen = false);
void AssistantKeyUp();
void OpenAssistantSettings();                       // vocab_screen.cc
void OpenNotes();                       // notes_screen.cc
void OpenNote(int id);
void OpenNotesForBook(const std::string& book_path);
void OpenFontSettings();
void OpenSettingsSection(int index);   // order of settings_screen.cc Section
void OpenKeySettings(int key);                // settings_screen.cc sections
void OpenStorageSettings();
void OpenOnlineLibraries();
// share_screen.cc: send books to anyone nearby (QR → download page), share an online library
// link, and receive books shared by another Metalio.
void OpenShareBooks(const std::string& title, const std::vector<std::string>& paths);
void OpenShareLink(const std::string& name, const std::string& link);
void OpenReceive();             // drive_screen.cc: Drive links saved on the card
// The reader can open at a given position (notes and bookmarks jump back into the book).
void OpenReaderAt(const std::string& path, int chapter, int para, size_t offset);
// system.cc
void InstallSystem();      // standby timer; call once after the first screen
void ShowStandby();
bool IsStandby();
bool WakeIfStandby();      // true when the input only woke the device
// Hardware keys of the Metalio E-Ink4-Plus (firmware metalio_keys + vk_key_handler):
//   Power (P17)  click: standby / wake   hold 3 s: power off        (fixed)
//   Vol+ / Vol–  click: volume ±10       hold: max / mute           (fixed outside the reader;
//                in the reader: page turn, or volume while music plays: Cài đặt › Phím cứng)
//   AI (BOOT)    click / double / hold: configurable
//   Home (cover) click / hold: configurable
//   Back (cover) click: back, previous page in the reader (fixed); hold: configurable
//   Next (cover) click in the reader: next page (fixed); elsewhere click / hold configurable
enum class HwKey { Power, VolUp, VolDown, Ai, Home, Back, Next };
enum class Press { Click, Double, Long, Up };   // Up: key released (AI key push-to-talk)
void OnKey(HwKey key, Press press);
void RunKeyAction(settings::KeyAction a);
void OnAiKey(bool hold);   // = OnKey(Ai, Click / Long)
void OnPowerKey();         // = OnKey(Power, Click)
void OpenQuickSettings();     // quick_settings.cc
bool QuickSettingsOpen();

}  // namespace screens
