#include <Arduino.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <stdio.h>

static constexpr size_t SERIAL_LINE_BUFFER_SIZE = 384;
static constexpr unsigned long DATA_STALE_TIMEOUT_MS = 3000;
static constexpr unsigned long PIXEL_SHIFT_INTERVAL_MS = 10UL * 60UL * 1000UL;
static constexpr int DISPLAY_WIDTH = 128;
static constexpr int DISPLAY_HEIGHT = 64;
static constexpr const char* FIRMWARE_VERSION = "i2c_ssd1306_128x64_v2";
static constexpr int METRIC_HISTORY_CAPACITY = 48;
char serial_line_buffer[SERIAL_LINE_BUFFER_SIZE];
size_t serial_line_length = 0;

// XIAO ESP32S3 default I2C pins: SDA=D4, SCL=D5.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const unsigned char image_cloud_bits[] U8X8_PROGMEM = {
    0x00, 0x00, 0x00, 0xe0, 0x03, 0x00, 0x10, 0x04, 0x00, 0x08, 0x08, 0x00,
    0x0c, 0x10, 0x00, 0x02, 0x70, 0x00, 0x01, 0x80, 0x00, 0x01, 0x00, 0x01,
    0x02, 0x00, 0x01, 0xfc, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const unsigned char image_menu_bits[] U8X8_PROGMEM = {
    0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00};

static const unsigned char image_arrow_down_bits[] U8X8_PROGMEM = {
    0x04, 0x04, 0x04, 0x04, 0x15, 0x0e, 0x04};

static const unsigned char image_arrow_up_bits[] U8X8_PROGMEM = {
    0x04, 0x0e, 0x15, 0x04, 0x04, 0x04, 0x04};

static const unsigned char image_Temperature_bits[] U8X8_PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x40, 0x01, 0x40, 0x01, 0x40,
    0x01, 0x40, 0x01, 0x40, 0x01, 0x40, 0x01, 0x20, 0x02, 0xe0, 0x03,
    0xe0, 0x03, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const unsigned char image_Voltage_bits[] U8X8_PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x80, 0x01, 0xc0,
    0x01, 0xe0, 0x00, 0xf0, 0x07, 0x80, 0x03, 0xc0, 0x01, 0xc0, 0x00,
    0x60, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const unsigned char image_device_power_button_bits[] U8X8_PROGMEM = {
    0x80, 0x00, 0x80, 0x00, 0x98, 0x0c, 0xa4, 0x12, 0x92, 0x24, 0x8a,
    0x28, 0x85, 0x50, 0x05, 0x50, 0x05, 0x50, 0x05, 0x50, 0x05, 0x50,
    0x0a, 0x28, 0x12, 0x24, 0xe4, 0x13, 0x18, 0x0c, 0xe0, 0x03};

struct MetricHistory {
  float values[METRIC_HISTORY_CAPACITY] = {0.0f};
  int count = 0;
  int head = 0;
};

struct HostStats {
  String timestamp_utc;
  String hostname;
  String ip;
  float cpu_load_percent = 0.0f;
  float net_upload_kbps = 0.0f;
  float net_download_kbps = 0.0f;
  bool has_core_temp = false;
  float core_temp_c = 0.0f;
};

HostStats latest_stats;
bool has_latest_stats = false;
unsigned long last_valid_data_ms = 0;
bool showing_offline_screen = true;
unsigned long last_pixel_shift_ms = 0;
int display_shift_x = 0;
int display_shift_y = 0;
MetricHistory temp_history;
MetricHistory cpu_history;

int clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

void draw_rframe_shifted(int x, int y, int w, int h, int r) {
  const int max_x = DISPLAY_WIDTH - w;
  const int max_y = DISPLAY_HEIGHT - h;
  const int sx = clamp_int(x + display_shift_x, 0, max_x);
  const int sy = clamp_int(y + display_shift_y, 0, max_y);
  u8g2.drawRFrame(sx, sy, w, h, r);
}

void draw_xbm_shifted(int x, int y, int w, int h, const uint8_t* bitmap) {
  const int max_x = DISPLAY_WIDTH - w;
  const int max_y = DISPLAY_HEIGHT - h;
  const int sx = clamp_int(x + display_shift_x, 0, max_x);
  const int sy = clamp_int(y + display_shift_y, 0, max_y);
  u8g2.drawXBM(sx, sy, w, h, bitmap);
}

void draw_str_shifted(int x, int y, const char* text, bool utf8 = false) {
  const int text_width = u8g2.getStrWidth(text);
  const int sx = clamp_int(x + display_shift_x, 0, DISPLAY_WIDTH - text_width);
  const int sy = clamp_int(y + display_shift_y, 0, DISPLAY_HEIGHT - 1);
  if (utf8) {
    u8g2.drawUTF8(sx, sy, text);
  } else {
    u8g2.drawStr(sx, sy, text);
  }
}

