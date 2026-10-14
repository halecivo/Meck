#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#include <MeshCore.h>
#include <Packet.h>
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
#endif

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

class PathEditorScreen : public UIScreen {
public:
  enum EditorState {
    STATE_MAIN,
    STATE_PICK_HOP
  };

  // Main-state menu items (dynamic, built each render)
  enum MenuItem {
    MENU_MODE = 0,    // "Mode: 1B/hop" or "Mode: 2B/hop"
    // After mode: hop lines (MENU_HOP_BASE + i)
    // Then: action items
    MENU_HOP_BASE = 1,
    // Dynamic items after hops:
    MENU_ADD_HOP = 100,
    MENU_SET_DIRECT,
    MENU_REMOVE_LAST,
    MENU_CLEAR_PATH,
    MENU_SAVE_EXIT
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;

  int _contactIdx;             // Index into contact table
  char _contactName[32];       // Contact name for header

  EditorState _state;
  int _menuSel;                // Selected menu item index (0-based in visible list)
  int _menuCount;              // Total visible menu items

  // Path being edited (working copy)
  uint8_t _pathBuf[MAX_PATH_SIZE];
  uint8_t _pathLen;            // Encoded: bits[7:6]=mode, bits[5:0]=hops
  int _hopCount;               // Decoded hop count
  int _bytesPerHop;            // 1 or 2

  // Repeater picker state
  static const int MAX_REPEATERS = 200;
  uint16_t* _repIdx;           // Indices into contact table (PSRAM)
  int _repCount;               // Number of repeaters found
  int _repSel;                 // Selected repeater in picker
  int _repScroll;              // Scroll offset in picker

  bool _dirty;                 // Path has been modified
  bool _wantExit;              // Set by Save & Exit — caller should navigate back
  bool _directLocked;          // True = path is explicitly set to direct (0 hops, locked)

  // --- helpers ---

  void decodePath() {
    _hopCount = _pathLen & 0x3F;
    uint8_t mode = (_pathLen >> 6) & 0x03;
    _bytesPerHop = mode + 1;
  }

  uint8_t encodePath() const {
    uint8_t mode = (_bytesPerHop - 1) & 0x03;
    return (mode << 6) | (_hopCount & 0x3F);
  }

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

  // Look up a contact name by matching pub_key prefix bytes
  bool findNameForHop(int hopIndex, char* name, size_t nameLen) const {
    if (hopIndex < 0 || hopIndex >= _hopCount) return false;
    int offset = hopIndex * _bytesPerHop;
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo c;
    for (uint32_t i = 0; i < numContacts; i++) {
      if (the_mesh.getContactByIdx(i, c)) {
        bool match = true;
        for (int b = 0; b < _bytesPerHop; b++) {
          if (c.id.pub_key[b] != _pathBuf[offset + b]) {
            match = false;
            break;
          }
        }
        if (match) {
          strncpy(name, c.name, nameLen);
          name[nameLen - 1] = '\0';
          return true;
        }
      }
    }
    return false;
  }

  // Build the visible menu items list and return count
  // Menu layout:
  //   0: Mode selector
  //   1..hopCount: each hop
  //   hopCount+1: Add hop
  //   hopCount+2: Remove last (only if hops > 0)
  //   hopCount+2 or +3: Clear path (only if custom path flag set or hops > 0)
  //   last: Save & Exit
  int buildMenuCount() const {
    int count = 1; // Mode selector
    count += _hopCount; // One per hop
    if (_hopCount < 8) count++; // Add hop (max 8 hops)
    count++; // Set Direct (always visible)
    if (_hopCount > 0) count++; // Remove last
    if (_hopCount > 0 || _directLocked || isCustomPathSet()) count++; // Clear path
    count++; // Save & Exit
    return count;
  }

  // Map a menu index to a MenuItem enum
  MenuItem menuItemAt(int idx) const {
    if (idx == 0) return MENU_MODE;
    int pos = 1;
    // Hop lines
    for (int h = 0; h < _hopCount; h++) {
      if (idx == pos) return (MenuItem)(MENU_HOP_BASE + h);
      pos++;
    }
    // Add hop
    if (_hopCount < 8) {
      if (idx == pos) return MENU_ADD_HOP;
      pos++;
    }
    // Set Direct
    if (idx == pos) return MENU_SET_DIRECT;
    pos++;
    // Remove last
    if (_hopCount > 0) {
      if (idx == pos) return MENU_REMOVE_LAST;
      pos++;
    }
    // Clear path
    if (_hopCount > 0 || _directLocked || isCustomPathSet()) {
      if (idx == pos) return MENU_CLEAR_PATH;
      pos++;
    }
    // Save & Exit
    return MENU_SAVE_EXIT;
  }

