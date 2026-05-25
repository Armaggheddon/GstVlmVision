#ifndef __VLM_REQUEST_H__
#define __VLM_REQUEST_H__

#include <glib.h>

typedef enum {
  VLM_IMAGE_ENCODING_BASE64_JPEG,
  VLM_IMAGE_ENCODING_DATA_URL_JPEG,
} VlmImageEncoding;

typedef struct {
  guchar *data;
  gsize size;
  gint64 pts_ns;
  gint frame_index;
} VlmFramePayload;

typedef struct {
  gchar *system_prompt;
  gchar *user_prompt;
  VlmFramePayload *frames;
  gint num_frames;
  gchar *model_name;
  gdouble temperature;
  gint max_tokens;
  gdouble top_p;
  gchar **stop_sequences;
  gchar *response_format;
} VlmRequest;

void vlm_request_free(VlmRequest *req);

#endif /* __VLM_REQUEST_H__ */
