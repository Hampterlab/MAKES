#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include "tool.h"

extern void port_set_outport_value(const char* name, float value) __attribute__((weak));

#ifndef I2C_SDA
#define I2C_SDA 8
#endif

#ifndef I2C_SCL
#define I2C_SCL 9
#endif

class TemperatureHumidityManager {
public:
  struct Sample {
    uint32_t ts_sec = 0;
    float temperature = NAN;
    float humidity = NAN;
  };

  struct SeriesView {
    const char* name;
    uint32_t interval_sec;
    size_t capacity;
    const Sample* buffer;
    size_t head;
    size_t count;
  };

  static TemperatureHumidityManager& instance() {
    static TemperatureHumidityManager inst;
    return inst;
  }

  bool begin() {
    if (_inited) return _sensorReady;

    Wire.begin(I2C_SDA, I2C_SCL);
    _sensorReady = _sht4.begin();
    if (_sensorReady) {
      _sht4.setPrecision(SHT4X_HIGH_PRECISION);
      _sht4.setHeater(SHT4X_NO_HEATER);
    }

    _lastPollMs = 0;
    _inited = true;
    return _sensorReady;
  }

  bool update(bool forceRead = false) {
    if (!_inited && !begin()) return false;
    if (!_sensorReady) return false;

    const uint32_t nowMs = millis();
    if (!forceRead && _hasCurrent && (nowMs - _lastPollMs) < kPollIntervalMs) {
      return _hasCurrent;
    }

    sensors_event_t humidityEvent, tempEvent;
    _sht4.getEvent(&humidityEvent, &tempEvent);

    _current.temperature = tempEvent.temperature;
    _current.humidity = humidityEvent.relative_humidity;
    _current.ts_sec = nowMs / 1000UL;
    _hasCurrent = true;
    _lastPollMs = nowMs;

    _publishOutports();
    _ingestHierarchical(_current);
    return true;
  }

  bool getCurrent(Sample& out, bool forceRead = false) {
    if (!update(forceRead)) return false;
    out = _current;
    return true;
  }

  bool getSeries(const String& window, SeriesView& out) const {
    if (window == "1min") {
      out = {"1min", 60, kCap1Min, _series1Min, _head1Min, _count1Min};
      return true;
    }
    if (window == "10min") {
      out = {"10min", 600, kCap10Min, _series10Min, _head10Min, _count10Min};
      return true;
    }
    if (window == "30min") {
      out = {"30min", 1800, kCap30Min, _series30Min, _head30Min, _count30Min};
      return true;
    }
    if (window == "1hour") {
      out = {"1hour", 3600, kCap1Hour, _series1Hour, _head1Hour, _count1Hour};
      return true;
    }
    if (window == "6hour") {
      out = {"6hour", 21600, kCap6Hour, _series6Hour, _head6Hour, _count6Hour};
      return true;
    }
    if (window == "24hour") {
      out = {"24hour", 86400, kCap24Hour, _series24Hour, _head24Hour, _count24Hour};
      return true;
    }
    return false;
  }

  bool getSeriesPreview(const String& window, Sample& out) const {
    const Aggregator* agg = _selectAggregator(window);
    if (!agg || !agg->active || agg->count == 0) {
      return false;
    }

    out.ts_sec = agg->bucketStartSec;
    out.temperature = agg->tempSum / agg->count;
    out.humidity = agg->humSum / agg->count;
    return true;
  }

  static const char* supportedWindows() {
    return "1min, 10min, 30min, 1hour, 6hour, 24hour";
  }

private:
  static constexpr uint32_t kPollIntervalMs = 5000;
  static constexpr size_t kCap1Min = 24 * 60;
  static constexpr size_t kCap10Min = (24 * 60) / 10;
  static constexpr size_t kCap30Min = (24 * 60) / 30;
  static constexpr size_t kCap1Hour = 24;
  static constexpr size_t kCap6Hour = 4;
  static constexpr size_t kCap24Hour = 1;

  struct Aggregator {
    uint32_t bucketStartSec = 0;
    uint32_t intervalSec = 0;
    float tempSum = 0.0f;
    float humSum = 0.0f;
    uint16_t count = 0;
    bool active = false;
  };

  TemperatureHumidityManager() {
    _agg1Min.intervalSec = 60;
    _agg10Min.intervalSec = 600;
    _agg30Min.intervalSec = 1800;
    _agg1Hour.intervalSec = 3600;
    _agg6Hour.intervalSec = 21600;
    _agg24Hour.intervalSec = 86400;
  }

  void _publishOutports() {
    if (port_set_outport_value) {
      port_set_outport_value("temperature_c", _current.temperature);
      port_set_outport_value("humidity_pct", _current.humidity);
    }
  }

  static void _pushSample(Sample* buffer, size_t cap, size_t& head, size_t& count, const Sample& s) {
    buffer[head] = s;
    head = (head + 1) % cap;
    if (count < cap) count++;
  }

  static void _accumulate(Aggregator& agg, const Sample& s, Sample* buffer, size_t cap, size_t& head, size_t& count) {
    uint32_t bucket = (s.ts_sec / agg.intervalSec) * agg.intervalSec;

    if (!agg.active) {
      agg.active = true;
      agg.bucketStartSec = bucket;
    }

    if (bucket != agg.bucketStartSec && agg.count > 0) {
      Sample finalized;
      finalized.ts_sec = agg.bucketStartSec;
      finalized.temperature = agg.tempSum / agg.count;
      finalized.humidity = agg.humSum / agg.count;
      _pushSample(buffer, cap, head, count, finalized);

      agg.bucketStartSec = bucket;
      agg.tempSum = 0.0f;
      agg.humSum = 0.0f;
      agg.count = 0;
    }

    agg.tempSum += s.temperature;
    agg.humSum += s.humidity;
    agg.count++;
  }

