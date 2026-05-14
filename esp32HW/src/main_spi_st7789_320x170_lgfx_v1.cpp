#include <Arduino.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <math.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>

static constexpr size_t SERIAL_LINE_BUFFER_SIZE = 512;
static constexpr unsigned long DATA_STALE_TIMEOUT_MS = 3000;
static constexpr unsigned long FRAME_INTERVAL_MS = 33;
static constexpr const char* FIRMWARE_VERSION = "spi_st7789_320x172_lgfx_v2";
static constexpr int HISTORY_CAPACITY = 600;
static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 170;
static constexpr int EDGE_GUARD_ROWS = 0;
static constexpr uint8_t DISPLAY_BRIGHTNESS = 140;
static constexpr uint8_t BRIGHTNESS_LEVELS[5] = {50, 85, 120, 170, 240};
static constexpr float DISPLAY_EASE_ALPHA = 0.14f;
static constexpr int HISTORY_PUSH_EVERY_N_FRAMES = 3;
static constexpr int SCREEN_MARGIN_X = 6;
static constexpr int SCREEN_MARGIN_Y = 4;
static constexpr int CARD_GAP_X = 6;
static constexpr int CARD_GAP_Y = 4;
static constexpr int CARD_RADIUS = 10;
static constexpr int CARD_COL_W = 151;
static constexpr int CARD_TOP_H = 24;
static constexpr int CARD_MID_H = 44;
static constexpr int CARD_BOT_H = 84;
static constexpr int CARD_LEFT_X = SCREEN_MARGIN_X;
static constexpr int CARD_RIGHT_X = CARD_LEFT_X + CARD_COL_W + CARD_GAP_X;
static constexpr int CARD_TOP_Y = SCREEN_MARGIN_Y;
static constexpr int CARD_MID_Y = CARD_TOP_Y + CARD_TOP_H + CARD_GAP_Y;
static constexpr int CARD_BOT_Y = CARD_MID_Y + CARD_MID_H + CARD_GAP_Y;
static constexpr int HEADER_H_TOP = 20;
static constexpr int HEADER_H_MID = 18;
static constexpr int HEADER_H_BOT = 20;
static constexpr int TOUCH_BRIGHTNESS_PIN = 1;        // XIAO D0 / GPIO1 (touch capable)
static constexpr int TOUCH_LAYOUT_PIN = 5;            // XIAO D4 / GPIO5 (touch capable)
static constexpr int TOUCH_POWER_PIN = 6;             // XIAO D5 / GPIO6 (touch capable)
static constexpr unsigned long TOUCH_DEBOUNCE_MS = 220;
static constexpr float TOUCH_TRIGGER_DELTA_PCT = 0.10f;  // higher sensitivity for touch-through PLA
static constexpr float TOUCH_RELEASE_DELTA_PCT = 0.05f;
static constexpr float TOUCH_BASELINE_ALPHA = 0.015f;
static constexpr int TOUCH_CALIBRATION_SAMPLES = 24;
static constexpr int WDT_TIMEOUT_SECONDS = 8;
static constexpr uint8_t TOTAL_LAYOUTS = 6;

static constexpr uint16_t COLOR_BG = 0x0000;
static constexpr uint16_t COLOR_CARD = 0x0861;
static constexpr uint16_t COLOR_BORDER = 0x18E3;
static constexpr uint16_t COLOR_TEXT = 0xEF5D;
static constexpr uint16_t COLOR_TEXT_DIM = 0x8C71;
static constexpr uint16_t COLOR_TEMP = 0x035F;  // panel-corrected subtle dark orange
static constexpr uint16_t COLOR_FAN = 0x9CD3;
static constexpr uint16_t COLOR_CPU = 0x3BEA;
static constexpr uint16_t COLOR_UP = 0x4558;
static constexpr uint16_t COLOR_DOWN = 0x9CD3;
static constexpr uint16_t COLOR_WARN_SOFT = 0x053F;  // BGR-swapped form of soft orange on this panel

static const unsigned char image_arrow_down_bits[] = {0x20,0x20,0x20,0x20,0xa8,0x70,0x20};
static const unsigned char image_arrow_up_bits[] = {0x20,0x70,0xa8,0x20,0x20,0x20,0x20};
static const unsigned char image_choice_right_bits[] = {0x03,0xc0,0x0c,0x30,0x11,0x88,0x26,0x64,0x48,0x12,0x50,0x0a,0x90,0x29,0xa4,0x45,0xa2,0x85,0x91,0x09,0x50,0x0a,0x48,0x12,0x26,0x64,0x11,0x88,0x0c,0x30,0x03,0xc0};
static const unsigned char image_earth_bits[] = {0x07,0xc0,0x1e,0x70,0x27,0xf8,0x61,0xe4,0x43,0xe4,0x87,0xca,0x9f,0xf6,0xdf,0x82,0xdf,0x82,0xe3,0xc2,0x61,0xf4,0x70,0xf4,0x31,0xf8,0x1b,0xf0,0x07,0xc0,0x00,0x00};
static const uint16_t image_monitor_pixels[] = {0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0xFFFF,0x0000,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0x0000,0xFFFF,0xFFFF,0x0000,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0x0000,0xFFFF,0xFFFF,0x0000,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0x0000,0xFFFF,0xFFFF,0x0000,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0x0000,0xFFFF,0xFFFF,0x0000,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0x0000,0xFFFF,0xFFFF,0x0000,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0x0000,0xFFFF,0xFFFF,0x0000,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0x0000,0xFFFF,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000};
static const unsigned char image_weather_temperature_bits[] = {0x1c,0x00,0x22,0x02,0x2b,0x05,0x2a,0x02,0x2b,0x38,0x2a,0x60,0x2b,0x40,0x2a,0x40,0x2a,0x60,0x49,0x38,0x9c,0x80,0xae,0x80,0xbe,0x80,0x9c,0x80,0x41,0x00,0x3e,0x00};
static const unsigned char image_weather_wind_bits[] = {0x00,0x00,0x00,0x00,0x00,0x30,0x03,0x88,0x04,0x44,0x04,0x44,0x00,0x44,0x00,0x88,0xff,0x32,0x00,0x00,0xad,0x82,0x00,0x60,0x00,0x10,0x00,0x10,0x01,0x20,0x00,0xc0};

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  LGFX(void) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 7;   // D8
      cfg.pin_mosi = 9;   // D10
      cfg.pin_miso = -1;
      cfg.pin_dc = 3;     // D2
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 2;     // D1
      cfg.pin_rst = 4;    // D3
      cfg.pin_busy = -1;

      // Common ST7789 1.9in IPS mapping (170x320 active area in 240x320 GRAM).
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 172;
      cfg.panel_height = 320;
      cfg.offset_x = 34;
      cfg.offset_y = 0;
      cfg.offset_rotation = 1;

      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = true;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 43;    // D6
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

