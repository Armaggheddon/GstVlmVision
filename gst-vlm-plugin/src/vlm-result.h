#ifndef __VLM_RESULT_H__
#define __VLM_RESULT_H__

#include <glib.h>

typedef struct {
  gchar *text;
  gchar *raw_json;
  gchar *error_message;
  gint http_status;
  gint64 request_pts_ns;
} VlmResult;

void vlm_result_free(VlmResult *result);

#endif /* __VLM_RESULT_H__ */
