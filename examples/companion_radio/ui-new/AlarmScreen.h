#pragma once

// =============================================================================
// AlarmScreen.h — Alarm clock app for LilyGo T-Deck Pro (audio variant)
//
// Features:
//   - 5 configurable alarm slots with per-alarm enable, time, day-of-week
//   - MP3 alarm tones loaded from /alarms/ on SD card
//   - Binary config persistence at /alarms/.alarmcfg
//   - Shared Audio* with audiobook player (lazy-init in main.cpp)
//   - Ringing mode: ANY key press instantly silences alarm
//   - Auto-timeout: alarm silences after 5 minutes if unattended
//   - Snooze: press Z during ringing to snooze for 5 minutes
//   - Background alarm check runs in main loop() every ~10 seconds
//
// Keyboard controls:
//   ALARM_LIST:  W/S = scroll slots, Enter = edit selected alarm,
//                E = toggle enable/disable, Shift+Del = exit to home
//   EDIT_ALARM:  W/S = move between fields, A/D = adjust value,
//                Enter = open sound picker (on sound field) or save & exit,
//                Shift+Del = cancel edit
//   PICK_SOUND:  W/S = scroll sounds, Enter = select, Shift+Del = cancel
//   RINGING:     ANY key = dismiss, Z = snooze 5 minutes
//
// Library dependencies: ESP32-audioI2S (shared with AudiobookPlayerScreen)
// =============================================================================

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
#endif
#include <SD.h>
#include <vector>

// Audio library — ESP32-audioI2S by schreibfaul1 (shared with AudiobookPlayerScreen)
#include "Audio.h"

#include "variant.h"

// Forward declarations
class UITask;

#ifdef HAS_ES8311_AUDIO
// Defined in target.cpp (where the board object + ES8311 driver are visible).
// Forward-declared here so this UI header doesn't need the heavy target.h.
void meck_audio_route_amp();
void meck_audio_codec_init();
#endif

#if defined(LilyGo_TDeck_Pro_Max)
// Silent (vibrate) alarm haptic shims, defined in target.cpp for the same
// reason as the audio shims above -- this UI header cannot see the board.
void meck_alarm_haptic_begin();
void meck_alarm_haptic_buzz();
void meck_alarm_haptic_stop();
#endif

// Lock-state query, defined in main.cpp. This UI header only forward-declares
// UITask, so it cannot call _task->isLocked() directly; the shim bridges it.
bool meck_alarm_is_locked();

// ============================================================================
// Configuration
// ============================================================================
#define ALARMS_FOLDER       "/alarms"
#define ALARMS_CONFIG_FILE  "/alarms/.alarmcfg"
#define ALARM_SLOT_COUNT    5
#define ALARM_SOUND_MAX     128   // Max filename length for alarm tone
#define ALARM_RINGING_TIMEOUT_MS  300000  // 5 minutes auto-dismiss
#define ALARM_SNOOZE_MS     300000  // 5 minutes snooze
#define ALARM_CHECK_INTERVAL_MS   10000   // Check alarms every 10 seconds
#define ALARM_FIRE_COOLDOWN_S     90      // Don't re-fire same alarm within 90s

#if defined(LilyGo_TDeck_Pro_Max)
// Silent (vibrate) alarm. The chosen-sound slot stores this one-byte sentinel
// instead of a filename; 0x01 can never be the first byte of a real filename.
// Same value as NotifSounds' marker, kept local so this header need not pull in
// NotifSounds.h.
#define ALARM_VIBRATE_MARKER   "\x01"
#define ALARM_VIBRATE_GROUP    3      // buzzes per group
#define ALARM_VIBRATE_BUZZ_MS  1200   // spacing between buzz starts (~1s effect + margin)
#define ALARM_VIBRATE_PAUSE_MS 4000   // pause after each group before repeating
#endif

// Config file magic + version for forward compatibility
#define ALARM_CFG_MAGIC     0x4D4B414C  // "MKAL"
#define ALARM_CFG_VERSION   2  // v2: ALARM_SOUND_MAX increased to 128

// Day-of-week bitmask (bit 0 = Sunday, bit 6 = Saturday)
#define DOW_SUN  (1 << 0)
#define DOW_MON  (1 << 1)
#define DOW_TUE  (1 << 2)
#define DOW_WED  (1 << 3)
#define DOW_THU  (1 << 4)
#define DOW_FRI  (1 << 5)
#define DOW_SAT  (1 << 6)
#define DOW_ALL  0x7F
#define DOW_WEEKDAYS (DOW_MON | DOW_TUE | DOW_WED | DOW_THU | DOW_FRI)
#define DOW_WEEKEND  (DOW_SAT | DOW_SUN)

// ============================================================================
// Data structures
// ============================================================================

struct AlarmSlot {
  bool     enabled;
  uint8_t  hour;        // 0–23
  uint8_t  minute;      // 0–59
  uint8_t  days;        // Day-of-week bitmask (0x7F = every day)
  uint8_t  volume;      // 1–21 (Audio library scale)
  char     sound[ALARM_SOUND_MAX];  // Filename in /alarms/ (empty = first available)
};

struct AlarmConfig {
  uint32_t  magic;
  uint8_t   version;
  AlarmSlot slots[ALARM_SLOT_COUNT];
  uint8_t   _pad[3];  // Alignment padding
};

// ============================================================================
// AlarmScreen
// ============================================================================
class AlarmScreen : public UIScreen {
public:
  enum Mode { ALARM_LIST, EDIT_ALARM, PICK_SOUND, RINGING };

  // Edit fields when in EDIT_ALARM mode
  enum EditField { FIELD_ENABLED, FIELD_HOUR, FIELD_MINUTE, FIELD_DAYS, FIELD_VOLUME, FIELD_SOUND, FIELD_COUNT };

private:
  UITask*  _task;
  Mode     _mode;
  bool     _sdReady;

  // Alarm data
  AlarmConfig _config;
  int _selectedSlot;       // 0–4 in ALARM_LIST
  int _scrollOffset;

  // Edit state
  int _editSlot;           // Which slot is being edited
  EditField _editField;    // Current field cursor
  AlarmSlot _editCopy;     // Working copy during edit

  // Direct digit entry (for hour/minute fields)
  bool    _digitEntry;     // Currently typing digits
  char    _digitBuf[4];    // Up to 3 chars + null
  int     _digitPos;       // Cursor in digit buffer

  // Sound picker state
  std::vector<String> _soundFiles;
  int _soundSelected;
  int _soundScroll;

  // Ringing state
  bool     _ringing;
  int      _ringingSlot;       // Which alarm triggered
  unsigned long _ringingStart; // millis() when alarm started
  bool     _snoozed;
  unsigned long _snoozeUntil;  // millis() for snooze wake-up

  // Fire tracking — prevent re-trigger within cooldown
  uint32_t _lastFiredEpoch[ALARM_SLOT_COUNT];

  // Audio — shared with audiobook player, managed by main.cpp
  // We do NOT own this pointer; main.cpp creates and shares it.
  Audio* _audio;
  bool   _alarmAudioActive;  // True when alarm is driving the Audio object
  String _resolvedSoundPath; // Full path of currently playing alarm sound
  int    _restartAttempts;   // Retry counter for audio restart loop