struct MetricHistory {
  float values[HISTORY_CAPACITY] = {0.0f};
  int count = 0;
  int head = 0;
};

struct TouchButtonState {
  float baseline = 0.0f;
  bool in_contact = false;
  unsigned long last_trigger_ms = 0;
};

struct GraphScaleState {
  bool initialized = false;
  float min_v = 0.0f;
  float max_v = 1.0f;
};

struct HostStats {
  String hostname;
  String ip;
  float cpu_load_percent = 0.0f;
  float net_upload_kbps = 0.0f;
  float net_download_kbps = 0.0f;
  bool has_core_temp = false;
  float core_temp_c = 0.0f;
  bool has_cpu_fan = false;
  float cpu_fan_rpm = 0.0f;
};

LGFX lcd;
lgfx::LGFX_Sprite canvas(&lcd);
char serial_line_buffer[SERIAL_LINE_BUFFER_SIZE];
size_t serial_line_length = 0;
HostStats latest_stats;
bool has_latest_stats = false;
bool showing_offline = true;
unsigned long last_valid_data_ms = 0;
unsigned long last_frame_ms = 0;
unsigned long last_anim_ms = 0;
int history_push_frame_counter = 0;

MetricHistory temp_history;
MetricHistory fan_history;
MetricHistory cpu_history;
MetricHistory up_history;
MetricHistory down_history;
GraphScaleState fan_scale_state;
GraphScaleState temp_scale_state;
GraphScaleState cpu_scale_state;
GraphScaleState net_scale_state;
float target_temp = 0.0f;
float target_fan = 0.0f;
float target_cpu = 0.0f;
float target_up = 0.0f;
float target_down = 0.0f;
float display_temp = 0.0f;
float display_fan = 0.0f;
float display_cpu = 0.0f;
float display_up = 0.0f;
float display_down = 0.0f;
bool display_targets_initialized = false;
bool display_enabled = true;
TouchButtonState brightness_touch;
TouchButtonState layout_touch;
TouchButtonState power_touch;
uint8_t brightness_level_index = 2;
uint8_t active_layout = 0;

void init_watchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
  const esp_task_wdt_config_t twdt_config = {
      .timeout_ms = static_cast<uint32_t>(WDT_TIMEOUT_SECONDS * 1000),
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true,
  };
  esp_task_wdt_init(&twdt_config);
#else
  esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
#endif
  esp_task_wdt_add(NULL);  // watch current loop task
}

void feed_watchdog() {
  esp_task_wdt_reset();
}

void apply_brightness_level() {
  lcd.setBrightness(BRIGHTNESS_LEVELS[brightness_level_index]);
}

void calibrate_touch_baseline(int pin, TouchButtonState& state) {
  uint64_t acc = 0;
  for (int i = 0; i < TOUCH_CALIBRATION_SAMPLES; ++i) {
    acc += touchRead(pin);
    delay(6);
  }
  state.baseline = static_cast<float>(acc) / static_cast<float>(TOUCH_CALIBRATION_SAMPLES);
  state.in_contact = false;
  state.last_trigger_ms = 0;
}

bool handle_touch_button(int pin, TouchButtonState& state) {
  const float raw = static_cast<float>(touchRead(pin));
  if (state.baseline < 1.0f) {
    state.baseline = raw;
    return false;
  }

  const float delta = fabsf(raw - state.baseline);
  const float trigger_delta = state.baseline * TOUCH_TRIGGER_DELTA_PCT;
  const float release_delta = state.baseline * TOUCH_RELEASE_DELTA_PCT;
  const unsigned long now = millis();

  if (!state.in_contact) {
    // Slowly follow ambient drift while idle.
    state.baseline = (state.baseline * (1.0f - TOUCH_BASELINE_ALPHA)) + (raw * TOUCH_BASELINE_ALPHA);
    if (delta >= trigger_delta && (now - state.last_trigger_ms) >= TOUCH_DEBOUNCE_MS) {
      state.in_contact = true;
      state.last_trigger_ms = now;
      return true;
    }
    return false;
  }

  // Stay latched until release window is reached (hysteresis).
  if (delta <= release_delta) {
    state.in_contact = false;
  }
  return false;
}

void handle_touch_brightness_cycle() {
  if (handle_touch_button(TOUCH_BRIGHTNESS_PIN, brightness_touch)) {
    brightness_level_index = static_cast<uint8_t>((brightness_level_index + 1) % 5);
    apply_brightness_level();
  }
}

void handle_touch_layout_cycle() {
  if (handle_touch_button(TOUCH_LAYOUT_PIN, layout_touch)) {
    active_layout = static_cast<uint8_t>((active_layout + 1) % TOTAL_LAYOUTS);
  }
}

void handle_touch_power_cycle() {
  if (!handle_touch_button(TOUCH_POWER_PIN, power_touch)) return;
  if (display_enabled) {
    display_enabled = false;
    lcd.setBrightness(0);
    canvas.fillSprite(COLOR_BG);
    canvas.pushSprite(0, 0);
    return;
  }
  // Second press while display is off: reboot firmware and resume normal boot/render flow.
  ESP.restart();
}

void push_metric(MetricHistory& history, float value) {
  history.values[history.head] = value;
  history.head = (history.head + 1) % HISTORY_CAPACITY;
  if (history.count < HISTORY_CAPACITY) history.count++;
}

float history_at(const MetricHistory& history, int index_from_oldest) {
  const int oldest_index = (history.head - history.count + HISTORY_CAPACITY) % HISTORY_CAPACITY;
  const int idx = (oldest_index + index_from_oldest) % HISTORY_CAPACITY;
  return history.values[idx];
}

float history_max(const MetricHistory& history) {
  if (history.count <= 0) return 0.0f;
  float mv = history_at(history, 0);
  for (int i = 1; i < history.count; ++i) {
    const float v = history_at(history, i);
    if (v > mv) mv = v;
  }
  return mv;
}

float history_at_resampled(const MetricHistory& history, float index_from_oldest_f) {
  if (history.count <= 0) return 0.0f;
  if (history.count == 1) return history_at(history, 0);
  if (index_from_oldest_f <= 0.0f) return history_at(history, 0);
  const float max_idx = static_cast<float>(history.count - 1);
  if (index_from_oldest_f >= max_idx) return history_at(history, history.count - 1);

  const int idx0 = static_cast<int>(index_from_oldest_f);
  const int idx1 = idx0 + 1;
  const float t = index_from_oldest_f - static_cast<float>(idx0);
  const float v0 = history_at(history, idx0);
  const float v1 = history_at(history, idx1);
  return v0 + (v1 - v0) * t;
}

uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha_0_255) {
  const uint32_t f_r = (fg >> 11) & 0x1F;
  const uint32_t f_g = (fg >> 5) & 0x3F;
  const uint32_t f_b = fg & 0x1F;
  const uint32_t b_r = (bg >> 11) & 0x1F;
  const uint32_t b_g = (bg >> 5) & 0x3F;
  const uint32_t b_b = bg & 0x1F;

  const uint32_t r = (f_r * alpha_0_255 + b_r * (255 - alpha_0_255)) / 255;
  const uint32_t g = (f_g * alpha_0_255 + b_g * (255 - alpha_0_255)) / 255;
  const uint32_t b = (f_b * alpha_0_255 + b_b * (255 - alpha_0_255)) / 255;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

void draw_card(lgfx::LGFX_Sprite& dst, int x, int y, int w, int h) {
  const uint8_t pulse = static_cast<uint8_t>(26 + ((millis() / 12) % 16));
  const uint16_t pulse_border = blend565(COLOR_TEXT_DIM, COLOR_BORDER, pulse);
  dst.fillRoundRect(x, y, w, h, CARD_RADIUS, COLOR_CARD);
  dst.drawRoundRect(x, y, w, h, CARD_RADIUS, pulse_border);
}

void draw_monitor_icon(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  dst.drawRoundRect(x, y, 16, 11, 2, color);
  dst.drawFastHLine(x + 2, y + 12, 12, color);
  dst.drawFastHLine(x + 5, y + 14, 6, color);
}

void draw_monitor_icon_large(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  dst.drawRoundRect(x, y, 19, 13, 2, color);
  dst.drawFastHLine(x + 2, y + 14, 15, color);
  dst.drawFastHLine(x + 6, y + 16, 7, color);
}

void draw_globe_icon_large(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  const int cx = x + 10;
  const int cy = y + 10;
  const int r = 8;
  // Globe ring
  dst.drawCircle(cx, cy, r, color);
  // Subtle globe grid to keep "IP/world" readability.
  dst.drawFastHLine(cx - 6, cy, 13, color);
  dst.drawArc(cx, cy, 6, 3, 200, 340, color);
  dst.drawArc(cx, cy, 6, 3, 20, 160, color);

  // Continent silhouettes (pixel-friendly and less abstract than pure meridians).
  // Americas-ish landmass
  dst.fillTriangle(cx - 4, cy - 3, cx - 6, cy + 1, cx - 2, cy + 1, color);
  dst.fillTriangle(cx - 3, cy + 1, cx - 5, cy + 5, cx - 1, cy + 4, color);
  // Euro/Africa-ish landmass
  dst.fillTriangle(cx + 1, cy - 4, cx + 5, cy - 2, cx + 2, cy + 1, color);
  dst.fillTriangle(cx + 2, cy + 1, cx + 5, cy + 4, cx + 1, cy + 5, color);
}

void draw_cpu_icon(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  dst.drawRect(x + 3, y + 3, 10, 10, color);
  for (int i = 0; i < 4; ++i) {
    dst.drawPixel(x + i + 4, y + 1, color);
    dst.drawPixel(x + i + 4, y + 14, color);
    dst.drawPixel(x + 1, y + i + 4, color);
    dst.drawPixel(x + 14, y + i + 4, color);
  }
  dst.drawRect(x + 6, y + 6, 4, 4, color);
}

void draw_cpu_icon_large(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  dst.drawRect(x + 4, y + 4, 14, 14, color);
  for (int i = 0; i < 6; ++i) {
    dst.drawPixel(x + i + 6, y + 2, color);
    dst.drawPixel(x + i + 6, y + 19, color);
    dst.drawPixel(x + 2, y + i + 6, color);
    dst.drawPixel(x + 19, y + i + 6, color);
  }
  dst.drawRect(x + 8, y + 8, 6, 6, color);
}

void draw_temp_icon_large(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  const int cx = x + 10;
  dst.drawRoundRect(x + 7, y + 2, 6, 13, 3, color);
  dst.drawFastVLine(cx, y + 5, 7, color);
  dst.fillCircle(cx, y + 18, 4, color);
  dst.drawCircle(cx, y + 18, 5, color);
  dst.drawFastHLine(x + 15, y + 6, 3, color);
  dst.drawFastHLine(x + 15, y + 10, 2, color);
}

void draw_fan_icon_large(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  const int cx = x + 10;
  const int cy = y + 10;
  dst.drawCircle(cx, cy, 8, color);
  dst.fillCircle(cx, cy, 2, color);
  dst.fillTriangle(cx, cy, cx - 6, cy - 3, cx - 1, cy - 8, color);
  dst.fillTriangle(cx, cy, cx + 6, cy - 3, cx + 1, cy - 8, color);
  dst.fillTriangle(cx, cy, cx - 6, cy + 3, cx - 1, cy + 8, color);
  dst.fillTriangle(cx, cy, cx + 6, cy + 3, cx + 1, cy + 8, color);
}

void draw_top_right_metric(lgfx::LGFX_Sprite& dst, int card_x, int card_y, int card_w, const char* text, uint16_t color) {
  dst.setFont(&fonts::Font0);
  dst.setTextColor(color, COLOR_CARD);
  const int tw = dst.textWidth(text);
  dst.drawString(text, card_x + card_w - tw - 6, card_y + 4);
}

void draw_offline_icon(lgfx::LGFX_Sprite& dst, int cx, int cy, uint16_t color) {
  // Circular badge + simple power-style glyph.
  dst.drawCircle(cx, cy, 11, color);
  dst.drawCircle(cx, cy, 10, color);
  dst.drawLine(cx, cy - 8, cx, cy - 1, color);
  dst.drawArc(cx, cy, 7, 6, 35, 325, color);
}

void draw_up_arrow_long_tail(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  dst.drawBitmap(x, y, image_arrow_up_bits, 5, 7, color);
  dst.drawFastVLine(x + 2, y + 7, 2, color);
}

void draw_down_arrow_long_tail(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  dst.drawBitmap(x, y, image_arrow_down_bits, 5, 7, color);
  dst.drawFastVLine(x + 2, y - 2, 2, color);
}

void draw_up_arrow_large(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  dst.drawLine(x + 5, y, x + 0, y + 6, color);
  dst.drawLine(x + 5, y, x + 10, y + 6, color);
  dst.drawFastVLine(x + 5, y + 1, 11, color);
}

void draw_down_arrow_large(lgfx::LGFX_Sprite& dst, int x, int y, uint16_t color) {
  dst.drawLine(x + 5, y + 12, x + 0, y + 6, color);
  dst.drawLine(x + 5, y + 12, x + 10, y + 6, color);
  dst.drawFastVLine(x + 5, y + 1, 11, color);
}

void draw_centered_in_card_header(lgfx::LGFX_Sprite& dst, int card_x, int card_w, int text_y,
                                  const char* text) {
  const int text_width = dst.textWidth(text);
  int tx = card_x + ((card_w - text_width) / 2);
  if (tx < card_x) {
    tx = card_x;
  }
  dst.drawString(text, tx, text_y);
}

void draw_centered_in_rect(lgfx::LGFX_Sprite& dst, int x, int y, int w, int h, const char* text,
                           bool bold = false) {
  const int text_w = dst.textWidth(text);
  const int text_h = dst.fontHeight();
  int tx = x + ((w - text_w) / 2);
  int ty = y + ((h - text_h) / 2);
  if (tx < x) tx = x;
  if (ty < y) ty = y;
  dst.drawString(text, tx, ty);
  if (bold) {
    dst.drawString(text, tx + 1, ty);
  }
}

void draw_centered_in_card_header_clamped(lgfx::LGFX_Sprite& dst, int card_x, int card_w, int text_y,
                                          const char* text) {
  const int text_width = dst.textWidth(text);
  int tx = card_x + ((card_w - text_width) / 2);
  if (tx < card_x + 22) {
    tx = card_x + 22;
  }
  dst.drawString(text, tx, text_y);
}

void draw_centered_in_card_header_bold(lgfx::LGFX_Sprite& dst, int card_x, int card_w, int text_y,
                                       const char* text) {
  const int text_width = dst.textWidth(text);
  int tx = card_x + ((card_w - text_width) / 2);
  if (tx < card_x + 22) {
    tx = card_x + 22;
  }
  dst.drawString(text, tx, text_y);
  dst.drawString(text, tx + 1, text_y);
}

void draw_graph_single(lgfx::LGFX_Sprite& dst, int x, int y, int w, int h, const MetricHistory& history,
                       uint16_t color, int card_x, int card_y, int card_w, int card_h,
                       float fixed_min = NAN, float fixed_max = NAN,
                       GraphScaleState* scale_state = nullptr) {
  if (history.count < 2 || w < 4 || h < 4) return;
  const int points = w;
  const int start = 0;
  const int plot_x0 = x;
  const float source_span = static_cast<float>(history.count - 1);
  float min_v = history_at(history, 0);
  float max_v = min_v;

  for (int i = 1; i < points; ++i) {
    const float src_idx = (source_span * static_cast<float>(i)) / static_cast<float>(points - 1);
    float v = history_at_resampled(history, src_idx);
    if (v < min_v) min_v = v;
    if (v > max_v) max_v = v;
  }

  if (!isnan(fixed_min) && !isnan(fixed_max) && fixed_max > fixed_min) {
    min_v = fixed_min;
    max_v = fixed_max;
  } else if ((max_v - min_v) < 0.1f) {
    max_v = min_v + 1.0f;
  } else {
    // Stabilize autoscale so tiny range changes do not cause frame jitter.
    float range = max_v - min_v;
    const float pad = range * 0.08f;
    min_v -= pad;
    max_v += pad;
    range = max_v - min_v;
    const float step = fmaxf(range / 12.0f, 0.5f);
    min_v = floorf(min_v / step) * step;
    max_v = ceilf(max_v / step) * step;
  }

  if (scale_state != nullptr) {
    if (!scale_state->initialized) {
      scale_state->initialized = true;
      scale_state->min_v = min_v;
      scale_state->max_v = max_v;
    } else {
      // Expand quickly to include new extremes; contract slowly to avoid jitter.
      if (min_v < scale_state->min_v) {
        scale_state->min_v = min_v;
      } else {
        scale_state->min_v = (scale_state->min_v * 0.92f) + (min_v * 0.08f);
      }

      if (max_v > scale_state->max_v) {
        scale_state->max_v = max_v;
      } else {
        scale_state->max_v = (scale_state->max_v * 0.92f) + (max_v * 0.08f);
      }

      if ((scale_state->max_v - scale_state->min_v) < 0.5f) {
        scale_state->max_v = scale_state->min_v + 0.5f;
      }
    }

    min_v = scale_state->min_v;
    max_v = scale_state->max_v;
  }

  int px_prev = plot_x0;
  float first = history_at_resampled(history, 0.0f);
  int py_prev_raw = y + h - 1 - static_cast<int>(((first - min_v) / (max_v - min_v)) * (h - 2));
  int py_prev = py_prev_raw;
  const uint16_t fill_color = blend565(color, COLOR_CARD, 72);
  const int card_bottom = card_y + card_h - 2;
  const int left_arc_center = card_x + CARD_RADIUS;
  const int right_arc_center = card_x + card_w - CARD_RADIUS - 1;
  auto corner_baseline_for_x = [&](int px) -> int {
    int baseline = card_bottom;
    if (px < left_arc_center) {
      const int dx = left_arc_center - px;
      if (dx < CARD_RADIUS) {
        const float y_in_arc = sqrtf(static_cast<float>(CARD_RADIUS * CARD_RADIUS - dx * dx));
        baseline -= static_cast<int>(CARD_RADIUS - y_in_arc);
      }
    } else if (px > right_arc_center) {
      const int dx = px - right_arc_center;
      if (dx < CARD_RADIUS) {
        const float y_in_arc = sqrtf(static_cast<float>(CARD_RADIUS * CARD_RADIUS - dx * dx));
        baseline -= static_cast<int>(CARD_RADIUS - y_in_arc);
      }
    }
    return baseline;
  };

  // Fill first plotted column too so area fill matches line coverage.
  {
    int baseline0 = corner_baseline_for_x(px_prev);
    int py0 = py_prev;
    if (py0 > baseline0) {
      py0 = baseline0;
    }
    dst.drawFastVLine(px_prev, py0, baseline0 - py0 + 1, fill_color);
  }

  for (int i = 1; i < points; ++i) {
    const float src_idx = (source_span * static_cast<float>(i)) / static_cast<float>(points - 1);
    float v = history_at_resampled(history, src_idx);
    int px = plot_x0 + i;
    int py_raw = y + h - 1 - static_cast<int>(((v - min_v) / (max_v - min_v)) * (h - 2));
    int py = ((3 * py_prev) + py_raw) / 4;
    const int baseline = corner_baseline_for_x(px);

    if (py > baseline) {
      py = baseline;
    }

    // Subtle filled area for modern chart styling.
    dst.drawFastVLine(px, py, baseline - py + 1, fill_color);

    int py_prev_clamped = py_prev;
    const int baseline_prev = corner_baseline_for_x(px_prev);
    if (py_prev_clamped > baseline_prev) {
      py_prev_clamped = baseline_prev;
    }
    dst.drawLine(px_prev, py_prev_clamped, px, py, color);
    // Keep thick strokes inside curved bottom bounds.
    if (py_prev_clamped + 1 <= baseline_prev && py + 1 <= baseline) {
      dst.drawLine(px_prev, py_prev_clamped + 1, px, py + 1, color);
    }
    dst.drawLine(px_prev, py_prev_clamped - 1, px, py - 1, color);
    px_prev = px;
    py_prev = py;
  }
}

void draw_graph_dual(lgfx::LGFX_Sprite& dst, int x, int y, int w, int h, const MetricHistory& a,
                     const MetricHistory& b, uint16_t color_a, uint16_t color_b,
                     int card_x, int card_y, int card_w, int card_h,
                     GraphScaleState* scale_state = nullptr) {
  if (a.count < 2 && b.count < 2) return;
  float max_v = 1.0f;
  for (int i = 0; i < a.count; ++i) {
    float v = history_at(a, i);
    if (v > max_v) max_v = v;
  }
  for (int i = 0; i < b.count; ++i) {
    float v = history_at(b, i);
    if (v > max_v) max_v = v;
  }

  if (scale_state != nullptr) {
    if (!scale_state->initialized) {
      scale_state->initialized = true;
      scale_state->min_v = 0.0f;
      scale_state->max_v = max_v;
    } else {
      if (max_v > scale_state->max_v) {
        scale_state->max_v = max_v;
      } else {
        scale_state->max_v = (scale_state->max_v * 0.92f) + (max_v * 0.08f);
      }
    }
    if (scale_state->max_v < 1.0f) scale_state->max_v = 1.0f;
    max_v = scale_state->max_v;
  }

  draw_graph_single(dst, x, y, w, h, a, color_a, card_x, card_y, card_w, card_h, 0.0f, max_v, nullptr);
  draw_graph_single(dst, x, y, w, h, b, color_b, card_x, card_y, card_w, card_h, 0.0f, max_v, nullptr);
}

void render_offline() {
  const int card_w = 220;
  const int card_h = 70;
  const int card_x = (SCREEN_W - card_w) / 2;
  const int card_y = (SCREEN_H - card_h) / 2;
  const int card_cx = card_x + (card_w / 2);

  // Smooth breathing aura: soft multi-layer glow pulse.
  const float phase = static_cast<float>((millis() % 2400UL) / 2400.0f) * 6.2831853f;
  const float wave = (sinf(phase) + 1.0f) * 0.5f;  // 0..1
  const int aura_expand = 3 + static_cast<int>(wave * 4.0f);  // 3..7
  const uint8_t aura_alpha_outer = static_cast<uint8_t>(10 + wave * 34);   // subtle
  const uint8_t aura_alpha_mid = static_cast<uint8_t>(16 + wave * 46);
  const uint8_t aura_alpha_inner = static_cast<uint8_t>(24 + wave * 58);
  const uint16_t aura_outer = blend565(COLOR_FAN, COLOR_BG, aura_alpha_outer);
  const uint16_t aura_mid = blend565(COLOR_FAN, COLOR_BG, aura_alpha_mid);
  const uint16_t aura_inner = blend565(COLOR_FAN, COLOR_BG, aura_alpha_inner);

  canvas.fillSprite(COLOR_BG);
  canvas.fillRoundRect(card_x - aura_expand - 4, card_y - aura_expand - 4,
                       card_w + ((aura_expand + 4) * 2), card_h + ((aura_expand + 4) * 2),
                       CARD_RADIUS + aura_expand + 4, aura_outer);
  canvas.fillRoundRect(card_x - aura_expand - 2, card_y - aura_expand - 2,
                       card_w + ((aura_expand + 2) * 2), card_h + ((aura_expand + 2) * 2),
                       CARD_RADIUS + aura_expand + 2, aura_mid);
  canvas.fillRoundRect(card_x - aura_expand, card_y - aura_expand,
                       card_w + (aura_expand * 2), card_h + (aura_expand * 2),
                       CARD_RADIUS + aura_expand, aura_inner);
  canvas.fillRoundRect(card_x, card_y, card_w, card_h, CARD_RADIUS, COLOR_CARD);
  canvas.drawRoundRect(card_x, card_y, card_w, card_h, CARD_RADIUS, COLOR_BORDER);

  const int content_y = card_y + (card_h / 2) - 7;
  const int icon_cx = card_cx - 66;
  const int icon_cy = content_y + 8;
  draw_offline_icon(canvas, icon_cx, icon_cy, COLOR_WARN_SOFT);

  canvas.setTextColor(COLOR_WARN_SOFT, COLOR_CARD);
  canvas.setTextSize(1);
  canvas.setFont(&fonts::Font2);
  draw_centered_in_rect(canvas, card_x + 34, content_y, card_w - 34, 18, "HOST DATA OFFLINE", true);
  canvas.pushSprite(0, 0);
}

void render_dashboard() {
  canvas.fillSprite(COLOR_BG);
  draw_card(canvas, CARD_LEFT_X, CARD_TOP_Y, CARD_COL_W, CARD_TOP_H);
  draw_card(canvas, CARD_RIGHT_X, CARD_TOP_Y, CARD_COL_W, CARD_TOP_H);
  draw_card(canvas, CARD_LEFT_X, CARD_MID_Y, CARD_COL_W, CARD_MID_H);
  draw_card(canvas, CARD_RIGHT_X, CARD_MID_Y, CARD_COL_W, CARD_MID_H);
  draw_card(canvas, CARD_LEFT_X, CARD_BOT_Y, CARD_COL_W, CARD_BOT_H);
  draw_card(canvas, CARD_RIGHT_X, CARD_BOT_Y, CARD_COL_W, CARD_BOT_H);

  char host_text[32] = "No Data";
  char ip_text[32] = "waiting...";
  char fan_text[20] = "-";
  char temp_text[20] = "-";
  char cpu_text[20] = "-";
  char up_text[20] = "0k";
  char down_text[20] = "0k";

  if (has_latest_stats) {
    if (latest_stats.hostname.length()) latest_stats.hostname.toCharArray(host_text, sizeof(host_text));
    if (latest_stats.ip.length()) latest_stats.ip.toCharArray(ip_text, sizeof(ip_text));
    if (latest_stats.has_cpu_fan) snprintf(fan_text, sizeof(fan_text), "%.0f rpm", latest_stats.cpu_fan_rpm);
    if (latest_stats.has_core_temp) snprintf(temp_text, sizeof(temp_text), "%.0f C", latest_stats.core_temp_c);
    snprintf(cpu_text, sizeof(cpu_text), "%.0f%%", latest_stats.cpu_load_percent);
    snprintf(up_text, sizeof(up_text), "%.0fk", latest_stats.net_upload_kbps);
    snprintf(down_text, sizeof(down_text), "%.0fk", latest_stats.net_download_kbps);
  }

  draw_monitor_icon(canvas, CARD_LEFT_X + 8, CARD_TOP_Y + 5, COLOR_FAN);
  canvas.drawBitmap(CARD_RIGHT_X + 8, CARD_TOP_Y + 5, image_earth_bits, 15, 16, COLOR_FAN);
  draw_up_arrow_long_tail(canvas, CARD_RIGHT_X + 8, CARD_MID_Y + 5, COLOR_UP);
  draw_down_arrow_long_tail(canvas, CARD_RIGHT_X + 82, CARD_MID_Y + 6, COLOR_DOWN);
  canvas.drawBitmap(CARD_LEFT_X + 6, CARD_MID_Y + 2, image_weather_wind_bits, 15, 16, COLOR_FAN);
  canvas.drawBitmap(CARD_LEFT_X + 4, CARD_BOT_Y + 3, image_weather_temperature_bits, 16, 16, COLOR_TEMP);
  draw_cpu_icon(canvas, CARD_RIGHT_X + 5, CARD_BOT_Y + 3, COLOR_CPU);

  canvas.setTextSize(1);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(COLOR_FAN, COLOR_CARD);
  draw_centered_in_rect(canvas, CARD_LEFT_X, CARD_TOP_Y, CARD_COL_W, CARD_TOP_H, host_text, true);
  draw_centered_in_rect(canvas, CARD_RIGHT_X, CARD_TOP_Y, CARD_COL_W, CARD_TOP_H, ip_text, false);

  canvas.setTextSize(1);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(COLOR_UP, COLOR_CARD);
  draw_centered_in_card_header_bold(canvas, CARD_RIGHT_X - 4, 46, CARD_MID_Y + 1, up_text);
  canvas.setTextColor(COLOR_DOWN, COLOR_CARD);
  draw_centered_in_card_header_bold(canvas, CARD_RIGHT_X + 70, 46, CARD_MID_Y + 1, down_text);

  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(COLOR_FAN, COLOR_CARD);
  draw_centered_in_card_header_bold(canvas, CARD_LEFT_X, CARD_COL_W, CARD_MID_Y + 1, fan_text);

  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(COLOR_TEMP, COLOR_CARD);
  draw_centered_in_card_header_bold(canvas, CARD_LEFT_X, CARD_COL_W, CARD_BOT_Y + 2, temp_text);
  canvas.setTextColor(COLOR_CPU, COLOR_CARD);
  draw_centered_in_card_header_bold(canvas, CARD_RIGHT_X, CARD_COL_W, CARD_BOT_Y + 2, cpu_text);

  char temp_max_text[24];
  char cpu_max_text[24];
  snprintf(temp_max_text, sizeof(temp_max_text), "max %.0fC", history_max(temp_history));
  snprintf(cpu_max_text, sizeof(cpu_max_text), "max %.0f%%", history_max(cpu_history));
  draw_top_right_metric(canvas, CARD_LEFT_X, CARD_BOT_Y, CARD_COL_W, temp_max_text, COLOR_TEMP);
  draw_top_right_metric(canvas, CARD_RIGHT_X, CARD_BOT_Y, CARD_COL_W, cpu_max_text, COLOR_CPU);

  const int graph_mid_x = CARD_LEFT_X + 1;
  const int graph_mid_w = CARD_COL_W - 2;
  const int graph_mid_y = CARD_MID_Y + HEADER_H_MID;
  const int graph_mid_h = CARD_MID_H - HEADER_H_MID - 1;

  const int graph_mid_r_x = CARD_RIGHT_X + 1;
  const int graph_mid_r_w = CARD_COL_W - 2;
  const int graph_mid_r_y = CARD_MID_Y + HEADER_H_MID;
  const int graph_mid_r_h = CARD_MID_H - HEADER_H_MID - 1;

  const int graph_bot_x = CARD_LEFT_X + 1;
  const int graph_bot_w = CARD_COL_W - 2;
  const int graph_bot_y = CARD_BOT_Y + HEADER_H_BOT + 8;
  const int graph_bot_h = CARD_BOT_H - HEADER_H_BOT - 1;

  const int graph_bot_r_x = CARD_RIGHT_X + 1;
  const int graph_bot_r_w = CARD_COL_W - 2;
  const int graph_bot_r_y = CARD_BOT_Y + HEADER_H_BOT + 8;
  const int graph_bot_r_h = CARD_BOT_H - HEADER_H_BOT - 1;

  draw_graph_single(canvas, graph_mid_x, graph_mid_y, graph_mid_w, graph_mid_h, fan_history, COLOR_FAN, CARD_LEFT_X, CARD_MID_Y, CARD_COL_W, CARD_MID_H, NAN, NAN, &fan_scale_state);
  draw_graph_dual(canvas, graph_mid_r_x, graph_mid_r_y, graph_mid_r_w, graph_mid_r_h, up_history, down_history, COLOR_UP, COLOR_DOWN, CARD_RIGHT_X, CARD_MID_Y, CARD_COL_W, CARD_MID_H, &net_scale_state);
  draw_graph_single(canvas, graph_bot_x, graph_bot_y, graph_bot_w, graph_bot_h, temp_history, COLOR_TEMP, CARD_LEFT_X, CARD_BOT_Y, CARD_COL_W, CARD_BOT_H, NAN, NAN, &temp_scale_state);
  draw_graph_single(canvas, graph_bot_r_x, graph_bot_r_y, graph_bot_r_w, graph_bot_r_h, cpu_history, COLOR_CPU, CARD_RIGHT_X, CARD_BOT_Y, CARD_COL_W, CARD_BOT_H, NAN, NAN, &cpu_scale_state);

  canvas.pushSprite(0, 0);
}

void draw_top_identity_cards(const char* host_text, const char* ip_text) {
  draw_card(canvas, CARD_LEFT_X, CARD_TOP_Y, CARD_COL_W, CARD_TOP_H);
  draw_card(canvas, CARD_RIGHT_X, CARD_TOP_Y, CARD_COL_W, CARD_TOP_H);
  draw_monitor_icon(canvas, CARD_LEFT_X + 8, CARD_TOP_Y + 5, COLOR_FAN);
  canvas.drawBitmap(CARD_RIGHT_X + 8, CARD_TOP_Y + 5, image_earth_bits, 15, 16, COLOR_FAN);
  canvas.setTextSize(1);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(COLOR_FAN, COLOR_CARD);
  draw_centered_in_rect(canvas, CARD_LEFT_X, CARD_TOP_Y, CARD_COL_W, CARD_TOP_H, host_text, true);
  draw_centered_in_rect(canvas, CARD_RIGHT_X, CARD_TOP_Y, CARD_COL_W, CARD_TOP_H, ip_text, false);
}

void render_layout_identity_only(const char* host_text, const char* ip_text) {
  canvas.fillSprite(COLOR_BG);
  const int x = SCREEN_MARGIN_X;
  const int y = SCREEN_MARGIN_Y;
  const int w = SCREEN_W - (2 * SCREEN_MARGIN_X);
  const int h = (SCREEN_H - (2 * SCREEN_MARGIN_Y) - CARD_GAP_Y) / 2;
  draw_card(canvas, x, y, w, h);
  draw_card(canvas, x, y + h + CARD_GAP_Y, w, h);
  draw_monitor_icon_large(canvas, x + 10, y + ((h - 18) / 2), COLOR_FAN);
  draw_globe_icon_large(canvas, x + 10, y + h + CARD_GAP_Y + ((h - 20) / 2), COLOR_FAN);
  canvas.setTextSize(1);
  canvas.setFont(&fonts::Font4);
  canvas.setTextColor(COLOR_FAN, COLOR_CARD);
  draw_centered_in_rect(canvas, x, y, w, h, host_text, true);
  draw_centered_in_rect(canvas, x, y + h + CARD_GAP_Y, w, h, ip_text, false);
  canvas.pushSprite(0, 0);
}

void render_layout_single_metric(const char* host_text, const char* ip_text, int mode) {
  canvas.fillSprite(COLOR_BG);
  draw_top_identity_cards(host_text, ip_text);

  const int big_x = CARD_LEFT_X;
  const int big_y = CARD_MID_Y;
  const int big_w = (CARD_COL_W * 2) + CARD_GAP_X;
  const int big_h = CARD_MID_H + CARD_GAP_Y + CARD_BOT_H;
  draw_card(canvas, big_x, big_y, big_w, big_h);

  if (mode == 0) {  // temp
    draw_temp_icon_large(canvas, big_x + 6, big_y + 4, COLOR_TEMP);
    canvas.setFont(&fonts::Font4);
    canvas.setTextColor(COLOR_TEMP, COLOR_CARD);
    char temp_text[24] = "-";
    if (has_latest_stats && latest_stats.has_core_temp) snprintf(temp_text, sizeof(temp_text), "%.0f C", latest_stats.core_temp_c);
    draw_centered_in_card_header_bold(canvas, big_x, big_w, big_y + 4, temp_text);
    char max_text[24];
    snprintf(max_text, sizeof(max_text), "max %.0fC", history_max(temp_history));
    draw_top_right_metric(canvas, big_x, big_y, big_w, max_text, COLOR_TEMP);
    draw_graph_single(canvas, big_x + 1, big_y + 28, big_w - 2, big_h - 30, temp_history, COLOR_TEMP, big_x, big_y, big_w, big_h, NAN, NAN, &temp_scale_state);
  } else if (mode == 1) {  // cpu
    draw_cpu_icon_large(canvas, big_x + 6, big_y + 4, COLOR_CPU);
    canvas.setFont(&fonts::Font4);
    canvas.setTextColor(COLOR_CPU, COLOR_CARD);
    char cpu_text[24] = "-";
    if (has_latest_stats) snprintf(cpu_text, sizeof(cpu_text), "%.0f%%", latest_stats.cpu_load_percent);
    draw_centered_in_card_header_bold(canvas, big_x, big_w, big_y + 4, cpu_text);
    char max_text[24];
    snprintf(max_text, sizeof(max_text), "max %.0f%%", history_max(cpu_history));
    draw_top_right_metric(canvas, big_x, big_y, big_w, max_text, COLOR_CPU);
    draw_graph_single(canvas, big_x + 1, big_y + 28, big_w - 2, big_h - 30, cpu_history, COLOR_CPU, big_x, big_y, big_w, big_h, NAN, NAN, &cpu_scale_state);
  } else if (mode == 2) {  // net
    draw_up_arrow_large(canvas, big_x + 10, big_y + 4, COLOR_UP);
    draw_down_arrow_large(canvas, big_x + (big_w / 2) + 10, big_y + 4, COLOR_DOWN);
    canvas.setFont(&fonts::Font2);
    canvas.setTextSize(1);
    char up_text[24] = "0k";
    char down_text[24] = "0k";
    if (has_latest_stats) {
      snprintf(up_text, sizeof(up_text), "%.0fk", latest_stats.net_upload_kbps);
      snprintf(down_text, sizeof(down_text), "%.0fk", latest_stats.net_download_kbps);
    }
    canvas.setTextColor(COLOR_UP, COLOR_CARD);
    draw_centered_in_card_header_bold(canvas, big_x + 34, (big_w / 2) - 44, big_y + 4, up_text);
    canvas.setTextColor(COLOR_DOWN, COLOR_CARD);
    draw_centered_in_card_header_bold(canvas, big_x + (big_w / 2) + 34, (big_w / 2) - 44, big_y + 4, down_text);
    draw_graph_dual(canvas, big_x + 1, big_y + 28, big_w - 2, big_h - 30, up_history, down_history, COLOR_UP, COLOR_DOWN, big_x, big_y, big_w, big_h, &net_scale_state);
  } else {  // fan
    draw_fan_icon_large(canvas, big_x + 6, big_y + 4, COLOR_FAN);
    canvas.setFont(&fonts::Font2);
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_FAN, COLOR_CARD);
    char fan_text[24] = "-";
    if (has_latest_stats && latest_stats.has_cpu_fan) snprintf(fan_text, sizeof(fan_text), "%.0f rpm", latest_stats.cpu_fan_rpm);
    draw_centered_in_card_header_bold(canvas, big_x, big_w, big_y + 6, fan_text);
    char max_text[24];
    snprintf(max_text, sizeof(max_text), "max %.0f", history_max(fan_history));
    draw_top_right_metric(canvas, big_x, big_y, big_w, max_text, COLOR_FAN);
    draw_graph_single(canvas, big_x + 1, big_y + 28, big_w - 2, big_h - 30, fan_history, COLOR_FAN, big_x, big_y, big_w, big_h, NAN, NAN, &fan_scale_state);
  }

  canvas.pushSprite(0, 0);
}