  bool isCustomPathSet() const {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(_contactIdx, c)) return false;
    return (c.flags & CONTACT_FLAG_CUSTOM_PATH) != 0;
  }

public:
  PathEditorScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _contactIdx(-1), _state(STATE_MAIN),
      _menuSel(0), _menuCount(1), _pathLen(0), _hopCount(0),
      _bytesPerHop(1), _repCount(0), _repSel(0), _repScroll(0),
      _dirty(false), _wantExit(false), _directLocked(false) {
    memset(_contactName, 0, sizeof(_contactName));
    memset(_pathBuf, 0, sizeof(_pathBuf));
  #if defined(ESP32) && defined(BOARD_HAS_PSRAM)
    _repIdx = (uint16_t*)ps_calloc(MAX_REPEATERS, sizeof(uint16_t));
  #else
    _repIdx = new uint16_t[MAX_REPEATERS]();
  #endif
  }

  void openForContact(int contactIdx) {
    _contactIdx = contactIdx;
    _state = STATE_MAIN;
    _menuSel = 0;
    _repSel = 0;
    _repScroll = 0;
    _dirty = false;
    _wantExit = false;
    _directLocked = false;

    // Load contact info
    ContactInfo c;
    if (the_mesh.getContactByIdx(contactIdx, c)) {
      strncpy(_contactName, c.name, sizeof(_contactName) - 1);
      _contactName[sizeof(_contactName) - 1] = '\0';

      // Copy current path
      if (c.out_path_len != OUT_PATH_UNKNOWN) {
        _pathLen = c.out_path_len;
        decodePath();
        int byteLen = _hopCount * _bytesPerHop;
        if (byteLen > MAX_PATH_SIZE) byteLen = MAX_PATH_SIZE;
        memcpy(_pathBuf, c.out_path, byteLen);
        // Detect existing direct-locked path
        if (_hopCount == 0 && (c.flags & CONTACT_FLAG_CUSTOM_PATH)) {
          _directLocked = true;
        }
      } else {
        _pathLen = 0;
        _hopCount = 0;
        _bytesPerHop = 1;
        memset(_pathBuf, 0, sizeof(_pathBuf));
      }
    } else {
      strcpy(_contactName, MECK_TR("Unknown", "Inconnu"));
      _pathLen = 0;
      _hopCount = 0;
      _bytesPerHop = 1;
    }

    _menuCount = buildMenuCount();
  }

  int render(DisplayDriver& display) override {
#ifdef MECK_UI_V2
    return (_state == STATE_PICK_HOP) ? renderPickerV2(display) : renderMainV2(display);
#endif
    if (_state == STATE_PICK_HOP) {
      return renderPicker(display);
    }
    return renderMain(display);
  }

#ifdef MECK_UI_V2
  // Hash of hop `h` as hex ("A3" or "A3F0")
  void hopHex(int h, char* out, size_t len) const {
    int o = h * _bytesPerHop;
    if (_bytesPerHop == 1) snprintf(out, len, "%02X", _pathBuf[o]);
    else snprintf(out, len, "%02X%02X", _pathBuf[o], _pathBuf[o + 1]);
  }

  // UI v2: the path as list rows. The title-bar chip shows unsaved changes
  // or a saved custom path.
  int renderMainV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    _menuCount = buildMenuCount();
    if (_dirty) Ui2Chrome::titleBar(c, _contactName, nullptr, MECK_TR("Edited", "Modifi\xC3\xA9"), UI2_IC_EDIT, true);
    else if (isCustomPathSet()) Ui2Chrome::titleBar(c, _contactName, nullptr, MECK_TR("Custom", "Perso"), UI2_IC_LOCK);
    else Ui2Chrome::titleBar(c, _contactName);

