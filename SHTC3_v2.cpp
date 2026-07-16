#include "wled.h"

class UsermodSHTC3 : public Usermod {
private:
  static const char _name[];

  // Wake sensor from low-power sleep state.
  static const uint16_t CMD_WAKEUP = 0x3517;
  // Return sensor to sleep after reading.
  static const uint16_t CMD_SLEEP = 0xB098;
  // Normal-power measurement command (no clock stretching).
  static const uint16_t CMD_MEASURE_T_RH = 0x7866;

  bool _enabled = true;
  bool _sensorAvailable = false;
  bool _hasValidReading = false;
  bool _useFahrenheit = false;

  uint32_t _lastReadMs = 0;
  uint32_t _checkIntervalMs = 60000;

  static const uint8_t SHTC3_I2C_ADDRESS = 0x70;

  float _temperatureC = NAN;
  float _humidityPct = NAN;

  bool sendCommand(uint16_t command) {
    Wire.beginTransmission(SHTC3_I2C_ADDRESS);
    Wire.write((uint8_t)(command >> 8));
    Wire.write((uint8_t)(command & 0xFF));
    return (Wire.endTransmission() == 0);
  }

  uint8_t crc8(const uint8_t *data, uint8_t len) {
    // SHTC3 CRC-8 polynomial 0x31, init 0xFF.
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
      crc ^= data[i];
      for (uint8_t bit = 0; bit < 8; bit++) {
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
      }
    }
    return crc;
  }

  float round1(float value) {
    return roundf(value * 10.0f) * 0.1f;
  }

  float getTemperature() const {
    return _useFahrenheit ? ((_temperatureC * 1.8f) + 32.0f) : _temperatureC;
  }

  const __FlashStringHelper *getTemperatureUnit() const {
    return _useFahrenheit ? F("°F") : F("°C");
  }

#ifndef WLED_DISABLE_MQTT
  void publishMqtt() {
    if (!WLED_MQTT_CONNECTED) return;

    char topic[128];
    char payload[16];

    snprintf_P(topic, sizeof(topic), PSTR("%s/temperature"), mqttDeviceTopic);
    dtostrf(round1(getTemperature()), 0, 1, payload);
    mqtt->publish(topic, 0, false, payload);

    snprintf_P(topic, sizeof(topic), PSTR("%s/humidity"), mqttDeviceTopic);
    dtostrf(round1(_humidityPct), 0, 1, payload);
    mqtt->publish(topic, 0, false, payload);
  }

  void mqttCreateHassSensor(const String &name, const String &topic, const String &deviceClass, const String &unitOfMeasurement) {
    // Publish retained discovery payload so HA can auto-create entities.
    String discoveryTopic = String(F("homeassistant/sensor/")) + mqttClientID + "/" + name + F("/config");

    StaticJsonDocument<600> doc;
    doc[F("name")] = name;
    doc[F("state_topic")] = topic;
    doc[F("unique_id")] = String(mqttClientID) + name;
    if (unitOfMeasurement.length() > 0) doc[F("unit_of_measurement")] = unitOfMeasurement;
    if (deviceClass.length() > 0) doc[F("device_class")] = deviceClass;
    doc[F("expire_after")] = 1800;

    JsonObject device = doc.createNestedObject(F("device"));
    device[F("name")] = serverDescription;
    device[F("identifiers")] = "wled-sensor-" + String(mqttClientID);
    device[F("manufacturer")] = F(WLED_BRAND);
    device[F("model")] = F(WLED_PRODUCT_NAME);
    device[F("sw_version")] = versionString;

    String payload;
    serializeJson(doc, payload);
    mqtt->publish(discoveryTopic.c_str(), 0, true, payload.c_str());
  }

  void mqttInitialize() {
    if (!WLED_MQTT_CONNECTED) return;

    char topic[128];
    snprintf_P(topic, sizeof(topic), PSTR("%s/temperature"), mqttDeviceTopic);
    mqttCreateHassSensor(F("SHTC3-Temperature"), topic, F("temperature"), getTemperatureUnit());

    snprintf_P(topic, sizeof(topic), PSTR("%s/humidity"), mqttDeviceTopic);
    mqttCreateHassSensor(F("SHTC3-Humidity"), topic, F("humidity"), F("%"));
  }

#endif

public:
#ifndef WLED_DISABLE_MQTT
  void onMqttConnect(bool sessionPresent) override {
    mqttInitialize();
  }