bool try_read_float_field(const JsonDocument& doc, const char* key, float& out_value) {
  JsonVariantConst value = doc[key];
  if (!value.is<float>() && !value.is<int>() && !value.is<double>()) return false;
  out_value = value.as<float>();
  return true;
}

void process_serial_line(const char* line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  if (!doc.is<JsonObject>()) return;

  HostStats parsed = latest_stats;
  if (doc["hostname"].is<const char*>()) parsed.hostname = String(doc["hostname"].as<const char*>());
  if (doc["ip"].is<const char*>()) parsed.ip = String(doc["ip"].as<const char*>());
  try_read_float_field(doc, "cpu_load_percent", parsed.cpu_load_percent);
  try_read_float_field(doc, "net_upload_kbps", parsed.net_upload_kbps);
  try_read_float_field(doc, "net_download_kbps", parsed.net_download_kbps);

  JsonVariantConst temp = doc["core_temp_c"];
  if (temp.isNull()) {
    parsed.has_core_temp = false;
    parsed.core_temp_c = 0.0f;
  } else if (temp.is<float>() || temp.is<int>() || temp.is<double>()) {
    parsed.has_core_temp = true;
    parsed.core_temp_c = temp.as<float>();
  }

  JsonVariantConst fan = doc["cpu_fan_rpm"];
  if (fan.isNull()) {
    parsed.has_cpu_fan = false;
    parsed.cpu_fan_rpm = 0.0f;
  } else if (fan.is<float>() || fan.is<int>() || fan.is<double>()) {
    parsed.has_cpu_fan = true;
    parsed.cpu_fan_rpm = fan.as<float>();
  }

  latest_stats = parsed;
  has_latest_stats = true;
  showing_offline = false;
  last_valid_data_ms = millis();
  target_temp = parsed.has_core_temp ? parsed.core_temp_c : target_temp;
  target_fan = parsed.has_cpu_fan ? parsed.cpu_fan_rpm : target_fan;
  target_cpu = parsed.cpu_load_percent;
  target_up = parsed.net_upload_kbps;
  target_down = parsed.net_download_kbps;

  if (!display_targets_initialized) {
    display_temp = target_temp;
    display_fan = target_fan;
    display_cpu = target_cpu;
    display_up = target_up;
    display_down = target_down;
    display_targets_initialized = true;
  }
}