  // Synthetic rows shown above the file list in the sound picker.
  // On MAX this is the "Buzzer (vibrate)" row; elsewhere there are none.
#if defined(LilyGo_TDeck_Pro_Max)
  static const int kVibrateRows = 1;
#else
  static const int kVibrateRows = 0;
#endif

#if defined(LilyGo_TDeck_Pro_Max)
  // Silent (vibrate) alarm cadence state
  bool          _vibrating;
  int           _vibBuzzCount;   // Buzzes fired in the current group (0..GROUP)
  unsigned long _vibNextMs;      // millis() of the next buzz/pause transition

  static bool slotIsVibrate(const AlarmSlot& s) {
    return (uint8_t)s.sound[0] == (uint8_t)ALARM_VIBRATE_MARKER[0];
  }

  void startVibrate() {
    meck_alarm_haptic_begin();
    _vibrating = true;
    meck_alarm_haptic_buzz();
    _vibBuzzCount = 1;
    _vibNextMs = millis() + ALARM_VIBRATE_BUZZ_MS;
  }
#endif

  // ---- Day-of-week helpers ----

  static const char* dowShort(int dow) {
    static const char* names[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
    static const char* names_fr[] = {"Di","Lu","Ma","Me","Je","Ve","Sa"};
    return (dow >= 0 && dow < 7) ? MECK_TR(names[dow], names_fr[dow]) : "??";
  }

  // Day of week from epoch (0=Sunday)
  static int dowFromEpoch(uint32_t epoch) {
    // Jan 1 1970 was a Thursday (4)
    return (int)((epoch / 86400 + 4) % 7);
  }

  // Format days bitmask as human string
  static void formatDays(uint8_t days, char* buf, int bufLen) {
    if (days == DOW_ALL) { strncpy(buf, MECK_TR("Every day", "Tous les jours"), bufLen); return; }
    if (days == DOW_WEEKDAYS) { strncpy(buf, MECK_TR("Weekdays", "En semaine"), bufLen); return; }
    if (days == DOW_WEEKEND) { strncpy(buf, MECK_TR("Weekend", "Week-end"), bufLen); return; }
    if (days == 0) { strncpy(buf, MECK_TR("Never", "Jamais"), bufLen); return; }
    buf[0] = '\0';
    for (int i = 0; i < 7; i++) {
      if (days & (1 << i)) {
        if (buf[0] != '\0') strncat(buf, " ", bufLen - strlen(buf) - 1);
        strncat(buf, dowShort(i), bufLen - strlen(buf) - 1);
      }
    }
  }

  // ---- Config persistence ----

  void loadConfig() {
    // Initialise all slots to defaults
    memset(&_config, 0, sizeof(_config));
    _config.magic = ALARM_CFG_MAGIC;
    _config.version = ALARM_CFG_VERSION;
    for (int i = 0; i < ALARM_SLOT_COUNT; i++) {
      _config.slots[i].enabled = false;
      _config.slots[i].hour = 7;
      _config.slots[i].minute = 0;
      _config.slots[i].days = DOW_WEEKDAYS;
      _config.slots[i].volume = 21;
      _config.slots[i].sound[0] = '\0';
    }

    if (!SD.exists(ALARMS_CONFIG_FILE)) {
      Serial.println("ALARM: No config file, using defaults");
      return;
    }
    File f = SD.open(ALARMS_CONFIG_FILE, FILE_READ);
    if (!f) return;
    AlarmConfig tmp;
    int bytesRead = f.read((uint8_t*)&tmp, sizeof(tmp));
    f.close();
    digitalWrite(SDCARD_CS, HIGH);

    if (bytesRead == sizeof(tmp) && tmp.magic == ALARM_CFG_MAGIC && tmp.version == ALARM_CFG_VERSION) {
      memcpy(&_config, &tmp, sizeof(_config));
      Serial.printf("ALARM: Loaded config (%d slots)\n", ALARM_SLOT_COUNT);
      // Sanitise loaded values
      for (int i = 0; i < ALARM_SLOT_COUNT; i++) {
        _config.slots[i].hour = _config.slots[i].hour % 24;
        _config.slots[i].minute = _config.slots[i].minute % 60;
        _config.slots[i].days &= DOW_ALL;
        if (_config.slots[i].volume == 0 || _config.slots[i].volume > 21)
          _config.slots[i].volume = 21;
      }
    } else {
      Serial.println("ALARM: Config invalid or wrong version, using defaults");
    }
  }

  void saveConfig() {
    // Ensure folder exists
    if (!SD.exists(ALARMS_FOLDER)) {
      SD.mkdir(ALARMS_FOLDER);
    }
    File f = SD.open(ALARMS_CONFIG_FILE, FILE_WRITE);
    if (!f) {
      Serial.println("ALARM: Failed to save config");
      return;
    }
    f.write((uint8_t*)&_config, sizeof(_config));
    f.close();
    digitalWrite(SDCARD_CS, HIGH);
    Serial.println("ALARM: Config saved");
  }

  // ---- Sound file scanner ----

  void scanSoundFiles() {
    _soundFiles.clear();
    if (!SD.exists(ALARMS_FOLDER)) {
      SD.mkdir(ALARMS_FOLDER);
      Serial.printf("ALARM: Created %s\n", ALARMS_FOLDER);
    }
    File root = SD.open(ALARMS_FOLDER);
    if (!root || !root.isDirectory()) {
      digitalWrite(SDCARD_CS, HIGH);
      return;
    }
    File entry = root.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String name = entry.name();
        // Skip dotfiles (config, etc.)
        if (name.length() > 0 && name.charAt(0) != '.') {
          String lower = name;
          lower.toLowerCase();
          if (lower.endsWith(".mp3")) {
            _soundFiles.push_back(name);
          }
        }
      }
      entry = root.openNextFile();
    }
    root.close();
    digitalWrite(SDCARD_CS, HIGH);

