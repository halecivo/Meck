#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#include <MeshCore.h>
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2Mesh.h"
#endif


// Timestamps before this (Jan 1 2026 UTC) are treated as invalid/unsynced
#define EPOCH_2026  1735689600UL

// Forward declarations — MyMesh.h (which defines AdvertPath) is always
// included by the translation unit before this header.
class UITask;
class MyMesh;
extern MyMesh the_mesh;

class ContactsScreen : public UIScreen {
public:
  // Filter modes for contact type
  enum FilterMode {
    FILTER_ALL = 0,
    FILTER_CHAT,       // Companions / Chat nodes
    FILTER_REPEATER,
    FILTER_ROOM,       // Room servers
    FILTER_SENSOR,
    FILTER_FAVOURITE,  // Contacts marked as favourite (any type)
    FILTER_COUNT       // keep last
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;

  int _scrollPos;        // Index into filtered list (top visible row)
  FilterMode _filter;    // Current filter mode

  // Cached filtered contact indices for efficient scrolling
  // We rebuild this on filter change or when entering the screen
  // Arrays allocated in PSRAM when available (supports 1000+ contacts)
  uint16_t* _filteredIdx;    // indices into contact table
  uint32_t* _filteredTs;     // cached lastmod for sorting
  int _filteredCount;                  // how many contacts match current filter
  AdvertPath _hopBuf[40];    // recently heard advert paths for hop-count display
  int _hopBufCount;
  bool _cacheValid;

  // How many rows fit on screen (computed during render)
  int _rowsPerPage;

  // Pointer to per-contact DM unread array (owned by UITask, set via setter)
  const uint8_t* _dmUnread = nullptr;

  // --- Select mode state ---
  bool _selectMode;
  uint8_t* _selectedBits;   // Bitfield: 1 bit per MAX_CONTACTS raw index


  // --- helpers ---

  static const char* filterLabel(FilterMode f) {
    switch (f) {
      case FILTER_ALL:       return MECK_TR("All", "Tous");
      case FILTER_CHAT:      return "Chat";
      case FILTER_REPEATER:  return MECK_TR("Rptr", "R\xC3\xA9p.");
      case FILTER_ROOM:      return MECK_TR("Room", "Salon");
      case FILTER_SENSOR:    return MECK_TR("Sens", "Capt.");
      case FILTER_FAVOURITE: return "Fav";
      default:               return "?";
    }
  }

  static const char* typeStr(uint8_t adv_type) {
    switch (adv_type) {
      case ADV_TYPE_CHAT:     return "C";
      case ADV_TYPE_REPEATER: return "R";
      case ADV_TYPE_ROOM:     return "RS";
      default:                return "?";
    }
  }

  bool matchesFilter(uint8_t adv_type, uint8_t flags = 0) const {
    switch (_filter) {
      case FILTER_ALL:       return true;
      case FILTER_CHAT:      return adv_type == ADV_TYPE_CHAT;
      case FILTER_REPEATER:  return adv_type == ADV_TYPE_REPEATER;
      case FILTER_ROOM:      return adv_type == ADV_TYPE_ROOM;
      case FILTER_SENSOR:    return (adv_type != ADV_TYPE_CHAT &&
                                     adv_type != ADV_TYPE_REPEATER &&
                                     adv_type != ADV_TYPE_ROOM);
      case FILTER_FAVOURITE: return (flags & 0x01) != 0;
      default:               return true;
    }
  }