void poll_serial_input() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      serial_line_buffer[serial_line_length] = '\0';
      if (serial_line_length > 0) process_serial_line(serial_line_buffer);
      serial_line_length = 0;
      continue;
    }
    if (serial_line_length < SERIAL_LINE_BUFFER_SIZE - 1) {
      serial_line_buffer[serial_line_length++] = c;
    } else {
      serial_line_length = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  init_watchdog();
  lcd.init();
  lcd.setRotation(0);
  // Keep backlight off until first frame is ready to avoid power-up flash.
  lcd.setBrightness(0);

  // --- SPRITE ---
  canvas.setColorDepth(16);

  if (!canvas.createSprite(SCREEN_W, SCREEN_H)) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.drawString("Sprite alloc failed", 10, 10);
    while (true) delay(1000);
  }

  // --- FIRST FRAME ---
  render_offline();
  calibrate_touch_baseline(TOUCH_BRIGHTNESS_PIN, brightness_touch);
  calibrate_touch_baseline(TOUCH_LAYOUT_PIN, layout_touch);
  calibrate_touch_baseline(TOUCH_POWER_PIN, power_touch);
  apply_brightness_level();
  last_anim_ms = millis();
}

void loop() {
  feed_watchdog();
  handle_touch_power_cycle();
  if (!display_enabled) {
    poll_serial_input();
    delay(10);
    return;
  }
  handle_touch_brightness_cycle();
  handle_touch_layout_cycle();

  poll_serial_input();

  const unsigned long now = millis();
  if (!showing_offline && (now - last_valid_data_ms >= DATA_STALE_TIMEOUT_MS)) {
    showing_offline = true;
    render_offline();
  }

  if (now - last_frame_ms >= FRAME_INTERVAL_MS) {
    last_frame_ms = now;
    const unsigned long dt_ms = now - last_anim_ms;
    last_anim_ms = now;

    if (display_targets_initialized) {
      // Time-aware easing to keep motion smooth and stable.
      const float frame_scale = static_cast<float>(dt_ms) / static_cast<float>(FRAME_INTERVAL_MS);
      float alpha = DISPLAY_EASE_ALPHA * frame_scale;
      if (alpha > 0.45f) alpha = 0.45f;
      if (alpha < 0.02f) alpha = 0.02f;

      display_temp += (target_temp - display_temp) * alpha;
      display_fan += (target_fan - display_fan) * alpha;
      display_cpu += (target_cpu - display_cpu) * alpha;
      display_up += (target_up - display_up) * alpha;
      display_down += (target_down - display_down) * alpha;

      history_push_frame_counter++;
      if (history_push_frame_counter >= HISTORY_PUSH_EVERY_N_FRAMES) {
        history_push_frame_counter = 0;
        push_metric(temp_history, display_temp);
        push_metric(fan_history, display_fan);
        push_metric(cpu_history, display_cpu);
        push_metric(up_history, display_up);
        push_metric(down_history, display_down);
      }
    }

    if (showing_offline) {
      render_offline();
    } else {
      char host_text[32] = "No Data";
      char ip_text[32] = "waiting...";
      if (has_latest_stats) {
        if (latest_stats.hostname.length()) latest_stats.hostname.toCharArray(host_text, sizeof(host_text));
        if (latest_stats.ip.length()) latest_stats.ip.toCharArray(ip_text, sizeof(ip_text));
      }

      switch (active_layout) {
        case 0: render_dashboard(); break;
        case 1: render_layout_identity_only(host_text, ip_text); break;
        case 2: render_layout_single_metric(host_text, ip_text, 0); break;  // temp
        case 3: render_layout_single_metric(host_text, ip_text, 1); break;  // cpu
        case 4: render_layout_single_metric(host_text, ip_text, 2); break;  // net
        case 5:
        default: render_layout_single_metric(host_text, ip_text, 3); break; // fan
      }
    }
  }
}
