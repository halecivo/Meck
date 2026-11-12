#pragma once
// =============================================================================
// Calendar account screen (Settings > Experimental > Calendar account)
//
// No account: Enter starts the pairing and the QR code appears; the phone that
// scans it types the server, user and password. With an account: the server,
// user, the calendars and the synced contacts; S syncs the contacts now, P
// pairs again, X twice forgets the account (and its synced contacts).
// Q leaves (and stops a pairing that is still waiting).
// =============================================================================

#if defined(MECK_DAV) && MECK_DAV

#include <Arduino.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/GxEPDDisplay.h>
#include "MeckDav.h"
#include "MeckLang.h"
#ifdef HAS_4G_MODEM
  #include "SMSContacts.h"
#endif
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
#endif

class DavScreen : public UIScreen {
public:
  bool wantsExit() const { return _exit; }
  void clearExit() { _exit = false; }
  void onOpen() { _exit = false; _forgetAt = 0; }

  bool handleInput(char c) override {
    MeckDav::Phase ph = meckDav.phase();
    if (c == 'q' || c == 'Q' || c == KEY_CANCEL) {
      meckDav.stopPairing();
      _exit = true;
      return true;
    }
    if (meckDav.pairingActive()) return true;          // the phone does the rest
    bool have = meckDav.configured();
    if (c == KEY_ENTER) {
      if (!have || ph == MeckDav::PH_FAILED || ph == MeckDav::PH_NO_WIFI) meckDav.startPairing();
      return true;
    }
    if (!have) return true;
    if (c == 'p' || c == 'P') { meckDav.startPairing(); return true; }
    if ((c == 's' || c == 'S') && meckDav.bookCount() > 0) { meckDav.syncNow(); return true; }
    if (c == 'x' || c == 'X') {
      if (_forgetAt && millis() - _forgetAt < 4000) { _forgetAt = 0; meckDav.stopPairing(); meckDav.forget(); }
      else _forgetAt = millis();
      return true;
    }
    return true;
  }

  int render(DisplayDriver& display) override {
    if (_forgetAt && millis() - _forgetAt >= 4000) _forgetAt = 0;
#ifdef MECK_UI_V2
    renderV2(display);
#else
    renderClassic(display);
#endif
    // Pairing changes on the phone's side, a sync moves on; otherwise nothing does
    return meckDav.pairingActive() || meckDav.syncing() || _forgetAt ? 1000 : 5000;
  }

private:
  bool _exit = false;
  unsigned long _forgetAt = 0;   // first X: a second one within 4 s forgets

  // The QR code in black on white even in dark mode (scanners expect that),
  // whole pixels per module, with the 4-module quiet zone. Returns its side.
  static int16_t drawQr(GxEPDDisplay& d, int16_t cx, int16_t top, int16_t maxSide) {
    int n = meckDav.qrSize();
    if (n <= 0) return 0;
    int scale = maxSide / (n + 8);
    if (scale < 1) return 0;
    int16_t side = (int16_t)(scale * (n + 8));
    int16_t x0 = cx - side / 2;
    bool dark = d.isDarkMode();
    d.fillRectRaw(x0, top, side, side, dark);              // white: "ink" in dark mode
    int16_t q = (int16_t)(4 * scale);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++)
        if (meckDav.qrModule(x, y))
          d.fillRectRaw(x0 + q + x * scale, top + q + y * scale, scale, scale, !dark);
    return side;
  }

  // "12 contacts, synced 5 min ago" and the like, for the account page
  static void contactsLine(char* out, size_t n) {
#ifdef HAS_4G_MODEM
    int k = smsContacts.syncedCount();
    if (meckDav.syncing()) snprintf(out, n, "%s", MECK_TR("Syncing contacts...", "Synchronisation des contacts..."));
    else if (!meckDav.syncAgoMs() && !k) snprintf(out, n, "%s", MECK_TR("Contacts: not synced yet", "Contacts : pas encore synchronis\xC3\xA9s"));
    else if (!meckDav.syncAgoMs()) snprintf(out, n, MECK_TR("%d contacts", "%d contacts"), k);
    else if (!meckDav.syncOk()) snprintf(out, n, MECK_TR("Sync failed: %s", "\xC3\x89" "chec : %s"), meckDav.syncError());
    else snprintf(out, n, MECK_TR("%d contacts, synced %lu min ago", "%d contacts, il y a %lu min"), k,
                  (unsigned long)(meckDav.syncAgoMs() / 60000));
#else
    snprintf(out, n, "-");
#endif
  }

  const char* stateText() const {
    switch (meckDav.phase()) {
      case MeckDav::PH_NO_WIFI: return MECK_TR("Turn WiFi on first (Settings > WiFi).", "Activez d'abord le WiFi.");
      case MeckDav::PH_SCAN:
        if (meckDav.error()[0]) return meckDav.error();
        return MECK_TR("Scan with your phone on the same network as the deck.",
                       "Scannez avec un t\xC3\xA9l\xC3\xA9phone sur le m\xC3\xAAme r\xC3\xA9seau.");
      case MeckDav::PH_WORKING: return meckDav.step();
      case MeckDav::PH_FAILED:  return meckDav.error();
      default: break;
    }
    if (!meckDav.configured()) return MECK_TR("Add a CalDAV account (Radicale, Nextcloud, iCloud...) from your phone.",
                                              "Ajoutez un compte CalDAV depuis votre t\xC3\xA9l\xC3\xA9phone.");
    return nullptr;
  }

