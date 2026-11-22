// led_driver.cpp - see led_driver.h
#include <Arduino.h>
#include <string.h>
#include "esp_idf_version.h"
#include "config.h"
#include "led_driver.h"

#if ESP_IDF_VERSION_MAJOR >= 5
  #include "driver/rmt_tx.h"
  #include "driver/rmt_encoder.h"
#else
  #include "driver/rmt.h"
#endif

namespace Leds {

static const uint8_t kMaxCh = MAX_CHANNELS;

static uint8_t  s_numCh = 0;
static int8_t   s_pins[kMaxCh];
static uint8_t  s_type[kMaxCh];       // timing the hardware is set up for
static uint8_t  s_wantType[kMaxCh];   // timing asked for; applied at the start of show()
static uint16_t s_count[kMaxCh];
static bool     s_ok[kMaxCh];
static bool     s_attached[kMaxCh];
static const size_t kBufBytes = MAX_LEDS_PER_CHANNEL * 3;
static uint8_t* s_render[kMaxCh];     // what effects draw into (kBufBytes each, on the heap)
static uint8_t* s_out[kMaxCh];        // what goes on the wire

// Byte positions of R,G,B in the wire order for each ColorOrder
static const uint8_t kOrderMap[ORDER_COUNT][3] = {
  {0, 1, 2},  // RGB: r->0 g->1 b->2
  {0, 2, 1},  // RBG
  {1, 0, 2},  // GRB
  {2, 0, 1},  // GBR
  {1, 2, 0},  // BRG
  {2, 1, 0},  // BGR
};

// Bit timings in nanoseconds, high then low, per LedType. Same values NeoPixelBus
// (and so WLED) uses for these chips.
struct BitTiming { uint16_t t0h, t0l, t1h, t1l; };
static const BitTiming kTiming[LED_TYPE_COUNT] = {
  { 300,  950,  900,  350 },   // WS2811 800 kHz    (1.25 us bit)
  { 400,  850,  800,  450 },   // WS2812B and kin   (1.25 us bit)
  { 800, 1700, 1600,  900 },   // WS2811 400 kHz    (2.5 us bit)
};

#if ESP_IDF_VERSION_MAJOR >= 5
// ---------------------------------------------------------------- ESP-IDF 5
// 20 MHz resolution: 1 tick = 50 ns, so every timing above is a whole number of ticks.
static const uint32_t kTickNs = 50;
static rmt_channel_handle_t s_rmt[kMaxCh];
static rmt_encoder_handle_t s_enc[kMaxCh];

static bool createChannel(uint8_t ch) {
  rmt_tx_channel_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.gpio_num          = (gpio_num_t)s_pins[ch];
  cfg.clk_src           = RMT_CLK_SRC_DEFAULT;
  cfg.resolution_hz     = 1000000000UL / kTickNs;
  // 2 blocks per channel when there are 4 or fewer (fewer glitches when Wi-Fi
  // is busy); the ESP32 has 8 blocks in all, so more channels get 1 each.
  cfg.mem_block_symbols = s_numCh <= 4 ? 128 : 64;
  cfg.trans_queue_depth = 2;
  if (rmt_new_tx_channel(&cfg, &s_rmt[ch]) != ESP_OK) {
    cfg.mem_block_symbols = 64;
    if (rmt_new_tx_channel(&cfg, &s_rmt[ch]) != ESP_OK) { s_rmt[ch] = NULL; return false; }
  }
  if (rmt_enable(s_rmt[ch]) != ESP_OK) { rmt_del_channel(s_rmt[ch]); s_rmt[ch] = NULL; return false; }
  return true;
}

static bool makeEncoder(uint8_t ch, uint8_t type) {
  const BitTiming& t = kTiming[type];
  rmt_bytes_encoder_config_t ec;
  memset(&ec, 0, sizeof(ec));
  ec.bit0.level0 = 1; ec.bit0.duration0 = t.t0h / kTickNs;
  ec.bit0.level1 = 0; ec.bit0.duration1 = t.t0l / kTickNs;
  ec.bit1.level0 = 1; ec.bit1.duration0 = t.t1h / kTickNs;
  ec.bit1.level1 = 0; ec.bit1.duration1 = t.t1l / kTickNs;
  ec.flags.msb_first = 1;
  return rmt_new_bytes_encoder(&ec, &s_enc[ch]) == ESP_OK;
}

static bool hwInit(uint8_t ch) {
  if (!makeEncoder(ch, s_type[ch])) { s_enc[ch] = NULL; return false; }
  return createChannel(ch);
}

// Only called between frames, when the channel is idle.
static bool hwSetType(uint8_t ch, uint8_t type) {
  rmt_encoder_handle_t old = s_enc[ch];
  if (!makeEncoder(ch, type)) { s_enc[ch] = old; return false; }   // keep the old timing
  if (old) rmt_del_encoder(old);
  return true;
}

static void hwStart(uint8_t ch, const uint8_t* data, size_t len) {
  rmt_transmit_config_t tc;
  memset(&tc, 0, sizeof(tc));
  tc.loop_count = 0;
  rmt_transmit(s_rmt[ch], s_enc[ch], data, len, &tc);
}

static void hwWait(uint8_t ch) { rmt_tx_wait_all_done(s_rmt[ch], 100); }

static void hwRelease(uint8_t ch) {
  if (s_rmt[ch]) {
    rmt_tx_wait_all_done(s_rmt[ch], 100);
    rmt_disable(s_rmt[ch]);
    rmt_del_channel(s_rmt[ch]);
    s_rmt[ch] = NULL;
  }
}

static bool hwAttach(uint8_t ch) { return s_rmt[ch] ? true : createChannel(ch); }

#else
// ---------------------------------------------------------------- ESP-IDF 4.4
// APB 80 MHz / 2 = 40 MHz: 1 tick = 25 ns.
// Up to 4 outputs use RMT channels 0,2,4,6 so each gets two memory blocks;
// more outputs use channels 0-7 with one block each.
static rmt_channel_t rmtCh(uint8_t ch) { return (rmt_channel_t)(s_numCh <= 4 ? ch * 2 : ch); }
static const uint32_t kTickNs = 25;
static rmt_item32_t s_bit0[kMaxCh], s_bit1[kMaxCh];   // read by the translator

// The translator gets no channel argument, so there is one copy per channel.
template <int CH>
static void IRAM_ATTR rmtAdapter(const void* src, rmt_item32_t* dest, size_t src_size,
                                 size_t wanted_num, size_t* translated_size, size_t* item_num) {
  if (src == NULL || dest == NULL) { *translated_size = 0; *item_num = 0; return; }
  const rmt_item32_t bit0 = s_bit0[CH], bit1 = s_bit1[CH];
  size_t size = 0, num = 0;
  const uint8_t* psrc = (const uint8_t*)src;
  rmt_item32_t* pdest = dest;
  while (size < src_size && num < wanted_num) {
    uint8_t b = *psrc;
    for (int i = 0; i < 8; i++) {
      pdest->val = (b & (1 << (7 - i))) ? bit1.val : bit0.val;
      num++; pdest++;
    }
    size++; psrc++;
  }
  *translated_size = size;
  *item_num = num;
}

static const sample_to_rmt_t kAdapters[8] = { rmtAdapter<0>, rmtAdapter<1>, rmtAdapter<2>, rmtAdapter<3>,
                                              rmtAdapter<4>, rmtAdapter<5>, rmtAdapter<6>, rmtAdapter<7> };

// Only called between frames, when the channel is idle.
static bool hwSetType(uint8_t ch, uint8_t type) {
  const BitTiming& t = kTiming[type];
  s_bit0[ch].level0 = 1; s_bit0[ch].duration0 = t.t0h / kTickNs;
  s_bit0[ch].level1 = 0; s_bit0[ch].duration1 = t.t0l / kTickNs;
  s_bit1[ch].level0 = 1; s_bit1[ch].duration0 = t.t1h / kTickNs;
  s_bit1[ch].level1 = 0; s_bit1[ch].duration1 = t.t1l / kTickNs;
  return true;
}

static bool hwInit(uint8_t ch) {
  hwSetType(ch, s_type[ch]);
  rmt_config_t c = RMT_DEFAULT_CONFIG_TX((gpio_num_t)s_pins[ch], rmtCh(ch));
  c.clk_div = 2;
  c.mem_block_num = s_numCh <= 4 ? 2 : 1;
  c.tx_config.idle_output_en = true;
  c.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  if (rmt_config(&c) != ESP_OK) return false;
  if (rmt_driver_install(c.channel, 0, 0) != ESP_OK) return false;
  if (rmt_translator_init(c.channel, kAdapters[ch]) != ESP_OK) return false;
  return true;
}

static void hwStart(uint8_t ch, const uint8_t* data, size_t len) {
  rmt_write_sample(rmtCh(ch), data, len, false);
}

static void hwWait(uint8_t ch) { rmt_wait_tx_done(rmtCh(ch), pdMS_TO_TICKS(100)); }

static void hwRelease(uint8_t ch) {
  // The channel stays configured; whoever takes the pin re-routes the GPIO
  // matrix. We just stop transmitting (see show()).
  rmt_wait_tx_done(rmtCh(ch), pdMS_TO_TICKS(100));
}

static bool hwAttach(uint8_t ch) {
  return rmt_set_gpio(rmtCh(ch), RMT_MODE_TX, (gpio_num_t)s_pins[ch], false) == ESP_OK;
}
#endif

// ---------------------------------------------------------------- common

bool begin(const int8_t* pins, uint8_t numChannels) {
  if (s_numCh) return false;   // once only
  if (numChannels > kMaxCh) numChannels = kMaxCh;
  // One block for every channel's two buffers.
  uint8_t* mem = numChannels ? (uint8_t*)calloc(numChannels, 2 * kBufBytes) : NULL;
  if (!mem) return false;
  s_numCh = numChannels;
  bool all = true;
  for (uint8_t ch = 0; ch < s_numCh; ch++) {
    s_render[ch] = mem + (2 * ch) * kBufBytes;
    s_out[ch]    = mem + (2 * ch + 1) * kBufBytes;
    s_pins[ch] = pins[ch];
    s_type[ch] = s_wantType[ch];   // LED_WS2811 unless setType() was called first
    s_count[ch] = 0;
    s_ok[ch] = s_pins[ch] >= 0 && hwInit(ch);
    s_attached[ch] = s_ok[ch];
    if (s_pins[ch] >= 0 && !s_ok[ch]) all = false;
  }
  return all;
}

uint8_t numChannels() { return s_numCh; }
int8_t  pin(uint8_t ch) { return ch < s_numCh && s_ok[ch] ? s_pins[ch] : -1; }

void setCount(uint8_t ch, uint16_t count) {
  if (ch >= s_numCh) return;
  if (count > MAX_LEDS_PER_CHANNEL) count = MAX_LEDS_PER_CHANNEL;
  if (count > s_count[ch]) memset(s_render[ch] + s_count[ch] * 3, 0, (count - s_count[ch]) * 3);
  s_count[ch] = count;
}

void setType(uint8_t ch, uint8_t type) {
  if (ch < kMaxCh) s_wantType[ch] = type < LED_TYPE_COUNT ? type : (uint8_t)LED_WS2811;
}

uint16_t count(uint8_t ch) { return ch < s_numCh ? s_count[ch] : 0; }
uint8_t* buffer(uint8_t ch) { return ch < s_numCh ? s_render[ch] : NULL; }
bool     attached(uint8_t ch) { return ch < s_numCh && s_attached[ch]; }
bool     ok(uint8_t ch) { return ch < s_numCh && s_ok[ch]; }

void show(uint8_t brightness, const uint8_t* colorOrders) {
  uint16_t scale = (uint16_t)brightness + 1;
  // Every channel finished at the end of the last show(), so timing can change now.
  for (uint8_t ch = 0; ch < s_numCh; ch++)
    if (s_ok[ch] && s_wantType[ch] != s_type[ch] && hwSetType(ch, s_wantType[ch])) s_type[ch] = s_wantType[ch];
  for (uint8_t ch = 0; ch < s_numCh; ch++) {
    if (!s_ok[ch] || !s_attached[ch] || s_count[ch] == 0) continue;
    uint8_t ord = colorOrders[ch] < ORDER_COUNT ? colorOrders[ch] : (uint8_t)ORDER_RGB;
    const uint8_t* m = kOrderMap[ord];
    const uint8_t* src = s_render[ch];
    uint8_t* dst = s_out[ch];
    for (uint16_t i = 0; i < s_count[ch]; i++, src += 3, dst += 3) {
      dst[m[0]] = (uint8_t)((src[0] * scale) >> 8);
      dst[m[1]] = (uint8_t)((src[1] * scale) >> 8);
      dst[m[2]] = (uint8_t)((src[2] * scale) >> 8);
    }
  }
  // Start every channel, then wait for all of them: they run in parallel.
  for (uint8_t ch = 0; ch < s_numCh; ch++)
    if (s_ok[ch] && s_attached[ch] && s_count[ch]) hwStart(ch, s_out[ch], s_count[ch] * 3);
  for (uint8_t ch = 0; ch < s_numCh; ch++)
    if (s_ok[ch] && s_attached[ch] && s_count[ch]) hwWait(ch);
}

void releasePin(uint8_t ch) {
  if (ch >= s_numCh || !s_ok[ch] || !s_attached[ch]) return;
  hwRelease(ch);
  s_attached[ch] = false;
}

bool attachPin(uint8_t ch) {
  if (ch >= s_numCh || !s_ok[ch]) return false;
  if (s_attached[ch]) return true;
  s_attached[ch] = hwAttach(ch);
  return s_attached[ch];
}

}  // namespace Leds
