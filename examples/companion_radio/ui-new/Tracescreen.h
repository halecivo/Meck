#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#include <MeshCore.h>
#include <Packet.h>
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
  #include "ui2/Ui2Mesh.h"
#endif

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

// ---------------------------------------------------------------------------
// TraceScreen
// ---------------------------------------------------------------------------
// Standalone trace path tool for the T-Deck Pro.  The user builds a repeater
// chain from the contacts list or by typing comma-separated hash values, sends
// a PAYLOAD_TYPE_TRACE packet direct-routed through the chain, and views
// per-hop SNR results.
//
// Path size (1-byte or 2-byte hashes) follows the device's path_hash_mode
// setting but can be toggled on this screen.
//
// The trace packet is created via Mesh::createTrace() and sent via
// Mesh::sendDirect().  Each repeater in the chain checks if its pub_key
// prefix matches the next hash in the payload; if so, it appends its receive
// SNR*4 to the packet's path field and forwards.  When the packet reaches
// the end of its given path, onTraceRecv() fires on the receiving node.
//
// For round-trip traces the user should build a symmetric path
// (e.g. A,B,C,B,A) and must be able to hear the last repeater directly.
// ---------------------------------------------------------------------------

#define TRACE_MAX_HOPS     16
#define TRACE_TIMEOUT_MS   30000   // 30 second timeout
#define TRACE_EDIT_BUF     80      // Max chars for typed path

class TraceScreen : public UIScreen {
public:
  enum ScreenState {
    STATE_BUILD,      // Building the path
    STATE_PICK_HOP,   // Picking a repeater from contacts
    STATE_RUNNING,    // Trace sent, waiting for response
    STATE_RESULTS     // Showing results
  };

  // Trace result data (filled by onTraceResult callback)
  struct TraceResult {
    uint8_t  hashes[TRACE_MAX_HOPS * 2];  // Hash bytes (1 or 2 per hop)
    int8_t   snrs[TRACE_MAX_HOPS];        // SNR*4 per hop
    int8_t   final_snr;                   // SNR of the response arriving back
    int      hopCount;                    // Number of hops that responded
    int      totalHops;                   // Total hops in the path
    uint32_t duration_ms;                 // Round-trip time
    bool     valid;
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;

  ScreenState _state;

  // Path being built
  uint8_t _pathBuf[TRACE_MAX_HOPS * 2];  // Hash bytes (max 2 bytes per hop)
  int     _hopCount;
  int     _bytesPerHop;                   // 1 or 2

  // Menu navigation (STATE_BUILD)
  int _menuSel;

  // Inline text editor (for Type Path)
  bool _editing;
  char _editBuf[TRACE_EDIT_BUF];
  int  _editPos;

  // Repeater picker (STATE_PICK_HOP)
  static const int MAX_REPEATERS = 200;
  uint16_t* _repIdx;       // Indices into contact table (PSRAM)
  int _repCount;
  int _repSel;
  int _repScroll;

  // Trace state (STATE_RUNNING / STATE_RESULTS)
  uint32_t _traceTag;
  uint32_t _traceAuth;
  unsigned long _traceSentAt;
  TraceResult _result;

  // Results scroll
  int _resultScroll;

  bool _wantExit;

  // --- Menu helpers (STATE_BUILD) ---
  // Menu layout:
  //   0: Mode selector (1-byte / 2-byte)
  //   1: Type Path (inline text editor)
  //   2..hopCount+1: each hop
  //   next: + Add repeater (if < TRACE_MAX_HOPS)
  //   next: Remove last (if hopCount > 0)
  //   next: Run Trace (if hopCount > 0)
  //   last: Exit
  enum MenuItem {
    MENU_PATH_SIZE = 0,
    MENU_TYPE_PATH = 1,
    MENU_HOP_BASE = 2,
    MENU_ADD_HOP = 200,
    MENU_REMOVE_LAST,
    MENU_RUN_TRACE,
    MENU_EXIT
  };

  int buildMenuCount() const {
    int count = 2;  // Mode + Type Path
    count += _hopCount;
    if (_hopCount < TRACE_MAX_HOPS) count++;  // Add hop
    if (_hopCount > 0) count++;  // Remove last
    if (_hopCount > 0) count++;  // Run Trace
    count++;  // Exit
    return count;
  }

  MenuItem menuItemAt(int idx) const {
    if (idx == 0) return MENU_PATH_SIZE;
    if (idx == 1) return MENU_TYPE_PATH;
    int pos = 2;
    for (int h = 0; h < _hopCount; h++) {
      if (idx == pos) return (MenuItem)(MENU_HOP_BASE + h);
      pos++;
    }
    if (_hopCount < TRACE_MAX_HOPS) {
      if (idx == pos) return MENU_ADD_HOP;
      pos++;
    }
    if (_hopCount > 0) {
      if (idx == pos) return MENU_REMOVE_LAST;
      pos++;
    }
    if (_hopCount > 0) {
      if (idx == pos) return MENU_RUN_TRACE;
      pos++;
    }
    return MENU_EXIT;
  }

  // Build repeater list from contacts
  void buildRepeaterList() {
    _repCount = 0;
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo c;
    for (uint32_t i = 0; i < numContacts && _repCount < MAX_REPEATERS; i++) {
      if (the_mesh.getContactByIdx(i, c)) {
        if (c.type == ADV_TYPE_REPEATER) {
          _repIdx[_repCount++] = (uint16_t)i;
        }
      }
    }
  }