    // Sort alphabetically
    std::sort(_soundFiles.begin(), _soundFiles.end());
    Serial.printf("ALARM: Found %d sound files\n", (int)_soundFiles.size());
  }

  // ---- DAC power (same pattern as AudiobookPlayerScreen) ----

  void enableDAC() {
#ifndef HAS_ES8311_AUDIO
    pinMode(41, OUTPUT);
    digitalWrite(41, HIGH);
    delay(50);
#endif
  }

  void disableDAC() {
#ifndef HAS_ES8311_AUDIO
    digitalWrite(41, LOW);
#endif
  }

  // ---- Audio control for alarm ringing ----

  void startAlarmAudio(int slotIdx) {
    if (!_audio) {
      Serial.println("ALARM: No Audio object!");
      return;
    }

    const AlarmSlot& slot = _config.slots[slotIdx];
    String soundFile;

    // Determine which file to play
    if (slot.sound[0] != '\0') {
      soundFile = String(ALARMS_FOLDER) + "/" + String(slot.sound);
      // Verify file still exists
      if (!SD.exists(soundFile.c_str())) {
        Serial.printf("ALARM: Sound '%s' missing, falling back\n", slot.sound);
        soundFile = "";
      }
      digitalWrite(SDCARD_CS, HIGH);  // Release SD after exists() check
    }

    // Fallback: use first available sound
    if (soundFile.length() == 0) {
      if (_soundFiles.empty()) scanSoundFiles();
      if (!_soundFiles.empty()) {
        soundFile = String(ALARMS_FOLDER) + "/" + _soundFiles[0];
      }
    }

    if (soundFile.length() == 0) {
      Serial.println("ALARM: No sound files available!");
      return;
    }

    Serial.printf("ALARM: Starting audio: '%s' vol=%d\n", soundFile.c_str(), slot.volume);

    // Stop any previous audio (stale audiobook state, etc.)
    _audio->stopSong();

    // Power on DAC and wait for it to stabilise
    enableDAC();
    delay(100);  // Cold-start needs longer than 50ms

    // Configure I2S pins (must be done after any stopSong that resets I2S)
#ifdef HAS_ES8311_AUDIO
    // MAX: route to the ES8311 + enable the speaker amp, then configure I2S
    // WITH MCLK (the ES8311 is clock slave and needs MCLK on BOARD_I2S_MCLK).
    // The codec registers are initialised after connecttoFS starts the clocks.
    meck_audio_route_amp();
    bool ok = _audio->setPinout(BOARD_I2S_BCLK, BOARD_I2S_LRC, BOARD_I2S_DOUT,
                                I2S_PIN_NO_CHANGE, BOARD_I2S_MCLK);
#else
    bool ok = _audio->setPinout(BOARD_I2S_BCLK, BOARD_I2S_LRC, BOARD_I2S_DOUT, 0);
    if (!ok) {
      ok = _audio->setPinout(BOARD_I2S_BCLK, BOARD_I2S_LRC, BOARD_I2S_DOUT);
    }
#endif
    if (!ok) {
      Serial.println("ALARM: setPinout FAILED");
    }

    // Connect to file FIRST, then set volume (matches audiobook working pattern)
    _audio->connecttoFS(SD, soundFile.c_str());
#ifdef HAS_ES8311_AUDIO
    // MAX: I2S clocks are now running, so initialise the ES8311 codec (once;
    // idempotent). Without this, alarm audio is silent until a notification
    // tone happens to bring the codec up.
    meck_audio_codec_init();
#endif
    _audio->setVolume(slot.volume);
    _alarmAudioActive = true;
    _resolvedSoundPath = soundFile;  // Store for restart loop
    _restartAttempts = 0;

    Serial.printf("ALARM: Playing '%s' at volume %d\n", soundFile.c_str(), slot.volume);
  }

  void stopAlarmAudio() {
    if (_audio && _alarmAudioActive) {
      _audio->stopSong();
      disableDAC();
      _alarmAudioActive = false;
      _resolvedSoundPath = "";
      _restartAttempts = 0;
      Serial.println("ALARM: Audio stopped");
    }
#if defined(LilyGo_TDeck_Pro_Max)
    if (_vibrating) {
      _vibrating = false;
      _vibBuzzCount = 0;
      meck_alarm_haptic_stop();
    }
#endif
  }

  // ---- Standard footer (matching all Meck screens) ----
  // GxEPD footer rule: only setTextSize(1) works at screen bottom.

  void drawFooter(DisplayDriver& display, const char* left, const char* right) {
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, footerY);
    display.print(left);
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

