#pragma once

// TX log events (T-Deck MAX): what happened to outgoing traffic. Recorded in
// RAM and written to /meshcore/txlog on the SD card by main.cpp. On other
// boards meckTxLog() compiles to nothing.

#include <stdint.h>

enum {
  MECK_TXLOG_TX_OK = 1,    // a = payload type, b = 1 direct / 0 flood, c = length
  MECK_TXLOG_TX_FAIL,      // same; the radio did not start or finish the send
  MECK_TXLOG_DM_SEND,      // a = attempt, b = planned attempts, c = MSG_SEND_SENT_*
  MECK_TXLOG_DM_ACK,       // a = attempts made, b = planned attempts
  MECK_TXLOG_DM_FAIL,      // a, b as above, c = 1 no ack, 2 contact removed, 3 not queued
  MECK_TXLOG_CH_SEND,      // a = channel index
  MECK_TXLOG_TX_POWER,     // a = dBm, c = RadioLib status of setOutputPower()
};

#if defined(LilyGo_TDeck_Pro_Max)
void meckTxLogEvent(uint8_t kind, uint8_t a, uint8_t b, int16_t c);
static inline void meckTxLog(uint8_t kind, uint8_t a = 0, uint8_t b = 0, int16_t c = 0) {
  meckTxLogEvent(kind, a, b, c);
}
#else
static inline void meckTxLog(uint8_t, uint8_t = 0, uint8_t = 0, int16_t = 0) {}
#endif