  // Look up contact name from hash prefix
  bool findNameForHash(const uint8_t* hash, int hashLen, char* name, size_t nameLen) const {
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo c;
    // First pass: repeaters only
    for (uint32_t i = 0; i < numContacts; i++) {
      if (the_mesh.getContactByIdx(i, c) && c.type == ADV_TYPE_REPEATER) {
        if (memcmp(c.id.pub_key, hash, hashLen) == 0) {
          strncpy(name, c.name, nameLen);
          name[nameLen - 1] = '\0';
          return true;
        }
      }
    }
    // Second pass: any contact
    for (uint32_t i = 0; i < numContacts; i++) {
      if (the_mesh.getContactByIdx(i, c)) {
        if (memcmp(c.id.pub_key, hash, hashLen) == 0) {
          strncpy(name, c.name, nameLen);
          name[nameLen - 1] = '\0';
          return true;
        }
      }
    }
    return false;
  }

  // Parse comma-separated decimal values from edit buffer into path
  // Returns number of hops parsed, or -1 on error
  int parseTypedPath() {
    if (_editBuf[0] == '\0') return 0;

    uint8_t tmpPath[TRACE_MAX_HOPS * 2];
    int hops = 0;
    const char* p = _editBuf;

    while (*p && hops < TRACE_MAX_HOPS) {
      // Skip whitespace/commas
      while (*p == ',' || *p == ' ') p++;
      if (*p == '\0') break;

      // Parse hex number (companion app uses hex hash values)
      char* end;
      long val = strtol(p, &end, 16);
      if (end == p) return -1;  // No digits found
      p = end;

      if (_bytesPerHop == 1) {
        if (val < 0 || val > 255) return -1;
        tmpPath[hops] = (uint8_t)val;
      } else {
        if (val < 0 || val > 65535) return -1;
        // Big-endian storage: hash display = (pub_key[0] << 8) | pub_key[1]
        // So val >> 8 is pub_key[0], val & 0xFF is pub_key[1]
        tmpPath[hops * 2]     = (uint8_t)((val >> 8) & 0xFF);
        tmpPath[hops * 2 + 1] = (uint8_t)(val & 0xFF);
      }
      hops++;
    }

    if (hops > 0) {
      memcpy(_pathBuf, tmpPath, hops * _bytesPerHop);
      _hopCount = hops;
    }
    return hops;
  }

  // Build display string from current path (for showing in edit field)
  void pathToEditBuf() {
    _editBuf[0] = '\0';
    _editPos = 0;
    for (int i = 0; i < _hopCount; i++) {
      char tmp[8];
      if (_bytesPerHop == 1) {
        snprintf(tmp, sizeof(tmp), "%02X", _pathBuf[i]);
      } else {
        uint16_t val = ((uint16_t)_pathBuf[i * 2] << 8) | _pathBuf[i * 2 + 1];
        snprintf(tmp, sizeof(tmp), "%04X", val);
      }
      if (i > 0) {
        if (_editPos < TRACE_EDIT_BUF - 1) _editBuf[_editPos++] = ',';
      }
      int tlen = strlen(tmp);
      if (_editPos + tlen < TRACE_EDIT_BUF - 1) {
        memcpy(&_editBuf[_editPos], tmp, tlen);
        _editPos += tlen;
      }
    }
    _editBuf[_editPos] = '\0';
  }

  // Truncate long names to maxLen chars + "..." for display
  static void truncateName(char* name, int maxLen = 10) {
    if ((int)strlen(name) > maxLen) {
      name[maxLen] = '\0';
      // Remove trailing space before ellipsis
      while (maxLen > 0 && name[maxLen - 1] == ' ') {
        name[--maxLen] = '\0';
      }
      strcat(name, "...");
    }
  }

  // Draw signal bars (3 bars) based on SNR
  void drawSignalBars(DisplayDriver& display, int x, int y, int8_t snr4) {
    float snr = snr4 / 4.0f;
    // 3 bars: low >= -5, mid >= 3, high >= 8
    int bars = 0;
    if (snr >= -5.0f) bars = 1;
    if (snr >= 3.0f) bars = 2;
    if (snr >= 8.0f) bars = 3;

    int barW = 3;
    int gap = 1;
    int heights[] = { 4, 7, 10 };
    for (int b = 0; b < 3; b++) {
      int bx = x + b * (barW + gap);
      int by = y + 10 - heights[b];
      if (b < bars) {
        display.setColor(DisplayDriver::GREEN);
      } else {
        display.setColor(DisplayDriver::DARK);
      }
      display.fillRect(bx, by, barW, heights[b]);
    }
  }

public:
  TraceScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _state(STATE_BUILD),
      _hopCount(0), _bytesPerHop(2),
      _menuSel(0), _editing(false), _editPos(0),
      _repCount(0), _repSel(0), _repScroll(0),
      _traceTag(0), _traceAuth(0), _traceSentAt(0),
      _resultScroll(0), _wantExit(false) {
    memset(_pathBuf, 0, sizeof(_pathBuf));
    memset(_editBuf, 0, sizeof(_editBuf));
    memset(&_result, 0, sizeof(_result));
  #if defined(ESP32) && defined(BOARD_HAS_PSRAM)
    _repIdx = (uint16_t*)ps_calloc(MAX_REPEATERS, sizeof(uint16_t));
  #else
    _repIdx = new uint16_t[MAX_REPEATERS]();
  #endif
  }

  bool wantsExit() const { return _wantExit; }
  bool isEditing() const { return _editing; }

  // True if the highlighted menu row is the Mode selector (STATE_BUILD only).
  bool isOnModeRow() const {
    return _state == STATE_BUILD && menuItemAt(_menuSel) == MENU_PATH_SIZE;
  }