#ifdef MECK_UI_V2
  // Sound name for display: "Buzzer (vibrate)", the file name without its
  // extension, or "(default)"
  void soundLabel(const AlarmSlot& s, char* buf, size_t len) {
#if defined(LilyGo_TDeck_Pro_Max)
    if (slotIsVibrate(s)) { snprintf(buf, len, "%s", MECK_TR("Buzzer (vibrate)", "Buzzer (vibreur)")); return; }
#endif
    if (s.sound[0] == '\0') { snprintf(buf, len, "%s", MECK_TR("(default)", "(d\xC3\xA9" "faut)")); return; }
    snprintf(buf, len, "%s", s.sound);
    char* dot = strrchr(buf, '.');
    if (dot && dot != buf) *dot = '\0';
  }

  // UI v2 alarm list: one row per slot (time, days and sound, on/off toggle)
  void renderAlarmListV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    Ui2Chrome::titleBar(c, MECK_TR("Alarm clock", "R\xC3\xA9veil"));
    for (int i = 0; i < ALARM_SLOT_COUNT; i++) {
      const AlarmSlot& s = _config.slots[i];
      char time[8], days[24], snd[40], sub[72];
      snprintf(time, sizeof(time), "%02d:%02d", s.hour, s.minute);
      formatDays(s.days, days, sizeof(days));
      soundLabel(s, snd, sizeof(snd));
      snprintf(sub, sizeof(sub), "%s \xC2\xB7 %s", days, snd);
      Ui2Row r;
      r.icon = UI2_IC_ALARM;
      r.title = time;
      r.sub = sub;
      r.toggle = s.enabled ? 1 : 0;
      r.selected = (i == _selectedSlot);
      Ui2List::drawRow(c, BODY_Y + i * ROW_H, r);
    }
    if (_soundFiles.empty() && _sdReady) {
      c.wrap(PAD, BODY_Y + ALARM_SLOT_COUNT * ROW_H + 18, SCREEN_W - 2 * PAD, 14, 2,
             MECK_TR("Put 44 kHz .mp3 tones in /alarms/ on the SD card.",
                     "Placez des .mp3 44 kHz dans /alarms/ sur la SD."),
             UI2_F_META, true, true, nullptr, UI2_CENTER);
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Edit", "\xC3\x89" "diter")),
      ui2Key("O", MECK_TR("On/off", "Oui/non")),
      ui2Key("A/D", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 4);
  }

  // UI v2 alarm editor: compact field rows, the day toggles under the Days
  // row, and the hour/minute digit entry as a box over the list
  void renderEditAlarmV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    char title[32];
    snprintf(title, sizeof(title), MECK_TR("Alarm %d", "R\xC3\xA9veil %d"), _editSlot + 1);
    Ui2Chrome::titleBar(c, title);

    const int16_t rowH = 34;
    char hh[4], mm[4], days[24], vol[4], snd[40];
    snprintf(hh, sizeof(hh), "%02d", _editCopy.hour);
    snprintf(mm, sizeof(mm), "%02d", _editCopy.minute);
    formatDays(_editCopy.days, days, sizeof(days));
    snprintf(vol, sizeof(vol), "%d", _editCopy.volume);
    soundLabel(_editCopy, snd, sizeof(snd));
    struct { Ui2Icon icon; const char* label; const char* value; } f[FIELD_COUNT] = {
      { UI2_IC_BELL,    MECK_TR("Enabled", "Activ\xC3\xA9"), nullptr },
      { UI2_IC_CLOCK,   MECK_TR("Hour", "Heure"), hh },
      { UI2_IC_CLOCK,   "Minute", mm },
      { UI2_IC_CALENDAR, MECK_TR("Days", "Jours"), days },
      { UI2_IC_SPEAKER, "Volume", vol },
      { UI2_IC_AUDIO,   MECK_TR("Sound", "Son"), snd },
    };
    for (int i = 0; i < FIELD_COUNT; i++) {
      Ui2Row r;
      r.icon = f[i].icon;
      r.title = f[i].label;
      r.right = f[i].value;
      r.rightMax = 130;
      r.height = rowH;
      if (i == FIELD_ENABLED) r.toggle = _editCopy.enabled ? 1 : 0;
      if (i == FIELD_SOUND) r.chevron = true;
      r.selected = (i == (int)_editField);
      Ui2List::drawRow(c, BODY_Y + i * rowH, r);
    }

    int16_t y = BODY_Y + FIELD_COUNT * rowH + 6;
    if (_editField == FIELD_DAYS) {
      // Day toggles, Sunday first as stored (bit 0 = Sunday)
      int16_t w = -4;
      for (int d = 0; d < 7; d++) w += Ui2Canvas::chipWidth(dowShort(d)) + 4;
      int16_t x = (SCREEN_W - w) / 2;
      for (int d = 0; d < 7; d++) x += c.chip(x, y, dowShort(d), UI2_NO_ICON, (_editCopy.days & (1 << d)) != 0) + 4;
    } else {
      const char* hint = (_editField == FIELD_HOUR || _editField == FIELD_MINUTE)
                           ? MECK_TR("A/D adjust \xC2\xB7 Enter types digits", "A/D r\xC3\xA9gler \xC2\xB7 Entr\xC3\xA9" "e saisie")
                           : (_editField == FIELD_SOUND) ? MECK_TR("Enter or A/D picks a sound", "Entr\xC3\xA9" "e ou A/D : choisir un son")
                           : MECK_TR("A/D adjust", "A/D r\xC3\xA9gler");
      c.textFit(PAD, y + 11, SCREEN_W - 2 * PAD, hint, UI2_F_META, true, UI2_CENTER);
    }

    if (_digitEntry) {
      // Digit entry box over the list
      const int16_t bx = DLG_X, bw = DLG_W, by = 112, bh = 76;
      c.fill(bx + 3, by + 3, bw, bh);
      c.fill(bx, by, bw, bh, false);
      c.rect(bx, by, bw, bh);
      c.rect(bx + 1, by + 1, bw - 2, bh - 2);
      c.text(bx + 12, by + 22, (_editField == FIELD_HOUR) ? MECK_TR("Hour (0-23)", "Heure (0-23)") : MECK_TR("Minute (0-59)", "Minute (0-59)"),
             UI2_F_TITLE);
      // Typed digits, then a bar cursor (the digit font has no underscore)
      int16_t cx = c.text(bx + 12, by + 58, _digitBuf, UI2_F_NUM22);
      c.fill(cx + 2, by + 56, 14, 3);
      const Ui2Key keys[] = {
        ui2KeyIcon(UI2_IC_ENTER, "OK"),
        ui2Key("0-9", MECK_TR("Type", "Saisir")),
        ui2Key("Sh+Del", MECK_TR("Cancel", "Annuler")),
      };
      Ui2Chrome::footer(c, keys, 3);
      return;
    }
    const Ui2Key keys[] = {
      ui2Key("A/D", MECK_TR("Adjust", "R\xC3\xA9gler"), true),
      ui2Key("W/S", MECK_TR("Field", "Champ"), true),
      ui2Key("Q", MECK_TR("Save", "OK")),
    };
    Ui2Chrome::footer(c, keys, 3);
  }

  void renderSoundPickerV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    const int total = kVibrateRows + (int)_soundFiles.size();
    char right[16];
    Ui2List::pageLabel(right, sizeof(right), _soundSelected, total, ROWS_PER_PAGE);
    Ui2Chrome::titleBar(c, MECK_TR("Alarm sound", "Son du r\xC3\xA9veil"), right);
    if (total == 0) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_AUDIO, MECK_TR("No alarm tones", "Aucune sonnerie"),
                          MECK_TR("Put 44 kHz .mp3 files in /alarms/ on the SD card.",
                                  "Placez des .mp3 44 kHz dans /alarms/ sur la SD."));
      const Ui2Key keys[] = { ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 1);
      return;
    }
    const int start = Ui2List::pageStart(_soundSelected, ROWS_PER_PAGE);
    for (int i = start; i < total && i < start + ROWS_PER_PAGE; i++) {
      Ui2Row r;
      String name;
      if (kVibrateRows && i == 0) {
        r.icon = UI2_IC_SILENT;
        r.title = MECK_TR("Buzzer (vibrate)", "Buzzer (vibreur)");
      } else {
        name = _soundFiles[i - kVibrateRows];
        int dot = name.lastIndexOf('.');
        if (dot > 0) name = name.substring(0, dot);
        r.icon = UI2_IC_AUDIO;
        r.title = name.c_str();
      }
      r.selected = (i == _soundSelected);
      Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Pick", "Choisir")),
      ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 3);
  }

  // Ringing: big time, alarm name and sound, and how to stop it
  void renderRingingV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    const AlarmSlot& slot = _config.slots[_ringingSlot];
    c.icon((SCREEN_W - 32) / 2, 56, UI2_IC_ALARM, 32);
    char time[8];
    snprintf(time, sizeof(time), "%02d:%02d", slot.hour, slot.minute);
    c.textFit(0, 150, SCREEN_W, time, UI2_F_NUM40, true, UI2_CENTER);
    char label[24], snd[40];
    snprintf(label, sizeof(label), MECK_TR("Alarm %d", "R\xC3\xA9veil %d"), _ringingSlot + 1);
    c.textFit(PAD, 180, SCREEN_W - 2 * PAD, label, UI2_F_TITLE, true, UI2_CENTER);
    soundLabel(slot, snd, sizeof(snd));
    c.textFit(PAD, 200, SCREEN_W - 2 * PAD, snd, UI2_F_BODY, true, UI2_CENTER);
    if (meck_alarm_is_locked()) {
      c.textFit(PAD, 250, SCREEN_W - 2 * PAD, MECK_TR("Unlock to dismiss", "D\xC3\xA9verrouillez pour arr\xC3\xAAter"),
                UI2_F_HEAD16, true, UI2_CENTER);
    } else {
      c.textFit(PAD, 250, SCREEN_W - 2 * PAD, MECK_TR("Any key dismisses", "Une touche arr\xC3\xAAte"),
                UI2_F_HEAD16, true, UI2_CENTER);
      const Ui2Key keys[] = { ui2Key("Z", MECK_TR("Snooze 5 min", "Rappel 5 min")) };
      Ui2Chrome::footer(c, keys, 1);
    }
  }