  void rebuildCache() {
    _filteredCount = 0;
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo contact;
    for (uint32_t i = 0; i < numContacts && _filteredCount < MAX_CONTACTS; i++) {
      if (the_mesh.getContactByIdx(i, contact)) {
        if (matchesFilter(contact.type, contact.flags)) {
          _filteredIdx[_filteredCount] = (uint16_t)i;
          // Use lastmod (our receive time) for sort/age; pre-2026 or zero → 0 sinks to bottom
          _filteredTs[_filteredCount] = (contact.lastmod >= EPOCH_2026) ? contact.lastmod : 0;
          _filteredCount++;
        }
      }
    }
    // Sort by lastmod descending (most recently heard first; pre-2026/unsynced sink to bottom)
    // Insertion sort — fine for up to ~1000 entries on ESP32
    for (int i = 1; i < _filteredCount; i++) {
      uint16_t tmpIdx = _filteredIdx[i];
      uint32_t tmpTs  = _filteredTs[i];
      int j = i - 1;
      while (j >= 0 && _filteredTs[j] < tmpTs) {
        _filteredIdx[j + 1] = _filteredIdx[j];
        _filteredTs[j + 1]  = _filteredTs[j];
        j--;
      }
      _filteredIdx[j + 1] = tmpIdx;
      _filteredTs[j + 1]  = tmpTs;
    }
    _cacheValid = true;
    // Refresh hop-count cache from the 12 most recently heard adverts
    _hopBufCount = the_mesh.getRecentlyHeard(_hopBuf, 40);
    // Clamp scroll position
    if (_scrollPos >= _filteredCount) {
      _scrollPos = (_filteredCount > 0) ? _filteredCount - 1 : 0;
    }
  }

  // Format seconds-ago as compact string: "3s" "5m" "2h" "4d" "--"
  static void formatAge(char* buf, size_t bufLen, uint32_t now, uint32_t timestamp) {
    if (timestamp == 0 || timestamp < EPOCH_2026 || now < timestamp) {
      strncpy(buf, "--", bufLen);
      return;
    }
    uint32_t secs = now - timestamp;
    if (secs < 60) {
      snprintf(buf, bufLen, "%ds", (int)secs);
    } else if (secs < 3600) {
      snprintf(buf, bufLen, MECK_TR("%dm", "%dmin"), (int)(secs / 60));
    } else if (secs < 86400) {
      snprintf(buf, bufLen, "%dh", (int)(secs / 3600));
    } else {
      snprintf(buf, bufLen, MECK_TR("%dd", "%dj"), (int)(secs / 86400));
    }
  }

  // Hop count shown for a contact: "D" direct, "3" hops on the known path,
  // "~3" / "~D" flood estimate from the recent advert cache, "?" unknown;
  // a trailing "*" marks a custom (locked) path.
  void hopString(const ContactInfo& contact, char* hopStr, size_t len) const {
    if (contact.out_path_len == 0xFF) {
      // No confirmed direct path — look up flood hop estimate from recent advert cache
      snprintf(hopStr, len, "?");  // default
      for (int h = 0; h < _hopBufCount; h++) {
        if (memcmp(contact.id.pub_key, _hopBuf[h].pubkey_prefix, 7) == 0) {
          uint8_t bph = (_hopBuf[h].path_len >> 6) + 1;
          uint8_t hops = _hopBuf[h].path_len & 0x3F;
          uint8_t max_hops = 64 / bph;  // sanity cap based on path encoding
          if (hops <= max_hops) {
            if (hops == 0)
              snprintf(hopStr, len, "~D");
            else
              snprintf(hopStr, len, "~%d", (int)hops);
          }
          break;
        }
      }
    } else if (contact.out_path_len == 0) {
      bool customDirect = (contact.flags & CONTACT_FLAG_CUSTOM_PATH) != 0;
      snprintf(hopStr, len, "%s", customDirect ? "D*" : "D");
    } else {
      int hops = contact.out_path_len & 0x3F;  // lower 6 bits = hop count
      bool customPath = (contact.flags & CONTACT_FLAG_CUSTOM_PATH) != 0;
      if (customPath) {
        snprintf(hopStr, len, "%d*", hops);  // asterisk = custom/locked path
      } else {
        snprintf(hopStr, len, "%d", hops);
      }
    }
  }

