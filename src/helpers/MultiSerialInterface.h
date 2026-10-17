#pragma once

// Several companion connections at once (e.g. Bluetooth and WiFi), one app
// client on each. Ported from MeshCore's MultiSerialInterface (e672679d,
// companion-v1.18.0) with these changes for Meck:
//
// - writeFrame() writes to the connected interfaces only and succeeds when
//   at least one write does. MeshCore writes to every enabled one and fails
//   unless all succeed, but an enabled interface with no client returns 0,
//   so with Bluetooth connected and WiFi merely on every write would "fail"
//   and the contact sync (which retries a failed write) would stall.
// - checkRecvFrame() takes turns between the interfaces, so a busy one
//   cannot starve the others.
// - isEnabled() is true while any interface is on (there is no separate
//   switch for the whole set), and enable() / disable() switch all of them.
// - Meck's interfaces have no loop(); they do their housekeeping (connect /
//   disconnect, advertising restart) in checkRecvFrame(), so every enabled
//   interface is polled on each call until one returns a frame.
//
// Replies go to every connected client. The companion protocol is meant
// for one client, so two apps at once can see each other's replies (as in
// MeshCore).

#include "BaseSerialInterface.h"

#ifndef MAX_INTERFACES
  #define MAX_INTERFACES 4   // ble, usb, wifi, ethernet
#endif

enum class InterfaceType : uint8_t {
  NONE,
  Bluetooth,
  USB,
  WiFi,
  Ethernet,
  HardwareSerial
};

class MultiSerialInterface : public BaseSerialInterface {
  struct RegisteredInterface {
    InterfaceType type = InterfaceType::NONE;
    BaseSerialInterface* instance = nullptr;
  };

  RegisteredInterface _interfaces[MAX_INTERFACES] = {};
  int _nextRead = 0;   // interface checkRecvFrame() asks first

public:
  bool addInterface(InterfaceType type, BaseSerialInterface* iface) {
    if (iface == nullptr) return false;
    for (int i = 0; i < MAX_INTERFACES; i++) {
      if (_interfaces[i].instance == nullptr) {
        _interfaces[i].instance = iface;
        _interfaces[i].type = type;
        return true;
      }
    }
    return false;   // no free slot
  }

  bool removeInterface(BaseSerialInterface* iface) {
    if (iface == nullptr) return false;
    for (int i = 0; i < MAX_INTERFACES; i++) {
      if (_interfaces[i].instance == iface) {
        _interfaces[i] = {};
        return true;
      }
    }
    return false;
  }

  // The interface of a type, or nullptr
  BaseSerialInterface* get(InterfaceType type) const {
    for (const auto& iface : _interfaces) {
      if (iface.instance && iface.type == type) return iface.instance;
    }
    return nullptr;
  }

  void enableBluetooth() { if (auto i = get(InterfaceType::Bluetooth)) i->enable(); }
  void disableBluetooth() { if (auto i = get(InterfaceType::Bluetooth)) i->disable(); }
  bool isBluetoothEnabled() const { auto i = get(InterfaceType::Bluetooth); return i && i->isEnabled(); }
  bool isBluetoothConnected() const { auto i = get(InterfaceType::Bluetooth); return i && i->isConnected(); }
  bool isWiFiEnabled() const { auto i = get(InterfaceType::WiFi); return i && i->isEnabled(); }
  bool isWiFiConnected() const { auto i = get(InterfaceType::WiFi); return i && i->isConnected(); }

  void enable() override {
    for (auto& iface : _interfaces) if (iface.instance) iface.instance->enable();
  }

  void disable() override {
    for (auto& iface : _interfaces) if (iface.instance) iface.instance->disable();
  }

  bool isEnabled() const override {
    for (const auto& iface : _interfaces) {
      if (iface.instance && iface.instance->isEnabled()) return true;
    }
    return false;
  }

  bool isConnected() const override {
    for (const auto& iface : _interfaces) {
      if (iface.instance && iface.instance->isEnabled() && iface.instance->isConnected()) return true;
    }
    return false;
  }

  bool isWriteBusy() const override {
    for (const auto& iface : _interfaces) {
      if (iface.instance && iface.instance->isEnabled() && iface.instance->isConnected() &&
          iface.instance->isWriteBusy()) return true;
    }
    return false;
  }

  bool hasPendingData() const override {
    for (const auto& iface : _interfaces) {
      if (iface.instance && iface.instance->isEnabled() && iface.instance->isConnected() &&
          iface.instance->hasPendingData()) return true;
    }
    return false;
  }

  size_t writeFrame(const uint8_t src[], size_t len) override {
    if (len == 0) return 0;
    bool anyOk = false;
    for (auto& iface : _interfaces) {
      if (iface.instance && iface.instance->isEnabled() && iface.instance->isConnected()) {
        if (iface.instance->writeFrame(src, len) == len) anyOk = true;
      }
    }
    return anyOk ? len : 0;
  }

  size_t checkRecvFrame(uint8_t dest[]) override {
    for (int n = 0; n < MAX_INTERFACES; n++) {
      int i = (_nextRead + n) % MAX_INTERFACES;
      auto& iface = _interfaces[i];
      if (iface.instance && iface.instance->isEnabled()) {
        size_t frameSize = iface.instance->checkRecvFrame(dest);
        if (frameSize > 0) {
          _nextRead = (i + 1) % MAX_INTERFACES;   // the next call asks the others first
          return frameSize;
        }
      }
    }
    return 0;
  }
};
