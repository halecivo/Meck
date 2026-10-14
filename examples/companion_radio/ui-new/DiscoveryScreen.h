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

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

class DiscoveryScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  int _scrollPos;
  int _rowsPerPage;

  static char typeChar(uint8_t adv_type) {
    switch (adv_type) {
      case ADV_TYPE_CHAT:     return 'C';
      case ADV_TYPE_REPEATER: return 'R';
      case ADV_TYPE_ROOM:     return 'S';
      case ADV_TYPE_SENSOR:   return 'N';
      default:                return '?';
    }
  }

  static const char* typeLabel(uint8_t adv_type) {
    switch (adv_type) {
      case ADV_TYPE_CHAT:     return "Chat";
      case ADV_TYPE_REPEATER: return MECK_TR("Rptr", "R\xC3\xA9p.");
      case ADV_TYPE_ROOM:     return MECK_TR("Room", "Salon");
      case ADV_TYPE_SENSOR:   return MECK_TR("Sens", "Capt.");
      default:                return "?";
    }
  }

public:
  DiscoveryScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _scrollPos(0), _rowsPerPage(5) {}

  void resetScroll() { _scrollPos = 0; }

  int getSelectedIdx() const { return _scrollPos; }

  // Tap-to-select: given virtual Y, select discovered node row.
  // Returns: 0=miss, 1=moved, 2=tapped current row.
  int selectRowAtVY(int vy) {
    int count = the_mesh.getDiscoveredCount();
    if (count == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = the_mesh.getNodePrefs()->smallLineH();
    const int bodyTop = headerH + the_mesh.getNodePrefs()->smallHighlightOff();
    if (vy < bodyTop || vy >= 128 - footerH) return 0;

    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                              count - maxVisible));

    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= count) return 0;

    if (tappedRow == _scrollPos) return 2;
    _scrollPos = tappedRow;
    return 1;
  }

#ifdef MECK_UI_V2
  // UI v2 (design B9): node rows with signal bars and SNR, a check for nodes
  // already in contacts. No spinner: the list redraws as results arrive.
  int renderV2(DisplayDriver& display) {
    using namespace ui2;
    const int count = the_mesh.getDiscoveredCount();
    const bool active = the_mesh.isDiscoveryActive();
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    char right[24];
    if (count > 0) snprintf(right, sizeof(right), MECK_TR("%d found", "%d trouv\xC3\xA9(s)"), count);
    else right[0] = 0;
    Ui2Chrome::titleBar(c, MECK_TR("Discover", "Recherche"), right,
                        active ? MECK_TR("Scanning", "Recherche") : nullptr, UI2_NO_ICON, active);
    _rowsPerPage = ROWS_PER_PAGE;

    if (count == 0) {
      if (active) Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_SEARCH, MECK_TR("Listening for adverts\xE2\x80\xA6", "\xC3\x89" "coute des annonces\xE2\x80\xA6"));
      else Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_SEARCH, MECK_TR("No nodes found", "Aucun n\xC5\x93ud trouv\xC3\xA9"),
                               nullptr, "F", MECK_TR("Scan again", "Relancer"));
    } else {
      const int start = Ui2List::pageStart(_scrollPos, ROWS_PER_PAGE);
      for (int i = start; i < count && i < start + ROWS_PER_PAGE; i++) {
        const DiscoveredNode& node = the_mesh.getDiscovered(i);
        char sub[40], snr[16], hops[16];
        Ui2Row r;
        r.icon = ui2NodeIcon(node.contact.type);
        r.title = node.contact.name;
        if (node.already_in_contacts) {
          snprintf(sub, sizeof(sub), "%s", MECK_TR("Already in contacts", "D\xC3\xA9j\xC3\xA0 dans les contacts"));
          r.check = true;
        } else {
          ui2HopsLabel(hops, sizeof(hops), node.path_len & 63);
          snprintf(sub, sizeof(sub), "%s \xC2\xB7 %s", ui2NodeTypeLabel(node.contact.type), hops);
        }
        r.sub = sub;
        if (node.snr != 0) {
          // Active discovery result: SNR is x4
          snprintf(snr, sizeof(snr), "SNR %+.1f", node.snr / 4.0f);
          r.right = snr;
          r.sig = ui2SignalIcon(node.snr / 4);
        }
        r.rightMax = 64;
        r.selected = (i == _scrollPos);
        Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
      }
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Add", "Ajouter")),
      ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("F", MECK_TR("Rescan", "Relancer")),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 4);
    return active ? 1000 : 5000;
  }

  // Touch hit test in physical pixels: 0 = miss, 1 = moved, 2 = tapped current row
  int selectRowAtPx(int py) {
    int count = the_mesh.getDiscoveredCount();
    if (count == 0) return 0;
    int row = Ui2List::rowAt(py, ui2::BODY_Y, ui2::ROW_H, ui2::ROWS_PER_PAGE);
    if (row < 0) return 0;
    int idx = Ui2List::pageStart(_scrollPos, ui2::ROWS_PER_PAGE) + row;
    if (idx >= count) return 0;
    if (idx == _scrollPos) return 2;
    _scrollPos = idx;
    return 1;
  }
