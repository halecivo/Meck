#pragma once

#include <RadioLib.h>

#define SX126X_IRQ_HEADER_VALID                     0b0000010000  //  4     4     valid LoRa header received
#define SX126X_IRQ_PREAMBLE_DETECTED           0x04

class CustomSX1262 : public SX1262 {
  public:
    CustomSX1262(Module *mod) : SX1262(mod) { }

  #ifdef RP2040_PLATFORM
    bool std_init(SPIClassRP2040* spi = NULL)
  #else
    bool std_init(SPIClass* spi = NULL)
  #endif
    {
  #ifdef SX126X_DIO3_TCXO_VOLTAGE
      float tcxo = SX126X_DIO3_TCXO_VOLTAGE;
  #else
      float tcxo = 1.6f;
  #endif

  #ifdef LORA_CR
      uint8_t cr = LORA_CR;
  #else
      uint8_t cr = 5;
  #endif

  #if defined(P_LORA_SCLK)
    #if defined(RP2040_PLATFORM)
      if (spi) {
        spi->setMISO(P_LORA_MISO);
        //spi->setCS(P_LORA_NSS); // Setting CS results in freeze
        spi->setSCK(P_LORA_SCLK);
        spi->setMOSI(P_LORA_MOSI);
        spi->begin();
      }
    #else
      if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    #endif
  #endif
      int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
      // if radio init fails with -707/-706, try again with tcxo voltage set to 0.0f
      if (status == RADIOLIB_ERR_SPI_CMD_FAILED || status == RADIOLIB_ERR_SPI_CMD_INVALID) {
        #define SX126X_DIO3_TCXO_VOLTAGE (0.0f);
        tcxo = SX126X_DIO3_TCXO_VOLTAGE;
        status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
      }
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false;  // fail
      }
    
      setCRC(1);
  
  #ifdef SX126X_CURRENT_LIMIT
      setCurrentLimit(SX126X_CURRENT_LIMIT);
  #endif
  #ifdef SX126X_DIO2_AS_RF_SWITCH
      setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
  #endif
  #ifdef SX126X_RX_BOOSTED_GAIN
      setRxBoostedGainMode(SX126X_RX_BOOSTED_GAIN);
  #endif
  #if defined(SX126X_RXEN) || defined(SX126X_TXEN)
    #ifndef SX126X_RXEN
      #define SX126X_RXEN RADIOLIB_NC
    #endif
    #ifndef SX126X_TXEN
      #define SX126X_TXEN RADIOLIB_NC
    #endif
      setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
  #endif 

      return true;  // success
    }

    // Receive with the PREAMBLE_DETECTED IRQ reported as well (RadioLib's
    // default flags leave it out), so isReceiving() sees a packet starting.
    // Also refreshes the activity deadlines from the current modem settings.
    int16_t startReceive() override {
      updateActivityDeadlines();
      return SX1262::startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF,
                                  RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED),
                                  RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    // True while a packet is arriving (listen before talk). A preamble that
    // never turns into a header, or a header whose packet never completes,
    // would otherwise keep this true and hold back transmitting; such flags
    // are cleared once they are older than the longest valid packet allows.
    // From MeshCore #2977, #3000, #3036, #3062.
    bool isReceiving() {
      uint32_t irq = getIrqFlags();
      bool preamble = irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED;
      bool header   = irq & RADIOLIB_SX126X_IRQ_HEADER_VALID;
      bool hdrErr   = irq & RADIOLIB_SX126X_IRQ_HEADER_ERR;
      const uint32_t activityFlags = RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_HEADER_VALID |
                                     RADIOLIB_SX126X_IRQ_HEADER_ERR | RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID;
      uint32_t now = millis();
      if (hdrErr) {
        clearIrqFlags(activityFlags);
        _activityAt = 0; _headerSeen = false;
        return false;
      }
      if (!header && _headerSeen) {   // something else cleared the header flag
        _activityAt = 0; _headerSeen = false;
        return false;
      }
      if (header) {
        if (!_headerSeen) { _headerSeen = true; _activityAt = now; }
        if (now - _activityAt > _maxPayloadMillis) {
          clearIrqFlags(activityFlags);
          _activityAt = 0; _headerSeen = false;
          return false;
        }
        return true;
      }
      if (preamble) {
        if (_activityAt == 0) _activityAt = now;
        if (now - _activityAt > _preambleMillis) {
          clearIrqFlags(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
          _activityAt = 0;
          return false;
        }
        return true;
      }
      _activityAt = 0; _headerSeen = false;
      return false;
    }

  private:
    uint32_t _preambleMillis = 66;     // preamble detected -> header valid, at most
    uint32_t _maxPayloadMillis = 3934; // header valid -> packet done, at most
    uint32_t _activityAt = 0;
    bool _headerSeen = false;

    // The two deadlines for the current SF / bandwidth / coding rate /
    // preamble, as MeshCore's RadioLibWrapper::calcMaxPacketMillis()
    // computes them (from RadioLib's time-on-air formula). Computing them
    // here keeps them right after any change of radio settings.
    void updateActivityDeadlines() {
      if (spreadingFactor == 0 || bandwidthKhz <= 0) return;   // not configured yet
      uint32_t tsym_us = ((uint32_t)10000 << spreadingFactor) / (uint32_t)(bandwidthKhz * 10);
      uint32_t sfCoeff1_x4 = (spreadingFactor == 5 || spreadingFactor == 6) ? 25 : 17;
      // preamble + sync word + SFD + header
      uint32_t preamble_us = (((uint32_t)(preambleLengthLoRa + 8) * 4 + sfCoeff1_x4) * tsym_us) / 4;
      // the rest of a maximum-size packet at the current settings
      uint32_t total_us = getTimeOnAir(255);
      uint32_t payload_us = total_us > preamble_us ? total_us - preamble_us : 4000000;
      // scaled to the slowest coding rate (4/8) a sender might use
      uint8_t cr = codingRate + 4;   // RadioLib stores 1..4 for 4/5..4/8
      if (cr >= 5 && cr < 8) payload_us = (payload_us * 8) / cr;
      _preambleMillis = (preamble_us + 999) / 1000;
      _maxPayloadMillis = (payload_us + 999) / 1000;
    }
};