void push_metric_history(MetricHistory& history, float value) {
  history.values[history.head] = value;
  history.head = (history.head + 1) % METRIC_HISTORY_CAPACITY;
  if (history.count < METRIC_HISTORY_CAPACITY) {
    history.count++;
  }
}

float history_value_at(const MetricHistory& history, int index_from_oldest) {
  const int oldest_index =
      (history.head - history.count + METRIC_HISTORY_CAPACITY) % METRIC_HISTORY_CAPACITY;
  const int idx = (oldest_index + index_from_oldest) % METRIC_HISTORY_CAPACITY;
  return history.values[idx];
}

void draw_history_graph_shifted(int x, int y, int w, int h, const MetricHistory& history) {
  if (w < 2 || h < 2 || history.count < 2) {
    return;
  }

  const int points = (history.count < w) ? history.count : w;
  const int start = history.count - points;
  float min_v = history_value_at(history, start);
  float max_v = min_v;

  for (int i = 1; i < points; ++i) {
    const float v = history_value_at(history, start + i);
    if (v < min_v) {
      min_v = v;
    }
    if (v > max_v) {
      max_v = v;
    }
  }

  if ((max_v - min_v) < 0.01f) {
    max_v = min_v + 1.0f;
  }

  int prev_x = x;
  const float first_value = history_value_at(history, start);
  int prev_y = y + h - 1 - static_cast<int>(((first_value - min_v) / (max_v - min_v)) * (h - 1));

  for (int i = 1; i < points; ++i) {
    const float v = history_value_at(history, start + i);
    const int px = x + i;
    const int py = y + h - 1 - static_cast<int>(((v - min_v) / (max_v - min_v)) * (h - 1));
    u8g2.drawLine(clamp_int(prev_x + display_shift_x, 0, DISPLAY_WIDTH - 1),
                  clamp_int(prev_y + display_shift_y, 0, DISPLAY_HEIGHT - 1),
                  clamp_int(px + display_shift_x, 0, DISPLAY_WIDTH - 1),
                  clamp_int(py + display_shift_y, 0, DISPLAY_HEIGHT - 1));
    prev_x = px;
    prev_y = py;
  }
}

void render_offline_display() {
  u8g2.clearBuffer();

  // [BEGIN lopaka generated]
  u8g2.setFontMode(1);
  u8g2.setBitmapMode(1);
  draw_xbm_shifted(57, 14, 15, 16, image_device_power_button_bits);

  u8g2.setFont(u8g2_font_profont17_tr);
  draw_str_shifted(33, 51, "Offline");

  u8g2.sendBuffer();
  // [END lopaka generated]
}

void render_display() {
  char ip_text[32] = "waiting...";
  char temp_text[16] = "n/a";
  char cpu_text[16] = "0%";
  char upload_text[16] = "0k";
  char download_text[16] = "0k";
  const char* title_text = "No Data";

  if (has_latest_stats) {
    if (latest_stats.ip.length() > 0) {
      latest_stats.ip.toCharArray(ip_text, sizeof(ip_text));
    }

    if (latest_stats.hostname.length() > 0) {
      title_text = latest_stats.hostname.c_str();
    }

    if (latest_stats.has_core_temp) {
      snprintf(temp_text, sizeof(temp_text), "%.0f\xC2\xB0""C", latest_stats.core_temp_c);
    }
    snprintf(cpu_text, sizeof(cpu_text), "%.0f%%", latest_stats.cpu_load_percent);
    snprintf(upload_text, sizeof(upload_text), "%.0fk", latest_stats.net_upload_kbps);
    snprintf(download_text, sizeof(download_text), "%.0fk", latest_stats.net_download_kbps);
  }

  auto centered_x = [](int area_width, const char* text, U8G2& display) -> int {
    const int text_width = display.getStrWidth(text);
    int x = (area_width - text_width) / 2;
    if (x < 0) {
      x = 0;
    }
    return x;
  };

  u8g2.clearBuffer();

  // [BEGIN lopaka generated]
  u8g2.setFontMode(1);
  u8g2.setBitmapMode(1);
  draw_rframe_shifted(0, 0, 128, 14, 5);

  u8g2.setFont(u8g2_font_haxrcorp4089_tr);
  draw_str_shifted(centered_x(128, title_text, u8g2), 10, title_text);

  draw_rframe_shifted(0, 16, 128, 13, 5);

  draw_rframe_shifted(0, 32, 62, 32, 5);

  draw_rframe_shifted(66, 32, 62, 32, 5);

  u8g2.setFont(u8g2_font_4x6_tr);
  draw_str_shifted(5, 25, ip_text);

  draw_xbm_shifted(73, 19, 5, 7, image_arrow_up_bits);
  draw_xbm_shifted(97, 19, 5, 7, image_arrow_down_bits);
  draw_str_shifted(80, 25, upload_text);
  draw_str_shifted(104, 25, download_text);

  draw_xbm_shifted(0, 40, 16, 16, image_Temperature_bits);

  u8g2.setFont(u8g2_font_profont12_tr);
  draw_str_shifted(30, 43, temp_text, true);

  draw_xbm_shifted(67, 40, 16, 16, image_Voltage_bits);

  draw_str_shifted(101, 43, cpu_text);

  // Keep graph area below the value text and to the right of the icon.
  draw_history_graph_shifted(18, 46, 42, 16, temp_history);
  draw_history_graph_shifted(84, 46, 42, 16, cpu_history);

  draw_xbm_shifted(10, 3, 8, 8, image_menu_bits);

  u8g2.sendBuffer();
  // [END lopaka generated]
}

