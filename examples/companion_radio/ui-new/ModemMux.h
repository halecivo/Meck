#pragma once

// =============================================================================
// ModemMux - GSM 07.10 (3GPP TS 27.010) basic mode multiplexer for the A7682E
//
// Mobile data needs the UART for PPP, while SMS, calls and tones still need
// AT commands. AT+CMUX splits the one UART into virtual channels:
//   DLC 0  control (open/close, modem status)
//   DLC 1  AT commands and URCs: ModemAtPort, what ModemManager talks to
//   DLC 2  ATD*99# and then PPP frames for the esp_netif PPP interface
//
// Without the mux ModemAtPort passes straight through to the UART, so the
// AT code in ModemManager is the same in both modes.
//
// Threads: pump() (reading the UART) runs on the modem task only; send()
// may also be called from the lwIP thread (PPP output) and takes a mutex
// so frames never interleave.
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ModemMux {
public:
  typedef void (*DataSink)(const uint8_t* data, size_t len);

  void init(HardwareSerial* uart);

  // After AT+CMUX=... answered OK: open DLC 0, 1 and 2. infoMax is the N1
  // the modem accepted. False (and inactive) when the modem did not answer.
  bool open(size_t infoMax);
  // Close the channels and leave mux mode; the UART carries plain AT again
  void close();
  bool isActive() const { return _active; }

  // Read what the UART has and hand each frame to its channel
  void pump();

  // Write len bytes to a channel as one or more UIH frames
  bool send(uint8_t dlci, const uint8_t* data, size_t len);

  // DLC 1 (AT) receive ring
  int  atAvailable();
  int  atRead();
  int  atPeek();

  // DLC 2: before PPP its text (CONNECT, NO CARRIER...) goes to a small ring,
  // in PPP mode every frame goes to the sink
  void setDataSink(DataSink sink) { _sink = sink; }
  void setPppMode(bool on) { _pppMode = on; }
  bool pppMode() const { return _pppMode; }
  int  dataAvailable();
  int  dataRead();

  // Frames with a bad checksum or that did not fit (debug CLI)
  uint32_t badFrames() const { return _badFrames; }
  uint32_t rxBytes() const { return _rxBytes; }
  uint32_t txBytes() const { return _txBytes; }

private:
  static const size_t RX_FRAME_MAX = 1600;   // modem N1 is at most 1500
  static const size_t AT_RING  = 2048;
  static const size_t DATA_RING = 256;

  HardwareSerial* _uart = nullptr;
  SemaphoreHandle_t _txMutex = nullptr;
  volatile bool _active = false;
  volatile bool _pppMode = false;
  size_t _infoMax = 31;
  DataSink _sink = nullptr;
  uint8_t _uaSeen = 0;      // bit per DLCI: UA received for our SABM

  // Frame parser
  enum RxState : uint8_t { RX_FLAG, RX_ADDR, RX_CTRL, RX_LEN1, RX_LEN2, RX_INFO, RX_FCS, RX_END };
  RxState _rxState = RX_FLAG;
  uint8_t _rxAddr = 0, _rxCtrl = 0, _rxLen1 = 0, _rxLen2 = 0;
  size_t  _rxLen = 0, _rxPos = 0;
  uint8_t* _rxBuf = nullptr;

  uint8_t _atRing[AT_RING];
  volatile size_t _atHead = 0, _atTail = 0;
  uint8_t _dataRing[DATA_RING];
  volatile size_t _dataHead = 0, _dataTail = 0;

  uint32_t _badFrames = 0, _rxBytes = 0, _txBytes = 0;

  void feed(uint8_t b);
  void handleFrame();
  void handleControl(const uint8_t* info, size_t len);
  bool sendFrame(uint8_t dlci, uint8_t ctrl, bool command, const uint8_t* info, size_t len);
  bool openDlc(uint8_t dlci);
  void sendMsc(uint8_t dlci);
  static uint8_t fcs(const uint8_t* p, size_t n);
};

// Stream for ModemManager's AT code: DLC 1 while the mux is on, the UART
// otherwise
class ModemAtPort : public Stream {
public:
  ModemAtPort(HardwareSerial& uart, ModemMux& mux) : _uart(uart), _mux(mux) {}
  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t len) override;
  void flush() override {}
  using Print::write;
private:
  HardwareSerial& _uart;
  ModemMux& _mux;
};
