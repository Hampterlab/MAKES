#include "hooks.h"
#include "registry.h"
#include "port_registry.h"
#include "temp_humidity_tool.h"

static TemperatureHumidityTool g_temperature_humidity_tool;

static float read_temperature_outport() {
  TemperatureHumidityManager::Sample current;
  if (!TemperatureHumidityManager::instance().getCurrent(current, false)) {
    return NAN;
  }
  return current.temperature;
}

static float read_humidity_outport() {
  TemperatureHumidityManager::Sample current;
  if (!TemperatureHumidityManager::instance().getCurrent(current, false)) {
    return NAN;
  }
  return current.humidity;
}

void register_tools(ToolRegistry& reg, const ToolConfig& cfg) {
  (void)cfg;
  reg.add(&g_temperature_humidity_tool);
}

void register_ports(PortRegistry& reg, const PortConfig& cfg) {
  (void)cfg;
  reg.addOutPort("temperature_c", "Ambient temperature in Celsius", "celsius", -20.0f, 60.0f, 5000, read_temperature_outport, 0.1f);
  reg.addOutPort("humidity_pct", "Relative humidity percentage", "percent", 0.0f, 100.0f, 5000, read_humidity_outport, 0.1f);
}