  // --- Bitfield helpers ---
  bool isSelectedRaw(int rawIdx) const {
    if (rawIdx < 0 || rawIdx >= MAX_CONTACTS) return false;
    return (_selectedBits[rawIdx / 8] & (1 << (rawIdx % 8))) != 0;
  }
  void setSelectedRaw(int rawIdx, bool sel) {
    if (rawIdx < 0 || rawIdx >= MAX_CONTACTS) return;
    if (sel) _selectedBits[rawIdx / 8] |= (1 << (rawIdx % 8));
    else     _selectedBits[rawIdx / 8] &= ~(1 << (rawIdx % 8));
  }

public:
  ContactsScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _scrollPos(0), _filter(FILTER_ALL),
      _filteredCount(0), _cacheValid(false), _rowsPerPage(5),
      _selectMode(false), _hopBufCount(0) {
  #if defined(ESP32) && defined(BOARD_HAS_PSRAM)
    _filteredIdx = (uint16_t*)ps_calloc(MAX_CONTACTS, sizeof(uint16_t));
    _filteredTs = (uint32_t*)ps_calloc(MAX_CONTACTS, sizeof(uint32_t));
    _selectedBits = (uint8_t*)ps_calloc((MAX_CONTACTS + 7) / 8, 1);
  #else
    _filteredIdx = new uint16_t[MAX_CONTACTS]();
    _filteredTs = new uint32_t[MAX_CONTACTS]();
    _selectedBits = new uint8_t[(MAX_CONTACTS + 7) / 8]();
  #endif
  }

  void invalidateCache() { _cacheValid = false; }

  // Set pointer to per-contact DM unread array (called by UITask after allocation)
  void setDMUnreadPtr(const uint8_t* ptr) { _dmUnread = ptr; }

  void resetScroll() {
    _scrollPos = 0;
    _cacheValid = false;
  }

  FilterMode getFilter() const { return _filter; }

  // --- Select mode API ---
  bool isInSelectMode() const { return _selectMode; }

  void enterSelectMode() {
    _selectMode = true;
    memset(_selectedBits, 0, (MAX_CONTACTS + 7) / 8);
    // Pre-select the currently highlighted contact
    if (_filteredCount > 0 && _scrollPos < _filteredCount) {
      setSelectedRaw(_filteredIdx[_scrollPos], true);
    }
  }

  void exitSelectMode() {
    _selectMode = false;
    memset(_selectedBits, 0, (MAX_CONTACTS + 7) / 8);
  }

  void toggleSelected() {
    if (_filteredCount == 0 || _scrollPos >= _filteredCount) return;
    int rawIdx = _filteredIdx[_scrollPos];
    setSelectedRaw(rawIdx, !isSelectedRaw(rawIdx));
  }

  void selectAll() {
    for (int i = 0; i < _filteredCount; i++) {
      setSelectedRaw(_filteredIdx[i], true);
    }
  }

  void deselectAll() {
    memset(_selectedBits, 0, (MAX_CONTACTS + 7) / 8);
  }

  int getSelectedCount() const {
    int count = 0;
    for (int i = 0; i < _filteredCount; i++) {
      if (isSelectedRaw(_filteredIdx[i])) count++;
    }
    return count;
  }

  // Fill outBuf with raw contact table indices of selected contacts
  int getSelectedRawIndices(uint16_t* outBuf, int maxOut) const {
    int count = 0;
    for (int i = 0; i < _filteredCount && count < maxOut; i++) {
      if (isSelectedRaw(_filteredIdx[i])) {
        outBuf[count++] = _filteredIdx[i];
      }
    }
    return count;
  }

  // Tap-to-select: given virtual Y, select contact row.
  // Returns: 0=miss, 1=moved, 2=tapped current row.
  int selectRowAtVY(int vy) {
    if (_filteredCount == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = the_mesh.getNodePrefs()->smallLineH();
    const int bodyTop = headerH + the_mesh.getNodePrefs()->smallHighlightOff();
    if (vy < bodyTop || vy >= 128 - footerH) return 0;

    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                              _filteredCount - maxVisible));

    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _filteredCount) return 0;

    if (tappedRow == _scrollPos) return 2;
    _scrollPos = tappedRow;
    return 1;
  }

  // Get the raw contact table index for the currently highlighted item
  // Returns -1 if no valid selection
  int getSelectedContactIdx() const {
    if (_filteredCount == 0) return -1;
    return _filteredIdx[_scrollPos];
  }

  // Get the adv_type of the currently highlighted contact
  // Returns 0xFF if no valid selection
  uint8_t getSelectedContactType() const {
    if (_filteredCount == 0) return 0xFF;
    ContactInfo contact;
    if (!the_mesh.getContactByIdx(_filteredIdx[_scrollPos], contact)) return 0xFF;
    return contact.type;
  }

  // Copy the name of the currently highlighted contact into buf
  // Returns false if no valid selection
  bool getSelectedContactName(char* buf, size_t bufLen) const {
    if (_filteredCount == 0) return false;
    ContactInfo contact;
    if (!the_mesh.getContactByIdx(_filteredIdx[_scrollPos], contact)) return false;
    strncpy(buf, contact.name, bufLen);
    buf[bufLen - 1] = '\0';
    return true;
  }


