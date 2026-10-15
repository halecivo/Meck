#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#include <MeshCore.h>
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
#endif

// Forward declarations
#include "../AbstractUITask.h"
class MyMesh;
extern MyMesh the_mesh;

#define ADMIN_PASSWORD_MAX    32
#define ADMIN_PARAM_MAX       64   // Max length for set-command parameter input
#define ADMIN_RESPONSE_MAX    1024 // CLI responses can be multi-line / large neighbor lists
#define ADMIN_TIMEOUT_MS      15000 // 15s timeout for login/commands
#define KEY_ADMIN_EXIT        0xFE  // Special key: Shift+Backspace exit (injected by main.cpp)

// --- Command definition flags ---
#define CMDF_PARAM            0x01  // Command needs user parameter input appended
#define CMDF_CONFIRM          0x02  // Needs y/n confirmation before send
#define CMDF_EXPECT_TIMEOUT   0x04  // Timeout is expected success (reboot/OTA)
#define CMDF_PARSE_NEIGHBORS  0x08  // Response should be parsed as neighbor list

// --- Single command definition ---
struct AdminCmdDef {
  const char* label;       // Menu display label
  const char* cmd;         // CLI command string (full, or prefix if CMDF_PARAM)
  const char* paramHint;   // Prompt for parameter input (NULL if not needed)
  uint8_t flags;
  const char* label_fr;     // French label (Experimental Features > Language)
  const char* paramHint_fr; // French prompt (NULL if not needed)
};

// =====================================================================
// Command tables per category
// =====================================================================

// --- Clock & Adverts ---
static const AdminCmdDef CMD_OPERATIONAL[] = {
  { "Clock Sync",    "clock sync",   nullptr,          0, "Synchro horloge", nullptr },
  { "Send Advert",   "advert",       nullptr,          0, "Envoyer une annonce", nullptr },
  { "Get Clock",     "clock",        nullptr,          0, "Lire l'horloge", nullptr },
};
#define CMD_OPERATIONAL_COUNT 3

// --- Neighbors ---
static const AdminCmdDef CMD_NEIGHBORS[] = {
  { "View Neighbors",  "neighbors",        nullptr,               CMDF_PARSE_NEIGHBORS, "Voir les voisins", nullptr },
  { "Remove Neighbor", "neighbor.remove ",  "Pubkey hex prefix:",  CMDF_PARAM, "Retirer un voisin", "Pr\xC3\xA9" "fixe hex de la cl\xC3\xA9 :" },
};
#define CMD_NEIGHBORS_COUNT 2

// --- Info ---
static const AdminCmdDef CMD_INFO[] = {
  { "Version",    "ver",    nullptr, 0, "Version", nullptr },
  { "Board",      "board",  nullptr, 0, "Carte", nullptr },
};
#define CMD_INFO_COUNT 2

// --- Get Config (read-only queries) ---
static const AdminCmdDef CMD_GET_CONFIG[] = {
  { "Get Name",             "get name",                  nullptr, 0, "Lire le nom", nullptr },
  { "Get TX Power",         "get tx",                    nullptr, 0, "Lire puissance TX", nullptr },
  { "Get AF",               "get af",                    nullptr, 0, "Lire AF", nullptr },
  { "Get Repeat",           "get repeat",                nullptr, 0, "Lire r\xC3\xA9p\xC3\xA9tition", nullptr },
  { "Get Radio",            "get radio",                 nullptr, 0, "Lire radio", nullptr },
  { "Get Flood Max",        "get flood.max",             nullptr, 0, "Lire flood max", nullptr },
  { "Get RX Delay",         "get rxdelay",               nullptr, 0, "Lire d\xC3\xA9lai RX", nullptr },
  { "Get TX Delay",         "get txdelay",               nullptr, 0, "Lire d\xC3\xA9lai TX", nullptr },
  { "Get Direct TX Delay",  "get direct.txdelay",        nullptr, 0, "Lire d\xC3\xA9lai TX direct", nullptr },
  { "Get Int Thresh",       "get int.thresh",            nullptr, 0, "Lire seuil interf.", nullptr },
  { "Get AGC Reset Int",    "get agc.reset.interval",    nullptr, 0, "Lire interv. reset AGC", nullptr },
  { "Get Multi Acks",       "get multi.acks",            nullptr, 0, "Lire multi-acks", nullptr },
  { "Get Advert Int",       "get advert.interval",       nullptr, 0, "Lire interv. annonce", nullptr },
  { "Get Flood Adv Int",    "get flood.advert.interval", nullptr, 0, "Lire interv. ann. flood", nullptr },
  { "Get Guest Password",   "get guest.password",        nullptr, 0, "Lire mdp invit\xC3\xA9", nullptr },
  { "Get Allow R/O",        "get allow.read.only",       nullptr, 0, "Lire acc\xC3\xA8s lecture seule", nullptr },
  { "Get ADC Multiplier",   "get adc.multiplier",        nullptr, 0, "Lire multiplicateur ADC", nullptr },
};
#define CMD_GET_CONFIG_COUNT 17

// --- Set Config (write commands, all need parameter input) ---
static const AdminCmdDef CMD_SET_CONFIG[] = {
  { "Set Name",             "set name ",                  "Name:",                 CMDF_PARAM, "R\xC3\xA9gler le nom", "Nom :" },
  { "Set TX Power",         "set tx ",                    "TX power (dBm):",       CMDF_PARAM, "R\xC3\xA9gler puissance TX", "Puissance TX (dBm) :" },
  { "Set AF",               "set af ",                    "Airtime factor:",       CMDF_PARAM, "R\xC3\xA9gler AF", "Facteur airtime :" },
  { "Set Repeat",           "set repeat ",                "on/off:",               CMDF_PARAM, "R\xC3\xA9gler r\xC3\xA9p\xC3\xA9tition", "on/off :" },
  { "Set Flood Max",        "set flood.max ",             "Max hops (0-64):",      CMDF_PARAM, "R\xC3\xA9gler flood max", "Sauts max (0-64) :" },
  { "Set Flood Max Unscoped", "set flood.max.unscoped ",  "Max hops (64=off):",    CMDF_PARAM, "R\xC3\xA9gler flood max global", "Sauts max (64=arr\xC3\xAAt) :" },
  { "Set Flood Adv Max",    "set flood.max.advert ",      "Max hops (def 8):",     CMDF_PARAM, "R\xC3\xA9gler flood max ann.", "Sauts max (d\xC3\xA9" "f. 8) :" },
  { "Set RX Delay",         "set rxdelay ",               "Base (0=off):",         CMDF_PARAM, "R\xC3\xA9gler d\xC3\xA9lai RX", "Base (0=arr\xC3\xAAt) :" },
  { "Set TX Delay",         "set txdelay ",               "Factor:",               CMDF_PARAM, "R\xC3\xA9gler d\xC3\xA9lai TX", "Facteur :" },
  { "Set Direct TX Delay",  "set direct.txdelay ",        "Factor:",               CMDF_PARAM, "R\xC3\xA9gler d\xC3\xA9lai TX direct", "Facteur :" },
  { "Set Int Thresh",       "set int.thresh ",            "dB (0=off, def 14):",   CMDF_PARAM, "R\xC3\xA9gler seuil interf.", "dB (0=arr\xC3\xAAt, d\xC3\xA9" "f. 14) :" },
  { "Set AGC Reset Int",    "set agc.reset.interval ",    "Seconds (0=off):",      CMDF_PARAM, "R\xC3\xA9gler interv. reset AGC", "Secondes (0=arr\xC3\xAAt) :" },
  { "Set Multi Acks",       "set multi.acks ",            "0 or 1:",               CMDF_PARAM, "R\xC3\xA9gler multi-acks", "0 ou 1 :" },
  { "Set Advert Interval",  "set advert.interval ",       "Minutes (0=off):",      CMDF_PARAM, "R\xC3\xA9gler interv. annonce", "Minutes (0=arr\xC3\xAAt) :" },
  { "Set Flood Adv Int",    "set flood.advert.interval ", "Hours (0=off, 3-48):",  CMDF_PARAM, "R\xC3\xA9gler interv. ann. flood", "Heures (0=arr\xC3\xAAt, 3-48) :" },
  { "Set Guest Password",   "set guest.password ",        "Password:",             CMDF_PARAM, "R\xC3\xA9gler mdp invit\xC3\xA9", "Mot de passe :" },
  { "Set Allow R/O",        "set allow.read.only ",       "on/off:",               CMDF_PARAM, "R\xC3\xA9gler acc\xC3\xA8s lect. seule", "on/off :" },
  { "Set ADC Multiplier",   "set adc.multiplier ",        "Factor (0=default):",   CMDF_PARAM, "R\xC3\xA9gler multiplic. ADC", "Facteur (0=d\xC3\xA9" "faut) :" },
  { "Set Radio",            "set radio ",                 "freq,bw,sf,cr:",        CMDF_PARAM, "R\xC3\xA9gler la radio", "freq,bw,sf,cr :" },
  { "Temp Radio",           "tempradio ",                 "freq,bw,sf,cr,mins:",   CMDF_PARAM, "Radio temporaire", "freq,bw,sf,cr,mins :" },
  { "Change Admin Pwd",     "password ",                  "New password:",          CMDF_PARAM | CMDF_CONFIRM, "Changer mdp admin", "Nouveau mot de passe :" },
};
#define CMD_SET_CONFIG_COUNT 21

