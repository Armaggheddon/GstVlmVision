#ifndef __VLM_CAPABILITY_H__
#define __VLM_CAPABILITY_H__

#include <glib.h>

typedef struct {
  const gchar *name;
  const gchar *default_base_url;
} VlmCapabilityProfile;

const VlmCapabilityProfile *vlm_capability_get_preset(const gchar *name);

#endif /* __VLM_CAPABILITY_H__ */
