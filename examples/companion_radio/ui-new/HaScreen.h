#pragma once
// =============================================================================
// Home Assistant screen (Settings > Experimental > Home Assistant)
//
// Not paired: Enter starts the pairing and the QR code appears; the phone
// that scans it logs in to Home Assistant. Paired: where the last update
// went and when; U sends one now, P pairs again, X twice forgets the link.
// Q leaves (and stops a pairing that is still waiting).
// =============================================================================

#if defined(MECK_HA) && MECK_HA

#include <Arduino.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/GxEPDDisplay.h>
#include "MeckHa.h"
#include "MeckLang.h"
#include "../NodePrefs.h"
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
#endif

class HaScreen : public UIScreen {
public:
  HaScreen(mesh::RTCClock* rtc, NodePrefs* prefs) : _rtc(rtc), _prefs(prefs) {}

  bool wantsExit() const { return _exit; }
  void clearExit() { _exit = false; }
  void onOpen() { _exit = false; _forgetAt = 0; }

  bool handleInput(char c) override {
    MeckHa::Phase ph = meckHa.phase();
    if (c == 'q' || c == 'Q' || c == KEY_CANCEL) {
      meckHa.stopPairing();
      _exit = true;
      return true;
    }
    if (meckHa.pairingActive()) return true;          // the phone does the rest
    bool paired = meckHa.paired() && !meckHa.removed();
    if (c == KEY_ENTER) {
      if (!paired || ph == MeckHa::PH_FAILED || ph == MeckHa::PH_NO_WIFI) meckHa.startPairing();
      return true;
    }
    if (!paired) return true;
    if (c == 'p' || c == 'P') { meckHa.startPairing(); return true; }
    if (c == 'u' || c == 'U') { meckHa.postSoon(0); return true; }
    if (c == 'x' || c == 'X') {
      if (_forgetAt && millis() - _forgetAt < 4000) { _forgetAt = 0; meckHa.stopPairing(); meckHa.forget(); }
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
    // Pairing changes on the phone's side; otherwise only the clock moves
    return meckHa.pairingActive() || _forgetAt ? 1000 : 5000;
  }

private:
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  bool _exit = false;
  unsigned long _forgetAt = 0;   // first X: a second one within 4 s forgets

  // The QR code in black on white even in dark mode (scanners expect that),
  // whole pixels per module, with the 4-module quiet zone. Returns its side.
  static int16_t drawQr(GxEPDDisplay& d, int16_t cx, int16_t top, int16_t maxSide) {
    int n = meckHa.qrSize();
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
        if (meckHa.qrModule(x, y))
          d.fillRectRaw(x0 + q + x * scale, top + q + y * scale, scale, scale, !dark);
    return side;
  }

  // "14:35" local time of an epoch, "" without one
  void fmtClock(char* out, size_t n, uint32_t t) const {
    out[0] = 0;
    if (t < 1700000000) return;
    int32_t local = (int32_t)t + (int32_t)_prefs->utc_offset_hours * 3600;
    snprintf(out, n, "%02d:%02d", (int)((local / 3600) % 24 + 24) % 24, (int)((local / 60) % 60 + 60) % 60);
  }

  void fmtLast(char* out, size_t n) const {
    int r = meckHa.lastResult();
    char at[8];
    fmtClock(at, sizeof(at), meckHa.lastPostEpoch());
    if (r == 0) snprintf(out, n, "%s", MECK_TR("Not sent yet", "Pas encore envoy\xC3\xA9"));
    else if (r == 200 || r == 201) snprintf(out, n, MECK_TR("OK %s", "OK %s"), at);
    else if (r < 0) snprintf(out, n, "%s", MECK_TR("No connection", "Pas de connexion"));
    else snprintf(out, n, MECK_TR("Error %d", "Erreur %d"), r);
  }

  const char* stateText() const {
    switch (meckHa.phase()) {
      case MeckHa::PH_NO_WIFI: return MECK_TR("Turn WiFi on first (Settings > WiFi).", "Activez d'abord le WiFi.");
      case MeckHa::PH_SCAN:    return MECK_TR("Scan with your phone on the same network as the deck.",
                                              "Scannez avec un t\xC3\xA9l\xC3\xA9phone sur le m\xC3\xAAme r\xC3\xA9seau.");
      case MeckHa::PH_LOGIN:   return MECK_TR("Log in to Home Assistant on the phone.", "Connectez-vous sur le t\xC3\xA9l\xC3\xA9phone.");
      case MeckHa::PH_WORKING: return meckHa.step();
      case MeckHa::PH_FAILED:  return meckHa.error();
      default: break;
    }
    if (meckHa.removed()) return MECK_TR("Removed in Home Assistant. Enter: pair again.",
                                         "Supprim\xC3\xA9 dans Home Assistant. Entr\xC3\xA9" "e : r\xC3\xA9" "associer.");
    if (!meckHa.paired()) return MECK_TR("The deck reports its battery and mesh to Home Assistant over WiFi.",
                                         "Le deck envoie batterie et mesh \xC3\xA0 Home Assistant par WiFi.");
    return nullptr;
  }

#ifdef MECK_UI_V2
  void renderV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    MeckHa::Phase ph = meckHa.phase();
    bool qr = ph == MeckHa::PH_SCAN || ph == MeckHa::PH_LOGIN;
    const char* chip = meckHa.pairingActive() ? MECK_TR("PAIRING", "ASSOCIATION")
                     : (meckHa.paired() && !meckHa.removed()) ? MECK_TR("ON", "ACTIF") : nullptr;
    Ui2Chrome::titleBar(c, "Home Assistant", nullptr, chip);
    int16_t y = BODY_Y + 2;
    const char* st = stateText();

    if (qr) {
      int16_t side = drawQr(c.raw(), SCREEN_W / 2, y, 176);
      y += side + 4;
      c.textFit(PAD, y + 10, SCREEN_W - 2 * PAD, meckHa.pageUrl(), UI2_F_BODY, true, UI2_CENTER);
      y += 16;
      if (st) { c.wrap(PAD, y + 10, SCREEN_W - 2 * PAD, 13, 2, st, UI2_F_META, true, true, nullptr, UI2_CENTER); y += 28; }
      char left[40];
      snprintf(left, sizeof(left), MECK_TR("%s, %lu min left", "%s, %lu min restantes"),
               meckHa.foundUrl()[0] ? MECK_TR("HA found", "HA trouv\xC3\xA9") : MECK_TR("Searching HA", "Recherche HA"),
               (unsigned long)((meckHa.pairLeftMs() + 59999) / 60000));
      c.textFit(PAD, y + 8, SCREEN_W - 2 * PAD, left, UI2_F_META, true, UI2_CENTER);
    } else {
      if (st) {
        int lines = c.wrap(PAD, y + 14, SCREEN_W - 2 * PAD, 15, 4, st, UI2_F_BODY);
        y += lines * 15 + 10;
      }
      if (meckHa.paired() && ph != MeckHa::PH_WORKING) {
        char host[40], v[40];
        haUrlHost(meckHa.haUrl(), host, sizeof(host));
        Ui2List::kvRow(c, y, MECK_TR("Server", "Serveur"), host); y += 20;
        fmtLast(v, sizeof(v));
        Ui2List::kvRow(c, y, MECK_TR("Last update", "Derni\xC3\xA8re m\xC3\xA0j"), v); y += 20;
        Ui2List::kvRow(c, y, MECK_TR("Sent via", "Via"), meckHa.lastBase()[0] ? meckHa.lastBase() : "-"); y += 20;
        Ui2List::kvRow(c, y, MECK_TR("Network", "R\xC3\xA9seau"),
                       meckHa.atHome() ? MECK_TR("home", "maison") : MECK_TR("away", "ext\xC3\xA9rieur")); y += 20;
        if (!meckHa.removed()) {
          snprintf(v, sizeof(v), MECK_TR("in %lu min", "dans %lu min"), (unsigned long)((meckHa.nextPostInMs() + 59999) / 60000));
          Ui2List::kvRow(c, y, MECK_TR("Next update", "Prochaine m\xC3\xA0j"), v); y += 20;
        }
      }
      if (_forgetAt) {
        c.wrap(PAD, FOOTER_Y - 26, SCREEN_W - 2 * PAD, 13, 2,
               MECK_TR("Press X again to forget Home Assistant", "X encore pour oublier Home Assistant"),
               UI2_F_META, true, true, nullptr, UI2_CENTER);
      }
    }

    Ui2Key keys[4];
    int k = 0;
    bool paired = meckHa.paired() && !meckHa.removed();
    if (!meckHa.pairingActive()) {
      if (!paired || ph == MeckHa::PH_FAILED || ph == MeckHa::PH_NO_WIFI)
        keys[k++] = ui2Key("Ent", MECK_TR("Pair", "Associer"));
      else {
        keys[k++] = ui2Key("U", MECK_TR("Update", "Envoyer"));
        keys[k++] = ui2Key("P", MECK_TR("Re-pair", "R\xC3\xA9" "associer"));
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
    d.drawTextCentered(cx, 2, "Home Assistant");
    MeckHa::Phase ph = meckHa.phase();
    const char* st = stateText();
    if (ph == MeckHa::PH_SCAN || ph == MeckHa::PH_LOGIN) {
      // Virtual 128x128 is stretched to 240x320: draw the code in real pixels
      GxEPDDisplay& g = static_cast<GxEPDDisplay&>(d);
      drawQr(g, g.rawWidth() / 2, 36, 200);
      d.drawTextEllipsized(2, d.height() - 34, d.width() - 4, meckHa.pageUrl());
      if (st) d.drawTextEllipsized(2, d.height() - 23, d.width() - 4, st);
      snprintf(line, sizeof(line), MECK_TR("%lu min left  Q:back", "%lu min  Q:retour"),
               (unsigned long)((meckHa.pairLeftMs() + 59999) / 60000));
      d.drawTextEllipsized(2, d.height() - 12, d.width() - 4, line);
      return;
    }
    int y = 16;
    if (st) { d.drawTextEllipsized(2, y, d.width() - 4, st); y += 13; }
    if (meckHa.paired() && ph != MeckHa::PH_WORKING) {
      char host[40], v[40];
      haUrlHost(meckHa.haUrl(), host, sizeof(host));
      snprintf(line, sizeof(line), MECK_TR("Server: %s", "Serveur : %s"), host);
      d.drawTextEllipsized(2, y, d.width() - 4, line); y += 11;
      fmtLast(v, sizeof(v));
      snprintf(line, sizeof(line), MECK_TR("Last: %s via %s", "Dernier : %s via %s"), v,
               meckHa.lastBase()[0] ? meckHa.lastBase() : "-");
      d.drawTextEllipsized(2, y, d.width() - 4, line); y += 11;
    }
    if (_forgetAt) d.drawTextEllipsized(2, d.height() - 24, d.width() - 4,
                                        MECK_TR("X again: forget", "X encore : oublier"));
    bool paired = meckHa.paired() && !meckHa.removed();
    d.drawTextEllipsized(2, d.height() - 12, d.width() - 4,
                         meckHa.pairingActive() ? "Q:back"
                         : (!paired || ph == MeckHa::PH_FAILED || ph == MeckHa::PH_NO_WIFI)
                           ? MECK_TR("Enter:pair  Q:back", "Entr\xC3\xA9" "e:associer Q:retour")
                           : MECK_TR("U:update P:re-pair X:forget Q:back", "U:envoi P:r\xC3\xA9" "assoc X:oublier Q"));
  }
#endif
};

#endif
