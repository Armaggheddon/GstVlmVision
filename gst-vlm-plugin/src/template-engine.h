#ifndef __TEMPLATE_ENGINE_H__
#define __TEMPLATE_ENGINE_H__

#include <glib.h>
#include "vlm-request.h"

typedef struct _VlmTemplateContext VlmTemplateContext;

struct _VlmTemplateContext {
  const gchar *base_url;
  const gchar *model;
  const gchar *system_prompt;
  const gchar *user_prompt;
  VlmFramePayload *frames;
  gint num_frames;
  gdouble temperature;
  gint max_tokens;
  gdouble top_p;
  gchar **stop_sequences;
  const gchar *schema_json;
  const gchar *request_id;
};

gchar *template_engine_substitute(const gchar *template_str, const VlmTemplateContext *ctx);

#endif /* __TEMPLATE_ENGINE_H__ */
