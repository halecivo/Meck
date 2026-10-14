#pragma once
// =============================================================================
// UI v2 -- small mesh-specific helpers shared by the node list screens
// (contacts, discovery, last heard)
// =============================================================================

#include "Ui2List.h"
#include "../MeckLang.h"

// Human name of a MeshCore node type (ADV_TYPE_*: 1 chat, 2 repeater, 3 room, 4 sensor)
inline const char* ui2NodeTypeLabel(uint8_t type) {
  switch (type) {
    case 1:  return MECK_TR("Companion", "Compagnon");
    case 2:  return MECK_TR("Repeater", "R\xC3\xA9p\xC3\xA9teur");
    case 3:  return MECK_TR("Room server", "Salon");
    case 4:  return MECK_TR("Sensor", "Capteur");
    default: return MECK_TR("Node", "N\xC5\x93ud");
  }
}

// Signal bars for a LoRa SNR in dB (4 bars from +5 dB, none below -15 dB)
inline Ui2Icon ui2SignalIcon(int snrDb) {
  if (snrDb >= 5)   return UI2_IC_SIG4;
  if (snrDb >= 0)   return UI2_IC_SIG3;
  if (snrDb >= -7)  return UI2_IC_SIG2;
  if (snrDb >= -15) return UI2_IC_SIG1;
  return UI2_IC_SIG0;
}

// "direct", "1 hop", "3 hops"
inline void ui2HopsLabel(char* buf, size_t len, int hops) {
  if (hops <= 0) snprintf(buf, len, "%s", MECK_TR("direct", "direct"));
  else if (hops == 1) snprintf(buf, len, MECK_TR("1 hop", "1 saut"));
  else snprintf(buf, len, MECK_TR("%d hops", "%d sauts"), hops);
}