  void enter(int pathHashMode) {
    _state = STATE_BUILD;
    _hopCount = 0;
    _menuSel = 0;
    _editing = false;
    _editPos = 0;
    memset(_editBuf, 0, sizeof(_editBuf));
    _repSel = 0;
    _repScroll = 0;
    _wantExit = false;
    _resultScroll = 0;
    memset(_pathBuf, 0, sizeof(_pathBuf));
    memset(&_result, 0, sizeof(_result));

    // Default to device path hash mode (clamped to 1 or 2 for trace)
    _bytesPerHop = (pathHashMode >= 1) ? 2 : 1;
  }

  // Called by MyMesh::onTraceRecv() via UITask
  void onTraceResult(uint32_t tag, uint8_t flags,
                     const uint8_t* path_snrs, const uint8_t* path_hashes,
                     uint8_t path_byte_len, int8_t final_snr) {
    if (_state != STATE_RUNNING) return;
    if (tag != _traceTag) return;  // Not our trace

    uint8_t pathSz = flags & 0x03;
    int numHops = (pathSz > 0) ? (path_byte_len >> pathSz) : path_byte_len;

    _result.valid = true;
    _result.totalHops = numHops;
    _result.final_snr = final_snr;
    _result.duration_ms = millis() - _traceSentAt;

    // Copy hash data
    int copyBytes = path_byte_len;
    if (copyBytes > (int)sizeof(_result.hashes)) copyBytes = sizeof(_result.hashes);
    memcpy(_result.hashes, path_hashes, copyBytes);

    // Count SNR entries (= number of hops that actually forwarded)
    int snrCount = numHops;
    if (snrCount > TRACE_MAX_HOPS) snrCount = TRACE_MAX_HOPS;
    _result.hopCount = snrCount;
    for (int i = 0; i < snrCount; i++) {
      _result.snrs[i] = (int8_t)path_snrs[i];
    }

    _state = STATE_RESULTS;
    _resultScroll = 0;
    Serial.printf("[Trace] Result received: %d hops, %dms\n", numHops, _result.duration_ms);
  }

  // --- Render ---
  int render(DisplayDriver& display) override {
#ifdef MECK_UI_V2
    return renderV2(display);
#endif
    // Header
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.print(MECK_TR("Trace Path", "Tracer un chemin"));
    display.drawRect(0, 11, display.width(), 1);

    if (_state == STATE_BUILD) {
      return renderBuild(display);
    } else if (_state == STATE_PICK_HOP) {
      return renderPicker(display);
    } else if (_state == STATE_RUNNING) {
      return renderRunning(display);
    } else {
      return renderResults(display);
    }
  }

private:
#ifdef MECK_UI_V2
  // Hash of hop `i` in `buf` as hex ("A3" or "A3F0")
  void hopHex(const uint8_t* buf, int i, char* out, size_t len) const {
    if (_bytesPerHop == 1) snprintf(out, len, "%02X", buf[i]);
    else snprintf(out, len, "%04X", ((uint16_t)buf[i * 2] << 8) | buf[i * 2 + 1]);
  }

  // Current path as "A3F0, 11B2"
  void pathText(char* out, size_t len) const {
    size_t pos = 0;
    out[0] = '\0';
    for (int i = 0; i < _hopCount && pos + 8 < len; i++) {
      char h[8];
      hopHex(_pathBuf, i, h, sizeof(h));
      pos += snprintf(out + pos, len - pos, i ? ", %s" : "%s", h);
    }
  }

  int renderV2(DisplayDriver& display) {
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    switch (_state) {
      case STATE_BUILD:    return renderBuildV2(c);
      case STATE_PICK_HOP: return renderPickerV2(c);
      case STATE_RUNNING:  return renderRunningV2(c);
      default:             return renderResultsV2(c);
    }
  }

  int renderBuildV2(Ui2Canvas& c) {
    using namespace ui2;
    const int count = buildMenuCount();
    char right[16];
    Ui2List::pageLabel(right, sizeof(right), _menuSel, count, ROWS_PER_PAGE);
    Ui2Chrome::titleBar(c, MECK_TR("Trace path", "Tracer un chemin"), right);

    const int start = Ui2List::pageStart(_menuSel, ROWS_PER_PAGE);
    for (int i = start; i < count && i < start + ROWS_PER_PAGE; i++) {
      MenuItem item = menuItemAt(i);
      Ui2Row r;
      char val[16], sub[TRACE_EDIT_BUF + 4], name[33];
      switch (item) {
        case MENU_PATH_SIZE:
          r.icon = UI2_IC_HASH;
          r.title = MECK_TR("Hash size", "Taille du hash");
          snprintf(val, sizeof(val), MECK_TR("%d-byte", "%d octet%s"), _bytesPerHop, _bytesPerHop > 1 ? "s" : "");
          r.right = val;
          r.sub = MECK_TR("A/D to change", "A/D pour changer");
          break;
        case MENU_TYPE_PATH:
          r.icon = UI2_IC_EDIT;
          r.title = MECK_TR("Type path", "Saisir le chemin");
          if (_editing) {
            snprintf(sub, sizeof(sub), "%s_", _editBuf);
            r.pending = true;
          } else if (_hopCount > 0) {
            pathText(sub, sizeof(sub));
          } else {
            snprintf(sub, sizeof(sub), "%s", MECK_TR("Hex hashes, comma separated", "Hashs hexa, s\xC3\xA9par\xC3\xA9s par virgule"));
          }
          r.sub = sub;
          break;
        case MENU_ADD_HOP:
          r.icon = UI2_IC_PLUS;
          r.title = MECK_TR("Add repeater", "Ajouter un r\xC3\xA9p\xC3\xA9teur");
          r.chevron = true;
          break;
        case MENU_REMOVE_LAST:
          r.icon = UI2_IC_MINUS;
          r.title = MECK_TR("Remove last hop", "Retirer le dernier saut");
          break;
        case MENU_RUN_TRACE:
          r.icon = UI2_IC_TRACE;
          r.title = MECK_TR("Run trace", "Lancer le tra\xC3\xA7" "age");
          r.sub = MECK_TR("Sends a trace packet", "Envoie un paquet de tra\xC3\xA7" "age");
          break;
        case MENU_EXIT:
          r.icon = UI2_IC_BACK;
          r.title = MECK_TR("Exit", "Quitter");
          break;
        default: {
          int h = item - MENU_HOP_BASE;
          r.icon = UI2_IC_REPEATER;
          hopHex(_pathBuf, h, val, sizeof(val));
          r.right = val;
          snprintf(sub, sizeof(sub), MECK_TR("Hop %d", "Saut %d"), h + 1);
          if (findNameForHash(&_pathBuf[h * _bytesPerHop], _bytesPerHop, name, sizeof(name))) {
            r.title = name;
            r.sub = sub;
          } else {
            r.title = sub;
            r.sub = MECK_TR("Not in contacts", "Pas dans les contacts");
          }
          break;
        }
      }
      r.selected = (i == _menuSel);
      Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
    }

    if (_editing) {
      const Ui2Key keys[] = {
        ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Apply", "Appliquer")),
        ui2Key("0-F", MECK_TR("Hash", "Hash")),
        ui2Key("Sh+Del", MECK_TR("Cancel", "Annuler")),
      };
      Ui2Chrome::footer(c, keys, 3);
    } else {
      const Ui2Key keys[] = {
        ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Select", "Choisir")),
        ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
        ui2Key("Q", MECK_TR("Exit", "Quitter")),
      };
      Ui2Chrome::footer(c, keys, 3);
    }
    return 5000;
  }

