#include <Arduino.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <stdio.h>

static constexpr unsigned long HEARTBEAT_MS = 2000;
unsigned long last_heartbeat = 0;
static constexpr unsigned long DISPLAY_REFRESH_MS = 500;
unsigned long last_display_refresh = 0;

static constexpr size_t SERIAL_LINE_BUFFER_SIZE = 384;
char serial_line_buffer[SERIAL_LINE_BUFFER_SIZE];
size_t serial_line_length = 0;

// XIAO ESP32S3 default I2C pins: SDA=D4, SCL=D5.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const unsigned char image_chart_bits[] U8X8_PROGMEM = {
    0x00, 0x00, 0x01, 0x00, 0xc1, 0x01, 0x41, 0x01, 0x41, 0x01, 0x41,
    0x1d, 0x41, 0x15, 0x41, 0x15, 0x5d, 0x15, 0x55, 0x15, 0x55, 0x15,
    0x55, 0x15, 0xdd, 0x1d, 0x01, 0x00, 0xff, 0x3f, 0x00, 0x00};

static const unsigned char image_cloud_bits[] U8X8_PROGMEM = {
    0x00, 0x00, 0x00, 0xe0, 0x03, 0x00, 0x10, 0x04, 0x00, 0x08, 0x08, 0x00,
    0x0c, 0x10, 0x00, 0x02, 0x70, 0x00, 0x01, 0x80, 0x00, 0x01, 0x00, 0x01,
    0x02, 0x00, 0x01, 0xfc, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const unsigned char image_music_record_button_bits[] U8X8_PROGMEM = {
    0x24, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0xa5, 0x00,
    0x00, 0x00, 0x00, 0x81, 0x00, 0x00, 0x00, 0x00, 0xa5, 0x00, 0x00, 0x00,
    0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08};

static const unsigned char image_weather_temperature_bits[] U8X8_PROGMEM = {
    0x38, 0x00, 0x44, 0x40, 0xd4, 0xa0, 0x54, 0x40, 0xd4, 0x1c, 0x54,
    0x06, 0xd4, 0x02, 0x54, 0x02, 0x54, 0x06, 0x92, 0x1c, 0x39, 0x01,
    0x75, 0x01, 0x7d, 0x01, 0x39, 0x01, 0x82, 0x00, 0x7c, 0x00};

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

void render_display() {
  char ip_text[32] = "waiting...";
  char temp_text[16] = "n/a";
  char cpu_text[16] = "0.00%";
  const char* title_text = "No Data";

  if (has_latest_stats) {
    if (latest_stats.ip.length() > 0) {
      latest_stats.ip.toCharArray(ip_text, sizeof(ip_text));
    }

    if (latest_stats.hostname.length() > 0) {
      title_text = latest_stats.hostname.c_str();
    }

    if (latest_stats.has_core_temp) {
      snprintf(temp_text, sizeof(temp_text), "%.1f\xC2\xB0""C", latest_stats.core_temp_c);
    }
    snprintf(cpu_text, sizeof(cpu_text), "%.2f%%", latest_stats.cpu_load_percent);
  }

  u8g2.clearBuffer();

  // [BEGIN lopaka generated]
  u8g2.setFontMode(1);
  u8g2.setBitmapMode(1);
  u8g2.drawRFrame(0, 0, 128, 14, 5);

  u8g2.drawXBM(32, 3, 36, 22, image_music_record_button_bits);

  u8g2.setFont(u8g2_font_haxrcorp4089_tr);
  u8g2.drawStr(46, 10, title_text);

  u8g2.drawRFrame(0, 16, 128, 14, 5);

  u8g2.drawRFrame(0, 32, 62, 30, 5);

  u8g2.drawRFrame(66, 32, 62, 30, 5);

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(27, 26, ip_text);

  u8g2.drawXBM(6, 17, 17, 16, image_cloud_bits);

  u8g2.drawXBM(4, 39, 16, 16, image_weather_temperature_bits);

  u8g2.setFont(u8g2_font_profont15_tr);
  u8g2.drawUTF8(22, 52, temp_text);

  u8g2.drawXBM(70, 39, 14, 16, image_chart_bits);

  u8g2.drawStr(87, 52, cpu_text);

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
    Serial.print("JSON decode failed: ");
    Serial.println(err.c_str());
    return false;
  }

  if (!doc.is<JsonObject>()) {
    Serial.println("JSON payload is not an object");
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

  Serial.print("stats host=");
  Serial.print(latest_stats.hostname);
  Serial.print(" ip=");
  Serial.print(latest_stats.ip);
  Serial.print(" cpu=");
  Serial.print(latest_stats.cpu_load_percent, 1);
  Serial.print("% temp=");
  if (latest_stats.has_core_temp) {
    Serial.print(latest_stats.core_temp_c, 1);
    Serial.println("C");
  } else {
    Serial.println("n/a");
  }
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
      Serial.println("Incoming serial line too long; dropped");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("ESP32 monitor receiver booted");
  u8g2.begin();
  render_display();
}

void loop() {
  poll_serial_input();
  const unsigned long now = millis();

  if (has_latest_stats) {
    if (now - last_display_refresh >= DISPLAY_REFRESH_MS) {
      last_display_refresh = now;
      render_display();
    }
  }

  if (now - last_heartbeat >= HEARTBEAT_MS) {
    last_heartbeat = now;
    if (has_latest_stats) {
      Serial.print("alive latest_cpu=");
      Serial.print(latest_stats.cpu_load_percent, 1);
      Serial.print("% at ");
      Serial.println(latest_stats.timestamp_utc);
    } else {
      Serial.println("ESP32 alive (waiting for stats)");
    }
  }
}