#ifdef MECK_UI_V2
  void renderV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    MeckDav::Phase ph = meckDav.phase();
    bool qr = ph == MeckDav::PH_SCAN;
    const char* chip = meckDav.pairingActive() ? MECK_TR("PAIRING", "ASSOCIATION")
                     : meckDav.configured() ? MECK_TR("ON", "ACTIF") : nullptr;
    Ui2Chrome::titleBar(c, MECK_TR("Calendar account", "Compte agenda"), nullptr, chip);
    int16_t y = BODY_Y + 2;
    const char* st = stateText();

    if (qr) {
      int16_t side = drawQr(c.raw(), SCREEN_W / 2, y, 176);
      y += side + 4;
      c.textFit(PAD, y + 10, SCREEN_W - 2 * PAD, meckDav.pageUrl(), UI2_F_META, true, UI2_CENTER);
      y += 16;
      if (st) { c.wrap(PAD, y + 10, SCREEN_W - 2 * PAD, 13, 2, st, UI2_F_META, true, true, nullptr, UI2_CENTER); y += 28; }
      char left[40];
      snprintf(left, sizeof(left), MECK_TR("%lu min left", "%lu min restantes"),
               (unsigned long)((meckDav.pairLeftMs() + 59999) / 60000));
      c.textFit(PAD, y + 8, SCREEN_W - 2 * PAD, left, UI2_F_META, true, UI2_CENTER);
    } else {
      if (st) {
        int lines = c.wrap(PAD, y + 14, SCREEN_W - 2 * PAD, 15, 4, st, UI2_F_BODY);
        y += lines * 15 + 10;
      }
      if (meckDav.configured() && ph != MeckDav::PH_WORKING) {
        char host[40], v[16];
        davUrlHost(meckDav.serverUrl(), host, sizeof(host));
        Ui2List::kvRow(c, y, MECK_TR("Server", "Serveur"), host); y += 20;
        Ui2List::kvRow(c, y, MECK_TR("User", "Utilisateur"), meckDav.user()); y += 20;
        snprintf(v, sizeof(v), "%d", meckDav.calCount());
        Ui2List::kvRow(c, y, MECK_TR("Calendars", "Agendas"), v); y += 20;
        for (int i = 0; i < meckDav.calCount() && i < 3; i++) {
          c.textFit(PAD + 8, y + 10, SCREEN_W - 2 * PAD - 8, meckDav.cal(i).name, UI2_F_META, true);
          y += 15;
        }
        snprintf(v, sizeof(v), "%d", meckDav.bookCount());
        Ui2List::kvRow(c, y, MECK_TR("Address books", "Carnets"), v); y += 20;
        if (meckDav.bookCount() > 0) {
          char line[64];
          contactsLine(line, sizeof(line));
          c.wrap(PAD + 8, y + 10, SCREEN_W - 2 * PAD - 8, 13, 2, line, UI2_F_META, true);
          y += 28;
        }
      }
      if (_forgetAt) {
        c.wrap(PAD, FOOTER_Y - 26, SCREEN_W - 2 * PAD, 13, 2,
               MECK_TR("Press X again to forget the account", "X encore pour oublier le compte"),
               UI2_F_META, true, true, nullptr, UI2_CENTER);
      }
    }

    Ui2Key keys[4];
    int k = 0;
    bool have = meckDav.configured();
    if (!meckDav.pairingActive()) {
      if (!have || ph == MeckDav::PH_FAILED || ph == MeckDav::PH_NO_WIFI)
        keys[k++] = ui2Key("Ent", MECK_TR("Pair", "Associer"));
      else {
        keys[k++] = ui2Key("P", MECK_TR("Re-pair", "R\xC3\xA9" "associer"));
        if (meckDav.bookCount() > 0) keys[k++] = ui2Key("S", MECK_TR("Sync", "Synchro"));
        keys[k++] = ui2Key("X", MECK_TR("Forget", "Oublier"));
      }
    }
    keys[k++] = ui2Key("Q", MECK_TR("Back", "Retour"), true);
    Ui2Chrome::footer(c, keys, k);
  }