  int renderPickerV2(Ui2Canvas& c) {
    using namespace ui2;
    char right[16];
    Ui2List::pageLabel(right, sizeof(right), _repSel, _repCount, ROWS_PER_PAGE);
    Ui2Chrome::titleBar(c, MECK_TR("Add repeater", "Ajouter un r\xC3\xA9p\xC3\xA9teur"), right);
    if (_repCount == 0) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_REPEATER, MECK_TR("No repeaters in contacts", "Aucun r\xC3\xA9p\xC3\xA9teur connu"),
                          MECK_TR("Type the path as hex hashes instead.", "Saisissez le chemin en hexa."));
      const Ui2Key keys[] = { ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 1);
      return 5000;
    }
    if (_repSel >= _repCount) _repSel = _repCount - 1;
    if (_repSel < 0) _repSel = 0;
    const int start = Ui2List::pageStart(_repSel, ROWS_PER_PAGE);
    for (int i = start; i < _repCount && i < start + ROWS_PER_PAGE; i++) {
      ContactInfo ci;
      if (!the_mesh.getContactByIdx(_repIdx[i], ci)) continue;
      char hex[8];
      if (_bytesPerHop == 1) snprintf(hex, sizeof(hex), "%02X", ci.id.pub_key[0]);
      else snprintf(hex, sizeof(hex), "%02X%02X", ci.id.pub_key[0], ci.id.pub_key[1]);
      Ui2Row r;
      r.icon = UI2_IC_REPEATER;
      r.title = ci.name;
      r.right = hex;
      r.selected = (i == _repSel);
      Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Add", "Ajouter")),
      ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 3);
    return 5000;
  }

  int renderRunningV2(Ui2Canvas& c) {
    using namespace ui2;
    Ui2Chrome::titleBar(c, MECK_TR("Trace path", "Tracer un chemin"));
    unsigned long elapsed = millis() - _traceSentAt;
    c.icon((SCREEN_W - 32) / 2, 80, UI2_IC_TRACE, 32);
    c.textFit(PAD, 140, SCREEN_W - 2 * PAD, MECK_TR("Tracing\xE2\x80\xA6", "Tra\xC3\xA7" "age\xE2\x80\xA6"), UI2_F_TITLE, true, UI2_CENTER);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), MECK_TR("%d hops \xC2\xB7 %d-byte hashes", "%d sauts \xC2\xB7 hash %d o"), _hopCount, _bytesPerHop);
    c.textFit(PAD, 162, SCREEN_W - 2 * PAD, tmp, UI2_F_BODY, true, UI2_CENTER);
    snprintf(tmp, sizeof(tmp), "%lu.%lu s", elapsed / 1000, (elapsed / 100) % 10);
    c.textFit(PAD, 196, SCREEN_W - 2 * PAD, tmp, UI2_F_NUM16, true, UI2_CENTER);
    const int16_t barX = 16, barW = SCREEN_W - 32;
    c.rect(barX, 212, barW, 8);
    int fill = (int)((unsigned long)(barW - 4) * elapsed / TRACE_TIMEOUT_MS);
    if (fill > barW - 4) fill = barW - 4;
    if (fill > 0) c.fill(barX + 2, 214, fill, 4);
    snprintf(tmp, sizeof(tmp), MECK_TR("Gives up after %d s", "Abandon apr\xC3\xA8s %d s"), TRACE_TIMEOUT_MS / 1000);
    c.textFit(PAD, 238, SCREEN_W - 2 * PAD, tmp, UI2_F_META, true, UI2_CENTER);

    if (elapsed >= TRACE_TIMEOUT_MS) {
      _state = STATE_RESULTS;
      _result.valid = false;
      _result.duration_ms = TRACE_TIMEOUT_MS;
      Serial.println("[Trace] Timeout");
    }
    const Ui2Key keys[] = { ui2Key("Sh+Del", MECK_TR("Cancel", "Annuler")) };
    Ui2Chrome::footer(c, keys, 1);
    return 1000;  // elapsed counter; e-ink refreshes stay coarse
  }

  int renderResultsV2(Ui2Canvas& c) {
    using namespace ui2;
    if (!_result.valid) {
      Ui2Chrome::titleBar(c, MECK_TR("Trace result", "R\xC3\xA9sultat"));
      char tmp[48];
      snprintf(tmp, sizeof(tmp), MECK_TR("No response after %d s.", "Pas de r\xC3\xA9ponse apr\xC3\xA8s %d s."), TRACE_TIMEOUT_MS / 1000);
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_WARN, MECK_TR("Trace timed out", "D\xC3\xA9lai d\xC3\xA9pass\xC3\xA9"), tmp);
    } else {
      char right[16];
      snprintf(right, sizeof(right), "%d ms", (int)_result.duration_ms);
      Ui2Chrome::titleBar(c, MECK_TR("Trace result", "R\xC3\xA9sultat"), right);
      const int total = _result.hopCount + 1;   // hops + return SNR
      if (_resultScroll > total - ROWS_PER_PAGE) _resultScroll = total - ROWS_PER_PAGE;
      if (_resultScroll < 0) _resultScroll = 0;
      for (int i = _resultScroll; i < total && i < _resultScroll + ROWS_PER_PAGE; i++) {
        Ui2Row r;
        char name[33], snr[16], sub[24];
        int8_t snr4 = (i < _result.hopCount) ? _result.snrs[i] : _result.final_snr;
        snprintf(snr, sizeof(snr), "%.1f dB", snr4 / 4.0f);
        r.right = snr;
        r.sig = ui2SignalIcon(snr4 / 4.0f);
        if (i < _result.hopCount) {
          r.icon = UI2_IC_REPEATER;
          char hex[8];
          hopHex(_result.hashes, i, hex, sizeof(hex));
          if (findNameForHash(&_result.hashes[i * _bytesPerHop], _bytesPerHop, name, sizeof(name))) {
            r.title = name;
            snprintf(sub, sizeof(sub), MECK_TR("Hop %d \xC2\xB7 %s", "Saut %d \xC2\xB7 %s"), i + 1, hex);
          } else {
            snprintf(name, sizeof(name), MECK_TR("Hop %d", "Saut %d"), i + 1);
            r.title = name;
            snprintf(sub, sizeof(sub), "%s", hex);
          }
          r.sub = sub;
        } else {
          r.icon = UI2_IC_DOWN;
          r.title = MECK_TR("Return to this node", "Retour \xC3\xA0 ce n\xC5\x93ud");
          r.sub = MECK_TR("Signal of the reply", "Signal de la r\xC3\xA9ponse");
        }
        Ui2List::drawRow(c, BODY_Y + (i - _resultScroll) * ROW_H, r);
      }
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Trace again", "Relancer")),
      ui2Key("W/S", MECK_TR("Scroll", "D\xC3\xA9" "filer"), true),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 3);
    return 5000;
  }