#endif

  int render(DisplayDriver& display) override {
#ifdef MECK_UI_V2
    return renderV2(display);
#endif
    int count = the_mesh.getDiscoveredCount();
    bool active = the_mesh.isDiscoveryActive();

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);

    char hdr[32];
    if (active) {
      snprintf(hdr, sizeof(hdr), MECK_TR("Scanning... %d found", "Recherche... %d trouv\xC3\xA9(s)"), count);
    } else {
      snprintf(hdr, sizeof(hdr), MECK_TR("Scan done: %d found", "Termin\xC3\xA9 : %d trouv\xC3\xA9(s)"), count);
    }
    display.print(hdr);

    // Divider
    display.drawRect(0, 11, display.width(), 1);

    // === Body — discovered node rows ===
    display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());  // tiny font for compact rows
    int lineHeight = the_mesh.getNodePrefs()->smallLineH();
    int headerHeight = 14;
    int footerHeight = 14;
    int maxY = display.height() - footerHeight;
    int y = headerHeight;
    int rowsDrawn = 0;

    if (count == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(4, 28);
      display.print(active ? MECK_TR("Listening for adverts...", "\xC3\x89" "coute des annonces...") : MECK_TR("No nodes found", "Aucun n\xC5\x93ud trouv\xC3\xA9"));
      if (!active) {
        display.setCursor(4, 38);
        display.print(MECK_TR("F: Scan again  Q: Back", "F : relancer  Q : retour"));
      }
    } else {
      // Center visible window around selected item
      int maxVisible = (maxY - headerHeight) / lineHeight;
      if (maxVisible < 3) maxVisible = 3;
      int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                                count - maxVisible));
      int endIdx = min(count, startIdx + maxVisible);

      for (int i = startIdx; i < endIdx && y + lineHeight <= maxY; i++) {
        const DiscoveredNode& node = the_mesh.getDiscovered(i);
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

        // Prefix: cursor + type
        char prefix[4];
        if (selected) {
          snprintf(prefix, sizeof(prefix), ">%c", typeChar(node.contact.type));
        } else {
          snprintf(prefix, sizeof(prefix), " %c", typeChar(node.contact.type));
        }
        display.print(prefix);

        // Build right-side info: SNR or hop count + status
        char rightStr[16];
        if (node.snr != 0) {
          // Active discovery result — show SNR in dB (value is ×4 scaled)
          int snr_db = node.snr / 4;
          if (node.already_in_contacts) {
            snprintf(rightStr, sizeof(rightStr), "%ddB [+]", snr_db);
          } else {
            snprintf(rightStr, sizeof(rightStr), "%ddB", snr_db);
          }
        } else {
          // Pre-seeded from cache — show hop count
          if (node.already_in_contacts) {
            snprintf(rightStr, sizeof(rightStr), "%dh [+]", node.path_len & 63);
          } else {
            snprintf(rightStr, sizeof(rightStr), "%dh", node.path_len & 63);
          }
        }
        int rightWidth = display.getTextWidth(rightStr) + 2;

        // Name (truncated with ellipsis)
        char filteredName[32];
        display.translateUTF8ToBlocks(filteredName, node.contact.name, sizeof(filteredName));
        int nameX = display.getTextWidth(prefix) + 2;
        int nameMaxW = display.width() - nameX - rightWidth - 2;
        display.drawTextEllipsized(nameX, y, nameMaxW, filteredName);

        // Right-aligned info
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
    display.print(MECK_TR("Q:X F:Scan", "Q:X F:Relance"));

    const char* right = MECK_TR("Tap/Ent:Add", "Ent:Ajouter");
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);

    // Faster refresh while actively scanning
    return active ? 1000 : 5000;
  }

  bool handleInput(char c) override {
    int count = the_mesh.getDiscoveredCount();

    // Shift+W: page up
    if (c == 'W') {
      int pageSize = (128 - 14 - 14) / the_mesh.getNodePrefs()->smallLineH();
      if (pageSize < 3) pageSize = 3;
      _scrollPos = max(0, _scrollPos - pageSize);
      return true;
    }
    // W - scroll up
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
      _scrollPos = min(count - 1, _scrollPos + pageSize);
      return true;
    }
    // S - scroll down
    if (c == 's' || c == 0xF1) {
      if (_scrollPos < count - 1) {
        _scrollPos++;
        return true;
      }
    }

    // F - rescan (handled here as well as in main.cpp for consistency)
    if (c == 'f') {
      the_mesh.startDiscovery();
      _scrollPos = 0;
      return true;
    }

    // Enter - handled by main.cpp for alert feedback

    return false;  // Q/back and Enter handled by main.cpp
  }
};