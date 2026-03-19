#include "hooks.h"
#include "registry.h"
#include "port_registry.h"
#include "express_emotion_tool.h"

static ExpressEmotionTool g_express_emotion_tool;

void register_tools(ToolRegistry& reg, const ToolConfig& cfg) {
  (void)cfg;
  reg.add(&g_express_emotion_tool);
}

void register_ports(PortRegistry& reg, const PortConfig& cfg) {
  (void)cfg;
}