    const int start = Ui2List::pageStart(_menuSel, ROWS_PER_PAGE);
    for (int i = start; i < _menuCount && i < start + ROWS_PER_PAGE; i++) {
      MenuItem item = menuItemAt(i);
      Ui2Row r;
      char val[16], sub[32], name[33];
      switch (item) {
        case MENU_MODE:
          r.icon = UI2_IC_HASH;
          r.title = MECK_TR("Hash size", "Taille du hash");
          if (_directLocked) {
            r.right = MECK_TR("Direct", "Direct");
            r.sub = MECK_TR("No repeaters in between", "Sans r\xC3\xA9p\xC3\xA9teur");
          } else {
            snprintf(val, sizeof(val), MECK_TR("%d-byte", "%d octet%s"), _bytesPerHop, _bytesPerHop > 1 ? "s" : "");
            r.right = val;
            r.sub = MECK_TR("A/D to change", "A/D pour changer");
          }
          break;
        case MENU_ADD_HOP:
          r.icon = UI2_IC_PLUS;
          r.title = MECK_TR("Add hop", "Ajouter un saut");
          r.chevron = true;
          break;
        case MENU_SET_DIRECT:
          r.icon = UI2_IC_SEND;
          r.title = MECK_TR("Direct route", "Route directe");
          r.sub = _directLocked ? MECK_TR("Set", "D\xC3\xA9" "finie") : MECK_TR("No repeaters in between", "Sans r\xC3\xA9p\xC3\xA9teur");
          r.check = _directLocked;
          break;
        case MENU_REMOVE_LAST:
          r.icon = UI2_IC_MINUS;
          r.title = MECK_TR("Remove last hop", "Retirer le dernier saut");
          break;
        case MENU_CLEAR_PATH:
          r.icon = UI2_IC_DEL;
          r.title = MECK_TR("Clear custom path", "Effacer le chemin");
          r.sub = MECK_TR("Back to automatic routing", "Retour au routage auto");
          break;
        case MENU_SAVE_EXIT:
          r.icon = UI2_IC_CHECK;
          r.title = MECK_TR("Save and exit", "Enregistrer et quitter");
          break;
        default: {
          int h = item - MENU_HOP_BASE;
          r.icon = UI2_IC_REPEATER;
          hopHex(h, val, sizeof(val));
          r.right = val;
          snprintf(sub, sizeof(sub), MECK_TR("Hop %d", "Saut %d"), h + 1);
          if (findNameForHop(h, name, sizeof(name))) { r.title = name; r.sub = sub; }
          else { r.title = sub; r.sub = MECK_TR("Not in contacts", "Pas dans les contacts"); }
          break;
        }
      }
      r.selected = (i == _menuSel);
      Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Select", "Choisir")),
      ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("Q", _dirty ? MECK_TR("Discard", "Abandonner") : MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 3);
    return 5000;
  }

  int renderPickerV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    char right[16];
    Ui2List::pageLabel(right, sizeof(right), _repSel, _repCount, ROWS_PER_PAGE);
    Ui2Chrome::titleBar(c, MECK_TR("Add hop", "Ajouter un saut"), right);
    if (_repCount == 0) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_REPEATER, MECK_TR("No repeaters in contacts", "Aucun r\xC3\xA9p\xC3\xA9teur connu"),
                          MECK_TR("Repeaters appear here once they are contacts.", "Ajoutez d'abord des r\xC3\xA9p\xC3\xA9teurs."));
      const Ui2Key keys[] = { ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 1);
      return 5000;
    }
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

  // Touch hit test in physical pixels: 0 = miss, 1 = moved, 2 = tapped current row
  int selectRowAtPx(int py) {
    int row = Ui2List::rowAt(py, ui2::BODY_Y, ui2::ROW_H, ui2::ROWS_PER_PAGE);
    if (row < 0) return 0;
    int& sel = (_state == STATE_PICK_HOP) ? _repSel : _menuSel;
    int count = (_state == STATE_PICK_HOP) ? _repCount : _menuCount;
    int idx = Ui2List::pageStart(sel, ui2::ROWS_PER_PAGE) + row;
    if (idx >= count) return 0;
    if (idx == sel) return 2;
    sel = idx;
    return 1;
  }