// --- Power ---
static const AdminCmdDef CMD_POWER[] = {
  { "Powersaving Status", "powersaving",     nullptr, 0, "\xC3\x89tat \xC3\xA9" "conomie \xC3\xA9nergie", nullptr },
  { "Powersaving On",     "powersaving on",  nullptr, 0, "\xC3\x89" "conomie \xC3\xA9nergie : oui", nullptr },
  { "Powersaving Off",    "powersaving off", nullptr, 0, "\xC3\x89" "conomie \xC3\xA9nergie : non", nullptr },
};
#define CMD_POWER_COUNT 3

// --- System ---
static const AdminCmdDef CMD_SYSTEM[] = {
  { "Reboot",       "reboot",     nullptr, CMDF_CONFIRM | CMDF_EXPECT_TIMEOUT, "Red\xC3\xA9marrer", nullptr },
  { "Start OTA",    "start ota",  nullptr, CMDF_CONFIRM | CMDF_EXPECT_TIMEOUT, "Lancer l'OTA", nullptr },
};
#define CMD_SYSTEM_COUNT 2

// =====================================================================
// Category table
// =====================================================================
enum AdminCategory {
  CAT_OPERATIONAL = 0,
  CAT_NEIGHBORS,
  CAT_GET_CONFIG,
  CAT_SET_CONFIG,
  CAT_POWER,
  CAT_REBOOT_OTA,
  CAT_FW_INFO,
  CAT_COUNT
};

struct AdminCategoryDef {
  const char* label;
  const AdminCmdDef* cmds;
  int count;
  const char* label_fr;  // French label (Experimental Features > Language)
};

static const AdminCategoryDef CATEGORIES[CAT_COUNT] = {
  { "Clock & Adverts",      CMD_OPERATIONAL, CMD_OPERATIONAL_COUNT, "Horloge et annonces" },
  { "Neighbors",            CMD_NEIGHBORS,   CMD_NEIGHBORS_COUNT, "Voisins" },
  { "Get Config",           CMD_GET_CONFIG,  CMD_GET_CONFIG_COUNT, "Lire la config" },
  { "Set Config",           CMD_SET_CONFIG,  CMD_SET_CONFIG_COUNT, "Modifier la config" },
  { "Powersaving",          CMD_POWER,       CMD_POWER_COUNT, "\xC3\x89" "conomie d'\xC3\xA9nergie" },
  { "Reboot & Start OTA",  CMD_SYSTEM,      CMD_SYSTEM_COUNT, "Red\xC3\xA9marrage et OTA" },
  { "Firmware & Device Info", CMD_INFO,      CMD_INFO_COUNT, "Firmware et infos" },
};

// =====================================================================
// Screen class
// =====================================================================

class RepeaterAdminScreen : public UIScreen {
public:
  enum AdminState {
    STATE_PASSWORD_ENTRY,   // Typing admin password
    STATE_LOGGING_IN,       // Waiting for login response
    STATE_CATEGORY_MENU,    // Top-level category selection
    STATE_COMMAND_MENU,     // Commands within selected category
    STATE_PARAM_ENTRY,      // Typing parameter for a set command
    STATE_CONFIRM,          // Confirmation prompt for destructive commands
    STATE_COMMAND_PENDING,  // Waiting for CLI response
    STATE_RESPONSE_VIEW,    // Displaying CLI response
    STATE_ERROR             // Error state (timeout, send fail)
  };

private:
  AbstractUITask* _task;
  mesh::RTCClock* _rtc;

  AdminState _state;
  int _contactIdx;              // Contact table index of the repeater
  char _repeaterName[32];       // Cached repeater name
  uint8_t _permissions;         // Login permissions (0=guest, 3=admin)
  uint32_t _serverTime;         // Server timestamp from login response

  // Password entry
  char _password[ADMIN_PASSWORD_MAX];
  int _pwdLen;
  unsigned long _lastCharAt;    // millis() when last char typed (for brief reveal)

  // Menu navigation
  int _catSel;                  // Selected category index
  int _cmdSel;                  // Selected command index within category
  int _scrollOffset;            // Scroll offset for long command lists

  // Parameter entry for set commands
  char _paramBuf[ADMIN_PARAM_MAX];
  int _paramLen;
  const AdminCmdDef* _pendingCmd;  // The command awaiting parameter / confirmation

  // Response buffer
  char _response[ADMIN_RESPONSE_MAX];
  int _responseLen;
  int _responseScroll;
  int _responseTotalLines;

  // Timing
  unsigned long _cmdSentAt;
  unsigned long _loginTimeoutMs;   // computed timeout for login (ms), falls back to ADMIN_TIMEOUT_MS
  bool _waitingForLogin;

  // Password cache
  static const int PWD_CACHE_SIZE = 8;
  struct PwdCacheEntry {
    int contactIdx;
    char password[ADMIN_PASSWORD_MAX];
  };
  PwdCacheEntry _pwdCache[PWD_CACHE_SIZE];
  int _pwdCacheCount;

  // Remote telemetry (from LPP response)
  float _telemVoltage;          // Battery voltage in volts
  float _telemTempC;            // Temperature in celsius
  bool _telemHasVoltage;
  bool _telemHasTemp;
  bool _telemRequested;         // Telemetry request already sent

  const char* getCachedPassword(int contactIdx) {
    for (int i = 0; i < _pwdCacheCount; i++) {
      if (_pwdCache[i].contactIdx == contactIdx) return _pwdCache[i].password;
    }
    return nullptr;
  }

  void cachePassword(int contactIdx, const char* pwd) {
    for (int i = 0; i < _pwdCacheCount; i++) {
      if (_pwdCache[i].contactIdx == contactIdx) {
        strncpy(_pwdCache[i].password, pwd, ADMIN_PASSWORD_MAX - 1);
        _pwdCache[i].password[ADMIN_PASSWORD_MAX - 1] = '\0';
        return;
      }
    }
    if (_pwdCacheCount < PWD_CACHE_SIZE) {
      int slot = _pwdCacheCount++;
      _pwdCache[slot].contactIdx = contactIdx;
      strncpy(_pwdCache[slot].password, pwd, ADMIN_PASSWORD_MAX - 1);
      _pwdCache[slot].password[ADMIN_PASSWORD_MAX - 1] = '\0';
    } else {
      for (int i = 0; i < PWD_CACHE_SIZE - 1; i++) _pwdCache[i] = _pwdCache[i + 1];
      _pwdCache[PWD_CACHE_SIZE - 1].contactIdx = contactIdx;
      strncpy(_pwdCache[PWD_CACHE_SIZE - 1].password, pwd, ADMIN_PASSWORD_MAX - 1);
      _pwdCache[PWD_CACHE_SIZE - 1].password[ADMIN_PASSWORD_MAX - 1] = '\0';
    }
  }

