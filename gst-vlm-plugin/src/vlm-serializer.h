#ifndef __VLM_SERIALIZER_H__
#define __VLM_SERIALIZER_H__

#include <glib.h>
#include "vlm-request.h"

typedef struct _VlmSerializer VlmSerializer;

struct _VlmSerializer {
  const gchar *name;
  gchar *(*build_body)(VlmSerializer *self, VlmRequest *req);
  gchar **(*build_headers)(VlmSerializer *self, const gchar *api_key);
  gchar *(*build_url)(VlmSerializer *self, const gchar *base_url, VlmRequest *req);
};

#endif /* __VLM_SERIALIZER_H__ */
