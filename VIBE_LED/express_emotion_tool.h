#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "tool.h"
#include "eye_controller.h"

class ExpressEmotionTool : public ITool {
public:
  bool init() override {
    EyeController::instance().begin();
    return true;
  }

  const char* name() const override { return "express_emotion"; }

  void describe(JsonObject& tool) override {
    tool["name"] = name();
    tool["description"] = "당신의 감정을 표현합니다.";

    auto params = tool["parameters"].to<JsonObject>();
    params["type"] = "object";
    auto props = params["properties"].to<JsonObject>();

    auto emotion = props["emotion"].to<JsonObject>();
    emotion["type"] = "string";
    emotion["description"] = "Emotion to express. Supported values: ANGRY, ANNOY, NEUTRAL.";

    auto req = params["required"].to<JsonArray>();
    req.add("emotion");
  }

  bool invoke(JsonObjectConst args, ObservationBuilder& out) override {
    const char* emotion = args["emotion"] | "";
    String e = String(emotion);
    e.trim();
    e.toUpperCase();

    EyeController::Mood mood = EyeController::Mood::Neutral;

    if (e == "ANGRY") {
      mood = EyeController::Mood::Angry;
    } else if (e == "ANNOY") {
      mood = EyeController::Mood::Annoyed;
    } else if (e == "NEUTRAL") {
      mood = EyeController::Mood::Neutral;
    } else {
      out.error("Invalid emotion", "Supported values are ANGRY, ANNOY, and NEUTRAL");
      return false;
    }

    auto& eye = EyeController::instance();
    eye.dynamicPattern.stop(); // keep normal blinking, stop dynamic patterns
    eye.setMood(mood, true);

    JsonDocument doc;
    doc["tool"] = name();
    doc["emotion"] = e;
    doc["status"] = "applied";
    doc["mode"] = "blinking";
    doc["color"] = (e == "ANGRY") ? "red" : ((e == "ANNOY") ? "yellow" : "green");

    String payload;
    serializeJson(doc, payload);
    out.success(payload.c_str());
    return true;
  }
};