  // --- LPP telemetry parser ---
  // Walks a CayenneLPP buffer extracting voltage and temperature.
  // Format per entry: [channel 1B][type 1B][data varies]
  void parseLPPTelemetry(const uint8_t* data, uint8_t len) {
    const uint8_t TELEM_TYPE_VOLTAGE     = 0x74;  // 2 bytes, uint16 / 100 = V
    const uint8_t TELEM_TYPE_TEMPERATURE = 0x67;  // 2 bytes, int16 / 10 = C

    int pos = 0;
    while (pos + 2 < len) {
      // uint8_t channel = data[pos];  // not needed
      uint8_t type = data[pos + 1];
      pos += 2;  // skip channel + type

      // Determine data size for this type
      int dataSize = 0;
      switch (type) {
        case TELEM_TYPE_VOLTAGE:     dataSize = 2; break;
        case TELEM_TYPE_TEMPERATURE: dataSize = 2; break;
        case 0x88:            dataSize = 9; break;  // GPS
        case 0x75:            dataSize = 2; break;  // Current
        case 0x68:            dataSize = 1; break;  // Humidity
        case 0x73:            dataSize = 2; break;  // Pressure
        case 0x76:            dataSize = 2; break;  // Altitude
        case 0x80:            dataSize = 2; break;  // Power
        default:              return;                // Unknown type, stop parsing
      }

      if (pos + dataSize > len) break;

      if (type == TELEM_TYPE_VOLTAGE) {
        uint16_t raw = ((uint16_t)data[pos] << 8) | data[pos + 1];
        _telemVoltage = raw / 100.0f;
        _telemHasVoltage = true;
      } else if (type == TELEM_TYPE_TEMPERATURE) {
        int16_t raw = ((int16_t)data[pos] << 8) | data[pos + 1];
        _telemTempC = raw / 10.0f;
        _telemHasTemp = true;
      }

      pos += dataSize;
    }
  }

  // --- Helpers ---

  static void formatTime(char* buf, size_t bufLen, uint32_t epoch) {
    if (epoch == 0) { strncpy(buf, "--:--:--", bufLen); return; }
    uint32_t secs = epoch % 60;
    uint32_t mins = (epoch / 60) % 60;
    uint32_t hrs  = (epoch / 3600) % 24;
    snprintf(buf, bufLen, "%02d:%02d:%02d", (int)hrs, (int)mins, (int)secs);
  }

  static void formatRelativeTime(char* buf, size_t bufLen, uint32_t seconds) {
    if (seconds < 60) {
      snprintf(buf, bufLen, MECK_TR("%ds ago", "il y a %ds"), (int)seconds);
    } else if (seconds < 3600) {
      snprintf(buf, bufLen, MECK_TR("%dm ago", "il y a %dmin"), (int)(seconds / 60));
    } else if (seconds < 86400) {
      int h = seconds / 3600;
      int m = (seconds % 3600) / 60;
      if (m > 0) snprintf(buf, bufLen, MECK_TR("%dh%dm ago", "il y a %dh%dmin"), h, m);
      else       snprintf(buf, bufLen, MECK_TR("%dh ago", "il y a %dh"), h);
    } else {
      snprintf(buf, bufLen, MECK_TR("%dd%dh ago", "il y a %dj%dh"), (int)(seconds / 86400), (int)((seconds % 86400) / 3600));
    }
  }

  int countLines(const char* text) {
    int lines = 0;
    const char* p = text;
    while (*p) {
      while (*p && *p != '\n') p++;
      if (*p == '\n') p++;
      lines++;
    }
    return lines;
  }