bool try_read_string_field(const JsonDocument& doc, const char* key, String& out_value) {
  const JsonVariantConst value = doc[key];
  if (!value.is<const char*>()) {
    return false;
  }
  out_value = String(value.as<const char*>());
  return true;
}

bool try_read_float_field(const JsonDocument& doc, const char* key, float& out_value) {
  const JsonVariantConst value = doc[key];
  if (!value.is<float>() && !value.is<int>() && !value.is<double>()) {
    return false;
  }
  out_value = value.as<float>();
  return true;
}

bool decode_stats_json(const char* json_line, HostStats& out_stats) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json_line);
  if (err) {
    return false;
  }

  if (!doc.is<JsonObject>()) {
    return false;
  }

  // Start from previous values so missing fields don't wipe known-good data.
  out_stats = latest_stats;

  try_read_string_field(doc, "timestamp_utc", out_stats.timestamp_utc);
  try_read_string_field(doc, "hostname", out_stats.hostname);
  try_read_string_field(doc, "ip", out_stats.ip);
  try_read_float_field(doc, "cpu_load_percent", out_stats.cpu_load_percent);
  try_read_float_field(doc, "net_upload_kbps", out_stats.net_upload_kbps);
  try_read_float_field(doc, "net_download_kbps", out_stats.net_download_kbps);

  const JsonVariantConst core_temp = doc["core_temp_c"];
  if (core_temp.isNull()) {
    out_stats.has_core_temp = false;
    out_stats.core_temp_c = 0.0f;
  } else if (core_temp.is<float>() || core_temp.is<int>() || core_temp.is<double>()) {
    out_stats.has_core_temp = true;
    out_stats.core_temp_c = core_temp.as<float>();
  }

  return true;
}

void process_serial_line(const char* line) {
  HostStats parsed;
  if (!decode_stats_json(line, parsed)) {
    return;
  }

  latest_stats = parsed;
  has_latest_stats = true;
  last_valid_data_ms = millis();
  showing_offline_screen = false;
  if (parsed.has_core_temp) {
    push_metric_history(temp_history, parsed.core_temp_c);
  } else if (temp_history.count > 0) {
    push_metric_history(temp_history, history_value_at(temp_history, temp_history.count - 1));
  } else {
    push_metric_history(temp_history, 0.0f);
  }
  push_metric_history(cpu_history, parsed.cpu_load_percent);
  render_display();
}

void poll_serial_input() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      serial_line_buffer[serial_line_length] = '\0';
      if (serial_line_length > 0) {
        process_serial_line(serial_line_buffer);
      }
      serial_line_length = 0;
      continue;
    }

    if (serial_line_length < SERIAL_LINE_BUFFER_SIZE - 1) {
      serial_line_buffer[serial_line_length++] = c;
    } else {
      // Drop oversized lines and reset framing.
      serial_line_length = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.printf("Firmware: %s\n", FIRMWARE_VERSION);
  Wire.begin();
  Wire.setClock(400000);
  u8g2.begin();
  last_pixel_shift_ms = millis();
  render_offline_display();
}

void loop() {
  poll_serial_input();

  const unsigned long now = millis();
  if (now - last_pixel_shift_ms >= PIXEL_SHIFT_INTERVAL_MS) {
    last_pixel_shift_ms = now;
    // Alternate between two safe states. Helpers clamp anything at edges.
    if (display_shift_x == 0 && display_shift_y == 0) {
      display_shift_x = 1;
      display_shift_y = 1;
    } else {
      display_shift_x = 0;
      display_shift_y = 0;
    }
    if (showing_offline_screen) {
      render_offline_display();
    } else {
      render_display();
    }
  }

  if (!showing_offline_screen && (now - last_valid_data_ms >= DATA_STALE_TIMEOUT_MS)) {
    showing_offline_screen = true;
    render_offline_display();
  }
}