#endif

  // ---- Render: Alarm list ----

  void renderAlarmList(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("Alarm Clock", "R\xC3\xA9veil"));

    if (_soundFiles.empty() && _sdReady) {
      // Show hint if no sounds yet
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(0);
      display.setCursor(0, 13);
      display.print(MECK_TR("Place 44kHz .mp3 in /alarms/", ".mp3 (44 kHz) dans /alarms/"));
    }

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(0);

    int itemHeight = 18;  // Two lines per slot: time + info
    int listTop = _soundFiles.empty() ? 22 : 13;
    int listBottom = display.height() - 14;
    int visibleItems = (listBottom - listTop) / itemHeight;
    if (visibleItems < 1) visibleItems = 1;

    // Keep selection visible
    if (_selectedSlot < _scrollOffset) _scrollOffset = _selectedSlot;
    if (_selectedSlot >= _scrollOffset + visibleItems) _scrollOffset = _selectedSlot - visibleItems + 1;

    for (int i = 0; i < visibleItems && (_scrollOffset + i) < ALARM_SLOT_COUNT; i++) {
      int idx = _scrollOffset + i;
      int y = listTop + i * itemHeight;
      const AlarmSlot& slot = _config.slots[idx];

      // Selection highlight
      if (idx == _selectedSlot) {
        display.setColor(DisplayDriver::LIGHT);
        // Size-0 text: the built-in Classic font draws below the cursor
        // (offset 5); the Noto/Montserrat 7pt fonts draw up from a baseline
        // (offset 0). Same values as NodePrefs::smallHighlightOff() gives at the
        // tiny size; this screen's list text is always size 0.
        display.fillRect(0, y + (display.getFontStyle() > 0 ? 0 : 5), display.width(), itemHeight - 1);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(slot.enabled ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
      }

      // Line 1: Alarm number + time + enabled
      char line1[40];
      snprintf(line1, sizeof(line1), "%d. %s %02d:%02d",
               idx + 1, slot.enabled ? MECK_TR("ON ", "OUI") : MECK_TR("OFF", "NON"), slot.hour, slot.minute);
      display.setCursor(0, y);
      display.print(line1);

      // Line 2: days + sound name
      if (idx == _selectedSlot) {
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      char daysBuf[24];
      formatDays(slot.days, daysBuf, sizeof(daysBuf));
      char line2[48];
      if (slot.sound[0] != '\0') {
        // Truncate sound name for display
        char sndShort[16];
        strncpy(sndShort, slot.sound, 15);
        sndShort[15] = '\0';
        // Strip extension for cleaner display
        char* dot = strrchr(sndShort, '.');
        if (dot) *dot = '\0';
        snprintf(line2, sizeof(line2), "  %s | %s", daysBuf, sndShort);
      } else {
        snprintf(line2, sizeof(line2), "  %s", daysBuf);
      }
      display.setCursor(0, y + 8);
      display.print(line2);
    }

    drawFooter(display, MECK_TR("O:On/Off Enter:Edit", "O:Oui/Non Ent:\xC3\x89" "diter"), "Q:X");
  }

  // ---- Render: Edit alarm ----

  void renderEditAlarm(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    char hdr[24];
    snprintf(hdr, sizeof(hdr), MECK_TR("Edit Alarm %d", "Modifier le r\xC3\xA9veil %d"), _editSlot + 1);
    display.print(hdr);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    display.setTextSize(0);
    int y = 16;
    int lineH = 10;

    // Field labels and values
    struct {
      const char* label;
      char value[32];
    } fields[FIELD_COUNT];

    snprintf(fields[FIELD_ENABLED].value, 32, "%s", _editCopy.enabled ? MECK_TR("ON", "OUI") : MECK_TR("OFF", "NON"));
    fields[FIELD_ENABLED].label = MECK_TR("Enabled", "Activ\xC3\xA9");

    snprintf(fields[FIELD_HOUR].value, 32, "%02d", _editCopy.hour);
    fields[FIELD_HOUR].label = MECK_TR("Hour", "Heure");

    snprintf(fields[FIELD_MINUTE].value, 32, "%02d", _editCopy.minute);
    fields[FIELD_MINUTE].label = "Minute";

    char daysBuf[24];
    formatDays(_editCopy.days, daysBuf, sizeof(daysBuf));
    strncpy(fields[FIELD_DAYS].value, daysBuf, 31);
    fields[FIELD_DAYS].value[31] = '\0';
    fields[FIELD_DAYS].label = MECK_TR("Days", "Jours");

    snprintf(fields[FIELD_VOLUME].value, 32, "%d", _editCopy.volume);
    fields[FIELD_VOLUME].label = "Volume";

#if defined(LilyGo_TDeck_Pro_Max)
    if (slotIsVibrate(_editCopy)) {
      strncpy(fields[FIELD_SOUND].value, MECK_TR("Buzzer (vibrate)", "Buzzer (vibreur)"), 31);
      fields[FIELD_SOUND].value[31] = '\0';
    } else
#endif
    if (_editCopy.sound[0] != '\0') {
      char sndDisplay[28];
      strncpy(sndDisplay, _editCopy.sound, 27);
      sndDisplay[27] = '\0';
      char* dot = strrchr(sndDisplay, '.');
      if (dot) *dot = '\0';
      strncpy(fields[FIELD_SOUND].value, sndDisplay, 31);
    } else {
      strcpy(fields[FIELD_SOUND].value, MECK_TR("(default)", "(d\xC3\xA9" "faut)"));
    }
    fields[FIELD_SOUND].label = MECK_TR("Sound", "Son");

    for (int f = 0; f < FIELD_COUNT; f++) {
      int fy = y + f * lineH;

      if (f == (int)_editField) {
        display.setColor(DisplayDriver::LIGHT);
        // Size-0 text: the built-in Classic font draws below the cursor
        // (offset 5); the Noto/Montserrat 7pt fonts draw up from a baseline
        // (offset 0). Same values as NodePrefs::smallHighlightOff() gives at the
        // tiny size; this screen's list text is always size 0.
        display.fillRect(0, fy + (display.getFontStyle() > 0 ? 0 : 5), display.width(), lineH - 1);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      char line[48];
      snprintf(line, sizeof(line), "%-8s: %s", fields[f].label, fields[f].value);
      display.setCursor(0, fy);
      display.print(line);

      // Show A/D arrows on selected field
      if (f == (int)_editField) {
        display.setCursor(display.width() - 18, fy);
        if (_editField == FIELD_SOUND) {
          display.print(".."); // Indicate Enter opens picker
        } else {
          display.print("<>");
        }
      }
    }

    // Days detail when on DAYS field
    if (_editField == FIELD_DAYS) {
      int detailY = y + FIELD_COUNT * lineH + 4;
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, detailY);
      display.print(MECK_TR("A/D: toggle day  ", "A/D : jour oui/non  "));
      // Show individual day toggles
      display.setCursor(0, detailY + 9);
      for (int d = 0; d < 7; d++) {
        bool on = (_editCopy.days & (1 << d));
        display.setColor(on ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
        char db[4];
        snprintf(db, sizeof(db), "%s ", dowShort(d));
        display.print(db);
      }
    }

    // Digit entry overlay (for hour/minute direct input)
    if (_digitEntry) {
      int bx = 10, by = 40, bw = display.width() - 20, bh = 30;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, bh);

      display.setTextSize(1);
      display.setColor(DisplayDriver::GREEN);
      const char* prompt = (_editField == FIELD_HOUR) ? MECK_TR("Hour (0-23):", "Heure (0-23) :") : MECK_TR("Min (0-59):", "Min (0-59) :");
      display.setCursor(bx + 4, by + 4);
      display.print(prompt);

      // Show typed digits with cursor
      display.setColor(DisplayDriver::LIGHT);
      char inputDisplay[8];
      snprintf(inputDisplay, sizeof(inputDisplay), "%s_", _digitBuf);
      display.setCursor(bx + 4, by + 16);
      display.print(inputDisplay);

      drawFooter(display, MECK_TR("Type digits", "Chiffres"), MECK_TR("Enter:OK Sh+Del:Cancel", "Ent:OK Sh+Del:Annuler"));
    } else {
      drawFooter(display, MECK_TR("A/D:Adjust Enter:Type", "A/D:R\xC3\xA9gler Ent:Saisie"), MECK_TR("Q:Save", "Q:OK"));
    }
  }

  // ---- Render: Sound picker ----

  void renderSoundPicker(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("Pick Alarm Sound", "Choisir le son du r\xC3\xA9veil"));

    display.setColor(DisplayDriver::LIGHT);

    if (_soundFiles.empty() && kVibrateRows == 0) {
      display.setTextSize(0);
      display.setCursor(0, 20);
      display.print(MECK_TR("No .mp3 files found.", "Aucun fichier .mp3 trouv\xC3\xA9."));
      display.setCursor(0, 30);
      display.print(MECK_TR("Place 44kHz .mp3 in", "Placez des .mp3 44 kHz dans"));
      display.setCursor(0, 38);
      display.print(MECK_TR("/alarms/ on SD card", "/alarms/ sur la carte SD"));
      drawFooter(display, MECK_TR("0 files", "0 fichier"), MECK_TR("Q:Back", "Q:Retour"));
      return;
    }

    display.setTextSize(0);
    int itemHeight = 8;
    int listTop = 13;
    int listBottom = display.height() - 14;
    int visibleItems = (listBottom - listTop) / itemHeight;
    int totalItems = kVibrateRows + (int)_soundFiles.size();

    if (_soundSelected < _soundScroll) _soundScroll = _soundSelected;
    if (_soundSelected >= _soundScroll + visibleItems) _soundScroll = _soundSelected - visibleItems + 1;

    for (int i = 0; i < visibleItems && (_soundScroll + i) < totalItems; i++) {
      int idx = _soundScroll + i;
      int y = listTop + i * itemHeight;

      if (idx == _soundSelected) {
        display.setColor(DisplayDriver::LIGHT);
        // Size-0 text: the built-in Classic font draws below the cursor
        // (offset 5); the Noto/Montserrat 7pt fonts draw up from a baseline
        // (offset 0). Same values as NodePrefs::smallHighlightOff() gives at the
        // tiny size; this screen's list text is always size 0.
        display.fillRect(0, y + (display.getFontStyle() > 0 ? 0 : 5), display.width(), itemHeight - 1);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      String displayName;
      if (kVibrateRows && idx == 0) {
        displayName = MECK_TR("Buzzer (vibrate)", "Buzzer (vibreur)");
      } else {
        // Display filename without extension
        displayName = _soundFiles[idx - kVibrateRows];
        int dot = displayName.lastIndexOf('.');
        if (dot > 0) displayName = displayName.substring(0, dot);
      }
      // Truncate if too long
      if (displayName.length() > 34) displayName = displayName.substring(0, 34);

      display.setCursor(0, y);
      display.print(displayName.c_str());
    }

    char countBuf[12];
    snprintf(countBuf, sizeof(countBuf), MECK_TR("%d files", "%d fichiers"), (int)_soundFiles.size());
    drawFooter(display, countBuf, MECK_TR("Enter:Pick Q:X", "Ent:Choisir Q:X"));
  }

  // ---- Render: Ringing ----

  void renderRinging(DisplayDriver& display) {
    const AlarmSlot& slot = _config.slots[_ringingSlot];

    // Big centered time
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(2);
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", slot.hour, slot.minute);
    display.drawTextCentered(display.width() / 2, 10, timeBuf);

    // Alarm label
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    char label[24];
    snprintf(label, sizeof(label), MECK_TR("Alarm %d", "R\xC3\xA9veil %d"), _ringingSlot + 1);
    display.drawTextCentered(display.width() / 2, 34, label);

    // Sound name
#if defined(LilyGo_TDeck_Pro_Max)
    if (slotIsVibrate(slot)) {
      display.setTextSize(0);
      display.drawTextCentered(display.width() / 2, 48, MECK_TR("Buzzer (vibrate)", "Buzzer (vibreur)"));
    } else
#endif
    if (slot.sound[0] != '\0') {
      display.setTextSize(0);
      char sndDisplay[24];
      strncpy(sndDisplay, slot.sound, 23);
      sndDisplay[23] = '\0';
      char* dot = strrchr(sndDisplay, '.');
      if (dot) *dot = '\0';
      display.drawTextCentered(display.width() / 2, 48, sndDisplay);
    }

    // Dismiss instruction — large and obvious
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);
    if (meck_alarm_is_locked()) {
      // Keys are ignored while locked; the user unlocks with the button first.
      display.drawTextCentered(display.width() / 2, 64, MECK_TR("Unlock to dismiss", "D\xC3\xA9verrouillez pour arr\xC3\xAAter"));
    } else {
      display.drawTextCentered(display.width() / 2, 64, MECK_TR("ANY KEY: Dismiss", "UNE TOUCHE : arr\xC3\xAAter"));

      display.setTextSize(0);
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(display.width() / 2, 80, MECK_TR("Z: Snooze 5 min", "Z : rappel dans 5 min"));
    }

    // No footer in ringing mode — keep it clean and urgent
  }

  // ---- Input: Alarm list ----

  bool handleAlarmListInput(char c) {
    // A/D - scroll up/down (w/s are global nav keys)
    if (c == 'a' || c == 0xF2) {
      if (_selectedSlot > 0) _selectedSlot--;
      return true;
    }
    if (c == 'd' || c == 0xF1) {
      if (_selectedSlot < ALARM_SLOT_COUNT - 1) _selectedSlot++;
      return true;
    }

    // O - toggle enable
    if (c == 'o') {
      _config.slots[_selectedSlot].enabled = !_config.slots[_selectedSlot].enabled;
      saveConfig();
      return true;
    }

    // Enter - edit alarm
    if (c == '\r' || c == '\n') {
      _editSlot = _selectedSlot;
      memcpy(&_editCopy, &_config.slots[_editSlot], sizeof(AlarmSlot));
      _editField = FIELD_ENABLED;
      _mode = EDIT_ALARM;
      return true;
    }

    return false;
  }

  // ---- Input: Edit alarm ----

  bool handleEditAlarmInput(char c) {
    // ---- Digit entry sub-mode (typing hour or minute directly) ----
    if (_digitEntry) {
      // Digits 0-9
      if (c >= '0' && c <= '9' && _digitPos < 2) {
        _digitBuf[_digitPos++] = c;
        _digitBuf[_digitPos] = '\0';
        return true;
      }
      // Backspace
      if (c == '\b' || c == 0x7F) {
        if (_digitPos > 0) {
          _digitBuf[--_digitPos] = '\0';
        }
        return true;
      }
      // Enter - confirm digit entry and advance to next field
      if (c == '\r' || c == '\n') {
        if (_digitPos > 0) {
          int val = atoi(_digitBuf);
          if (_editField == FIELD_HOUR) {
            _editCopy.hour = constrain(val, 0, 23);
          } else if (_editField == FIELD_MINUTE) {
            _editCopy.minute = constrain(val, 0, 59);
          }
        }
        _digitEntry = false;
        // Auto-advance to next field so Enter doesn't re-open digit entry
        int f = (int)_editField;
        if (f < FIELD_COUNT - 1) _editField = (EditField)(f + 1);
        return true;
      }
      // Shift+Del - cancel digit entry
      if (c == KEY_CANCEL) {
        _digitEntry = false;
        return true;
      }
      return true;  // Consume all other keys while in digit mode
    }

    // ---- Normal edit field navigation ----

    // W/S - move between fields
    if (c == 'w' || c == 0xF2) {
      int f = (int)_editField;
      if (f > 0) _editField = (EditField)(f - 1);
      return true;
    }
    if (c == 's' || c == 0xF1) {
      int f = (int)_editField;
      if (f < FIELD_COUNT - 1) _editField = (EditField)(f + 1);
      return true;
    }

    // A/D - adjust value
    if (c == 'a' || c == 'd') {
      int dir = (c == 'd') ? 1 : -1;
      switch (_editField) {
        case FIELD_ENABLED:
          _editCopy.enabled = !_editCopy.enabled;
          break;
        case FIELD_HOUR:
          _editCopy.hour = (_editCopy.hour + 24 + dir) % 24;
          break;
        case FIELD_MINUTE:
          _editCopy.minute = (_editCopy.minute + 60 + dir) % 60;
          break;
        case FIELD_DAYS: {
          // Cycle through individual days: A goes backward, D forward
          static int dayCursor = 0;
          dayCursor = (dayCursor + 7 + dir) % 7;
          _editCopy.days ^= (1 << dayCursor);  // Toggle that day
          break;
        }
        case FIELD_VOLUME:
          _editCopy.volume = constrain((int)_editCopy.volume + dir, 1, 21);
          break;
        case FIELD_SOUND:
          // A/D on sound field: open picker instead of adjust
          scanSoundFiles();
          _soundSelected = 0;
          _soundScroll = 0;
#if defined(LilyGo_TDeck_Pro_Max)
          if (slotIsVibrate(_editCopy)) {
            _soundSelected = 0;
          } else
#endif
          if (_editCopy.sound[0] != '\0') {
            for (int i = 0; i < (int)_soundFiles.size(); i++) {
              if (_soundFiles[i] == String(_editCopy.sound)) {
                _soundSelected = kVibrateRows + i;
                break;
              }
            }
          }
          _mode = PICK_SOUND;
          break;
        default: break;
      }
      return true;
    }

    // Enter - context-dependent: digit entry for hour/minute, picker for sound, save otherwise
    if (c == '\r' || c == '\n') {
      if (_editField == FIELD_HOUR || _editField == FIELD_MINUTE) {
        // Start digit entry — pre-fill with current value
        _digitEntry = true;
        _digitPos = 0;
        _digitBuf[0] = '\0';
        return true;
      }
      if (_editField == FIELD_SOUND) {
        // Open sound picker
        scanSoundFiles();
        _soundSelected = 0;
        _soundScroll = 0;
#if defined(LilyGo_TDeck_Pro_Max)
        if (slotIsVibrate(_editCopy)) {
          _soundSelected = 0;
        } else
#endif
        if (_editCopy.sound[0] != '\0') {
          for (int i = 0; i < (int)_soundFiles.size(); i++) {
            if (_soundFiles[i] == String(_editCopy.sound)) {
              _soundSelected = kVibrateRows + i;
              break;
            }
          }
        }
        _mode = PICK_SOUND;
      } else {
        // Save and exit edit
        memcpy(&_config.slots[_editSlot], &_editCopy, sizeof(AlarmSlot));
        saveConfig();
        _mode = ALARM_LIST;
      }
      return true;
    }

    // Shift+Del - save and exit edit
    if (c == KEY_CANCEL || c == 'q') {
      memcpy(&_config.slots[_editSlot], &_editCopy, sizeof(AlarmSlot));
      saveConfig();
      _mode = ALARM_LIST;
      return true;
    }

    return false;
  }

  // ---- Input: Sound picker ----

  bool handleSoundPickerInput(char c) {
    if (c == 'w' || c == 0xF2) {
      if (_soundSelected > 0) _soundSelected--;
      return true;
    }
    if (c == 's' || c == 0xF1) {
      if (_soundSelected < kVibrateRows + (int)_soundFiles.size() - 1) _soundSelected++;
      return true;
    }

    // Enter - pick sound
    if (c == '\r' || c == '\n') {
#if defined(LilyGo_TDeck_Pro_Max)
      if (_soundSelected == 0) {
        // Synthetic "Buzzer (vibrate)" row
        strncpy(_editCopy.sound, ALARM_VIBRATE_MARKER, ALARM_SOUND_MAX - 1);
        _editCopy.sound[ALARM_SOUND_MAX - 1] = '\0';
      } else
#endif
      {
        int fileIdx = _soundSelected - kVibrateRows;
        if (fileIdx >= 0 && fileIdx < (int)_soundFiles.size()) {
          strncpy(_editCopy.sound, _soundFiles[fileIdx].c_str(), ALARM_SOUND_MAX - 1);
          _editCopy.sound[ALARM_SOUND_MAX - 1] = '\0';
        }
      }
      _mode = EDIT_ALARM;
      return true;
    }

    // Shift+Del - cancel
    if (c == KEY_CANCEL || c == 'q') {
      _mode = EDIT_ALARM;
      return true;
    }

    return false;
  }

  // ---- Input: Ringing (ANY key dismisses, Z snoozes) ----

  bool handleRingingInput(char c) {
    if (c == 'z') {
      // Snooze
      stopAlarmAudio();
      _ringing = false;
      _snoozed = true;
      _snoozeUntil = millis() + ALARM_SNOOZE_MS;
      Serial.println("ALARM: Snoozed for 5 minutes");
      _mode = ALARM_LIST;
      return true;
    }
    // ANY other key: dismiss
    dismiss();
    return true;
  }

