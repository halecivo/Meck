#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#include <helpers/AdvertDataHelpers.h>
#include <MeshCore.h>
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2Mesh.h"
#endif

extern MyMesh the_mesh;

// ==========================================================================
// Last Heard Screen — passive advert list
// Shows all recently heard nodes from the advert path table, sorted by
// recency. Unlike Discovery (active zero-hop scan), this is purely passive
// — it shows nodes whose adverts have been received over time.
// ==========================================================================
// Display cap — we never need to show all 200 storage entries at once
#define LAST_HEARD_DISPLAY_SIZE 100

class LastHeardScreen : public UIScreen {
  mesh::RTCClock* _rtc;
  int _scrollPos;

  // Local sorted copy of advert paths (PSRAM-allocated, refreshed each render)
  AdvertPath* _entries;
  int _count;

  static char typeChar(uint8_t adv_type) {
    switch (adv_type) {
      case ADV_TYPE_CHAT:     return 'C';
      case ADV_TYPE_REPEATER: return 'R';
      case ADV_TYPE_ROOM:     return 'S';
      case ADV_TYPE_SENSOR:   return 'N';
      default:                return '?';
    }
  }

  // Format age as human-readable string (e.g. "2m", "1h", "3d")
  static void formatAge(uint32_t now, uint32_t timestamp, char* buf, int bufLen) {
    if (timestamp == 0 || now < timestamp) {
      snprintf(buf, bufLen, "---");
      return;
    }
    uint32_t age = now - timestamp;
    if (age < 60)         snprintf(buf, bufLen, "%ds", age);
    else if (age < 3600)  snprintf(buf, bufLen, MECK_TR("%dm", "%dmin"), age / 60);
    else if (age < 86400) snprintf(buf, bufLen, "%dh", age / 3600);
    else                  snprintf(buf, bufLen, MECK_TR("%dd", "%dj"), age / 86400);
  }

public:
  LastHeardScreen(mesh::RTCClock* rtc)
    : _rtc(rtc), _scrollPos(0), _count(0) {
    _entries = (AdvertPath*)ps_calloc(LAST_HEARD_DISPLAY_SIZE, sizeof(AdvertPath));
  }

  void resetScroll() { _scrollPos = 0; }

  int getSelectedIdx() const { return _scrollPos; }

  // Check if selected node is already in contacts
  bool isSelectedInContacts() const {
    if (_scrollPos < 0 || _scrollPos >= _count) return false;
    return the_mesh.lookupContactByPubKey(_entries[_scrollPos].pubkey_prefix, 8) != nullptr;
  }

  // Get selected entry (for add/delete operations)
  const AdvertPath* getSelectedEntry() const {
    if (_scrollPos < 0 || _scrollPos >= _count) return nullptr;
    return &_entries[_scrollPos];
  }

  // Tap-to-select: given virtual Y, select row.
  // Returns: 0=miss, 1=moved, 2=tapped current row.
  int selectRowAtVY(int vy) {
    if (_count == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = the_mesh.getNodePrefs()->smallLineH();
    const int bodyTop = headerH + the_mesh.getNodePrefs()->smallHighlightOff();
    if (vy < bodyTop || vy >= 128 - footerH) return 0;

    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                              _count - maxVisible));

    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _count) return 0;

    if (tappedRow == _scrollPos) return 2;
    _scrollPos = tappedRow;
    return 1;
  }

#ifdef MECK_UI_V2
  // UI v2: node rows with type, age and hops; a star for favourites and a
  // check for nodes already in contacts (Enter adds or removes).
  int renderV2(DisplayDriver& display, uint32_t now) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    char right[16];
    Ui2List::pageLabel(right, sizeof(right), _scrollPos, _count, ROWS_PER_PAGE);
    Ui2Chrome::titleBar(c, MECK_TR("Last heard", "Entendus"), right);

    if (_count == 0) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_RADIO, MECK_TR("No adverts received yet", "Aucune annonce re\xC3\xA7ue"),
                          MECK_TR("Nodes appear here as their adverts arrive.", "Les n\xC5\x93uds apparaissent ici d\xC3\xA8s leur annonce."));
    } else {
      const int start = Ui2List::pageStart(_scrollPos, ROWS_PER_PAGE);
      for (int i = start; i < _count && i < start + ROWS_PER_PAGE; i++) {
        const AdvertPath& e = _entries[i];
        char age[8], hops[16];
        Ui2Row r;
        r.icon = ui2NodeIcon(e.type);
        r.title = e.name;
        r.sub = ui2NodeTypeLabel(e.type);
        formatAge(now, e.recv_timestamp, age, sizeof(age));
        r.right = age;
        ui2HopsLabel(hops, sizeof(hops), e.path_len & 63);
        r.right2 = hops;
        ContactInfo* ci = the_mesh.lookupContactByPubKey(e.pubkey_prefix, 8);
        r.star = ci && (ci->flags & 0x01);
        r.check = ci && !r.star;
        r.rightMax = 64;
        r.selected = (i == _scrollPos);
        Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
      }
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Add/remove", "Ajout/suppr.")),
      ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 3);
    return 5000;
  }

  // Touch hit test in physical pixels: 0 = miss, 1 = moved, 2 = tapped current row
  int selectRowAtPx(int py) {
    if (_count == 0) return 0;
    int row = Ui2List::rowAt(py, ui2::BODY_Y, ui2::ROW_H, ui2::ROWS_PER_PAGE);
    if (row < 0) return 0;
    int idx = Ui2List::pageStart(_scrollPos, ui2::ROWS_PER_PAGE) + row;
    if (idx >= _count) return 0;
    if (idx == _scrollPos) return 2;
    _scrollPos = idx;
    return 1;
  }