  // Parse raw neighbor response into formatted display text
  void parseNeighborResponse(const char* raw) {
    char* dp = _response;
    int remaining = ADMIN_RESPONSE_MAX - 1;
    uint32_t now = _rtc->getCurrentTime();
    int count = 0;

    const char* p = raw;
    while (*p && remaining > 80) {
      char line[128];
      int li = 0;
      while (*p && *p != '\n' && li < 127) line[li++] = *p++;
      line[li] = '\0';
      if (*p == '\n') p++;
      if (li == 0) continue;

      // Parse: {hex}:{timestamp}:{snr*4}
      char hexPart[20] = {0};
      uint32_t timestamp = 0;
      int snrX4 = 0;

      char* sep1 = strchr(line, ':');
      if (!sep1) {
        int n = snprintf(dp, remaining, "%s\n", line);
        dp += n; remaining -= n; count++;
        continue;
      }

      int hexLen = sep1 - line;
      if (hexLen > 19) hexLen = 19;
      memcpy(hexPart, line, hexLen);
      hexPart[hexLen] = '\0';

      char* afterSep1 = sep1 + 1;
      char* sep2 = strchr(afterSep1, ':');
      if (sep2) {
        *sep2 = '\0';
        timestamp = strtoul(afterSep1, NULL, 10);
        snrX4 = atoi(sep2 + 1);
      } else {
        timestamp = strtoul(afterSep1, NULL, 10);
      }

      // Resolve hex prefix to contact name
      const char* nodeName = NULL;
      uint8_t pubkeyBytes[PUB_KEY_SIZE];
      int byteLen = hexLen / 2;
      if (byteLen > 0 && byteLen <= PUB_KEY_SIZE) {
        if (mesh::Utils::fromHex(pubkeyBytes, byteLen, hexPart)) {
          ContactInfo* contact = the_mesh.lookupContactByPubKey(pubkeyBytes, byteLen);
          if (contact && contact->name[0] != '\0') nodeName = contact->name;
        }
      }

      // Format SNR (snr*4 encoded)
      char snrStr[12];
      if (snrX4 % 4 == 0) {
        snprintf(snrStr, sizeof(snrStr), "%ddB", snrX4 / 4);
      } else {
        snprintf(snrStr, sizeof(snrStr), "%d.%ddB", snrX4 / 4, ((abs(snrX4) % 4) * 10) / 4);
      }

      // Format relative time
      char timeStr[24];
      if (timestamp > 0 && now > timestamp) {
        formatRelativeTime(timeStr, sizeof(timeStr), now - timestamp);
      } else if (timestamp > 0) {
        strncpy(timeStr, MECK_TR("just now", "\xC3\xA0 l'instant"), sizeof(timeStr));
      } else {
        strncpy(timeStr, "?", sizeof(timeStr));
      }

      int n;
      if (nodeName) {
        n = snprintf(dp, remaining, "%.18s %s %s\n", nodeName, snrStr, timeStr);
      } else {
        n = snprintf(dp, remaining, "<%s> %s %s\n", hexPart, snrStr, timeStr);
      }
      dp += n; remaining -= n; count++;
    }

    // Remove trailing newline
    if (dp > _response && *(dp - 1) == '\n') { *(dp - 1) = '\0'; dp--; }

    // Prepend count header
    char header[48];
    snprintf(header, sizeof(header), MECK_TR("Neighbors: %d\n", "Voisins : %d\n"), count);
    int headerLen = strlen(header);
    int contentLen = dp - _response;
    if (headerLen + contentLen < ADMIN_RESPONSE_MAX - 1) {
      memmove(_response + headerLen, _response, contentLen + 1);
      memcpy(_response, header, headerLen);
      _responseLen = headerLen + contentLen;
    } else {
      _responseLen = contentLen;
    }
  }

public:
  RepeaterAdminScreen(AbstractUITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _state(STATE_PASSWORD_ENTRY),
      _contactIdx(-1), _permissions(0), _serverTime(0),
      _pwdLen(0), _lastCharAt(0),
      _catSel(0), _cmdSel(0), _scrollOffset(0),
      _paramLen(0), _pendingCmd(nullptr),
      _responseLen(0), _responseScroll(0), _responseTotalLines(0),
      _cmdSentAt(0), _loginTimeoutMs(ADMIN_TIMEOUT_MS), _waitingForLogin(false), _pwdCacheCount(0),
      _telemVoltage(0), _telemTempC(0),
      _telemHasVoltage(false), _telemHasTemp(false), _telemRequested(false) {
    _password[0] = '\0';
    _repeaterName[0] = '\0';
    _response[0] = '\0';
    _paramBuf[0] = '\0';
  }

  void openForContact(int contactIdx, const char* name) {
    _contactIdx = contactIdx;
    strncpy(_repeaterName, name, sizeof(_repeaterName) - 1);
    _repeaterName[sizeof(_repeaterName) - 1] = '\0';

    _state = STATE_PASSWORD_ENTRY;
    _lastCharAt = 0;
    _catSel = 0;
    _cmdSel = 0;
    _scrollOffset = 0;
    _permissions = 0;
    _serverTime = 0;
    _responseLen = 0;
    _responseScroll = 0;
    _responseTotalLines = 0;
    _response[0] = '\0';
    _waitingForLogin = false;
    _pendingCmd = nullptr;
    _paramLen = 0;
    _paramBuf[0] = '\0';
    _telemHasVoltage = false;
    _telemHasTemp = false;
    _telemRequested = false;

    const char* cached = getCachedPassword(contactIdx);
    if (cached) {
      strncpy(_password, cached, ADMIN_PASSWORD_MAX - 1);
      _password[ADMIN_PASSWORD_MAX - 1] = '\0';
      _pwdLen = strlen(_password);
    } else {
      _pwdLen = 0;
      _password[0] = '\0';
    }
  }

  int getContactIdx() const { return _contactIdx; }
  AdminState getState() const { return _state; }
  uint8_t getPermissions() const { return _permissions; }

  // Map a touch Y (128-virtual space) to a menu row and select it.
  // Returns 0 = miss/no-op, 1 = selection moved, 2 = tapped current selection.
  // Mirrors the row layout in renderCategoryMenu / renderCommandMenu.
  int selectRowAtVY(int vy) {
    const int headerH = 14, footerH = 14;
    int lineH = the_mesh.getNodePrefs()->smallLineH();

    if (_state == STATE_CATEGORY_MENU) {
      int bodyTop = headerH;
      if (_serverTime > 0) bodyTop += lineH + 2;                                  // clock drift line
      if (_telemHasVoltage || _telemHasTemp || _telemRequested) bodyTop += lineH + 2;  // telemetry line
      if (vy < bodyTop || vy >= 128 - footerH) return 0;
      int row = (vy - bodyTop) / lineH;
      if (row < 0 || row >= CAT_COUNT) return 0;
      if (row == _catSel) return 2;
      _catSel = row;
      return 1;
    }

    if (_state == STATE_COMMAND_MENU) {
      int bodyTop = headerH + lineH + 2;   // category title line
      if (vy < bodyTop || vy >= 128 - footerH) return 0;
      int idx = _scrollOffset + (vy - bodyTop) / lineH;
      int count = CATEGORIES[_catSel].count;
      if (idx < 0 || idx >= count) return 0;
      if (idx == _cmdSel) return 2;
      _cmdSel = idx;
      return 1;
    }

    return 0;
  }

  void onLoginResult(bool success, uint8_t permissions, uint32_t server_time) {
    _waitingForLogin = false;
    if (success) {
      _permissions = permissions;
      _serverTime = server_time;
      _state = STATE_CATEGORY_MENU;
      cachePassword(_contactIdx, _password);

      // Auto-request telemetry (battery & temperature) after login
      if (!_telemRequested) {
        _telemRequested = true;
        bool sent = the_mesh.uiSendTelemetryRequest(_contactIdx);
        Serial.printf("[Admin] Telemetry request %s for contact idx %d\n",
                      sent ? "sent" : "FAILED", _contactIdx);
      }
    } else {
      snprintf(_response, sizeof(_response), MECK_TR("Login failed.\nCheck password.", "\xC3\x89" "chec de connexion.\nV\xC3\xA9rifiez le mot de passe."));
      _responseLen = strlen(_response);
      _state = STATE_ERROR;
    }
  }

  void onCliResponse(const char* text) {
    if (_state != STATE_COMMAND_PENDING) return;

    // Special parsing for neighbor responses
    if (_pendingCmd && (_pendingCmd->flags & CMDF_PARSE_NEIGHBORS)) {
      parseNeighborResponse(text);
      _responseScroll = 0;
      _responseTotalLines = countLines(_response);
      _state = STATE_RESPONSE_VIEW;
      return;
    }

    int tlen = strlen(text);
    if (tlen >= ADMIN_RESPONSE_MAX) tlen = ADMIN_RESPONSE_MAX - 1;
    memcpy(_response, text, tlen);
    _response[tlen] = '\0';
    _responseLen = tlen;
    _responseScroll = 0;
    _responseTotalLines = countLines(_response);
    _state = STATE_RESPONSE_VIEW;
  }

  void onTelemetryResult(const uint8_t* data, uint8_t len) {
    Serial.printf("[Admin] Telemetry response received, %d bytes:", len);
    for (int i = 0; i < len && i < 32; i++) Serial.printf(" %02X", data[i]);
    Serial.println();
    parseLPPTelemetry(data, len);
    Serial.printf("[Admin] Parsed: hasVoltage=%d (%.2fV) hasTemp=%d (%.1fC)\n",
                  _telemHasVoltage, _telemVoltage, _telemHasTemp, _telemTempC);
  }

  void poll() override {
    if (_cmdSentAt > 0) {
      unsigned long elapsed = millis() - _cmdSentAt;
      unsigned long timeout = (_state == STATE_LOGGING_IN) ? _loginTimeoutMs : ADMIN_TIMEOUT_MS;

      if ((_state == STATE_LOGGING_IN || _state == STATE_COMMAND_PENDING) && elapsed > timeout) {
        if (_pendingCmd && (_pendingCmd->flags & CMDF_EXPECT_TIMEOUT)) {
          snprintf(_response, sizeof(_response), MECK_TR("Command sent.\nTimeout is expected\n(device is rebooting/updating).", "Commande envoy\xC3\xA9" "e.\nPas de r\xC3\xA9ponse attendue\n(red\xC3\xA9marrage/mise \xC3\xA0 jour)."));
          _responseLen = strlen(_response);
          _responseTotalLines = countLines(_response);
          _state = STATE_RESPONSE_VIEW;
        } else {
          snprintf(_response, sizeof(_response), MECK_TR("Timeout - no response.", "Pas de r\xC3\xA9ponse (d\xC3\xA9lai)."));
          _responseLen = strlen(_response);
          _state = STATE_ERROR;
        }
        _task->forceRefresh();  // Immediate redraw on state change
      }
    }
  }

  // =====================================================================
  // Render
  // =====================================================================

  int render(DisplayDriver& display) override {
#ifdef MECK_UI_V2
    return renderV2(display);
#endif
    char tmp[64];

    // --- Header ---
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    const char* hdrPrefix = (_state == STATE_PASSWORD_ENTRY || _state == STATE_LOGGING_IN)
                             ? MECK_TR("Login", "Connexion") : "Admin";
    snprintf(tmp, sizeof(tmp), "%s: %.16s", hdrPrefix, _repeaterName);
    display.print(tmp);

    if (_state >= STATE_CATEGORY_MENU && _state <= STATE_RESPONSE_VIEW) {
      const char* perm = (_permissions & 0x03) >= 3 ? "ADM" :
                         (_permissions & 0x03) >= 2 ? "R/W" : "R/O";
      display.setCursor(display.width() - display.getTextWidth(perm) - 2, 0);
      display.print(perm);
    }

    display.drawRect(0, 11, display.width(), 1);

    int bodyY = 14;
    int footerY = display.height() - 12;
    int bodyHeight = footerY - bodyY - 4;

    // --- Body ---
    switch (_state) {
      case STATE_PASSWORD_ENTRY:  renderPasswordEntry(display, bodyY); break;
      case STATE_LOGGING_IN:      renderWaiting(display, bodyY, MECK_TR("Logging in...", "Connexion...")); break;
      case STATE_CATEGORY_MENU:   renderCategoryMenu(display, bodyY, bodyHeight); break;
      case STATE_COMMAND_MENU:    renderCommandMenu(display, bodyY, bodyHeight); break;
      case STATE_PARAM_ENTRY:     renderParamEntry(display, bodyY); break;
      case STATE_CONFIRM:         renderConfirm(display, bodyY); break;
      case STATE_COMMAND_PENDING: renderWaiting(display, bodyY, MECK_TR("Waiting...", "Attente...")); break;
      case STATE_RESPONSE_VIEW:   renderResponse(display, bodyY, bodyHeight); break;
      case STATE_ERROR:           renderResponse(display, bodyY, bodyHeight); break;
    }

    // --- Footer ---
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setTextSize(1);
    display.setCursor(0, footerY);

    switch (_state) {
      case STATE_PASSWORD_ENTRY:
        display.print(MECK_TR("Sh+Del:Exit", "Sh+Del:Sortir"));
        renderFooterRight(display, footerY, MECK_TR("Ent:Login", "Ent:Connexion"));
        break;

      case STATE_LOGGING_IN:
      case STATE_COMMAND_PENDING:
        display.print(MECK_TR("Q:Cancel", "Q:Annuler"));
        break;

      case STATE_CATEGORY_MENU:
        display.print(MECK_TR("Q:Exit", "Q:Sortir"));
        renderFooterMidRight(display, footerY, MECK_TR("Q:Exit", "Q:Sortir"), MECK_TR("Ent:Open", "Ent:Ouvrir"), MECK_TR("W/S:Sel", "W/S:S\xC3\xA9l"));
        break;

      case STATE_COMMAND_MENU:
        display.print(MECK_TR("Q:Back", "Q:Ret"));
        renderFooterMidRight(display, footerY, MECK_TR("Q:Back", "Q:Ret"), MECK_TR("Ent:Run", "Ent:Lancer"), MECK_TR("W/S:Sel", "W/S:S\xC3\xA9l"));
        break;

      case STATE_PARAM_ENTRY:
        display.print(MECK_TR("Sh+Del:Cancel", "Sh+Del:Annuler"));
        renderFooterRight(display, footerY, MECK_TR("Ent:Send", "Ent:Envoyer"));
        break;

      case STATE_CONFIRM:
        display.print(MECK_TR("Q:No", "Q:Non"));
        renderFooterRight(display, footerY, MECK_TR("Ent:Yes", "Ent:Oui"));
        break;

      case STATE_RESPONSE_VIEW:
      case STATE_ERROR:
        display.print(MECK_TR("Q:Back", "Q:Retour"));
        if (_responseTotalLines > bodyHeight / 9) {
          renderFooterRight(display, footerY, MECK_TR("W/S:Scrll", "W/S:D\xC3\xA9" "fil"));
        }
        break;
    }


    if (_state == STATE_LOGGING_IN || _state == STATE_COMMAND_PENDING) return 30000;  // static text; poll()/callbacks force refresh on state change
    if (_state == STATE_PASSWORD_ENTRY && _lastCharAt > 0 && (millis() - _lastCharAt) < 800) {
      return _lastCharAt + 800 - millis() + 50;
    }
    return 5000;
  }

  // =====================================================================
  // Input dispatch
  // =====================================================================

  bool handleInput(char c) override {
    switch (_state) {
      case STATE_PASSWORD_ENTRY:  return handlePasswordInput(c);
      case STATE_LOGGING_IN:
      case STATE_COMMAND_PENDING:
        if (c == KEY_ADMIN_EXIT) {
          _state = (_state == STATE_LOGGING_IN) ? STATE_PASSWORD_ENTRY : STATE_COMMAND_MENU;
          return true;
        }
        return false;
      case STATE_CATEGORY_MENU:   return handleCategoryInput(c);
      case STATE_COMMAND_MENU:    return handleCommandInput(c);
      case STATE_PARAM_ENTRY:     return handleParamInput(c);
      case STATE_CONFIRM:         return handleConfirmInput(c);
      case STATE_RESPONSE_VIEW:
      case STATE_ERROR:           return handleResponseInput(c);
    }
    return false;
  }

private:

#ifdef MECK_UI_V2
  // =====================================================================
  // UI v2
  // =====================================================================

  const char* permLabel() const {
    uint8_t p = _permissions & 0x03;
    return p >= 3 ? "Admin" : p >= 2 ? "R/W" : MECK_TR("Read only", "Lecture");
  }

  static Ui2Icon categoryIcon(int cat) {
    switch (cat) {
      case CAT_OPERATIONAL: return UI2_IC_CLOCK;
      case CAT_NEIGHBORS:   return UI2_IC_RADIO;
      case CAT_GET_CONFIG:  return UI2_IC_INFO;
      case CAT_SET_CONFIG:  return UI2_IC_SETTINGS;
      case CAT_POWER:       return UI2_IC_BOLT;
      case CAT_REBOOT_OTA:  return UI2_IC_WARN;
      default:              return UI2_IC_CHIP;
    }
  }

  int renderV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    switch (_state) {
      case STATE_PASSWORD_ENTRY:  renderPasswordV2(c); break;
      case STATE_LOGGING_IN:      renderWaitingV2(c, MECK_TR("Logging in\xE2\x80\xA6", "Connexion\xE2\x80\xA6")); break;
      case STATE_CATEGORY_MENU:   renderCategoriesV2(c); break;
      case STATE_COMMAND_MENU:    renderCommandsV2(c); break;
      case STATE_PARAM_ENTRY:     renderParamV2(c); break;
      case STATE_CONFIRM:         renderCommandsV2(c); renderConfirmV2(c); break;
      case STATE_COMMAND_PENDING: renderWaitingV2(c, MECK_TR("Waiting for the repeater\xE2\x80\xA6", "Attente du r\xC3\xA9p\xC3\xA9teur\xE2\x80\xA6")); break;
      case STATE_RESPONSE_VIEW:
      case STATE_ERROR:           renderResponseV2(c); break;
    }
    if (_state == STATE_LOGGING_IN || _state == STATE_COMMAND_PENDING) return 30000;
    if (_state == STATE_PASSWORD_ENTRY && _lastCharAt > 0 && (millis() - _lastCharAt) < 800) {
      return _lastCharAt + 800 - millis() + 50;
    }
    return 5000;
  }