#endif

  int renderMain(DisplayDriver& display) {
    char tmp[64];

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    snprintf(tmp, sizeof(tmp), MECK_TR("Path: %s", "Chemin : %s"), _contactName);
    // Truncate if too long
    if (display.getTextWidth(tmp) > display.width() - 4) {
      snprintf(tmp, sizeof(tmp), MECK_TR("Path: %.12s..", "Chemin : %.12s.."), _contactName);
    }
    display.print(tmp);

    // Show lock icon or dirty indicator on right
    if (_dirty) {
      const char* mod = "[*]";
      display.setCursor(display.width() - display.getTextWidth(mod) - 2, 0);
      display.print(mod);
    } else if (isCustomPathSet()) {
      const char* lock = "[L]";
      display.setCursor(display.width() - display.getTextWidth(lock) - 2, 0);
      display.print(lock);
    }

    display.drawRect(0, 11, display.width(), 1);

    // === Body ===
    display.setTextSize(0);
    int lineH = 9;
    int headerH = 14;
    int footerH = 14;
    int maxY = display.height() - footerH;
    int y = headerH;

    _menuCount = buildMenuCount();

    // Center visible window around selected item
    int maxVisible = (maxY - headerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_menuSel - maxVisible / 2, _menuCount - maxVisible));
    if (startIdx < 0) startIdx = 0;
    int endIdx = min(_menuCount, startIdx + maxVisible);

    for (int i = startIdx; i < endIdx && y + lineH <= maxY; i++) {
      bool selected = (i == _menuSel);
      MenuItem item = menuItemAt(i);

      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        // Size-0 text: the built-in Classic font draws below the cursor
        // (offset 5); the Noto/Montserrat 7pt fonts draw up from a baseline
        // (offset 0). Same values as NodePrefs::smallHighlightOff() gives at the
        // tiny size; this screen's list text is always size 0.
        display.fillRect(0, y + (display.getFontStyle() > 0 ? 0 : 5), display.width(), lineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      display.setCursor(2, y);
      char prefix = selected ? '>' : ' ';

      switch (item) {
        case MENU_MODE:
          if (_directLocked) {
            snprintf(tmp, sizeof(tmp), MECK_TR("%c Mode: DIRECT", "%c Mode : DIRECT"), prefix);
          } else {
            snprintf(tmp, sizeof(tmp), MECK_TR("%c Mode: %dB/hop", "%c Mode : %d o/saut"), prefix, _bytesPerHop);
          }
          display.print(tmp);
          // Show hint on right
          if (!_directLocked) {
            const char* hint = "(A/D)";
            display.setCursor(display.width() - display.getTextWidth(hint) - 4, y);
            display.print(hint);
          }
          break;

        case MENU_ADD_HOP:
          snprintf(tmp, sizeof(tmp), MECK_TR("%c + Add hop...", "%c + Ajouter un saut..."), prefix);
          display.print(tmp);
          break;

        case MENU_SET_DIRECT:
          if (_directLocked) {
            snprintf(tmp, sizeof(tmp), MECK_TR("%c * Direct (set)", "%c * Direct (d\xC3\xA9" "fini)"), prefix);
          } else {
            snprintf(tmp, sizeof(tmp), MECK_TR("%c * Set Direct", "%c * Passer en direct"), prefix);
          }
          display.print(tmp);
          break;

        case MENU_REMOVE_LAST:
          snprintf(tmp, sizeof(tmp), MECK_TR("%c - Remove last hop", "%c - Retirer dernier saut"), prefix);
          display.print(tmp);
          break;

        case MENU_CLEAR_PATH:
          snprintf(tmp, sizeof(tmp), MECK_TR("%c   Clear custom path", "%c   Effacer le chemin"), prefix);
          display.print(tmp);
          break;

        case MENU_SAVE_EXIT:
          snprintf(tmp, sizeof(tmp), MECK_TR("%c   Save & Exit", "%c   Enregistrer et quitter"), prefix);
          display.print(tmp);
          break;

        default:
          // Hop line: MENU_HOP_BASE + hopIndex
          if (item >= MENU_HOP_BASE && item < MENU_HOP_BASE + 64) {
            int hopIdx = item - MENU_HOP_BASE;
            char hopName[24];
            int offset = hopIdx * _bytesPerHop;

            if (findNameForHop(hopIdx, hopName, sizeof(hopName))) {
              if (_bytesPerHop == 1) {
                snprintf(tmp, sizeof(tmp), "%c %d: %s (%02X)", prefix, hopIdx + 1,
                         hopName, _pathBuf[offset]);
              } else {
                snprintf(tmp, sizeof(tmp), "%c %d: %s (%02X%02X)", prefix, hopIdx + 1,
                         hopName, _pathBuf[offset], _pathBuf[offset + 1]);
              }
            } else {
              if (_bytesPerHop == 1) {
                snprintf(tmp, sizeof(tmp), "%c %d: ??? (%02X)", prefix, hopIdx + 1,
                         _pathBuf[offset]);
              } else {
                snprintf(tmp, sizeof(tmp), "%c %d: ??? (%02X%02X)", prefix, hopIdx + 1,
                         _pathBuf[offset], _pathBuf[offset + 1]);
              }
            }
            display.drawTextEllipsized(2, y, display.width() - 4, tmp);
          }
          break;
      }

      y += lineH;
    }

    // === Footer ===
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

    display.setCursor(0, footerY);
    display.print(MECK_TR("Q:Bk W/S:Nav", "Q:Ret W/S:Nav"));
    const char* right = MECK_TR("Enter:Sel", "Entr\xC3\xA9" "e:S\xC3\xA9l");
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);

    return 5000;
  }

  int renderPicker(DisplayDriver& display) {
    char tmp[64];

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    snprintf(tmp, sizeof(tmp), MECK_TR("Select Repeater (%d)", "Choisir r\xC3\xA9p\xC3\xA9teur (%d)"), _repCount);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);

    // === Body ===
    display.setTextSize(0);
    int lineH = 9;
    int headerH = 14;
    int footerH = 14;
    int maxY = display.height() - footerH;
    int y = headerH;

    if (_repCount == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      display.print(MECK_TR("No repeaters in contacts", "Aucun r\xC3\xA9p\xC3\xA9teur connu"));
      display.setCursor(0, y + lineH);
      display.print(MECK_TR("Add repeaters first", "Ajoutez des r\xC3\xA9p\xC3\xA9teurs"));
    } else {
      int maxVisible = (maxY - headerH) / lineH;
      if (maxVisible < 3) maxVisible = 3;
      int startIdx = max(0, min(_repSel - maxVisible / 2, _repCount - maxVisible));
      if (startIdx < 0) startIdx = 0;
      int endIdx = min(_repCount, startIdx + maxVisible);

      for (int i = startIdx; i < endIdx && y + lineH <= maxY; i++) {
        ContactInfo c;
        if (!the_mesh.getContactByIdx(_repIdx[i], c)) continue;

        bool selected = (i == _repSel);

        if (selected) {
          display.setColor(DisplayDriver::LIGHT);
          // Size-0 text: the built-in Classic font draws below the cursor
          // (offset 5); the Noto/Montserrat 7pt fonts draw up from a baseline
          // (offset 0). Same values as NodePrefs::smallHighlightOff() gives at the
          // tiny size; this screen's list text is always size 0.
          display.fillRect(0, y + (display.getFontStyle() > 0 ? 0 : 5), display.width(), lineH);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        display.setCursor(2, y);
        char prefix = selected ? '>' : ' ';

        if (_bytesPerHop == 1) {
          snprintf(tmp, sizeof(tmp), "%c %s (%02X)", prefix, c.name, c.id.pub_key[0]);
        } else {
          snprintf(tmp, sizeof(tmp), "%c %s (%02X%02X)", prefix, c.name,
                   c.id.pub_key[0], c.id.pub_key[1]);
        }
        display.drawTextEllipsized(2, y, display.width() - 4, tmp);

        y += lineH;
      }
    }

    // === Footer ===
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

    display.setCursor(0, footerY);
    display.print(MECK_TR("Q:Cancel W/S:Scroll", "Q:Ret W/S:D\xC3\xA9" "fil"));
    const char* right = MECK_TR("Enter:Add", "Ent:Ajouter");
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);

    return 5000;
  }


  bool handleInput(char c) override {
    if (_state == STATE_PICK_HOP) {
      return handlePickerInput(c);
    }
    return handleMainInput(c);
  }

  bool handleMainInput(char c) {
    // W - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_menuSel > 0) {
        _menuSel--;
        return true;
      }
      return false;
    }

    // S - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_menuSel < _menuCount - 1) {
        _menuSel++;
        return true;
      }
      return false;
    }

    // A/D — toggle mode (only when Mode item is selected and not direct-locked)
    if (c == 'a' || c == 'A' || c == 'd' || c == 'D') {
      MenuItem item = menuItemAt(_menuSel);
      if (item == MENU_MODE && !_directLocked) {
        // Toggle between 1-byte and 2-byte
        if (_bytesPerHop == 1) {
          switchMode(2);
        } else {
          switchMode(1);
        }
        _dirty = true;
        _menuCount = buildMenuCount();
        return true;
      }
      return false;
    }

    // Enter - select
    if (c == 13 || c == KEY_ENTER || c == '\r') {
      MenuItem item = menuItemAt(_menuSel);

      switch (item) {
        case MENU_MODE:
          // Toggle mode on Enter too (no-op if direct locked)
          if (!_directLocked) {
            if (_bytesPerHop == 1) {
              switchMode(2);
            } else {
              switchMode(1);
            }
            _dirty = true;
            _menuCount = buildMenuCount();
          }
          return true;

        case MENU_ADD_HOP:
          _directLocked = false;
          // Enter picker mode -- adding a hop clears direct lock
          buildRepeaterList();
          _repSel = 0;
          _repScroll = 0;
          _state = STATE_PICK_HOP;
          return true;

        case MENU_SET_DIRECT:
          // Set path to direct (0 hops, locked)
          _hopCount = 0;
          _pathLen = 0;
          memset(_pathBuf, 0, sizeof(_pathBuf));
          _directLocked = true;
          _dirty = true;
          _menuCount = buildMenuCount();
          return true;

        case MENU_REMOVE_LAST:
          if (_hopCount > 0) {
            _hopCount--;
            _pathLen = encodePath();
            _dirty = true;
            _menuCount = buildMenuCount();
            // Clamp selection
            if (_menuSel >= _menuCount) _menuSel = _menuCount - 1;
          }
          return true;

        case MENU_CLEAR_PATH:
          _hopCount = 0;
          _pathLen = 0;
          _directLocked = false;
          memset(_pathBuf, 0, sizeof(_pathBuf));
          _dirty = true;
          _menuCount = buildMenuCount();
          _menuSel = 0;
          return true;

        case MENU_SAVE_EXIT:
          savePath();
          _wantExit = true;  // Signal to main.cpp to navigate back to contacts
          return true;

        default:
          // Hop line — no action (could add remove-specific-hop later)
          break;
      }
      return true;
    }

    // Shift+Del - back (discard changes or prompt?)
    // For simplicity, just go back without saving
    if (c == KEY_CANCEL) {
      // Return to contacts screen without saving
      // The UITask will handle this via the key falling through
      return false;  // Let UITask handle Q as back
    }

    return false;
  }

  bool handlePickerInput(char c) {
    // W - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_repSel > 0) {
        _repSel--;
        return true;
      }
      return false;
    }

    // S - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_repSel < _repCount - 1) {
        _repSel++;
        return true;
      }
      return false;
    }

    // Enter - add selected repeater as hop
    if (c == 13 || c == KEY_ENTER || c == '\r') {
      if (_repCount > 0 && _repSel >= 0 && _repSel < _repCount) {
        addHopFromContact(_repIdx[_repSel]);
      }
      _state = STATE_MAIN;
      _menuCount = buildMenuCount();
      return true;
    }

    // Shift+Del - cancel picker, return to main
    if (c == KEY_CANCEL) {
      _state = STATE_MAIN;
      return true;
    }

    return false;
  }

  // Tap-to-select for touch
  int selectRowAtVY(int vy) {
    if (_state == STATE_PICK_HOP) {
      return selectPickerRowAtVY(vy);
    }
    return selectMainRowAtVY(vy);
  }

  int selectMainRowAtVY(int vy) {
    if (_menuCount == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = 9;
    const int bodyTop = headerH + 5;
    if (vy < bodyTop || vy >= 128 - footerH) return 0;

    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_menuSel - maxVisible / 2, _menuCount - maxVisible));
    if (startIdx < 0) startIdx = 0;

    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _menuCount) return 0;
    if (tappedRow == _menuSel) return 2;
    _menuSel = tappedRow;
    return 1;
  }

  int selectPickerRowAtVY(int vy) {
    if (_repCount == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = 9;
    const int bodyTop = headerH + 5;
    if (vy < bodyTop || vy >= 128 - footerH) return 0;

    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_repSel - maxVisible / 2, _repCount - maxVisible));
    if (startIdx < 0) startIdx = 0;

    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _repCount) return 0;
    if (tappedRow == _repSel) return 2;
    _repSel = tappedRow;
    return 1;
  }

  EditorState getState() const { return _state; }
  bool isDirty() const { return _dirty; }
  bool wantsExit() const { return _wantExit; }

