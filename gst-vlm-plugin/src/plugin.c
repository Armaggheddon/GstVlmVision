#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstvlmvision.h"

#ifndef PACKAGE
#define PACKAGE "vlmvision"
#endif

static gboolean
plugin_init(GstPlugin *plugin) {
  GST_DEBUG_CATEGORY_INIT(gst_vlm_vision_debug_category, "vlmvision", 0,
                          "VLM Vision Plugin");
  return gst_element_register(plugin, "vlmvision", GST_RANK_NONE,
                              GST_TYPE_VLM_VISION);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vlmvision,
    "Processes video frames with OpenAI-compatible vision-language models",
    plugin_init,
    "1.0.0",
    "MIT",
    "gstvlmvision",
    "https://github.com/Armaggheddon/GstVlmVision")