  void renderPasswordV2(Ui2Canvas& c) {
    using namespace ui2;
    Ui2Chrome::titleBar(c, _repeaterName);
    char masked[ADMIN_PASSWORD_MAX + 2];
    bool revealing = (_pwdLen > 0 && (millis() - _lastCharAt) < 800);
    int i;
    for (i = 0; i < _pwdLen && i < ADMIN_PASSWORD_MAX - 1; i++) {
      masked[i] = (revealing && i == _pwdLen - 1) ? _password[i] : '*';
    }
    masked[i++] = '_';
    masked[i] = '\0';
    Ui2Row r0;
    r0.icon = UI2_IC_REPEATER;
    r0.title = MECK_TR("Log in to this node", "Connexion au n\xC5\x93ud");
    r0.sub = MECK_TR("Admin or guest password", "Mot de passe admin ou invit\xC3\xA9");
    Ui2Row r1;
    r1.icon = UI2_IC_KEY;
    r1.title = MECK_TR("Password", "Mot de passe");
    r1.sub = masked;
    r1.selected = true;
    r1.pending = true;
    Ui2List::drawRow(c, BODY_Y, r0);
    Ui2List::drawRow(c, BODY_Y + ROW_H, r1);
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Log in", "Connexion")),
      ui2Key("Sh+Del", MECK_TR("Exit", "Sortir")),
    };
    Ui2Chrome::footer(c, keys, 2);
  }

  void renderWaitingV2(Ui2Canvas& c, const char* msg) {
    using namespace ui2;
    Ui2Chrome::titleBar(c, _repeaterName);
    c.icon((SCREEN_W - 32) / 2, 96, UI2_IC_REFRESH, 32);
    c.textFit(PAD, 156, SCREEN_W - 2 * PAD, msg, UI2_F_TITLE, true, UI2_CENTER);
    if (_state == STATE_COMMAND_PENDING && _pendingCmd) {
      c.textFit(PAD, 178, SCREEN_W - 2 * PAD, _pendingCmd->cmd, UI2_F_BODY, true, UI2_CENTER);
    }
    const Ui2Key keys[] = { ui2Key("Q", MECK_TR("Cancel", "Annuler")) };
    Ui2Chrome::footer(c, keys, 1);
  }

  void renderCategoriesV2(Ui2Canvas& c) {
    using namespace ui2;
    Ui2Chrome::titleBar(c, _repeaterName, nullptr, permLabel(), UI2_IC_LOCK, (_permissions & 0x03) >= 3);
    int16_t y = BODY_Y;
    // Clock of the repeater against ours, then its battery and temperature
    if (_serverTime > 0) {
      char srv[12], val[32];
      formatTime(srv, sizeof(srv), _serverTime);
      int drift = (int)(_rtc->getCurrentTime() - _serverTime);
      if (abs(drift) < 2) snprintf(val, sizeof(val), MECK_TR("%s \xC2\xB7 in sync", "%s \xC2\xB7 synchro"), srv);
      else snprintf(val, sizeof(val), "%s \xC2\xB7 %+d s", srv, drift);
      Ui2List::kvRow(c, y + 2, MECK_TR("Repeater clock", "Horloge"), val);
      y += 20;
    }
    if (_telemHasVoltage || _telemHasTemp || _telemRequested) {
      char val[32] = "";
      int p = 0;
      if (_telemHasVoltage) p += snprintf(val + p, sizeof(val) - p, "%.2f V", _telemVoltage);
      if (_telemHasTemp) p += snprintf(val + p, sizeof(val) - p, "%s%.1f \xC2\xB0" "C", p ? " \xC2\xB7 " : "", _telemTempC);
      if (!p) snprintf(val, sizeof(val), "%s", MECK_TR("requesting\xE2\x80\xA6", "demande\xE2\x80\xA6"));
      Ui2List::kvRow(c, y + 2, MECK_TR("Battery \xC2\xB7 temp", "Batterie \xC2\xB7 temp."), val);
      y += 20;
    }
    if (y > BODY_Y) { c.hline(0, y + 3, SCREEN_W); y += 4; }
    // Seven categories share what is left (rows of at least 28 px)
    int16_t rowH = (FOOTER_Y - y) / CAT_COUNT;
    if (rowH > ROW_H) rowH = ROW_H;
    for (int i = 0; i < CAT_COUNT; i++) {
      char n[4];
      snprintf(n, sizeof(n), "%d", i + 1);
      Ui2Row r;
      r.icon = categoryIcon(i);
      r.title = MECK_TR(CATEGORIES[i].label, CATEGORIES[i].label_fr);
      r.right = n;
      r.height = rowH;
      r.selected = (i == _catSel);
      Ui2List::drawRow(c, y + i * rowH, r);
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Open", "Ouvrir")),
      ui2Key("1-7", MECK_TR("Jump", "Aller")),
      ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("Q", MECK_TR("Exit", "Sortir")),
    };
    Ui2Chrome::footer(c, keys, 4);
  }

  static Ui2Icon commandIcon(const AdminCmdDef& d) {
    if (d.flags & (CMDF_CONFIRM | CMDF_EXPECT_TIMEOUT)) return UI2_IC_WARN;
    if (d.flags & CMDF_PARAM) return UI2_IC_EDIT;
    return UI2_IC_SEND;
  }

  void renderCommandsV2(Ui2Canvas& c) {
    using namespace ui2;
    const AdminCategoryDef& cat = CATEGORIES[_catSel];
    char right[16];
    Ui2List::pageLabel(right, sizeof(right), _cmdSel, cat.count, ROWS_PER_PAGE);
    Ui2Chrome::titleBar(c, MECK_TR(cat.label, cat.label_fr), right);
    const int start = Ui2List::pageStart(_cmdSel, ROWS_PER_PAGE);
    for (int i = start; i < cat.count && i < start + ROWS_PER_PAGE; i++) {
      Ui2Row r;
      r.icon = commandIcon(cat.cmds[i]);
      r.title = MECK_TR(cat.cmds[i].label, cat.cmds[i].label_fr);
      r.sub = cat.cmds[i].cmd;   // the CLI command it sends
      r.selected = (i == _cmdSel);
      Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Run", "Lancer")),
      ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 3);
  }

  void renderParamV2(Ui2Canvas& c) {
    using namespace ui2;
    Ui2Chrome::titleBar(c, _pendingCmd ? MECK_TR(_pendingCmd->label, _pendingCmd->label_fr) : "");
    char val[ADMIN_PARAM_MAX + 2], preview[ADMIN_PARAM_MAX + 40];
    snprintf(val, sizeof(val), "%s_", _paramBuf);
    snprintf(preview, sizeof(preview), "%s%s", _pendingCmd ? _pendingCmd->cmd : "", _paramBuf);
    Ui2Row r;
    r.icon = UI2_IC_EDIT;
    r.title = (_pendingCmd && _pendingCmd->paramHint) ? MECK_TR(_pendingCmd->paramHint, _pendingCmd->paramHint_fr)
                                                      : MECK_TR("Value", "Valeur");
    r.sub = val;
    r.selected = true;
    r.pending = true;
    Ui2List::drawRow(c, BODY_Y, r);
    c.textFit(PAD, BODY_Y + ROW_H + 20, SCREEN_W - 2 * PAD, MECK_TR("Sends:", "Envoie :"), UI2_F_META);
    c.wrap(PAD, BODY_Y + ROW_H + 36, SCREEN_W - 2 * PAD, 15, 3, preview, UI2_F_SMALL);
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Send", "Envoyer")),
      ui2Key("Sh+Del", MECK_TR("Cancel", "Annuler")),
    };
    Ui2Chrome::footer(c, keys, 2);
  }

  void renderConfirmV2(Ui2Canvas& c) {
    if (!_pendingCmd) return;
    char body[ADMIN_PARAM_MAX + 80];
    int p = 0;
    if ((_pendingCmd->flags & CMDF_PARAM) && _paramLen > 0)
      p += snprintf(body + p, sizeof(body) - p, MECK_TR("Value: %s\n", "Valeur : %s\n"), _paramBuf);
    p += snprintf(body + p, sizeof(body) - p, MECK_TR("Sends \"%s\".", "Envoie \"%s\"."), _pendingCmd->cmd);
    if (_pendingCmd->flags & CMDF_EXPECT_TIMEOUT)
      snprintf(body + p, sizeof(body) - p, "%s", MECK_TR("\nNo answer is expected.", "\nPas de r\xC3\xA9ponse attendue."));
    Ui2List::dialog(c, UI2_IC_WARN, MECK_TR(_pendingCmd->label, _pendingCmd->label_fr), body,
                    MECK_TR("No", "Non"), MECK_TR("Yes", "Oui"));
  }

  void renderResponseV2(Ui2Canvas& c) {
    using namespace ui2;
    bool err = (_state == STATE_ERROR);
    const char* title = err ? MECK_TR("Error", "Erreur")
                      : (_pendingCmd ? MECK_TR(_pendingCmd->label, _pendingCmd->label_fr) : MECK_TR("Response", "R\xC3\xA9ponse"));
    char right[16] = "";
    if (_responseTotalLines > 1) snprintf(right, sizeof(right), "%d / %d", _responseScroll + 1, _responseTotalLines);
    Ui2Chrome::titleBar(c, title, right[0] ? right : nullptr);
    int16_t y = BODY_Y + 6;
    if (err) {
      c.icon(PAD, y, UI2_IC_WARN, 24);
      y += 30;
    }
    // Lines from _responseScroll, each wrapped to the width
    const char* p = _response;
    for (int skip = _responseScroll; skip > 0 && *p; skip--) {
      while (*p && *p != '\n') p++;
      if (*p == '\n') p++;
    }
    char line[96];
    while (*p && y + 16 <= FOOTER_Y) {
      int i = 0;
      while (*p && *p != '\n' && i < (int)sizeof(line) - 1) line[i++] = *p++;
      while (*p && *p != '\n') p++;
      if (*p == '\n') p++;
      line[i] = '\0';
      int maxLines = (FOOTER_Y - y) / 16;
      if (maxLines > 4) maxLines = 4;
      int n = line[0] ? c.wrap(PAD, y + 12, SCREEN_W - 2 * PAD, 16, maxLines, line, UI2_F_BODY) : 1;
      y += n * 16;
    }
    const Ui2Key keys[] = {
      ui2Key("W/S", MECK_TR("Scroll", "D\xC3\xA9" "filer"), true),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 2);
  }