  void _ingestHierarchical(const Sample& s) {
    _accumulate(_agg1Min, s, _series1Min, kCap1Min, _head1Min, _count1Min);
    _accumulate(_agg10Min, s, _series10Min, kCap10Min, _head10Min, _count10Min);
    _accumulate(_agg30Min, s, _series30Min, kCap30Min, _head30Min, _count30Min);
    _accumulate(_agg1Hour, s, _series1Hour, kCap1Hour, _head1Hour, _count1Hour);
    _accumulate(_agg6Hour, s, _series6Hour, kCap6Hour, _head6Hour, _count6Hour);
    _accumulate(_agg24Hour, s, _series24Hour, kCap24Hour, _head24Hour, _count24Hour);
  }

  const Aggregator* _selectAggregator(const String& window) const {
    if (window == "1min") return &_agg1Min;
    if (window == "10min") return &_agg10Min;
    if (window == "30min") return &_agg30Min;
    if (window == "1hour") return &_agg1Hour;
    if (window == "6hour") return &_agg6Hour;
    if (window == "24hour") return &_agg24Hour;
    return nullptr;
  }

  bool _inited = false;
  bool _sensorReady = false;
  bool _hasCurrent = false;
  uint32_t _lastPollMs = 0;
  Adafruit_SHT4x _sht4;
  Sample _current;
  Aggregator _agg1Min;
  Aggregator _agg10Min;
  Aggregator _agg30Min;
  Aggregator _agg1Hour;
  Aggregator _agg6Hour;
  Aggregator _agg24Hour;
  Sample _series1Min[kCap1Min] = {};
  Sample _series10Min[kCap10Min] = {};
  Sample _series30Min[kCap30Min] = {};
  Sample _series1Hour[kCap1Hour] = {};
  Sample _series6Hour[kCap6Hour] = {};
  Sample _series24Hour[kCap24Hour] = {};
  size_t _head1Min = 0, _count1Min = 0;
  size_t _head10Min = 0, _count10Min = 0;
  size_t _head30Min = 0, _count30Min = 0;
  size_t _head1Hour = 0, _count1Hour = 0;
  size_t _head6Hour = 0, _count6Hour = 0;
  size_t _head24Hour = 0, _count24Hour = 0;
};

class TemperatureHumidityTool : public ITool {
public:
  bool init() override {
    return TemperatureHumidityManager::instance().begin();
  }

  const char* name() const override { return "read_temperature_humidity"; }

  void describe(JsonObject& tool) override {
    tool["name"] = name();
    tool["description"] = "Read current temperature and humidity, publish them to outports, and query recent summarized history.";

    auto params = tool["parameters"].to<JsonObject>();
    params["type"] = "object";
    auto props = params["properties"].to<JsonObject>();

    auto window = props["window"].to<JsonObject>();
    window["type"] = "string";
    window["description"] = "Optional history resolution: 1min, 10min, 30min, 1hour, 6hour, 24hour.";

    auto limit = props["limit"].to<JsonObject>();
    limit["type"] = "integer";
    limit["description"] = "Optional number of points to return. Default 20.";
  }

  bool invoke(JsonObjectConst args, ObservationBuilder& out) override {
    auto& mgr = TemperatureHumidityManager::instance();

    TemperatureHumidityManager::Sample current;
    if (!mgr.getCurrent(current, false)) {
      out.error("Sensor read failed", "SHT4x sensor is not available or I2C initialization failed");
      return false;
    }

    JsonDocument doc;
    doc["temperature_c"] = current.temperature;
    doc["humidity_pct"] = current.humidity;
    doc["timestamp_sec"] = current.ts_sec;

    const char* window = args["window"] | nullptr;
    size_t limit = args["limit"] | 20;
    if (window && *window) {
      mgr.update(false);
      TemperatureHumidityManager::SeriesView view;
      if (!mgr.getSeries(String(window), view)) {
        out.error("Invalid window", TemperatureHumidityManager::supportedWindows());
        return false;
      }

      TemperatureHumidityManager::Sample preview;
      bool hasPreview = mgr.getSeriesPreview(String(window), preview);
      JsonArray points = doc["history"].to<JsonArray>();
      size_t persistedCount = view.count;
      size_t totalCount = persistedCount + (hasPreview ? 1 : 0);
      size_t emit = min(limit, totalCount);
      size_t previewSlots = (hasPreview && emit > 0) ? 1 : 0;
      size_t persistedToEmit = min(persistedCount, emit - previewSlots);

      for (size_t idx = 0; idx < persistedToEmit; ++idx) {
        size_t offset = persistedCount - persistedToEmit + idx;
        size_t pos = (view.head + view.capacity - view.count + offset) % view.capacity;
        const auto& sample = view.buffer[pos];
        JsonObject item = points.add<JsonObject>();
        item["ts_sec"] = sample.ts_sec;
        item["temperature_c"] = sample.temperature;
        item["humidity_pct"] = sample.humidity;
      }
      if (previewSlots) {
        JsonObject item = points.add<JsonObject>();
        item["ts_sec"] = preview.ts_sec;
        item["temperature_c"] = preview.temperature;
        item["humidity_pct"] = preview.humidity;
        item["partial"] = true;
      }
      doc["window"] = view.name;
      doc["history_points"] = totalCount;
    }

    String payload;
    serializeJson(doc, payload);
    out.success(payload.c_str());
    return true;
  }
};