#endif

  int renderBuild(DisplayDriver& display) {
    char tmp[TRACE_EDIT_BUF + 16];
    int y = 14;
    int lineH = 11;
    int menuCount = buildMenuCount();
    int maxVisible = (display.height() - y - 14) / lineH;
    if (maxVisible < 1) maxVisible = 1;

    // Scroll window
    int scrollTop = 0;
    if (_menuSel >= scrollTop + maxVisible) scrollTop = _menuSel - maxVisible + 1;
    if (_menuSel < scrollTop) scrollTop = _menuSel;

    for (int vi = 0; vi < maxVisible && (scrollTop + vi) < menuCount; vi++) {
      int idx = scrollTop + vi;
      MenuItem item = menuItemAt(idx);
      char prefix = (idx == _menuSel) ? '>' : ' ';

      display.setCursor(0, y);
      display.setColor(DisplayDriver::LIGHT);

      switch (item) {
        case MENU_PATH_SIZE:
          snprintf(tmp, sizeof(tmp), MECK_TR("%c Mode: %d-byte", "%c Mode : %d o"), prefix, _bytesPerHop);
          display.print(tmp);
          if (idx == _menuSel) {
            const char* hint = "(A/D)";
            display.setCursor(display.width() - display.getTextWidth(hint) - 4, y);
            display.print(hint);
          }
          break;

        case MENU_TYPE_PATH:
          if (_editing) {
            // Active text editor with cursor
            display.setColor(DisplayDriver::GREEN);
            snprintf(tmp, sizeof(tmp), MECK_TR("  Path: %s_", "  Chemin : %s_"), _editBuf);
            display.print(tmp);
          } else if (_hopCount > 0) {
            // Show current path as decimal values
            char pathStr[TRACE_EDIT_BUF];
            pathStr[0] = '\0';
            int pos = 0;
            for (int i = 0; i < _hopCount && pos < (int)sizeof(pathStr) - 8; i++) {
              if (i > 0) pathStr[pos++] = ',';
              if (_bytesPerHop == 1) {
                pos += snprintf(&pathStr[pos], sizeof(pathStr) - pos, "%02X", _pathBuf[i]);
              } else {
                uint16_t val = ((uint16_t)_pathBuf[i * 2] << 8) | _pathBuf[i * 2 + 1];
                pos += snprintf(&pathStr[pos], sizeof(pathStr) - pos, "%04X", val);
              }
            }
            snprintf(tmp, sizeof(tmp), MECK_TR("%c Path: %s", "%c Chemin : %s"), prefix, pathStr);
            display.print(tmp);
          } else {
            snprintf(tmp, sizeof(tmp), MECK_TR("%c Type Path: [Press Enter]", "%c Saisir chemin : [Entr\xC3\xA9" "e]"), prefix);
            display.print(tmp);
          }
          break;

        case MENU_ADD_HOP:
          display.setColor(DisplayDriver::GREEN);
          snprintf(tmp, sizeof(tmp), MECK_TR("%c + Add repeater...", "%c + Ajouter un r\xC3\xA9p\xC3\xA9teur..."), prefix);
          display.print(tmp);
          break;

        case MENU_REMOVE_LAST:
          snprintf(tmp, sizeof(tmp), MECK_TR("%c - Remove last", "%c - Retirer le dernier"), prefix);
          display.print(tmp);
          break;

        case MENU_RUN_TRACE:
          display.setColor(DisplayDriver::YELLOW);
          snprintf(tmp, sizeof(tmp), MECK_TR("%c   Run Trace", "%c   Lancer le tra\xC3\xA7" "age"), prefix);
          display.print(tmp);
          break;

        case MENU_EXIT:
          snprintf(tmp, sizeof(tmp), MECK_TR("%c   Exit", "%c   Quitter"), prefix);
          display.print(tmp);
          break;

        default:
          // Hop line
          if (item >= MENU_HOP_BASE && item < MENU_HOP_BASE + TRACE_MAX_HOPS) {
            int hopIdx = item - MENU_HOP_BASE;
            int offset = hopIdx * _bytesPerHop;
            char hopName[24];
            uint16_t hashVal;
            if (_bytesPerHop == 1) {
              hashVal = _pathBuf[offset];
            } else {
              hashVal = ((uint16_t)_pathBuf[offset] << 8) | _pathBuf[offset + 1];
            }
            if (findNameForHash(&_pathBuf[offset], _bytesPerHop, hopName, sizeof(hopName))) {
              truncateName(hopName);
              display.setColor(DisplayDriver::GREEN);
              snprintf(tmp, sizeof(tmp), "%c%d: %s (%X)", prefix, hopIdx + 1,
                       hopName, hashVal);
            } else {
              snprintf(tmp, sizeof(tmp), "%c%d: (%X)", prefix, hopIdx + 1, hashVal);
            }
            display.print(tmp);
          }
          break;
      }
      y += lineH;
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, footerY);
    if (_editing) {
      display.print(MECK_TR("Q:Cancel Enter:Apply", "Q:Annuler Entr\xC3\xA9" "e:Appliquer"));
    } else {
      display.print(MECK_TR("Q:Exit W/S:Nav Ent:Sel", "Q:Quitter W/S:Nav Ent:S\xC3\xA9l"));
    }

    return 5000;
  }

  int renderPicker(DisplayDriver& display) {
    char tmp[48];
    int y = 14;
    int lineH = 11;
    int maxVisible = (display.height() - y - 14) / lineH;
    if (maxVisible < 1) maxVisible = 1;

    if (_repCount == 0) {
      display.setCursor(0, y);
      display.setColor(DisplayDriver::RED);
      display.print(MECK_TR("No repeaters in contacts", "Aucun r\xC3\xA9p\xC3\xA9teur connu"));
      y += lineH;
      display.setColor(DisplayDriver::LIGHT);
      display.print(MECK_TR("Press Q to go back", "Q pour revenir"));
    } else {
      // Clamp scroll
      if (_repSel >= _repCount) _repSel = _repCount - 1;
      if (_repSel < 0) _repSel = 0;
      if (_repSel < _repScroll) _repScroll = _repSel;
      if (_repSel >= _repScroll + maxVisible) _repScroll = _repSel - maxVisible + 1;

      for (int vi = 0; vi < maxVisible && (_repScroll + vi) < _repCount; vi++) {
        int idx = _repScroll + vi;
        uint16_t contactIdx = _repIdx[idx];
        ContactInfo c;
        if (!the_mesh.getContactByIdx(contactIdx, c)) continue;

        char prefix = (idx == _repSel) ? '>' : ' ';
        display.setCursor(0, y);

        // Show name + decimal hash value
        char filteredName[24];
        display.translateUTF8ToBlocks(filteredName, c.name, sizeof(filteredName));
        truncateName(filteredName, 14);  // Picker has more room
        uint16_t hashVal;
        if (_bytesPerHop == 1) {
          hashVal = c.id.pub_key[0];
        } else {
          hashVal = ((uint16_t)c.id.pub_key[0] << 8) | c.id.pub_key[1];
        }
        snprintf(tmp, sizeof(tmp), "%c %s (%X)", prefix, filteredName, hashVal);
        display.setColor((idx == _repSel) ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
        display.print(tmp);
        y += lineH;
      }
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, footerY);
    display.print(MECK_TR("Q:Back W/S:Scroll Ent:Add", "Q:Ret W/S:D\xC3\xA9" "fil Ent:Ajout"));

    return 5000;
  }

  int renderRunning(DisplayDriver& display) {
    int y = 14;
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, y);
    display.print(MECK_TR("Tracing...", "Tra\xC3\xA7" "age..."));
    y += 14;

    // Show path summary
    display.setColor(DisplayDriver::LIGHT);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), MECK_TR("%d hops, %d-byte mode", "%d sauts, mode %d o"), _hopCount, _bytesPerHop);
    display.setCursor(0, y);
    display.print(tmp);
    y += 14;

    // Elapsed time
    unsigned long elapsed = millis() - _traceSentAt;
    snprintf(tmp, sizeof(tmp), MECK_TR("Elapsed: %lu ms", "\xC3\x89" "coul\xC3\xA9 : %lu ms"), elapsed);
    display.setCursor(0, y);
    display.print(tmp);
    y += 14;

    // Timeout bar
    int barW = display.width() - 20;
    int barH = 4;
    int barX = 10;
    display.setColor(DisplayDriver::DARK);
    display.drawRect(barX, y, barW, barH);
    int fill = (int)((unsigned long)barW * elapsed / TRACE_TIMEOUT_MS);
    if (fill > barW) fill = barW;
    display.setColor(DisplayDriver::GREEN);
    display.fillRect(barX, y, fill, barH);

    // Check timeout
    if (elapsed >= TRACE_TIMEOUT_MS) {
      _state = STATE_RESULTS;
      _result.valid = false;
      _result.duration_ms = TRACE_TIMEOUT_MS;
      Serial.println("[Trace] Timeout");
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, footerY);
    display.print(MECK_TR("Q:Cancel", "Q:Annuler"));

    return 500;  // Fast refresh for elapsed timer
  }

  int renderResults(DisplayDriver& display) {
    char tmp[48];
    int y = 14;
    int lineH = 12;

    if (!_result.valid) {
      display.setColor(DisplayDriver::RED);
      display.setCursor(0, y);
      display.print(MECK_TR("Trace timed out", "D\xC3\xA9lai de tra\xC3\xA7" "age d\xC3\xA9pass\xC3\xA9"));
      y += lineH;
      display.setColor(DisplayDriver::LIGHT);
      snprintf(tmp, sizeof(tmp), MECK_TR("No response after %ds", "Pas de r\xC3\xA9ponse apr\xC3\xA8s %ds"), TRACE_TIMEOUT_MS / 1000);
      display.setCursor(0, y);
      display.print(tmp);
    } else {
      // Duration header
      display.setColor(DisplayDriver::GREEN);
      snprintf(tmp, sizeof(tmp), MECK_TR("Complete: %dms", "Termin\xC3\xA9 : %dms"), (int)_result.duration_ms);
      display.setCursor(0, y);
      display.print(tmp);
      y += lineH + 2;

      int maxVisible = (display.height() - y - 14) / lineH;
      if (maxVisible < 1) maxVisible = 1;

      // Clamp scroll
      int totalItems = _result.hopCount + 1;  // hops + final SNR line
      if (_resultScroll > totalItems - maxVisible) _resultScroll = totalItems - maxVisible;
      if (_resultScroll < 0) _resultScroll = 0;

      for (int vi = 0; vi < maxVisible && (_resultScroll + vi) < totalItems; vi++) {
        int idx = _resultScroll + vi;
        display.setCursor(0, y);

        if (idx < _result.hopCount) {
          // Hop entry
          int offset = idx * _bytesPerHop;
          char hopName[20];
          bool resolved = findNameForHash(&_result.hashes[offset], _bytesPerHop,
                                          hopName, sizeof(hopName));
          if (resolved) truncateName(hopName);

          float snr = _result.snrs[idx] / 4.0f;

          display.setColor(resolved ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
          if (resolved) {
            snprintf(tmp, sizeof(tmp), "%d: %s", idx + 1, hopName);
          } else {
            uint16_t hashVal;
            if (_bytesPerHop == 1) {
              hashVal = _result.hashes[offset];
            } else {
              hashVal = ((uint16_t)_result.hashes[offset] << 8) | _result.hashes[offset + 1];
            }
            snprintf(tmp, sizeof(tmp), "%d: (%X)", idx + 1, hashVal);
          }
          display.print(tmp);

          // SNR value on right
          snprintf(tmp, sizeof(tmp), "%.1fdB", snr);
          int snrW = display.getTextWidth(tmp);
          int barsW = 14;
          display.setCursor(display.width() - snrW - barsW - 4, y);
          display.setColor(DisplayDriver::LIGHT);
          display.print(tmp);

          // Signal bars
          drawSignalBars(display, display.width() - barsW - 1, y, _result.snrs[idx]);

        } else {
          // Final SNR (response arriving back at this node)
          float snr = _result.final_snr / 4.0f;
          display.setColor(DisplayDriver::YELLOW);
          snprintf(tmp, sizeof(tmp), MECK_TR("Return SNR: %.1fdB", "SNR retour : %.1fdB"), snr);
          display.print(tmp);
          drawSignalBars(display, display.width() - 15, y, _result.final_snr);
        }
        y += lineH;
      }
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, footerY);
    display.print(MECK_TR("Q:Back  Ent:New Trace", "Q:Ret  Ent:Nouveau tra\xC3\xA7" "age"));

    return 5000;
  }

public:
  // --- Input handling ---
  bool handleInput(char c) override {
    // Text editing mode consumes all keys
    if (_editing) {
      return handleEditInput(c);
    }

    switch (_state) {
      case STATE_BUILD:   return handleBuildInput(c);
      case STATE_PICK_HOP: return handlePickerInput(c);
      case STATE_RUNNING: return handleRunningInput(c);
      case STATE_RESULTS: return handleResultsInput(c);
    }
    return false;
  }

private:
  // --- Text editor for typed path ---
  bool handleEditInput(char c) {
    // Enter: apply typed path
    if (c == '\r' || c == 13) {
      int parsed = parseTypedPath();
      if (parsed < 0) {
        Serial.println("[Trace] Failed to parse typed path");
        // Stay in edit mode -- user can fix
      } else {
        Serial.printf("[Trace] Parsed %d hops from typed path\n", parsed);
        _editing = false;
      }
      return true;
    }
    // Shift+Del: cancel edit
    if (c == KEY_CANCEL) {
      _editing = false;
      return true;
    }
    // Backspace
    if (c == '\b') {
      if (_editPos > 0) {
        _editPos--;
        _editBuf[_editPos] = '\0';
      }
      return true;
    }
    // Accept hex digits, commas, spaces
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')
        || c == ',' || c == ' ') {
      if (_editPos < TRACE_EDIT_BUF - 1) {
        _editBuf[_editPos++] = c;
        _editBuf[_editPos] = '\0';
      }
      return true;
    }
    return true;  // Consume all keys in edit mode
  }

  bool handleBuildInput(char c) {
    int menuCount = buildMenuCount();

    // W - up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_menuSel > 0) _menuSel--;
      return true;
    }
    // S - down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_menuSel < menuCount - 1) _menuSel++;
      return true;
    }
    // A/D - toggle mode on path size row
    if ((c == 'a' || c == 'A' || c == 'd' || c == 'D') && menuItemAt(_menuSel) == MENU_PATH_SIZE) {
      _bytesPerHop = (_bytesPerHop == 1) ? 2 : 1;
      // Changing mode clears path (byte layout is different)
      _hopCount = 0;
      memset(_pathBuf, 0, sizeof(_pathBuf));
      return true;
    }
    // Shift+Del - exit
    if (c == KEY_CANCEL) {
      _wantExit = true;
      return true;
    }
    // Enter - select
    if (c == '\r' || c == 13) {
      MenuItem item = menuItemAt(_menuSel);
      switch (item) {
        case MENU_TYPE_PATH:
          pathToEditBuf();
          // Enter inline edit mode -- pre-fill with current path if any
          _editing = true;
          return true;

        case MENU_ADD_HOP:
          buildRepeaterList();
          _repSel = 0;
          _repScroll = 0;
          _state = STATE_PICK_HOP;
          return true;

        case MENU_REMOVE_LAST:
          if (_hopCount > 0) {
            _hopCount--;
            if (_menuSel >= buildMenuCount()) _menuSel = buildMenuCount() - 1;
          }
          return true;

        case MENU_RUN_TRACE:
          return sendTrace();

        case MENU_EXIT:
          _wantExit = true;
          return true;

        default:
          break;
      }
      return true;
    }
    return false;
  }

  bool handlePickerInput(char c) {
    // W - up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_repSel > 0) _repSel--;
      return true;
    }
    // S - down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_repSel < _repCount - 1) _repSel++;
      return true;
    }
    // Shift+Del - back to build
    if (c == KEY_CANCEL) {
      _state = STATE_BUILD;
      return true;
    }
    // Enter - add selected repeater
    if (c == '\r' || c == 13) {
      if (_repCount > 0 && _repSel >= 0 && _repSel < _repCount) {
        ContactInfo contact;
        if (the_mesh.getContactByIdx(_repIdx[_repSel], contact)) {
          int offset = _hopCount * _bytesPerHop;
          memcpy(&_pathBuf[offset], contact.id.pub_key, _bytesPerHop);
          _hopCount++;
          uint16_t hashVal = ((uint16_t)contact.id.pub_key[0] << 8)
                           | contact.id.pub_key[1];
          Serial.printf("[Trace] Added hop %d: %s (%X)\n",
                        _hopCount, contact.name, hashVal);
        }
        _state = STATE_BUILD;
        _menuSel = _hopCount + 1;  // Point to row after last hop
      }
      return true;
    }
    return false;
  }

  bool handleRunningInput(char c) {
    // Shift+Del - cancel
    if (c == KEY_CANCEL) {
      _state = STATE_BUILD;
      return true;
    }
    return true;  // Consume all keys while running
  }

  bool handleResultsInput(char c) {
    // W - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_resultScroll > 0) _resultScroll--;
      return true;
    }
    // S - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      _resultScroll++;
      return true;
    }
    // Shift+Del - back to build screen (keep path)
    if (c == KEY_CANCEL) {
      _state = STATE_BUILD;
      _menuSel = 0;
      return true;
    }
    // Enter - new trace (re-run with same path)
    if (c == '\r' || c == 13) {
      return sendTrace();
    }
    return false;
  }

  // --- Send trace ---
  bool sendTrace() {
    if (_hopCount <= 0) return true;

    // Generate random tag and auth code
    the_mesh.getRNG()->random((uint8_t*)&_traceTag, 4);
    the_mesh.getRNG()->random((uint8_t*)&_traceAuth, 4);

    // flags: lower 2 bits = path_sz
    // path_sz 0 = 1-byte hashes, path_sz 1 = 2-byte hashes
    uint8_t pathSz = (_bytesPerHop == 2) ? 1 : 0;
    uint8_t flags = pathSz;

    mesh::Packet* pkt = the_mesh.createTrace(_traceTag, _traceAuth, flags);
    if (!pkt) {
      Serial.println("[Trace] Failed to create trace packet (pool empty)");
      return true;
    }

    // Path bytes to send
    uint8_t pathByteLen = _hopCount * _bytesPerHop;

    // sendDirect for TRACE appends path to payload and sets path_len=0
    the_mesh.sendDirect(pkt, _pathBuf, pathByteLen);

    _traceSentAt = millis();
    _state = STATE_RUNNING;
    memset(&_result, 0, sizeof(_result));

    Serial.printf("[Trace] Sent: tag=0x%08X, %d hops, %d-byte, %d path bytes\n",
                  _traceTag, _hopCount, _bytesPerHop, pathByteLen);
    Serial.printf("[Trace] Path hex:");
    for (int i = 0; i < pathByteLen; i++) {
      Serial.printf(" %02X", _pathBuf[i]);
    }
    Serial.println();
    return true;
  }
};