#endif

  int render(DisplayDriver& display) override {
    // Refresh sorted list from mesh
    _count = the_mesh.getRecentlyHeard(_entries, LAST_HEARD_DISPLAY_SIZE);

    // Filter out empty entries (recv_timestamp == 0)
    int validCount = 0;
    for (int i = 0; i < _count; i++) {
      if (_entries[i].recv_timestamp > 0) validCount++;
      else break;  // sorted by recency, so first zero means rest are empty
    }
    _count = validCount;

    if (_scrollPos >= _count) _scrollPos = max(0, _count - 1);

    uint32_t now = _rtc->getCurrentTime();
#ifdef MECK_UI_V2
    return renderV2(display, now);
#endif

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);

    char hdr[32];
    snprintf(hdr, sizeof(hdr), MECK_TR("Last Heard: %d nodes", "Entendus : %d n\xC5\x93uds"), _count);
    display.print(hdr);

    display.drawRect(0, 11, display.width(), 1);

    // === Body — node rows ===
    display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
    int lineHeight = the_mesh.getNodePrefs()->smallLineH();
    int headerHeight = 14;
    int footerHeight = 14;
    int maxY = display.height() - footerHeight;
    int y = headerHeight;

    if (_count == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(4, 28);
      display.print(MECK_TR("No adverts received yet", "Aucune annonce re\xC3\xA7ue"));
      display.setCursor(4, 38);
      display.print(MECK_TR("Nodes appear as adverts arrive", "Les n\xC5\x93uds apparaissent ici"));
    } else {
      int maxVisible = (maxY - headerHeight) / lineHeight;
      if (maxVisible < 3) maxVisible = 3;
      int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                                _count - maxVisible));
      int endIdx = min(_count, startIdx + maxVisible);

      for (int i = startIdx; i < endIdx && y + lineHeight <= maxY; i++) {
        const AdvertPath& entry = _entries[i];
        bool selected = (i == _scrollPos);

        // Highlight selected row
        if (selected) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y + the_mesh.getNodePrefs()->smallHighlightOff(), display.width(), lineHeight);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        display.setCursor(0, y);

        // Prefix: cursor + type char
        char prefix[4];
        snprintf(prefix, sizeof(prefix), "%c%c",
                 selected ? '>' : ' ', typeChar(entry.type));
        display.print(prefix);

        // Right side: age + hops + [★] for favourites, [+] for other contacts
        char rightStr[20];
        char ageBuf[8];
        formatAge(now, entry.recv_timestamp, ageBuf, sizeof(ageBuf));

        ContactInfo* ci = the_mesh.lookupContactByPubKey(entry.pubkey_prefix, 8);
        bool inContacts = (ci != nullptr);
        bool isFav = inContacts && (ci->flags & 0x01);
        if (isFav) {
          snprintf(rightStr, sizeof(rightStr), "%s %dh [*]", ageBuf, entry.path_len & 63);
        } else if (inContacts) {
          snprintf(rightStr, sizeof(rightStr), "%s %dh [+]", ageBuf, entry.path_len & 63);
        } else {
          snprintf(rightStr, sizeof(rightStr), "%s %dh", ageBuf, entry.path_len & 63);
        }
        int rightWidth = display.getTextWidth(rightStr) + 2;

        // Name (truncated with ellipsis)
        char filteredName[32];
        display.translateUTF8ToBlocks(filteredName, entry.name, sizeof(filteredName));
        int nameX = display.getTextWidth(prefix) + 2;
        int nameMaxW = display.width() - nameX - rightWidth - 2;
        display.drawTextEllipsized(nameX, y, nameMaxW, filteredName);

        // Right-aligned info
        display.setCursor(display.width() - rightWidth, y);
        display.print(rightStr);

        y += lineHeight;
      }
    }

    display.setTextSize(1);

    // === Footer ===
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

    display.setCursor(0, footerY);
    display.print(MECK_TR("Q:Bk", "Q:Ret"));
    const char* right = MECK_TR("Tap/Ent:Add/Del", "Ent:Ajout/Suppr");
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);

    return 5000;  // refresh every 5s to update ages
  }

  bool handleInput(char c) override {
    // Shift+W: page up
    if (c == 'W') {
      int pageSize = (128 - 14 - 14) / the_mesh.getNodePrefs()->smallLineH();
      if (pageSize < 3) pageSize = 3;
      _scrollPos = max(0, _scrollPos - pageSize);
      return true;
    }
    // Scroll up
    if (c == 'w' || c == 0xF2) {
      if (_scrollPos > 0) { _scrollPos--; return true; }
      return false;
    }

    // Shift+S: page down
    if (c == 'S') {
      int pageSize = (128 - 14 - 14) / the_mesh.getNodePrefs()->smallLineH();
      if (pageSize < 3) pageSize = 3;
      _scrollPos = min(_count - 1, _scrollPos + pageSize);
      return true;
    }
    // Scroll down
    if (c == 's' || c == 0xF1) {
      if (_scrollPos < _count - 1) { _scrollPos++; return true; }
      return false;
    }

    // Enter — handled by main.cpp (needs access to private MyMesh methods)
    // Q — handled by main.cpp (navigation)
    return false;
  }
};