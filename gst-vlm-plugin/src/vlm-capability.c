#include "vlm-capability.h"
#include <string.h>

static const VlmCapabilityProfile presets[] = {
  {
    .name = "openai",
    .default_base_url = "https://api.openai.com",
  },
  { NULL, NULL }
};

const VlmCapabilityProfile *
vlm_capability_get_preset(const gchar *name) {
  if (!name || name[0] == '\0')
    return &presets[0];
  for (int i = 0; presets[i].name != NULL; i++) {
    if (strcmp(presets[i].name, name) == 0)
      return &presets[i];
  }
  return &presets[0];
}
