#ifdef MECK_UI_V2

#include "Ui2Chrome.h"
#include <stdio.h>

using namespace ui2;

Ui2Status ui2Status;

namespace Ui2Chrome {

void statusHeader(Ui2Canvas& c) {
  const Ui2Status& st = ui2Status;

  // Node name, 11B, at most 90 px
  c.textFit(6, Ui2Canvas::boxBaseline(UI2_F_NODE, 0, HDR_H - 1), HDR_NODE_MAX_W,
            st.nodeName ? st.nodeName : "", UI2_F_NODE);

  // Clock, Montserrat 11, centred in its 32 px slot; only once time is synced
  if (st.clockValid) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02u:%02u", st.hour, st.minute);
    c.textFit(HDR_CLOCK_X, Ui2Canvas::boxBaseline(UI2_F_NUM11, 0, HDR_H - 1), HDR_CLOCK_W,
              buf, UI2_F_NUM11, true, UI2_CENTER);
  }

  // Right block, laid out from the right edge: percentage, battery,
  // charging bolt, then status icons while they fit left of x = 140
  int16_t x = SCREEN_W - 4;
  char pct[6];
  snprintf(pct, sizeof(pct), "%d%%", st.batteryPct);
  int16_t pw = Ui2Canvas::textWidth(pct, UI2_F_META);
  x -= pw;
  c.text(x, Ui2Canvas::boxBaseline(UI2_F_META, 0, HDR_H - 1), pct, UI2_F_META);
  x -= 2 + 20;
  c.battery(x, 7, st.batteryPct);
  x -= 1;
  if (st.charging) { x -= 12; c.icon(x, 6, UI2_IC_BOLT, 12); x -= 1; }

  const Ui2Icon order[] = {
    st.unread ? UI2_IC_MSGDOT : UI2_NO_ICON,
    st.bleConnected ? UI2_IC_BLE : UI2_NO_ICON,
    st.gpsOn ? UI2_IC_GPS : UI2_NO_ICON,
    st.alarmOn ? UI2_IC_ALARM : UI2_NO_ICON,
    st.audioPlaying ? UI2_IC_PLAY : UI2_NO_ICON,
    st.externalAntenna ? UI2_IC_ANTEXT : UI2_NO_ICON,
    st.silent ? UI2_IC_SILENT : UI2_NO_ICON,
  };
  for (Ui2Icon ic : order) {
    if (ic == UI2_NO_ICON) continue;
    if (x - 13 < HDR_RIGHT_X) break;
    x -= 13;
    c.icon(x, 6, ic, 12);
  }

  c.hline(0, HDR_H - 1, SCREEN_W);
}

void titleBar(Ui2Canvas& c, const char* title, const char* right,
              const char* chip, Ui2Icon chipIcon, bool chipActive) {
  int16_t xr = SCREEN_W - PAD;
  if (right && *right) {
    int16_t rw = Ui2Canvas::textWidth(right, UI2_F_META);
    xr -= rw;
    c.text(xr, Ui2Canvas::boxBaseline(UI2_F_META, TITLE_Y, TITLE_H - 1), right, UI2_F_META);
    xr -= 6;
  }
  if ((chip && *chip) || chipIcon != UI2_NO_ICON) {
    int16_t cw = Ui2Canvas::chipWidth(chip, chipIcon);
    xr -= cw;
    c.chip(xr, TITLE_Y + 5, chip, chipIcon, chipActive);
    xr -= 6;
  }
  c.textFit(PAD, Ui2Canvas::boxBaseline(UI2_F_TITLE, TITLE_Y, TITLE_H - 1), xr - PAD,
            title ? title : "", UI2_F_TITLE);
  c.hline(0, TITLE_Y + TITLE_H - 1, SCREEN_W);
}

void footer(Ui2Canvas& c, const Ui2Key* keys, int count) {
  const int16_t LEFT = 4, AVAIL = SCREEN_W - 2 * LEFT, ITEM_GAP = 6, LABEL_GAP = 3;
  const int MAXK = 6;
  if (count > MAXK) count = MAXK;
  bool showLabel[MAXK];
  int16_t capW[MAXK], labW[MAXK];
  for (int i = 0; i < count; i++) {
    capW[i] = Ui2Canvas::keycapWidth(keys[i].key, keys[i].icon);
    labW[i] = (keys[i].label && *keys[i].label) ? Ui2Canvas::textWidth(keys[i].label, UI2_F_META) : 0;
    showLabel[i] = labW[i] > 0;
  }
  auto total = [&]() {
    int16_t w = 0;
    for (int i = 0; i < count; i++) {
      w += capW[i] + (showLabel[i] ? LABEL_GAP + labW[i] : 0);
      if (i) w += ITEM_GAP;
    }
    return w;
  };
  // Rule: the label goes, the key stays. Navigation labels go first.
  for (int i = 0; i < count && total() > AVAIL; i++) if (keys[i].nav) showLabel[i] = false;
  for (int i = count - 1; i >= 0 && total() > AVAIL; i--) showLabel[i] = false;

  c.hline(0, FOOTER_Y, SCREEN_W);
  const int16_t top = FOOTER_Y + 5;
  int16_t x = LEFT;
  for (int i = 0; i < count; i++) {
    x += c.keycap(x, top, keys[i].key, keys[i].icon, keys[i].active);
    if (showLabel[i]) {
      x += LABEL_GAP;
      c.text(x, Ui2Canvas::boxBaseline(UI2_F_META, top, 13), keys[i].label, UI2_F_META);
      x += labW[i];
    }
    x += ITEM_GAP;
  }
}

}  // namespace Ui2Chrome

#endif  // MECK_UI_V2