#endif

  // --- Footer helpers ---
  void renderFooterRight(DisplayDriver& display, int footerY, const char* text) {
    display.setCursor(display.width() - display.getTextWidth(text) - 6, footerY);
    display.print(text);
  }

  void renderFooterMidRight(DisplayDriver& display, int footerY,
                            const char* left, const char* right, const char* mid) {
    int leftEnd = display.getTextWidth(left) + 2;
    int rightStart = display.width() - display.getTextWidth(right) - 6;
    int midX = leftEnd + (rightStart - leftEnd - display.getTextWidth(mid)) / 2;
    display.setCursor(midX, footerY);
    display.print(mid);
    display.setCursor(rightStart, footerY);
    display.print(right);
  }

  // =====================================================================
  // Password Entry
  // =====================================================================

  void renderPasswordEntry(DisplayDriver& display, int y) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, y);
    display.print(MECK_TR("Password:", "Mot de passe :"));

    y += 14;
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, y);

    char masked[ADMIN_PASSWORD_MAX];
    bool revealing = (_pwdLen > 0 && (millis() - _lastCharAt) < 800);
    int revealIdx = revealing ? _pwdLen - 1 : -1;
    int i;
    for (i = 0; i < _pwdLen && i < ADMIN_PASSWORD_MAX - 1; i++) {
      masked[i] = (i == revealIdx) ? _password[i] : '*';
    }
    masked[i] = '\0';
    display.print(masked);
    display.print("_");
  }

  bool handlePasswordInput(char c) {
    if (c == KEY_ADMIN_EXIT) return false;  // exit screen
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      if (_pwdLen > 0) return doLogin();
      return true;
    }
    if (c == 0x08 || c == 0x7F) {
      if (_pwdLen > 0) { _pwdLen--; _password[_pwdLen] = '\0'; _lastCharAt = 0; }
      return true;
    }
    if (c >= 32 && c < 127 && _pwdLen < ADMIN_PASSWORD_MAX - 1) {
      _password[_pwdLen++] = c;
      _password[_pwdLen] = '\0';
      _lastCharAt = millis();
      return true;
    }
    return false;
  }

  bool doLogin();

  // =====================================================================
  // Category Menu (top level)
  // =====================================================================

  void renderCategoryMenu(DisplayDriver& display, int y, int bodyHeight) {
    display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
    int lineHeight = the_mesh.getNodePrefs()->smallLineH();

    // Clock drift info line
    if (_serverTime > 0) {
      char ourTime[12], srvTime[12];
      uint32_t now = _rtc->getCurrentTime();
      formatTime(ourTime, sizeof(ourTime), now);
      formatTime(srvTime, sizeof(srvTime), _serverTime);
      int drift = (int)(now - _serverTime);
      char driftStr[24];
      if (abs(drift) < 2) snprintf(driftStr, sizeof(driftStr), MECK_TR("Synced", "Synchro"));
      else                snprintf(driftStr, sizeof(driftStr), MECK_TR("Drift:%+ds", "\xC3\x89" "cart:%+ds"), drift);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      char info[48];
      snprintf(info, sizeof(info), MECK_TR("Rpt:%s Us:%s %s", "R\xC3\xA9p:%s Ici:%s %s"), srvTime, ourTime, driftStr);
      display.print(info);
      y += lineHeight + 2;
    }

    // Remote telemetry info line (battery & temperature)
    if (_telemHasVoltage || _telemHasTemp) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      char telem[48];
      int tpos = 0;
      if (_telemHasVoltage) {
        tpos += snprintf(telem + tpos, sizeof(telem) - tpos, "Batt:%.2fV", _telemVoltage);
      }
      if (_telemHasTemp) {
        if (tpos > 0) tpos += snprintf(telem + tpos, sizeof(telem) - tpos, " ");
        tpos += snprintf(telem + tpos, sizeof(telem) - tpos, "Temp:%.1fC", _telemTempC);
      }
      display.print(telem);
      y += lineHeight + 2;
    } else if (_telemRequested) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      display.print(MECK_TR("Telemetry: requesting...", "T\xC3\xA9l\xC3\xA9m\xC3\xA9trie : demande..."));
      y += lineHeight + 2;
    }

    // Render categories
    for (int i = 0; i < CAT_COUNT && y + lineHeight <= display.height() - 16; i++) {
      bool isSystem = (i == CAT_REBOOT_OTA);
      renderMenuItem(display, y, lineHeight, i == _catSel, MECK_TR(CATEGORIES[i].label, CATEGORIES[i].label_fr), isSystem && (i != _catSel));
      y += lineHeight;
    }
    display.setTextSize(1);
  }

  bool handleCategoryInput(char c) {
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_catSel > 0) _catSel--;
      return true;
    }
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_catSel < CAT_COUNT - 1) _catSel++;
      return true;
    }
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      _cmdSel = 0;
      _scrollOffset = 0;
      _state = STATE_COMMAND_MENU;
      return true;
    }
    if (c == KEY_ADMIN_EXIT) return false;  // exit screen
    // Number keys 1-7
    if (c >= '1' && c <= '0' + CAT_COUNT) {
      _catSel = c - '1';
      _cmdSel = 0;
      _scrollOffset = 0;
      _state = STATE_COMMAND_MENU;
      return true;
    }
    return false;
  }

  // =====================================================================
  // Command Menu (within a category)
  // =====================================================================

  void renderCommandMenu(DisplayDriver& display, int y, int bodyHeight) {
    display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
    int lineHeight = the_mesh.getNodePrefs()->smallLineH();
    const AdminCategoryDef& cat = CATEGORIES[_catSel];

    // Category title
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, y);
    display.print(MECK_TR(cat.label, cat.label_fr));
    y += lineHeight + 2;

    // Compute visible window
    int maxVisible = (display.height() - 16 - y) / lineHeight;
    if (maxVisible < 1) maxVisible = 1;
    if (_cmdSel < _scrollOffset) _scrollOffset = _cmdSel;
    if (_cmdSel >= _scrollOffset + maxVisible) _scrollOffset = _cmdSel - maxVisible + 1;

    bool isSystemCat = (_catSel == CAT_REBOOT_OTA);

    for (int i = _scrollOffset; i < cat.count && (y + lineHeight <= display.height() - 16); i++) {
      bool warn = isSystemCat || (cat.cmds[i].flags & (CMDF_CONFIRM | CMDF_EXPECT_TIMEOUT));
      renderMenuItem(display, y, lineHeight, i == _cmdSel, MECK_TR(cat.cmds[i].label, cat.cmds[i].label_fr), warn && (i != _cmdSel));
      y += lineHeight;
    }

    // Scroll indicators
    if (_scrollOffset > 0) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 8, 14 + lineHeight + 2);
      display.print("^");
    }
    if (_scrollOffset + maxVisible < cat.count) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 8, display.height() - 16 - lineHeight);
      display.print("v");
    }

    display.setTextSize(1);
  }

  bool handleCommandInput(char c) {
    int count = CATEGORIES[_catSel].count;
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_cmdSel > 0) _cmdSel--;
      return true;
    }
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_cmdSel < count - 1) _cmdSel++;
      return true;
    }
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      return initiateCommand(&CATEGORIES[_catSel].cmds[_cmdSel]);
    }
    if (c == KEY_ADMIN_EXIT) {
      _state = STATE_CATEGORY_MENU;
      return true;
    }
    // Number keys for quick select (1-9 within category)
    if (c >= '1' && c <= '9') {
      int idx = c - '1';
      if (idx < count) {
        _cmdSel = idx;
        return initiateCommand(&CATEGORIES[_catSel].cmds[_cmdSel]);
      }
    }
    return false;
  }

  // Decide what to do when a command is selected
  bool initiateCommand(const AdminCmdDef* cmd) {
    _pendingCmd = cmd;

    // If it needs parameter input, go to param entry first
    if (cmd->flags & CMDF_PARAM) {
      _paramLen = 0;
      _paramBuf[0] = '\0';
      _state = STATE_PARAM_ENTRY;
      return true;
    }

    // If it needs confirmation (but no param), show confirm dialog
    if (cmd->flags & CMDF_CONFIRM) {
      _state = STATE_CONFIRM;
      return true;
    }

    // Otherwise send immediately
    return sendCommand(cmd->cmd);
  }

  // =====================================================================
  // Parameter Entry
  // =====================================================================

  void renderParamEntry(DisplayDriver& display, int y) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, y);
    if (_pendingCmd) display.print(MECK_TR(_pendingCmd->label, _pendingCmd->label_fr));

    y += 14;
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, y);
    if (_pendingCmd && _pendingCmd->paramHint) display.print(MECK_TR(_pendingCmd->paramHint, _pendingCmd->paramHint_fr));

    y += 14;
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, y);
    display.print(_paramBuf);
    display.print("_");
  }

  bool handleParamInput(char c) {
    if (c == KEY_ADMIN_EXIT) {
      _state = STATE_COMMAND_MENU;
      return true;
    }
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      if (_paramLen == 0) return true;  // don't send empty

      // If this command also needs confirmation, go to confirm now
      if (_pendingCmd && (_pendingCmd->flags & CMDF_CONFIRM)) {
        _state = STATE_CONFIRM;
        return true;
      }

      // Build and send: prefix + param
      return sendParamCommand();
    }
    if (c == 0x08 || c == 0x7F) {
      if (_paramLen > 0) { _paramLen--; _paramBuf[_paramLen] = '\0'; }
      return true;
    }
    if (c >= 32 && c < 127 && _paramLen < ADMIN_PARAM_MAX - 1) {
      _paramBuf[_paramLen++] = c;
      _paramBuf[_paramLen] = '\0';
      return true;
    }
    return false;
  }

  bool sendParamCommand() {
    if (!_pendingCmd || _contactIdx < 0) return false;
    char fullCmd[160];
    snprintf(fullCmd, sizeof(fullCmd), "%s%s", _pendingCmd->cmd, _paramBuf);
    return sendCommand(fullCmd);
  }

  // =====================================================================
  // Confirmation
  // =====================================================================

  void renderConfirm(DisplayDriver& display, int y) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, y);
    display.print(MECK_TR("Confirm:", "Confirmer :"));

    y += 16;
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, y);
    if (_pendingCmd) display.print(MECK_TR(_pendingCmd->label, _pendingCmd->label_fr));

    y += 14;
    display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
    display.setCursor(0, y);

    // Show the param value if one was collected
    if (_pendingCmd && (_pendingCmd->flags & CMDF_PARAM) && _paramLen > 0) {
      char preview[80];
      snprintf(preview, sizeof(preview), MECK_TR("Value: %s", "Valeur : %s"), _paramBuf);
      display.print(preview);
      y += the_mesh.getNodePrefs()->smallLineH() + 1;
      display.setCursor(0, y);
    }

    if (_pendingCmd && (_pendingCmd->flags & CMDF_EXPECT_TIMEOUT)) {
      display.print(MECK_TR("Timeout response is normal.", "Pas de r\xC3\xA9ponse : normal."));
    } else {
      display.print(MECK_TR("Enter=Yes  Q=No", "Entr\xC3\xA9" "e=Oui  Q=Non"));
    }

    display.setTextSize(1);
  }

  bool handleConfirmInput(char c) {
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      if (_pendingCmd && (_pendingCmd->flags & CMDF_PARAM) && _paramLen > 0) {
        return sendParamCommand();
      } else if (_pendingCmd) {
        return sendCommand(_pendingCmd->cmd);
      }
      return false;
    }
    if (c == KEY_ADMIN_EXIT || c == 'n' || c == 'N') {
      _state = STATE_COMMAND_MENU;
      return true;
    }
    return false;
  }

  // =====================================================================
  // Response View
  // =====================================================================

  void renderResponse(DisplayDriver& display, int y, int bodyHeight) {
    display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
    int lineHeight = the_mesh.getNodePrefs()->smallLineH();

    display.setColor((_state == STATE_ERROR) ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);

    int maxLines = bodyHeight / lineHeight;
    int lineCount = 0;
    int skipLines = _responseScroll;

    const char* p = _response;
    char lineBuf[80];

    while (*p && lineCount < maxLines + skipLines) {
      int i = 0;
      while (*p && *p != '\n' && i < 79) lineBuf[i++] = *p++;
      lineBuf[i] = '\0';
      if (*p == '\n') p++;

      if (lineCount >= skipLines && lineCount < skipLines + maxLines) {
        display.setCursor(2, y);
        display.print(lineBuf);
        y += lineHeight;
      }
      lineCount++;
    }

    // Scroll indicators
    if (_responseScroll > 0) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 8, 14);
      display.print("^");
    }
    if (lineCount > skipLines + maxLines) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 8, y - lineHeight);
      display.print("v");
    }

    display.setTextSize(1);
  }

  bool handleResponseInput(char c) {
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_responseScroll > 0) { _responseScroll--; return true; }
    }
    if (c == 's' || c == 'S' || c == 0xF1) {
      _responseScroll++; return true;
    }
    if (c == KEY_ADMIN_EXIT) {
      if (_state == STATE_ERROR && _permissions == 0) {
        _state = STATE_PASSWORD_ENTRY;
      } else {
        _state = STATE_COMMAND_MENU;
      }
      return true;
    }
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      _state = STATE_COMMAND_MENU;
      return true;
    }
    return false;
  }

  // =====================================================================
  // Waiting
  // =====================================================================

  void renderWaiting(DisplayDriver& display, int y, const char* msg) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    int cx = (display.width() - display.getTextWidth(msg)) / 2;
    int cy = y + 20;
    display.setCursor(cx, cy);
    display.print(msg);

    if (_cmdSentAt > 0) {
      char elapsed[16];
      snprintf(elapsed, sizeof(elapsed), "%lus", (millis() - _cmdSentAt) / 1000);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor((display.width() - display.getTextWidth(elapsed)) / 2, cy + 14);
      display.print(elapsed);
    }
  }

  // =====================================================================
  // Shared menu item renderer
  // =====================================================================

  void renderMenuItem(DisplayDriver& display, int y, int lineHeight,
                      bool selected, const char* label, bool warn) {
    if (selected) {
      display.setColor(DisplayDriver::LIGHT);
      display.fillRect(0, y + the_mesh.getNodePrefs()->smallHighlightOff(), display.width(), lineHeight);
      display.setColor(DisplayDriver::DARK);
    } else if (warn) {
      display.setColor(DisplayDriver::YELLOW);
    } else {
      display.setColor(DisplayDriver::LIGHT);
    }

    display.setCursor(2, y);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s %s", selected ? ">" : " ", label);
    display.print(buf);
  }

  // =====================================================================
  // Send command to repeater
  // =====================================================================

  bool sendCommand(const char* cmd) {
    if (_contactIdx < 0 || !cmd || cmd[0] == '\0') return false;

    if (the_mesh.uiSendCliCommand(_contactIdx, cmd)) {
      _state = STATE_COMMAND_PENDING;
      _cmdSentAt = millis();
      _response[0] = '\0';
      _responseLen = 0;
      _responseScroll = 0;
      _responseTotalLines = 0;
      return true;
    } else {
      snprintf(_response, sizeof(_response), MECK_TR("Send failed.", "\xC3\x89" "chec de l'envoi."));
      _responseLen = strlen(_response);
      _state = STATE_ERROR;
      return true;
    }
  }
};

// =====================================================================
// Login implementation (requires MyMesh)
// =====================================================================

inline bool RepeaterAdminScreen::doLogin() {
  if (_contactIdx < 0 || _pwdLen == 0) return false;

  uint32_t timeout_ms = 0;
  if (the_mesh.uiLoginToRepeater(_contactIdx, _password, timeout_ms)) {
    _state = STATE_LOGGING_IN;
    _cmdSentAt = millis();
    // Add a 5s buffer over the mesh estimate to account for blocking e-ink
    // refreshes.
    // Fall back to ADMIN_TIMEOUT_MS if the estimate came back zero.
    _loginTimeoutMs = (timeout_ms > 0) ? timeout_ms + 5000 : ADMIN_TIMEOUT_MS;
    _waitingForLogin = true;
    return true;
  } else {
    snprintf(_response, sizeof(_response), MECK_TR("Send failed.\nCheck contact path.", "\xC3\x89" "chec de l'envoi.\nV\xC3\xA9rifiez le chemin."));
    _responseLen = strlen(_response);
    _state = STATE_ERROR;
    return true;
  }
}