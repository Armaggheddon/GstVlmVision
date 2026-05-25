#ifndef __VLM_BACKEND_H__
#define __VLM_BACKEND_H__

#include <glib.h>
#include "vlm-result.h"

typedef struct _VlmResponseParser VlmResponseParser;

struct _VlmResponseParser {
  const gchar *name;
  gboolean (*parse)(VlmResponseParser *self, const gchar *body, gsize body_len,
                    gint http_status, VlmResult *result_out);
};

#endif /* __VLM_BACKEND_H__ */