private:
  void switchMode(int newBytesPerHop) {
    if (newBytesPerHop == _bytesPerHop) return;

    if (_hopCount > 0) {
      // Rebuild path buffer for new mode
      // We need the full pub_keys to re-extract the right prefix bytes
      uint8_t newBuf[MAX_PATH_SIZE];
      memset(newBuf, 0, sizeof(newBuf));
      int newHopCount = 0;

      for (int h = 0; h < _hopCount && newHopCount < 8; h++) {
        int oldOffset = h * _bytesPerHop;
        // Try to find the contact that matches this hop
        uint32_t numContacts = the_mesh.getNumContacts();
        ContactInfo c;
        bool found = false;
        for (uint32_t i = 0; i < numContacts; i++) {
          if (the_mesh.getContactByIdx(i, c)) {
            bool match = true;
            for (int b = 0; b < _bytesPerHop; b++) {
              if (c.id.pub_key[b] != _pathBuf[oldOffset + b]) {
                match = false;
                break;
              }
            }
            if (match) {
              // Found the contact — copy new prefix size
              int newOffset = newHopCount * newBytesPerHop;
              for (int b = 0; b < newBytesPerHop; b++) {
                newBuf[newOffset + b] = c.id.pub_key[b];
              }
              newHopCount++;
              found = true;
              break;
            }
          }
        }
        if (!found) {
          // Contact not found — copy what we can
          int newOffset = newHopCount * newBytesPerHop;
          int oldOff = h * _bytesPerHop;
          for (int b = 0; b < newBytesPerHop; b++) {
            if (b < _bytesPerHop) {
              newBuf[newOffset + b] = _pathBuf[oldOff + b];
            } else {
              newBuf[newOffset + b] = 0x00; // pad with zero
            }
          }
          newHopCount++;
        }
      }

      _hopCount = newHopCount;
      memcpy(_pathBuf, newBuf, sizeof(newBuf));
    }

    _bytesPerHop = newBytesPerHop;
    _pathLen = encodePath();
  }

  void addHopFromContact(uint16_t contactTableIdx) {
    if (_hopCount >= 8) return;
    ContactInfo c;
    if (!the_mesh.getContactByIdx(contactTableIdx, c)) return;

    int offset = _hopCount * _bytesPerHop;
    if (offset + _bytesPerHop > MAX_PATH_SIZE) return;

    for (int b = 0; b < _bytesPerHop; b++) {
      _pathBuf[offset + b] = c.id.pub_key[b];
    }
    _hopCount++;
    _pathLen = encodePath();
    _dirty = true;
  }

  void savePath() {
    if (_contactIdx < 0) return;

    if (_directLocked) {
      // Set as direct (0 hops) with lock — prevents flood routing
      the_mesh.setCustomPath(_contactIdx, _pathBuf, 0, true);
      Serial.printf("PathEditor: set DIRECT path for contact %d (%s)\n",
                     _contactIdx, _contactName);
    } else if (_hopCount > 0) {
      // Set custom path with lock
      the_mesh.setCustomPath(_contactIdx, _pathBuf, encodePath(), true);
      Serial.printf("PathEditor: saved %d-hop %dB/hop path for contact %d (%s)\n",
                     _hopCount, _bytesPerHop, _contactIdx, _contactName);
    } else {
      // Clear custom path — revert to auto-discovery
      the_mesh.clearCustomPath(_contactIdx);
      Serial.printf("PathEditor: cleared custom path for contact %d (%s)\n",
                     _contactIdx, _contactName);
    }

    // Trigger contact save to SD
    the_mesh.saveContacts();
    _dirty = false;
  }
};