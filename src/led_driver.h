// led_driver.h - WS2811/WS2812 output on up to 8 channels using the ESP32 RMT
// peripheral. All channels transmit at the same time.
// Works with Arduino-ESP32 core 2.x (ESP-IDF 4.4) and core 3.x (ESP-IDF 5).
#pragma once
#include <stdint.h>

namespace Leds {
  // Call once. pins[ch] < 0 leaves that channel off (it still has a buffer).
  // Returns false if any channel with a pin failed to start.
  bool     begin(const int8_t* pins, uint8_t numChannels);
  uint8_t  numChannels();
  int8_t   pin(uint8_t ch);      // GPIO, -1 if the channel is off
  void     setCount(uint8_t ch, uint16_t count);
  uint16_t count(uint8_t ch);
  // LED chip (LedType in types.h): sets the bit timing. Takes effect at the next show().
  void     setType(uint8_t ch, uint8_t type);
  uint8_t* buffer(uint8_t ch);   // RGB, 3 bytes per LED, MAX_LEDS_PER_CHANNEL long
  // Apply brightness and per-channel color order, then transmit all channels.
  void     show(uint8_t brightness, const uint8_t* colorOrders);
  // Stop driving a channel's GPIO (so the pin can be used for something else)
  // and take it back again.
  void     releasePin(uint8_t ch);
  bool     attachPin(uint8_t ch);
  bool     attached(uint8_t ch);
  bool     ok(uint8_t ch);       // channel initialised
}
