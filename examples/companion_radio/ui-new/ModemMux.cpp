#include "ModemManager.h"   // HAS_4G_MODEM, MODEM_DATA

#if defined(HAS_4G_MODEM) && defined(MODEM_DATA)

#include "ModemMux.h"

// Frame types (control field without the P/F bit)
#define MUX_SABM  0x2F
#define MUX_UA    0x63
#define MUX_DM    0x0F
#define MUX_DISC  0x43
#define MUX_UIH   0xEF
#define MUX_UI    0x03
#define MUX_PF    0x10
#define MUX_FLAG  0xF9

// Control channel message types (type octet with EA set, C/R clear)
#define MUX_MSG_CLD  0xC1
#define MUX_MSG_TEST 0x21
#define MUX_MSG_MSC  0xE1
#define MUX_MSG_CR   0x02

static uint8_t crcTable[256];

static void buildCrcTable() {
  if (crcTable[1]) return;
  // TS 27.010 annex B: reflected x^8 + x^2 + x + 1
  for (int i = 0; i < 256; i++) {
    uint8_t r = i;
    for (int b = 0; b < 8; b++) r = (r & 1) ? (r >> 1) ^ 0xE0 : r >> 1;
    crcTable[i] = r;
  }
}

uint8_t ModemMux::fcs(const uint8_t* p, size_t n) {
  uint8_t c = 0xFF;
  while (n--) c = crcTable[c ^ *p++];
  return 0xFF - c;
}

void ModemMux::init(HardwareSerial* uart) {
  _uart = uart;
  buildCrcTable();
  if (!_txMutex) _txMutex = xSemaphoreCreateMutex();
  // In PSRAM: a frame buffer is only needed while the mux runs
  if (!_rxBuf) _rxBuf = (uint8_t*)ps_malloc(RX_FRAME_MAX);
  if (!_rxBuf) _rxBuf = (uint8_t*)malloc(RX_FRAME_MAX);
}