public:
  AlarmScreen(UITask* task) : _task(task) {
    _mode = ALARM_LIST;
    _sdReady = false;
    _selectedSlot = 0;
    _scrollOffset = 0;
    _editSlot = 0;
    _editField = FIELD_ENABLED;
    _digitEntry = false;
    _digitBuf[0] = '\0';
    _digitPos = 0;
    _soundSelected = 0;
    _soundScroll = 0;
    _ringing = false;
    _ringingSlot = 0;
    _ringingStart = 0;
    _snoozed = false;
    _snoozeUntil = 0;
    _audio = nullptr;
    _alarmAudioActive = false;
    _resolvedSoundPath = "";
    _restartAttempts = 0;
#if defined(LilyGo_TDeck_Pro_Max)
    _vibrating = false;
    _vibBuzzCount = 0;
    _vibNextMs = 0;
#endif
    memset(_lastFiredEpoch, 0, sizeof(_lastFiredEpoch));
    memset(&_editCopy, 0, sizeof(_editCopy));
    loadConfig();
  }

  void setSDReady(bool ready) { _sdReady = ready; }
  void setAudio(Audio* audio) { _audio = audio; }

  // Called from main.cpp when entering the screen
  void enter(DisplayDriver& display) {
    if (_ringing) {
      _mode = RINGING;
    } else {
      _mode = ALARM_LIST;
      // Refresh sound file list on entry
      if (_sdReady) scanSoundFiles();
    }
  }

  // ---- Public state queries ----

  bool isRinging() const { return _ringing; }
  bool isAlarmAudioActive() const { return _alarmAudioActive; }
  bool isSnoozed() const { return _snoozed; }
  Mode getMode() const { return _mode; }

  // How many alarms are enabled (for home screen indicator)
  int enabledCount() const {
    int n = 0;
    for (int i = 0; i < ALARM_SLOT_COUNT; i++) {
      if (_config.slots[i].enabled) n++;
    }
    return n;
  }

  // ---- Dismiss alarm (callable from main.cpp for any-key handling) ----

  void dismiss() {
    stopAlarmAudio();
    _ringing = false;
    _snoozed = false;
    _mode = ALARM_LIST;
    Serial.println("ALARM: Dismissed");
  }

  // ---- Background alarm check (called from main loop every ~10s) ----
  // Returns the slot index if an alarm should fire NOW, or -1 if not.

  int checkAlarms(uint32_t rtcEpoch, int8_t utcOffsetHours) {
    if (rtcEpoch < 1704067200UL) return -1;  // No valid time

    // Apply timezone offset
    int32_t localEpoch = (int32_t)rtcEpoch + ((int32_t)utcOffsetHours * 3600);
    if (localEpoch < 0) return -1;

    int localHour = ((uint32_t)localEpoch / 3600) % 24;
    int localMinute = ((uint32_t)localEpoch / 60) % 60;
    int dow = dowFromEpoch((uint32_t)localEpoch);

    // Check snooze wake-up first
    if (_snoozed && millis() >= _snoozeUntil) {
      _snoozed = false;
      return _ringingSlot;  // Re-fire the snoozed alarm
    }

    for (int i = 0; i < ALARM_SLOT_COUNT; i++) {
      const AlarmSlot& slot = _config.slots[i];
      if (!slot.enabled) continue;
      if (slot.hour != localHour || slot.minute != localMinute) continue;
      if (!(slot.days & (1 << dow))) continue;

      // Cooldown: don't re-fire if we fired this alarm recently
      if (rtcEpoch - _lastFiredEpoch[i] < ALARM_FIRE_COOLDOWN_S) continue;

      return i;
    }
    return -1;
  }

  // ---- Fire alarm (called from main loop when checkAlarms returns >= 0) ----

  void fireAlarm(int slotIdx) {
    _ringingSlot = slotIdx;
    _ringing = true;
    _ringingStart = millis();
    _lastFiredEpoch[slotIdx] = millis() / 1000;  // Approximate — replaced by RTC below
    _mode = RINGING;
#if defined(LilyGo_TDeck_Pro_Max)
    if (slotIsVibrate(_config.slots[slotIdx])) {
      startVibrate();
    } else
#endif
    {
      startAlarmAudio(slotIdx);
    }
    Serial.printf("ALARM: Firing alarm %d (%02d:%02d)\n",
                  slotIdx + 1, _config.slots[slotIdx].hour, _config.slots[slotIdx].minute);
  }

  // Update last-fired with actual RTC epoch (called right after fireAlarm)
  void setLastFiredEpoch(int slotIdx, uint32_t epoch) {
    if (slotIdx >= 0 && slotIdx < ALARM_SLOT_COUNT) {
      _lastFiredEpoch[slotIdx] = epoch;
    }
  }

  // ---- Audio tick (called from main loop for alarm playback) ----

  void alarmAudioTick() {
#if defined(LilyGo_TDeck_Pro_Max)
    if (_vibrating) {
      // Auto-timeout (mirrors the audio path's 5-minute auto-dismiss)
      if (_ringing && (millis() - _ringingStart > ALARM_RINGING_TIMEOUT_MS)) {
        dismiss();
        return;
      }
      // ALARM_VIBRATE_GROUP buzzes spaced ALARM_VIBRATE_BUZZ_MS apart, then a
      // ALARM_VIBRATE_PAUSE_MS pause, repeating until dismiss/snooze/timeout.
      if ((long)(millis() - _vibNextMs) >= 0) {
        meck_alarm_haptic_buzz();
        _vibBuzzCount++;
        if (_vibBuzzCount >= ALARM_VIBRATE_GROUP) {
          _vibBuzzCount = 0;
          _vibNextMs = millis() + ALARM_VIBRATE_PAUSE_MS;
        } else {
          _vibNextMs = millis() + ALARM_VIBRATE_BUZZ_MS;
        }
      }
      return;
    }
#endif
    if (!_audio || !_alarmAudioActive) return;

    _audio->loop();

    // Auto-timeout
    if (_ringing && (millis() - _ringingStart > ALARM_RINGING_TIMEOUT_MS)) {
      Serial.println("ALARM: Auto-dismiss (timeout)");
      dismiss();
      return;
    }

    // If audio ended (short file), loop it by restarting.
    // Grace period: don't check isRunning() in first 2 seconds (file may still
    // be buffering/decoding headers, and isRunning() can be false briefly).
    if (_alarmAudioActive && (millis() - _ringingStart > 2000) && !_audio->isRunning()) {
      if (_restartAttempts >= 3) {
        // Give up after 3 failed restarts — file is broken or unsupported
        Serial.println("ALARM: Audio restart failed 3 times, giving up");
        _alarmAudioActive = false;
        return;
      }
      _restartAttempts++;
      if (_resolvedSoundPath.length() > 0) {
        Serial.printf("ALARM: Audio ended, restarting loop (attempt %d)\n", _restartAttempts);
        _audio->connecttoFS(SD, _resolvedSoundPath.c_str());
        _audio->setVolume(_config.slots[_ringingSlot].volume);
      } else {
        Serial.println("ALARM: No resolved path for restart");
        _alarmAudioActive = false;
      }
    }
  }

  // ---- UIScreen interface ----

  int render(DisplayDriver& display) override {
    if (!_sdReady) {
#ifdef MECK_UI_V2
      Ui2Canvas c(display);
      Ui2Chrome::statusHeader(c);
      Ui2Chrome::titleBar(c, MECK_TR("Alarm clock", "R\xC3\xA9veil"));
      Ui2List::emptyState(c, ui2::BODY_Y, ui2::FOOTER_Y, UI2_IC_SD, MECK_TR("No SD card", "Pas de carte SD"),
                          MECK_TR("Insert an SD card with an /alarms/ folder.", "Ins\xC3\xA9rez une SD avec un dossier /alarms/."));
      const Ui2Key keys[] = { ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 1);
      return 5000;
#endif
      display.setTextSize(1);
      display.setColor(DisplayDriver::RED);
      display.setCursor(0, 20);
      display.print(MECK_TR("No SD card", "Pas de carte SD"));
      display.setCursor(0, 35);
      display.print(MECK_TR("Insert SD card and", "Ins\xC3\xA9rez une carte SD et"));
      display.setCursor(0, 43);
      display.print(MECK_TR("create /alarms/", "cr\xC3\xA9" "ez /alarms/"));
      return 5000;
    }

    switch (_mode) {
#ifdef MECK_UI_V2
      case ALARM_LIST:  renderAlarmListV2(display); break;
      case EDIT_ALARM:  renderEditAlarmV2(display); break;
      case PICK_SOUND:  renderSoundPickerV2(display); break;
      case RINGING:     renderRingingV2(display); break;
#else
      case ALARM_LIST:  renderAlarmList(display); break;
      case EDIT_ALARM:  renderEditAlarm(display); break;
      case PICK_SOUND:  renderSoundPicker(display); break;
      case RINGING:     renderRinging(display); break;
#endif
    }

    // Refresh rates: fast during ringing (for timeout), normal otherwise
    if (_mode == RINGING) return 1000;
    return 5000;
  }

  bool handleInput(char c) override {
    switch (_mode) {
      case ALARM_LIST:  return handleAlarmListInput(c);
      case EDIT_ALARM:  return handleEditAlarmInput(c);
      case PICK_SOUND:  return handleSoundPickerInput(c);
      case RINGING:     return handleRingingInput(c);
    }
    return false;
  }
};