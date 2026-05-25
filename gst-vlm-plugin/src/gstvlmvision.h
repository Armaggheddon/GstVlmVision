#ifndef __GST_VLM_VISION_H__
#define __GST_VLM_VISION_H__

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "gstvlmvisionmeta.h"
#include "vlm-request.h"
#include "vlm-result.h"
#include "vlm-serializer.h"
#include "vlm-backend.h"
#include "queue-policy.h"

GST_DEBUG_CATEGORY_EXTERN(gst_vlm_vision_debug_category);

G_BEGIN_DECLS

#define GST_TYPE_VLM_VISION (gst_vlm_vision_get_type())
#define GST_VLM_VISION(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_VLM_VISION, GstVlmVision))
#define GST_VLM_VISION_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VLM_VISION, GstVlmVisionClass))
#define GST_IS_VLM_VISION(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VLM_VISION))
#define GST_IS_VLM_VISION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VLM_VISION))
#define GST_VLM_VISION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_VLM_VISION, GstVlmVisionClass))

typedef struct _GstVlmVision GstVlmVision;
typedef struct _GstVlmVisionClass GstVlmVisionClass;

/* Output mode flags */
typedef enum {
  VLM_OUTPUT_META   = 1 << 0,
  VLM_OUTPUT_SIGNAL = 1 << 1,
  VLM_OUTPUT_BUS    = 1 << 2,
  VLM_OUTPUT_JSON   = 1 << 3,
} VlmOutputMode;

/* Request mode */
typedef enum {
  VLM_REQUEST_MODE_CHAT_COMPLETIONS,
  /* VLM_REQUEST_MODE_RESPONSES — reserved for future */
} VlmRequestMode;

typedef enum {
  VLM_ERROR_POLICY_SKIP = 0,
  VLM_ERROR_POLICY_BUS  = 1,
  VLM_ERROR_POLICY_SIGNAL = 2,
} VlmErrorPolicy;

/* Forward declaration for worker thread data */
typedef struct _VlmWorkerRequest VlmWorkerRequest;
typedef struct _VlmWorkerResult VlmWorkerResult;

struct _VlmWorkerRequest {
  guchar **image_data;
  gsize *image_sizes;
  gint64 *pts_values;
  gint num_frames;
  GstBuffer **buffers;
  GstVlmVision *element;
};

struct _VlmWorkerResult {
  gchar *description;
  gchar *raw_json;
  gchar *error_message;
  gint http_status;
  GstBuffer *original_buffer;
  GstVlmVision *element;
};

struct _GstVlmVision {
  GstBaseTransform parent;

  /* Properties — endpoint config */
  gchar *base_url;
  gchar *api_key;
  gchar *model;
  gchar *system_prompt;
  gchar *user_prompt;
  gchar *profile_name;
  /* Properties — sampling */
  gdouble analysis_interval_sec;
  gint frames_per_request;
  VlmRequestMode request_mode;

  /* Properties — generation */
  gchar **stop_sequences;
  gdouble temperature;
  gint max_output_tokens;
  gdouble top_p;

  /* Properties — output */
  VlmOutputMode output_mode;

  /* Properties — concurrency */
  gint timeout_sec;
  gint max_inflight;
  VlmQueuePolicy queue_policy;
  VlmErrorPolicy error_policy;

  /* Properties — template override */
  gchar *template_body_path;
  gchar *template_headers_path;
  gchar *template_response_path;

  /* Internal — async architecture */
  GAsyncQueue *request_queue;
  GAsyncQueue *result_queue;
  GThread *worker_thread;
  gboolean worker_running;
  GSource *result_source;

  /* Internal — video state */
  GstVideoInfo input_video_info;
  gboolean input_is_jpeg;

  /* Internal — sampling */
  gboolean analysis_in_progress;
  GstClockTime analysis_interval;
  GstClockTime last_analysis_time_ns;
  gint frame_count_since_last_request;
  GPtrArray *pending_frames;  /* VlmPendingFrame* accumulator */

  /* Internal — concurrency */
  GMutex inflight_mutex;
  gint current_inflight;
  gint64 last_dispatched_pts_ns;

  /* Internal — result staging */
  GMutex pending_mutex;
  gchar *pending_description;
  gchar *pending_raw_json;

  /* Internal — backends (owned) */
  VlmSerializer *serializer;
  VlmResponseParser *response_parser;

  /* Guard to avoid re-entering transform_ip after teardown starts */
  gboolean stopping;
};

struct _GstVlmVisionClass {
  GstBaseTransformClass parent_class;

  /* Signals */
  void (*description_received)(GstVlmVision *self, const gchar *description, GstBuffer *buffer);
  void (*analysis_error)(GstVlmVision *self, const gchar *error_message, gint http_status);
};

GType gst_vlm_vision_get_type(void);

enum {
  VLM_SIGNAL_DESCRIPTION_RECEIVED,
  VLM_SIGNAL_ANALYSIS_ERROR,
  VLM_LAST_SIGNAL
};

G_END_DECLS

#endif /* __GST_VLM_VISION_H__ */
