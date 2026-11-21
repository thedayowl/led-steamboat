// types.h - basic types shared by config.h and the firmware
#pragma once
#include <stdint.h>

// What a section does
enum Role : uint8_t {
  ROLE_STATIC = 0,   // solid color
  ROLE_WATER  = 1,   // moving blue water (uses WATER_COLORS, section color ignored)
  ROLE_SMOKE  = 2,   // smoke puff; "group" = puff number (0 = nearest the stack)
  ROLE_SPOKE  = 3    // paddle wheel spoke set; "group" = set number in rotation order
};

// Byte order the pixels expect. 12 mm WS2811 nodes are usually RGB,
// WS2812B strip is usually GRB. Can be changed at runtime ("chan 1 order GRB").
enum ColorOrder : uint8_t {
  ORDER_RGB = 0, ORDER_RBG, ORDER_GRB, ORDER_GBR, ORDER_BRG, ORDER_BGR, ORDER_COUNT
};

// LED chip on a channel: sets the data timing. Changing it at runtime also
// sets that chip's usual color order ("chan 1 type ws2812b").
enum LedType : uint8_t {
  LED_WS2811 = 0,      // WS2811 at 800 kHz (12 V bullet-node strings)
  LED_WS2812B,         // WS2812/WS2812B, also WS2813, WS2815, SK6812 (RGB)
  LED_WS2811_400K,     // WS2811 wired for 400 kHz "slow" mode (older strings)
  LED_TYPE_COUNT
};

// One entry in the default section table (config.h)
struct SectionDef {
  const char* name;   // shown in console / web page; must be unique
  Role        role;
  uint8_t     group;  // puff number or spoke set number (0-based); 0 for others
  const char* leds;   // LED runs, e.g. "1:0-89" or "1:90-120, 1:200-170, 2:5-9"
  uint32_t    color;  // 0xRRGGBB
};