#else
  void renderClassic(DisplayDriver& d) {
    char line[64];
    int cx = d.width() / 2;
    d.setTextSize(1);
    d.setColor(DisplayDriver::LIGHT);
    d.drawTextCentered(cx, 2, MECK_TR("Calendar account", "Compte agenda"));
    MeckDav::Phase ph = meckDav.phase();
    const char* st = stateText();
    if (ph == MeckDav::PH_SCAN) {
      // Virtual 128x128 is stretched to 240x320: draw the code in real pixels
      GxEPDDisplay& g = static_cast<GxEPDDisplay&>(d);
      drawQr(g, g.rawWidth() / 2, 36, 200);
      d.drawTextEllipsized(2, d.height() - 34, d.width() - 4, meckDav.pageUrl());
      if (st) d.drawTextEllipsized(2, d.height() - 23, d.width() - 4, st);
      snprintf(line, sizeof(line), MECK_TR("%lu min left  Q:back", "%lu min  Q:retour"),
               (unsigned long)((meckDav.pairLeftMs() + 59999) / 60000));
      d.drawTextEllipsized(2, d.height() - 12, d.width() - 4, line);
      return;
    }
    int y = 16;
    if (st) { d.drawTextEllipsized(2, y, d.width() - 4, st); y += 13; }
    if (meckDav.configured() && ph != MeckDav::PH_WORKING) {
      char host[40];
      davUrlHost(meckDav.serverUrl(), host, sizeof(host));
      snprintf(line, sizeof(line), MECK_TR("Server: %s", "Serveur : %s"), host);
      d.drawTextEllipsized(2, y, d.width() - 4, line); y += 11;
      snprintf(line, sizeof(line), MECK_TR("User: %s", "Utilisateur : %s"), meckDav.user());
      d.drawTextEllipsized(2, y, d.width() - 4, line); y += 11;
      for (int i = 0; i < meckDav.calCount() && i < 4; i++) {
        d.drawTextEllipsized(8, y, d.width() - 10, meckDav.cal(i).name); y += 11;
      }
      if (meckDav.bookCount() > 0) {
        contactsLine(line, sizeof(line));
        d.drawTextEllipsized(2, y, d.width() - 4, line); y += 11;
      }
    }
    if (_forgetAt) d.drawTextEllipsized(2, d.height() - 24, d.width() - 4,
                                        MECK_TR("X again: forget", "X encore : oublier"));
    bool have = meckDav.configured();
    d.drawTextEllipsized(2, d.height() - 12, d.width() - 4,
                         meckDav.pairingActive() ? "Q:back"
                         : (!have || ph == MeckDav::PH_FAILED || ph == MeckDav::PH_NO_WIFI)
                           ? MECK_TR("Enter:pair  Q:back", "Entr\xC3\xA9" "e:associer Q:retour")
                           : meckDav.bookCount() > 0 ? MECK_TR("P:pair S:sync X:forget Q", "P:assoc S:sync X Q")
                           : MECK_TR("P:re-pair X:forget Q:back", "P:r\xC3\xA9" "assoc X:oublier Q"));
  }
#endif
};

#endif
