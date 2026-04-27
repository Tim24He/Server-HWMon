#include <Arduino.h>
#include <ArduinoJson.h>

static constexpr unsigned long HEARTBEAT_MS = 2000;
unsigned long last_heartbeat = 0;

static constexpr size_t SERIAL_LINE_BUFFER_SIZE = 384;
char serial_line_buffer[SERIAL_LINE_BUFFER_SIZE];
size_t serial_line_length = 0;

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
}

void loop() {
  poll_serial_input();

  const unsigned long now = millis();
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