#ifdef MECK_UI_V2
  // UI v2 (design B7): icon by node type, name, type line, age and hops on
  // the right, star for favourites, DM unread badge; check marks in select
  // mode. Pages of 6 rows.
  int renderV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    char title[32], right[16];
    if (_selectMode) snprintf(title, sizeof(title), MECK_TR("%d selected", "%d s\xC3\xA9lectionn\xC3\xA9(s)"), getSelectedCount());
    else snprintf(title, sizeof(title), "Contacts");
    Ui2List::pageLabel(right, sizeof(right), _scrollPos, _filteredCount, ROWS_PER_PAGE);
    Ui2Chrome::titleBar(c, title, right, (_filter != FILTER_ALL) ? filterLabel(_filter) : nullptr);
    _rowsPerPage = ROWS_PER_PAGE;

    if (_filteredCount == 0) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_CONTACT, MECK_TR("No contacts", "Aucun contact"),
                          (_filter != FILTER_ALL) ? MECK_TR("A/D changes the filter.", "A/D change le filtre.") : nullptr);
    } else {
      const uint32_t now = _rtc->getCurrentTime();
      const int start = Ui2List::pageStart(_scrollPos, ROWS_PER_PAGE);
      for (int i = start; i < _filteredCount && i < start + ROWS_PER_PAGE; i++) {
        ContactInfo contact;
        if (!the_mesh.getContactByIdx(_filteredIdx[i], contact)) continue;
        char age[8], hops[20], badge[6], hopStr[6];
        Ui2Row r;
        r.icon = ui2NodeIcon(contact.type);
        r.title = contact.name;
        r.sub = ui2NodeTypeLabel(contact.type);
        formatAge(age, sizeof(age), now, contact.lastmod);
        r.right = age;
        hopString(contact, hopStr, sizeof(hopStr));
        const bool approx = hopStr[0] == '~';
        const char* h = approx ? hopStr + 1 : hopStr;
        if (*h == '?') {
          hops[0] = 0;
        } else {
          char lbl[16];
          ui2HopsLabel(lbl, sizeof(lbl), (*h == 'D') ? 0 : atoi(h));
          snprintf(hops, sizeof(hops), "%s%s%s", approx ? "~" : "", lbl, strchr(h, '*') ? " *" : "");
        }
        if (hops[0]) r.right2 = hops;
        int dm = (_dmUnread && _filteredIdx[i] < MAX_CONTACTS) ? _dmUnread[_filteredIdx[i]] : 0;
        if (dm > 0) { snprintf(badge, sizeof(badge), "%d", dm > 99 ? 99 : dm); r.badge = badge; }
        r.star = (contact.flags & 0x01) != 0;
        r.check = _selectMode && isSelectedRaw(_filteredIdx[i]);
        r.rightMax = 64;
        r.selected = (i == _scrollPos);
        Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
      }
    }

    if (_selectMode) {
      const Ui2Key keys[] = {
        ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Toggle", "Cocher")),
        ui2Key("A/D", MECK_TR("All/none", "Tout/rien")),
        ui2Key("X", MECK_TR("Export", "Exporter")),
        ui2Key("Q", MECK_TR("Done", "OK")),
      };
      Ui2Chrome::footer(c, keys, 4);
    } else {
      const Ui2Key keys[] = {
        ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Open", "Ouvrir")),
        ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
        ui2Key("A/D", MECK_TR("Filter", "Filtre")),
        ui2Key("P", MECK_TR("Path", "Chemin")),
      };
      Ui2Chrome::footer(c, keys, 4);
    }
    return 5000;
  }

  // Touch hit test in physical pixels: 0 = miss, 1 = moved, 2 = tapped current row
  int selectRowAtPx(int py) {
    if (_filteredCount == 0) return 0;
    int row = Ui2List::rowAt(py, ui2::BODY_Y, ui2::ROW_H, ui2::ROWS_PER_PAGE);
    if (row < 0) return 0;
    int idx = Ui2List::pageStart(_scrollPos, ui2::ROWS_PER_PAGE) + row;
    if (idx >= _filteredCount) return 0;
    if (idx == _scrollPos) return 2;
    _scrollPos = idx;
    return 1;
  }