bool ModemMux::sendFrame(uint8_t dlci, uint8_t ctrl, bool command, const uint8_t* info, size_t len) {
  if (!_uart) return false;
  // We started the mux, so our commands carry C/R = 1 and responses 0
  uint8_t hdr[5];
  size_t h = 0;
  hdr[h++] = MUX_FLAG;
  hdr[h++] = (dlci << 2) | (command ? 0x02 : 0) | 0x01;
  hdr[h++] = ctrl;
  if (len <= 127) {
    hdr[h++] = (len << 1) | 0x01;
  } else {
    hdr[h++] = (len & 0x7F) << 1;
    hdr[h++] = len >> 7;
  }
  uint8_t tail[2] = { fcs(hdr + 1, h - 1), MUX_FLAG };

  if (xSemaphoreTake(_txMutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
  _uart->write(hdr, h);
  if (len) _uart->write(info, len);
  _uart->write(tail, 2);
  xSemaphoreGive(_txMutex);
  _txBytes += len;
  return true;
}

bool ModemMux::send(uint8_t dlci, const uint8_t* data, size_t len) {
  if (!_active) return false;
  while (len) {
    size_t n = len > _infoMax ? _infoMax : len;
    if (!sendFrame(dlci, MUX_UIH, true, data, n)) return false;
    data += n;
    len -= n;
  }
  return true;
}

bool ModemMux::openDlc(uint8_t dlci) {
  for (int attempt = 0; attempt < 3; attempt++) {
    _uaSeen &= ~(1 << dlci);
    sendFrame(dlci, MUX_SABM | MUX_PF, true, nullptr, 0);
    unsigned long start = millis();
    while (millis() - start < 1500) {
      pump();
      if (_uaSeen & (1 << dlci)) return true;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  Serial.printf("[Mux] DLC %u did not open\n", dlci);
  return false;
}

void ModemMux::sendMsc(uint8_t dlci) {
  // Modem status: RTC, RTR and DV set, no flow control stop
  uint8_t msg[4] = { MUX_MSG_MSC | MUX_MSG_CR, (2 << 1) | 1,
                     (uint8_t)((dlci << 2) | 0x03), 0x8D };
  sendFrame(0, MUX_UIH, true, msg, sizeof(msg));
}

bool ModemMux::open(size_t infoMax) {
  if (!_uart || !_rxBuf) return false;
  _infoMax = infoMax;
  _rxState = RX_FLAG;
  _atHead = _atTail = 0;
  _dataHead = _dataTail = 0;
  _pppMode = false;
  _active = true;
  // The modem switches after its OK; give it a moment
  vTaskDelay(pdMS_TO_TICKS(100));
  for (uint8_t d = 0; d <= 2; d++) {
    if (!openDlc(d)) { _active = false; return false; }
  }
  sendMsc(1);
  sendMsc(2);
  Serial.printf("[Mux] open, N1=%u\n", (unsigned)_infoMax);
  return true;
}

void ModemMux::close() {
  if (!_active) return;
  _pppMode = false;
  for (int d = 2; d >= 1; d--) {
    sendFrame(d, MUX_DISC | MUX_PF, true, nullptr, 0);
    unsigned long start = millis();
    while (millis() - start < 300) { pump(); vTaskDelay(pdMS_TO_TICKS(10)); }
  }
  uint8_t cld[2] = { MUX_MSG_CLD | MUX_MSG_CR, 0x01 };
  sendFrame(0, MUX_UIH, true, cld, sizeof(cld));
  unsigned long start = millis();
  while (millis() - start < 500) { pump(); vTaskDelay(pdMS_TO_TICKS(10)); }
  _active = false;
  Serial.printf("[Mux] closed (rx %lu, tx %lu, bad frames %lu)\n",
                (unsigned long)_rxBytes, (unsigned long)_txBytes, (unsigned long)_badFrames);
}

void ModemMux::pump() {
  if (!_active || !_uart) return;
  uint8_t buf[128];
  int n;
  while ((n = _uart->available()) > 0) {
    if (n > (int)sizeof(buf)) n = sizeof(buf);
    n = _uart->read(buf, n);
    for (int i = 0; i < n; i++) feed(buf[i]);
  }
}

void ModemMux::feed(uint8_t b) {
  switch (_rxState) {
    case RX_FLAG:
      if (b == MUX_FLAG) _rxState = RX_ADDR;
      break;
    case RX_ADDR:
      if (b == MUX_FLAG) break;             // flags between frames
      if (!(b & 0x01)) { _rxState = RX_FLAG; _badFrames++; break; }
      _rxAddr = b;
      _rxState = RX_CTRL;
      break;
    case RX_CTRL:
      _rxCtrl = b;
      _rxState = RX_LEN1;
      break;
    case RX_LEN1:
      _rxLen1 = b;
      _rxLen = b >> 1;
      _rxPos = 0;
      if (b & 0x01) _rxState = _rxLen ? RX_INFO : RX_FCS;
      else _rxState = RX_LEN2;
      break;
    case RX_LEN2:
      _rxLen |= (size_t)b << 7;
      _rxLen2 = b;   // part of the checksum
      _rxState = _rxLen ? RX_INFO : RX_FCS;
      if (_rxLen > RX_FRAME_MAX) { _rxState = RX_FLAG; _badFrames++; }
      break;
    case RX_INFO:
      _rxBuf[_rxPos++] = b;
      if (_rxPos >= _rxLen) _rxState = RX_FCS;
      break;
    case RX_FCS: {
      uint8_t hdr[4] = { _rxAddr, _rxCtrl, _rxLen1, _rxLen2 };
      size_t h = (_rxLen1 & 0x01) ? 3 : 4;
      if (fcs(hdr, h) != b) {
        _badFrames++;
        _rxState = RX_FLAG;
        break;
      }
      _rxState = RX_END;
      break;
    }
    case RX_END:
      // The closing flag doubles as the next opening one
      if (b == MUX_FLAG) {
        handleFrame();
        _rxState = RX_ADDR;
      } else {
        _badFrames++;
        _rxState = RX_FLAG;
      }
      break;
  }
}

void ModemMux::handleFrame() {
  uint8_t dlci = _rxAddr >> 2;
  uint8_t type = _rxCtrl & ~MUX_PF;
  _rxBytes += _rxLen;

  switch (type) {
    case MUX_UA:
      if (dlci < 8) _uaSeen |= 1 << dlci;
      return;
    case MUX_DM:
      return;
    case MUX_SABM:
      sendFrame(dlci, MUX_UA | MUX_PF, false, nullptr, 0);
      return;
    case MUX_DISC:
      sendFrame(dlci, MUX_UA | MUX_PF, false, nullptr, 0);
      if (dlci == 0) _active = false;
      return;
    case MUX_UIH:
    case MUX_UI:
      break;
    default:
      return;
  }

  if (dlci == 0) {
    handleControl(_rxBuf, _rxLen);
  } else if (dlci == 1) {
    for (size_t i = 0; i < _rxLen; i++) {
      size_t next = (_atHead + 1) % AT_RING;
      if (next == _atTail) { _badFrames++; break; }   // AT side not reading
      _atRing[_atHead] = _rxBuf[i];
      _atHead = next;
    }
  } else if (dlci == 2) {
    if (_pppMode && _sink) {
      _sink(_rxBuf, _rxLen);
    } else {
      for (size_t i = 0; i < _rxLen; i++) {
        size_t next = (_dataHead + 1) % DATA_RING;
        if (next == _dataTail) break;
        _dataRing[_dataHead] = _rxBuf[i];
        _dataHead = next;
      }
    }
  }
}

void ModemMux::handleControl(const uint8_t* info, size_t len) {
  if (len < 2) return;
  uint8_t type = info[0];
  if (!(type & MUX_MSG_CR)) return;          // a response to us: nothing to do
  uint8_t base = type & ~MUX_MSG_CR;
  if (base == MUX_MSG_MSC || base == MUX_MSG_TEST) {
    // Answer with the same values
    uint8_t resp[16];
    size_t n = len > sizeof(resp) ? sizeof(resp) : len;
    memcpy(resp, info, n);
    resp[0] = base;
    sendFrame(0, MUX_UIH, false, resp, n);
  } else if (base == MUX_MSG_CLD) {
    uint8_t resp[2] = { MUX_MSG_CLD, 0x01 };
    sendFrame(0, MUX_UIH, false, resp, 2);
    _active = false;
  }
}

int ModemMux::atAvailable() {
  return (int)((_atHead + AT_RING - _atTail) % AT_RING);
}

int ModemMux::atRead() {
  if (_atHead == _atTail) return -1;
  uint8_t b = _atRing[_atTail];
  _atTail = (_atTail + 1) % AT_RING;
  return b;
}

int ModemMux::atPeek() {
  if (_atHead == _atTail) return -1;
  return _atRing[_atTail];
}

int ModemMux::dataAvailable() {
  return (int)((_dataHead + DATA_RING - _dataTail) % DATA_RING);
}

int ModemMux::dataRead() {
  if (_dataHead == _dataTail) return -1;
  uint8_t b = _dataRing[_dataTail];
  _dataTail = (_dataTail + 1) % DATA_RING;
  return b;
}

// ---------------------------------------------------------------------------

int ModemAtPort::available() {
  if (!_mux.isActive()) return _uart.available();
  _mux.pump();
  return _mux.atAvailable();
}

int ModemAtPort::read() {
  if (!_mux.isActive()) return _uart.read();
  if (!_mux.atAvailable()) _mux.pump();
  return _mux.atRead();
}

int ModemAtPort::peek() {
  if (!_mux.isActive()) return _uart.peek();
  if (!_mux.atAvailable()) _mux.pump();
  return _mux.atPeek();
}

size_t ModemAtPort::write(const uint8_t* buf, size_t len) {
  if (!_mux.isActive()) return _uart.write(buf, len);
  return _mux.send(1, buf, len) ? len : 0;
}

#endif // HAS_4G_MODEM && MODEM_DATA