#endif

  bool readSensor() {
    uint8_t data[6] = {0};

    if (!sendCommand(CMD_WAKEUP)) {
      _sensorAvailable = false;
      return false;
    }

    delay(1);

    if (!sendCommand(CMD_MEASURE_T_RH)) {
      _sensorAvailable = false;
      return false;
    }

    // Datasheet conversion time is ~12ms, give it a small margin.
    delay(15);

    if (Wire.requestFrom((int)SHTC3_I2C_ADDRESS, 6) != 6) {
      _sensorAvailable = false;
      return false;
    }

    for (uint8_t i = 0; i < 6; i++) {
      data[i] = Wire.read();
    }

    // Data frame: T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC
    if (crc8(data, 2) != data[2] || crc8(data + 3, 2) != data[5]) {
      _sensorAvailable = false;
      return false;
    }

    uint16_t rawTemp = ((uint16_t)data[0] << 8) | data[1];
    uint16_t rawHumidity = ((uint16_t)data[3] << 8) | data[4];

    _temperatureC = -45.0f + 175.0f * ((float)rawTemp / 65536.0f);
    _humidityPct = 100.0f * ((float)rawHumidity / 65536.0f);

    // Return sensor to sleep to reduce self-heating and I2C traffic.
    sendCommand(CMD_SLEEP);

    _sensorAvailable = true;
    _hasValidReading = true;

#ifndef WLED_DISABLE_MQTT
    publishMqtt();
#endif

    return true;
  }

public:
  void setup() override {
    _sensorAvailable = readSensor();

    if (_sensorAvailable) {
      DEBUG_PRINTLN(F("SHTC3: initialized"));
    } else {
      DEBUG_PRINTLN(F("SHTC3: sensor not found"));
    }
  }

  void loop() override {
    if (!_enabled) return;
    if (strip.isUpdating()) return;

    uint32_t now = millis();
    if (now - _lastReadMs < _checkIntervalMs) return;

    _lastReadMs = now;

    if (!_sensorAvailable) {
      _sensorAvailable = readSensor();
      if (!_sensorAvailable) return;
    }

    readSensor();
  }

  void addToJsonInfo(JsonObject &root) override {
    JsonObject user = root[F("u")];
    if (user.isNull()) user = root.createNestedObject(F("u"));

    JsonArray headline = user.createNestedArray(F("SHTC3 Sensor"));
    headline.add(F("Temperature and Humidity"));

    JsonArray jsonTemp = user.createNestedArray(F("Temperature"));
    JsonArray jsonHumidity = user.createNestedArray(F("Humidity"));

    // Keep last valid values visible even if one poll fails.
    if (_enabled && _hasValidReading) {
      jsonTemp.add(round1(getTemperature()));
      jsonHumidity.add(round1(_humidityPct));
    } else {
      jsonTemp.add(nullptr);
      jsonHumidity.add(nullptr);
    }

    jsonTemp.add(getTemperatureUnit());
    jsonHumidity.add(F("%"));

    JsonObject sensor = root[F("sensor")];
    if (sensor.isNull()) sensor = root.createNestedObject(F("sensor"));

    JsonArray sensorTemp = sensor.createNestedArray(F("SHTC3 Temperature"));
    JsonArray sensorHumidity = sensor.createNestedArray(F("SHTC3 Humidity"));

    if (_enabled && _hasValidReading) {
      sensorTemp.add(round1(getTemperature()));
      sensorHumidity.add(round1(_humidityPct));
    } else {
      sensorTemp.add(nullptr);
      sensorHumidity.add(nullptr);
    }

    sensorTemp.add(getTemperatureUnit());
    sensorHumidity.add(F("%"));
  }

  void addToConfig(JsonObject &root) override {
    JsonObject top = root.createNestedObject(FPSTR(_name));

    top[F("Enabled")] = _enabled;
    top[F("CheckInterval")] = _checkIntervalMs / 1000;
    top[F("UseFahrenheit")] = _useFahrenheit;
  }

  bool readFromConfig(JsonObject &root) override {
    JsonObject top = root[FPSTR(_name)];
    if (top.isNull()) return false;

    bool configComplete = true;
    bool enabled = _enabled;
    uint16_t checkIntervalSec = _checkIntervalMs / 1000;
    bool useFahrenheit = _useFahrenheit;

    configComplete &= getJsonValue(top[F("Enabled")], enabled);
    configComplete &= getJsonValue(top[F("CheckInterval")], checkIntervalSec);
    getJsonValue(top[F("UseFahrenheit")], useFahrenheit);

    _enabled = enabled;
    _useFahrenheit = useFahrenheit;

    if (checkIntervalSec < 5) checkIntervalSec = 5;
    if (checkIntervalSec > 600) checkIntervalSec = 600;
    _checkIntervalMs = (uint32_t)checkIntervalSec * 1000;

  #ifndef WLED_DISABLE_MQTT
    if (WLED_MQTT_CONNECTED) mqttInitialize();
  #endif

    return configComplete;
  }

  void appendConfigData() override {
    oappend(F("dd=addDropdown('"));
    oappend(FPSTR(_name));
    oappend(F("','UseFahrenheit');"));
    oappend(F("addOption(dd,'Celsius',0);"));
    oappend(F("addOption(dd,'Fahrenheit',1);"));
  }
};

const char UsermodSHTC3::_name[] PROGMEM = "SHTC3";

UsermodSHTC3 usermodSHTC3;
REGISTER_USERMOD(usermodSHTC3);