#endif

  int render(DisplayDriver& display) override {
    if (!_cacheValid) rebuildCache();
#ifdef MECK_UI_V2
    return renderV2(display);
#endif

    char tmp[48];

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    if (_selectMode) {
      int selCount = getSelectedCount();
      snprintf(tmp, sizeof(tmp), MECK_TR("%d Selected [%s]", "S\xC3\xA9lection : %d [%s]"), selCount, filterLabel(_filter));
    } else {
      snprintf(tmp, sizeof(tmp), "Contacts [%s]", filterLabel(_filter));
    }
    display.print(tmp);

    // Count on right: All → total/max, filtered → matched/total
    if (_filter == FILTER_ALL) {
      snprintf(tmp, sizeof(tmp), "%d/%d", (int)the_mesh.getNumContacts(), MAX_CONTACTS);
    } else {
      snprintf(tmp, sizeof(tmp), "%d/%d", _filteredCount, (int)the_mesh.getNumContacts());
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    // Divider
    display.drawRect(0, 11, display.width(), 1);

    // === Body - contact rows ===
    display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());  // tiny font for compact rows
    int lineHeight = the_mesh.getNodePrefs()->smallLineH();      // 8px font + 1px gap
    int headerHeight = 14;
    int footerHeight = 14;
    int maxY = display.height() - footerHeight;
    int y = headerHeight;

    uint32_t now = _rtc->getCurrentTime();
    int rowsDrawn = 0;

    if (_filteredCount == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      display.print(MECK_TR("No contacts", "Aucun contact"));
      display.setCursor(0, y + lineHeight);
      display.print(MECK_TR("A/D: Change filter", "A/D : changer de filtre"));
    } else {
      // Center visible window around selected item (TextReaderScreen pattern)
      int maxVisible = (maxY - headerHeight) / lineHeight;
      if (maxVisible < 3) maxVisible = 3;
      int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                                _filteredCount - maxVisible));
      int endIdx = min(_filteredCount, startIdx + maxVisible);

      for (int i = startIdx; i < endIdx && y + lineHeight <= maxY; i++) {
        ContactInfo contact;
        if (!the_mesh.getContactByIdx(_filteredIdx[i], contact)) continue;

        bool selected = (i == _scrollPos);
        bool sel = _selectMode && isSelectedRaw(_filteredIdx[i]);

        // Highlight: fill LIGHT rect first, then draw DARK text on top
        if (selected) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y + the_mesh.getNodePrefs()->smallHighlightOff(), display.width(), lineHeight);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        // Set cursor AFTER fillRect so text draws on top of highlight
        display.setCursor(0, y);

        // Prefix: select mode uses * for selected, normal uses > for cursor
        char prefix[5];
        if (_selectMode) {
          snprintf(prefix, sizeof(prefix), "%c%s",
                   sel ? '*' : (selected ? '>' : ' '),
                   typeStr(contact.type));
        } else if (selected) {
          snprintf(prefix, sizeof(prefix), ">%s", typeStr(contact.type));
        } else {
          snprintf(prefix, sizeof(prefix), " %s", typeStr(contact.type));
        }
        display.print(prefix);

        // Contact name (truncated to fit)
        char filteredName[32];
        display.translateUTF8ToBlocks(filteredName, contact.name, sizeof(filteredName));

        // Reserve space for hops + age on right side
        char hopStr[6];
        hopString(contact, hopStr, sizeof(hopStr));

        char ageStr[6];
        formatAge(ageStr, sizeof(ageStr), now, contact.lastmod);

        // Build right-side string: "*N hops age" if unread, else "hops age"
        int dmCount = (_dmUnread && _filteredIdx[i] < MAX_CONTACTS) ? _dmUnread[_filteredIdx[i]] : 0;
        char rightStr[20];
        if (dmCount > 0) {
          snprintf(rightStr, sizeof(rightStr), "*%d %sh %s", dmCount, hopStr, ageStr);
        } else {
          snprintf(rightStr, sizeof(rightStr), "%sh %s", hopStr, ageStr);
        }
        int rightWidth = display.getTextWidth(rightStr) + 2;

        // Name region: after prefix + small gap, before right info
        int nameX = display.getTextWidth(prefix) + 2;
        int nameMaxW = display.width() - nameX - rightWidth - 2;
        display.drawTextEllipsized(nameX, y, nameMaxW, filteredName);

        // Right-aligned: hops + age
        display.setCursor(display.width() - rightWidth, y);
        display.print(rightStr);

        y += lineHeight;
        rowsDrawn++;
      }
      _rowsPerPage = (rowsDrawn > 0) ? rowsDrawn : 1;
    }

    display.setTextSize(1);  // restore for footer

    // === Footer ===
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

    display.setCursor(0, footerY);
    if (_selectMode) {
      display.print(MECK_TR("A:All D:Clr", "A:Tous D:Rien"));
      const char* right = MECK_TR("X:Exp F:Fav Q:Done", "X:Exp F:Fav Q:OK");
      display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
      display.print(right);
    } else {
      display.print(MECK_TR("A/D:Filter", "A/D:Filtre"));
      const char* right = MECK_TR("P:Path Ent:Sel", "P:Chemin Ent:S\xC3\xA9l");
      display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
      display.print(right);
    }


    return 5000;  // e-ink: next render after 5s
  }

  bool handleInput(char c) override {
    // Shift+W: page up
    if (c == 'W') {
      int pageSize = (128 - 14 - 14) / the_mesh.getNodePrefs()->smallLineH();
      if (pageSize < 3) pageSize = 3;
      _scrollPos = max(0, _scrollPos - pageSize);
      return true;
    }
    // W - scroll up (previous contact)
    if (c == 'w' || c == 0xF2) {
      if (_scrollPos > 0) {
        _scrollPos--;
        return true;
      }
    }

    // Shift+S: page down
    if (c == 'S') {
      int pageSize = (128 - 14 - 14) / the_mesh.getNodePrefs()->smallLineH();
      if (pageSize < 3) pageSize = 3;
      _scrollPos = min(_filteredCount - 1, _scrollPos + pageSize);
      return true;
    }
    // S - scroll down (next contact)
    if (c == 's' || c == 0xF1) {
      if (_scrollPos < _filteredCount - 1) {
        _scrollPos++;
        return true;
      }
    }

    // --- Select mode key handling ---
    if (_selectMode) {
      // Enter/tap: toggle selection on current contact
      if (c == 13 || c == KEY_ENTER) {
        toggleSelected();
        return true;
      }
      // A: select all in current filter
      if (c == 'a' || c == 'A') {
        selectAll();
        return true;
      }
      // D: deselect all
      if (c == 'd' || c == 'D') {
        deselectAll();
        return true;
      }
      // Q, X, F, Backspace — handled by main.cpp (needs mesh/SD access)
      return false;
    }

    // --- Normal mode key handling ---

    // A - previous filter
    if (c == 'a' || c == 'A') {
      _filter = (FilterMode)(((int)_filter + FILTER_COUNT - 1) % FILTER_COUNT);
      _scrollPos = 0;
      _cacheValid = false;
      return true;
    }

    // D - next filter
    if (c == 'd' || c == 'D') {
      _filter = (FilterMode)(((int)_filter + 1) % FILTER_COUNT);
      _scrollPos = 0;
      _cacheValid = false;
      return true;
    }

    // Enter - select contact (future: open RepeaterAdmin for repeaters)
    if (c == 13 || c == KEY_ENTER) {
      // TODO Phase 3: if selected contact is a repeater, open RepeaterAdminScreen
      // For now, just acknowledge the selection
      return true;
    }

    return false;
  }
};