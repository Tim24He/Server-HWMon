#include <Arduino.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <stdio.h>

static constexpr size_t SERIAL_LINE_BUFFER_SIZE = 384;
static constexpr unsigned long DATA_STALE_TIMEOUT_MS = 3000;
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

struct HostStats {
  String timestamp_utc;
  String hostname;
  String ip;
  float cpu_load_percent = 0.0f;
  bool has_core_temp = false;
  float core_temp_c = 0.0f;
};

HostStats latest_stats;
bool has_latest_stats = false;
unsigned long last_valid_data_ms = 0;
bool showing_offline_screen = true;

void render_offline_display() {
  u8g2.clearBuffer();

  // [BEGIN lopaka generated]
  u8g2.setFontMode(1);
  u8g2.setBitmapMode(1);
  u8g2.drawXBM(57, 14, 15, 16, image_device_power_button_bits);

  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawStr(33, 51, "Offline");

  u8g2.sendBuffer();
  // [END lopaka generated]
}

void render_display() {
  char ip_text[32] = "waiting...";
  char temp_text[16] = "n/a";
  char cpu_text[16] = "0%";
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
  }

  auto centered_x = [](int area_width, const char* text, U8G2& display) -> int {
    const int text_width = display.getStrWidth(text);
    int x = (area_width - text_width) / 2;
    if (x < 0) {
      x = 0;
    }
    return x;
  };

  auto centered_x_in_region = [](int region_left, int region_width, const char* text,
                                 U8G2& display) -> int {
    const int text_width = display.getStrWidth(text);
    int x = region_left + (region_width - text_width) / 2;
    if (x < region_left) {
      x = region_left;
    }
    return x;
  };

  u8g2.clearBuffer();

  // [BEGIN lopaka generated]
  u8g2.setFontMode(1);
  u8g2.setBitmapMode(1);
  u8g2.drawRFrame(0, 0, 128, 14, 5);

  u8g2.setFont(u8g2_font_haxrcorp4089_tr);
  u8g2.drawStr(centered_x(128, title_text, u8g2), 10, title_text);

  u8g2.drawRFrame(0, 16, 128, 14, 5);

  u8g2.drawRFrame(0, 32, 62, 32, 5);

  u8g2.drawRFrame(66, 32, 62, 32, 5);

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(centered_x(128, ip_text, u8g2), 26, ip_text);

  u8g2.drawXBM(6, 17, 17, 16, image_cloud_bits);

  u8g2.drawXBM(0, 40, 16, 16, image_Temperature_bits);

  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawUTF8(centered_x_in_region(14, 48, temp_text, u8g2), 53, temp_text);

  u8g2.drawXBM(67, 40, 16, 16, image_Voltage_bits);

  u8g2.drawStr(centered_x_in_region(84, 44, cpu_text, u8g2), 53, cpu_text);

  u8g2.drawXBM(10, 3, 8, 8, image_menu_bits);

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
  Wire.begin();
  Wire.setClock(400000);
  u8g2.begin();
  render_offline_display();
}

void loop() {
  poll_serial_input();

  if (!showing_offline_screen && (millis() - last_valid_data_ms >= DATA_STALE_TIMEOUT_MS)) {
    showing_offline_screen = true;
    render_offline_display();
  }
}
