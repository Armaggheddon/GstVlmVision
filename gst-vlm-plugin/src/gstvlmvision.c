#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvlmvision.h"
#include "jpeg-encoder.h"
#include "http-client.h"
#include "serializer-openai-chat.h"
#include "serializer-template.h"
#include "response-parser-openai-chat.h"
#include "response-parser-template.h"

#include <gst/video/video.h>
#include <glib/gbase64.h>
#include <curl/curl.h>
#include <string.h>

GST_DEBUG_CATEGORY(gst_vlm_vision_debug_category);
#define GST_CAT_DEFAULT gst_vlm_vision_debug_category

G_DEFINE_TYPE(GstVlmVision, gst_vlm_vision, GST_TYPE_BASE_TRANSFORM);

/* ─── Properties ─────────────────────────────────────────────────────── */

enum {
  PROP_0,
  PROP_BASE_URL,
  PROP_API_KEY,
  PROP_MODEL,
  PROP_SYSTEM_PROMPT,
  PROP_USER_PROMPT,
  PROP_PROFILE,
  PROP_ANALYSIS_INTERVAL,
  PROP_FRAMES_PER_REQUEST,
  PROP_REQUEST_MODE,
  PROP_STOP_SEQUENCES,
  PROP_TEMPERATURE,
  PROP_MAX_OUTPUT_TOKENS,
  PROP_TOP_P,
  PROP_OUTPUT_MODE,
  PROP_TIMEOUT,
  PROP_MAX_INFLIGHT,
  PROP_QUEUE_POLICY,
  PROP_TEMPLATE_BODY,
  PROP_TEMPLATE_HEADERS,
  PROP_TEMPLATE_RESPONSE,
  PROP_ERROR_POLICY,
  PROP_LAST
};

#define GST_TYPE_VLM_REQUEST_MODE (gst_vlm_request_mode_get_type())
static GType
gst_vlm_request_mode_get_type(void) {
  static GType type = 0;
  static const GEnumValue values[] = {
    { VLM_REQUEST_MODE_CHAT_COMPLETIONS, "Chat Completions", "chat-completions" },
    { 0, NULL, NULL }
  };
  if (g_once_init_enter(&type)) {
    GType t = g_enum_register_static("VlmRequestMode", values);
    g_once_init_leave(&type, t);
  }
  return type;
}

#define GST_TYPE_VLM_OUTPUT_MODE (gst_vlm_output_mode_get_type())
static GType
gst_vlm_output_mode_get_type(void) {
  static GType type = 0;
  static const GFlagsValue values[] = {
    { VLM_OUTPUT_META,   "GstMeta attachment",        "meta" },
    { VLM_OUTPUT_SIGNAL, "GObject signal emission",   "signal" },
    { VLM_OUTPUT_BUS,    "GstBus message posting",    "bus" },
    { VLM_OUTPUT_JSON,   "Include raw JSON in output","json" },
    { 0, NULL, NULL }
  };
  if (g_once_init_enter(&type)) {
    GType t = g_flags_register_static("VlmOutputMode", values);
    g_once_init_leave(&type, t);
  }
  return type;
}

#define GST_TYPE_VLM_QUEUE_POLICY (gst_vlm_queue_policy_get_type())
static GType
gst_vlm_queue_policy_get_type(void) {
  static GType type = 0;
  static const GEnumValue values[] = {
    { VLM_QUEUE_POLICY_DROP,  "Drop oldest request when full", "drop" },
    { VLM_QUEUE_POLICY_BLOCK, "Block until a slot opens", "block" },
    { 0, NULL, NULL }
  };
  if (g_once_init_enter(&type)) {
    GType t = g_enum_register_static("VlmQueuePolicy", values);
    g_once_init_leave(&type, t);
  }
  return type;
}

#define GST_TYPE_VLM_ERROR_POLICY (gst_vlm_error_policy_get_type())
static GType
gst_vlm_error_policy_get_type(void) {
  static GType type = 0;
  static const GEnumValue values[] = {
    { VLM_ERROR_POLICY_SKIP,  "Silently skip errors",         "skip" },
    { VLM_ERROR_POLICY_BUS,   "Post error as GstBus message", "bus" },
    { VLM_ERROR_POLICY_SIGNAL,"Emit analysis-error signal",   "signal" },
    { 0, NULL, NULL }
  };
  if (g_once_init_enter(&type)) {
    GType t = g_enum_register_static("VlmErrorPolicy", values);
    g_once_init_leave(&type, t);
  }
  return type;
}

/* ─── Frame accumulation ─────────────────────────────────────────────── */

typedef struct {
  guchar *jpeg_data;
  gsize jpeg_size;
  gint64 pts_ns;
  GstBuffer *buffer;
} VlmPendingFrame;

static void
pending_frame_free(VlmPendingFrame *f) {
  if (f) {
    g_free(f->jpeg_data);
    if (f->buffer) gst_buffer_unref(f->buffer);
    g_free(f);
  }
}

/* ─── Helper: configure serializer / response parser ─────────────────── */

static void
vlm_vision_setup_backends(GstVlmVision *self) {
  /* Free old */
  if (self->serializer) {
    if (g_strcmp0(self->serializer->name, "template") == 0)
      serializer_template_free(self->serializer);
    else
      serializer_openai_chat_free(self->serializer);
    self->serializer = NULL;
  }
  if (self->response_parser) {
    if (g_strcmp0(self->response_parser->name, "template") == 0)
      response_parser_template_free(self->response_parser);
    else
      response_parser_openai_chat_free(self->response_parser);
    self->response_parser = NULL;
  }

  /* Serializer */
  if (self->template_body_path || self->template_headers_path) {
    self->serializer = serializer_template_new(
        self->template_body_path,
        self->template_headers_path,
        NULL);
  } else {
    self->serializer = serializer_openai_chat_new();
  }

  /* Response parser */
  if (self->template_response_path) {
    self->response_parser = response_parser_template_new(
        "$.choices[0].message.content",
        "$.error.message",
        NULL);
  } else {
    self->response_parser = response_parser_openai_chat_new();
  }

}

/* ─── Worker Thread ──────────────────────────────────────────────────── */

static gpointer
vlm_worker_thread_func(gpointer data) {
  GstVlmVision *self = GST_VLM_VISION(data);
  VlmWorkerRequest *wreq;

  curl_global_init(CURL_GLOBAL_ALL);

  while (self->worker_running) {
    wreq = g_async_queue_pop(self->request_queue);

    if (!self->worker_running) {
      if (wreq) {
        for (int i = 0; i < wreq->num_frames; i++)
          g_free(wreq->image_data[i]);
        g_free(wreq->image_data);
        g_free(wreq->image_sizes);
        g_free(wreq->pts_values);
        for (int i = 0; wreq->buffers && wreq->buffers[i]; i++)
          gst_buffer_unref(wreq->buffers[i]);
        g_free(wreq->buffers);
        g_free(wreq);
      }
      break;
    }

    if (!wreq) continue;

    if (!wreq->image_data || wreq->num_frames == 0) {
      g_free(wreq);
      continue;
    }

    /* Build VlmFramePayload array from all accumulated frames */
    VlmFramePayload *frames = g_newa(VlmFramePayload, wreq->num_frames);
    for (int i = 0; i < wreq->num_frames; i++) {
      frames[i].data = wreq->image_data[i];
      frames[i].size = wreq->image_sizes[i];
      frames[i].pts_ns = wreq->pts_values[i];
      frames[i].frame_index = i;
    }

    VlmRequest vreq = {
      .system_prompt = self->system_prompt,
      .user_prompt = self->user_prompt,
      .frames = frames,
      .num_frames = wreq->num_frames,
      .model_name = self->model,
      .temperature = self->temperature,
      .max_tokens = self->max_output_tokens,
      .top_p = self->top_p,
      .stop_sequences = self->stop_sequences,
      .response_format = NULL,
    };

    /* Serialize */
    gchar *url = self->serializer->build_url(self->serializer, self->base_url, &vreq);
    gchar **headers = self->serializer->build_headers(self->serializer, self->api_key);
    gchar *body = self->serializer->build_body(self->serializer, &vreq);

    GST_DEBUG_OBJECT(self, "Sending request to %s", url);

    /* HTTP call */
    HttpResponse *resp = http_client_send(url, headers, body,
                                          body ? strlen(body) : 0,
                                          self->timeout_sec);

    /* Send result back to main thread */
    VlmWorkerResult *wresult = g_new0(VlmWorkerResult, 1);
    wresult->element = self;

    if (resp) {
      VlmResult parsed;
      gboolean ok = self->response_parser->parse(
          self->response_parser,
          resp->data, resp->size,
          resp->http_status, &parsed);

      wresult->description = parsed.text;
      wresult->raw_json = parsed.raw_json;
      wresult->error_message = parsed.error_message;
      wresult->http_status = parsed.http_status;
      wresult->original_buffer = wreq->buffers ? wreq->buffers[0] : NULL;
      if (wreq->buffers) wreq->buffers[0] = NULL; /* transfer ownership */

      http_response_free(resp);

      if (!ok) {
        GST_WARNING_OBJECT(self, "API error (HTTP %d): %s",
                           wresult->http_status,
                           wresult->error_message ? wresult->error_message : "unknown");
      }
    } else {
      wresult->error_message = g_strdup("HTTP request failed (curl error)");
      wresult->http_status = 0;
      wresult->original_buffer = wreq->buffers ? wreq->buffers[0] : NULL;
      if (wreq->buffers) wreq->buffers[0] = NULL;
    }

    g_async_queue_push(self->result_queue, wresult);
    if (self->result_source)
      g_source_set_ready_time(self->result_source, 0);

    /* Cleanup */
    g_free(url);
    g_strfreev(headers);
    g_free(body);
    for (int i = 0; i < wreq->num_frames; i++)
      g_free(wreq->image_data[i]);
    g_free(wreq->image_data);
    g_free(wreq->image_sizes);
    g_free(wreq->pts_values);
    for (int i = 0; i < wreq->num_frames; i++)
      if (wreq->buffers && wreq->buffers[i])
        gst_buffer_unref(wreq->buffers[i]);
    g_free(wreq->buffers);
    g_free(wreq);
  }

  curl_global_cleanup();
  GST_DEBUG_OBJECT(self, "Worker thread finished.");
  return NULL;
}

/* ─── Dispose ─────────────────────────────────────────────────────────── */

static void
gst_vlm_vision_dispose(GObject *object) {
  GstVlmVision *self = GST_VLM_VISION(object);

  if (self->worker_running) {
    self->worker_running = FALSE;
    if (self->request_queue) {
      VlmWorkerRequest *dummy = g_new0(VlmWorkerRequest, 1);
      g_async_queue_push(self->request_queue, dummy);
    }
    if (self->worker_thread) {
      g_thread_join(self->worker_thread);
      self->worker_thread = NULL;
    }
  }
  /* Safety net: join even if stop() didn't run (direct NULL transition) */
  if (self->worker_thread) {
    self->worker_running = FALSE;
    g_thread_join(self->worker_thread);
    self->worker_thread = NULL;
  }

  if (self->request_queue) {
    VlmWorkerRequest *r;
    while ((r = g_async_queue_try_pop(self->request_queue))) {
      for (int i = 0; i < r->num_frames; i++)
        g_free(r->image_data[i]);
      g_free(r->image_data);
      g_free(r->image_sizes);
      g_free(r->pts_values);
      for (int i = 0; r->buffers && r->buffers[i]; i++)
        gst_buffer_unref(r->buffers[i]);
      g_free(r->buffers);
      g_free(r);
    }
    g_async_queue_unref(self->request_queue);
    self->request_queue = NULL;
  }

  /* Free pending frame accumulator */
  if (self->pending_frames) {
    while (self->pending_frames->len > 0) {
      VlmPendingFrame *f = (VlmPendingFrame *)g_ptr_array_steal_index(
          self->pending_frames, 0);
      pending_frame_free(f);
    }
    g_ptr_array_unref(self->pending_frames);
    self->pending_frames = NULL;
  }

  if (self->result_queue) {
    VlmWorkerResult *r;
    while ((r = g_async_queue_try_pop(self->result_queue))) {
      g_free(r->description);
      g_free(r->raw_json);
      g_free(r->error_message);
      if (r->original_buffer) gst_buffer_unref(r->original_buffer);
      g_free(r);
    }
    g_async_queue_unref(self->result_queue);
    self->result_queue = NULL;
  }

  if (self->result_source) {
    g_source_destroy(self->result_source);
    g_source_unref(self->result_source);
    self->result_source = NULL;
  }

  g_free(self->base_url);
  self->base_url = NULL;
  g_free(self->api_key);
  self->api_key = NULL;
  g_free(self->model);
  self->model = NULL;
  g_free(self->system_prompt);
  self->system_prompt = NULL;
  g_free(self->user_prompt);
  self->user_prompt = NULL;
  g_free(self->profile_name);
  self->profile_name = NULL;

  if (self->stop_sequences) g_strfreev(self->stop_sequences);
  self->stop_sequences = NULL;

  g_free(self->pending_description);
  self->pending_description = NULL;
  g_free(self->pending_raw_json);
  self->pending_raw_json = NULL;
  g_mutex_clear(&self->pending_mutex);

  g_free(self->template_body_path);
  self->template_body_path = NULL;
  g_free(self->template_headers_path);
  self->template_headers_path = NULL;
  g_free(self->template_response_path);
  self->template_response_path = NULL;

  if (self->serializer) {
    if (g_strcmp0(self->serializer->name, "template") == 0)
      serializer_template_free(self->serializer);
    else
      serializer_openai_chat_free(self->serializer);
    self->serializer = NULL;
  }
  if (self->response_parser) {
    if (g_strcmp0(self->response_parser->name, "template") == 0)
      response_parser_template_free(self->response_parser);
    else
      response_parser_openai_chat_free(self->response_parser);
    self->response_parser = NULL;
  }

  g_mutex_clear(&self->inflight_mutex);

  G_OBJECT_CLASS(gst_vlm_vision_parent_class)->dispose(object);
}

/* ─── Result processing (main context) ──────────────────────────────── */

static gboolean
process_vlm_result_callback(gpointer data) {
  GstVlmVision *self = GST_VLM_VISION(data);
  VlmWorkerResult *result;

  while ((result = g_async_queue_try_pop(self->result_queue))) {
    /* Decrement inflight counter */
    g_mutex_lock(&self->inflight_mutex);
    if (self->current_inflight > 0)
      self->current_inflight--;
    g_mutex_unlock(&self->inflight_mutex);

    if (result->description) {
      GST_INFO_OBJECT(self, "Received description: %s", result->description);

      g_mutex_lock(&self->pending_mutex);
      g_free(self->pending_description);
      self->pending_description = g_strdup(result->description);
      g_free(self->pending_raw_json);
      self->pending_raw_json = result->raw_json;
      result->raw_json = NULL;
      g_mutex_unlock(&self->pending_mutex);
    } else if (result->error_message) {
      GST_WARNING_OBJECT(self, "Analysis error (HTTP %d): %s",
                         result->http_status, result->error_message);

      switch (self->error_policy) {
        case VLM_ERROR_POLICY_SKIP:
          break;
        case VLM_ERROR_POLICY_BUS: {
          GstStructure *s = gst_structure_new("vlmvision-error",
              "message", G_TYPE_STRING, result->error_message,
              "http-status", G_TYPE_INT, result->http_status,
              NULL);
          gst_element_post_message(GST_ELEMENT(self),
              gst_message_new_element(GST_OBJECT(self), s));
          break;
        }
        case VLM_ERROR_POLICY_SIGNAL:
          g_signal_emit(self,
              g_signal_lookup("analysis-error", GST_TYPE_VLM_VISION),
              0, result->error_message, result->http_status);
          break;
      }
    }

    g_atomic_int_set(&self->analysis_in_progress, FALSE);

    /* Emit description signal (only on success) */
    if ((self->output_mode & VLM_OUTPUT_SIGNAL) && result->original_buffer
        && result->description) {
      g_signal_emit(self,
          g_signal_lookup("description-received", GST_TYPE_VLM_VISION),
          0, result->description, result->original_buffer);
    }

    /* Post bus message (only on success) */
    if ((self->output_mode & VLM_OUTPUT_BUS) && result->description) {
      GstStructure *s = gst_structure_new("vlmvision-result",
          "description", G_TYPE_STRING, result->description,
          "pts", G_TYPE_UINT64,
              result->original_buffer ? GST_BUFFER_PTS(result->original_buffer) : GST_CLOCK_TIME_NONE,
          "model", G_TYPE_STRING, self->model ? self->model : "",
          "raw-json", G_TYPE_STRING,
              self->pending_raw_json ? self->pending_raw_json : "",
          NULL);
      gst_element_post_message(GST_ELEMENT(self),
          gst_message_new_element(GST_OBJECT(self), s));
    }

    g_free(result->description);
    g_free(result->raw_json);
    g_free(result->error_message);
    if (result->original_buffer) gst_buffer_unref(result->original_buffer);
    g_free(result);
  }

  return G_SOURCE_CONTINUE;
}

/* ─── start / stop / set_caps ────────────────────────────────────────── */

static gboolean
gst_vlm_vision_start(GstBaseTransform *trans) {
  GstVlmVision *self = GST_VLM_VISION(trans);
  GST_INFO_OBJECT(self, "Starting");

  self->last_analysis_time_ns = 0;
  self->frame_count_since_last_request = 0;
  g_atomic_int_set(&self->analysis_in_progress, FALSE);
  self->current_inflight = 0;
  self->last_dispatched_pts_ns = GST_CLOCK_TIME_NONE;
  self->stopping = FALSE;

  /* Init frame accumulator */
  if (!self->pending_frames)
    self->pending_frames = g_ptr_array_new();

  /* Setup backends */
  vlm_vision_setup_backends(self);

  self->worker_running = TRUE;
  gchar *thread_name = g_strdup_printf("%s-worker", GST_OBJECT_NAME(self));
  self->worker_thread = g_thread_new(thread_name, vlm_worker_thread_func, self);
  g_free(thread_name);
  GST_INFO_OBJECT(self, "Worker thread created.");

  if (!self->result_source) {
    self->result_source = g_idle_source_new();
    g_source_set_priority(self->result_source, G_PRIORITY_DEFAULT_IDLE);
    g_source_set_callback(self->result_source, process_vlm_result_callback, self, NULL);
    g_source_attach(self->result_source, g_main_context_get_thread_default());
  }

  return TRUE;
}

static gboolean
gst_vlm_vision_stop(GstBaseTransform *trans) {
  GstVlmVision *self = GST_VLM_VISION(trans);
  GST_INFO_OBJECT(self, "Stopping");
  self->stopping = TRUE;

  /* Flush partial frame accumulator so we don't leak */
  if (self->pending_frames) {
    while (self->pending_frames->len > 0) {
      VlmPendingFrame *f = (VlmPendingFrame *)g_ptr_array_steal_index(
          self->pending_frames, 0);
      pending_frame_free(f);
    }
  }

  if (self->worker_running) {
    self->worker_running = FALSE;
    if (self->request_queue) {
      VlmWorkerRequest *dummy = g_new0(VlmWorkerRequest, 1);
      g_async_queue_push(self->request_queue, dummy);
    }
    if (self->worker_thread) {
      g_thread_join(self->worker_thread);
      self->worker_thread = NULL;
    }
    if (self->result_source) {
      g_source_destroy(self->result_source);
      g_source_unref(self->result_source);
      self->result_source = NULL;
    }
  }

  return TRUE;
}

static gboolean
gst_vlm_vision_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
  GstVlmVision *self = GST_VLM_VISION(trans);
  GST_INFO_OBJECT(self, "Setting caps: in %" GST_PTR_FORMAT ", out %" GST_PTR_FORMAT, incaps, outcaps);

  GstStructure *s = gst_caps_get_structure(incaps, 0);
  const gchar *name = gst_structure_get_name(s);

  if (g_str_equal(name, "image/jpeg")) {
    self->input_is_jpeg = TRUE;
  } else {
    self->input_is_jpeg = FALSE;
    if (!gst_video_info_from_caps(&self->input_video_info, incaps)) {
      GST_ERROR_OBJECT(self, "Failed to parse video info from caps");
      return FALSE;
    }
  }

  return TRUE;
}

/* ─── transform_ip ───────────────────────────────────────────────────── */

static GstFlowReturn
gst_vlm_vision_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
  GstVlmVision *self = GST_VLM_VISION(trans);
  GstClockTime current_time = GST_BUFFER_PTS(buf);

  if (self->stopping)
    goto attach_meta;

  /* Determine if it's time to sample */
  gboolean should_sample = FALSE;
  if (!g_atomic_int_get(&self->analysis_in_progress)) {
    if (!GST_CLOCK_TIME_IS_VALID(self->last_analysis_time_ns) ||
        !GST_CLOCK_TIME_IS_VALID(current_time)) {
      should_sample = TRUE;
    } else if (current_time - self->last_analysis_time_ns >= self->analysis_interval) {
      should_sample = TRUE;
    }
  }

  if (should_sample) {
    if (!self->worker_running || !self->worker_thread) {
      GST_WARNING_OBJECT(self, "Worker not running. Skipping analysis.");
      goto attach_meta;
    }

    if (!self->model || self->model[0] == '\0')
      GST_WARNING_OBJECT(self, "Model not set — API calls may fail. Set the 'model' property.");

    /* Backpressure: check inflight limit */
    g_mutex_lock(&self->inflight_mutex);
    if (self->current_inflight >= self->max_inflight) {
      if (self->queue_policy == VLM_QUEUE_POLICY_DROP) {
        VlmWorkerRequest *old = g_async_queue_try_pop(self->request_queue);
        if (old) {
          GST_DEBUG_OBJECT(self, "Dropping oldest queued request (inflight=%d, max=%d)",
                           self->current_inflight, self->max_inflight);
          if (self->current_inflight > 0)
            self->current_inflight--;
          for (int i = 0; i < old->num_frames; i++)
            g_free(old->image_data[i]);
          g_free(old->image_data);
          g_free(old->image_sizes);
          g_free(old->pts_values);
          for (int i = 0; i < old->num_frames; i++)
            if (old->buffers && old->buffers[i])
              gst_buffer_unref(old->buffers[i]);
          g_free(old->buffers);
          g_free(old);
        }
      } else {
        /* BLOCK: skip this frame, don't queue */
        g_mutex_unlock(&self->inflight_mutex);
        goto attach_meta;
      }
    }
    g_mutex_unlock(&self->inflight_mutex);

    GstMapInfo map;
    if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
      unsigned char *jpeg_data = NULL;
      gsize jpeg_size = 0;

      if (!self->input_is_jpeg) {
        if (!encode_frame_to_jpeg(GST_OBJECT(self), &self->input_video_info,
                                   map.data, map.size, &jpeg_data, &jpeg_size)) {
          GST_ERROR_OBJECT(self, "Failed to encode frame to JPEG");
          gst_buffer_unmap(buf, &map);
          return GST_FLOW_ERROR;
        }
      } else {
        jpeg_data = g_malloc(map.size);
        if (jpeg_data) {
          memcpy(jpeg_data, map.data, map.size);
          jpeg_size = map.size;
        } else {
          GST_ERROR_OBJECT(self, "Failed to allocate memory for JPEG data copy");
          gst_buffer_unmap(buf, &map);
          return GST_FLOW_ERROR;
        }
      }

      /* Accumulate frame */
      VlmPendingFrame *pf = g_new(VlmPendingFrame, 1);
      pf->jpeg_data = jpeg_data;
      pf->jpeg_size = jpeg_size;
      pf->pts_ns = current_time;
      pf->buffer = gst_buffer_ref(buf);

      g_ptr_array_add(self->pending_frames, pf);
      gst_buffer_unmap(buf, &map);

      self->last_analysis_time_ns = current_time;

      GST_DEBUG_OBJECT(self, "Accumulated frame %d/%d (PTS=%" GST_TIME_FORMAT ")",
                       self->pending_frames->len, self->frames_per_request,
                       GST_TIME_ARGS(current_time));

      /* Dispatch when we have enough frames */
      if (self->pending_frames->len >= self->frames_per_request) {
        gint n = self->pending_frames->len;

        VlmWorkerRequest *wreq = g_new0(VlmWorkerRequest, 1);
        wreq->num_frames = n;
        wreq->image_data = g_new0(guchar *, n);
        wreq->image_sizes = g_new0(gsize, n);
        wreq->pts_values = g_new0(gint64, n);
        wreq->buffers = g_new0(GstBuffer *, n + 1); /* NULL-terminated */
        wreq->element = self;

        for (gint i = 0; i < n; i++) {
          VlmPendingFrame *pf2 = g_ptr_array_index(self->pending_frames, i);
          wreq->image_data[i] = pf2->jpeg_data;
          pf2->jpeg_data = NULL; /* transfer ownership */
          wreq->image_sizes[i] = pf2->jpeg_size;
          wreq->pts_values[i] = pf2->pts_ns;
          wreq->buffers[i] = pf2->buffer;
          pf2->buffer = NULL; /* transfer ownership */
        }

        /* Clear accumulator */
        while (self->pending_frames->len > 0) {
          VlmPendingFrame *pf3 = (VlmPendingFrame *)g_ptr_array_steal_index(
              self->pending_frames, 0);
          pending_frame_free(pf3);
        }

        g_mutex_lock(&self->inflight_mutex);
        self->current_inflight++;
        g_mutex_unlock(&self->inflight_mutex);

        g_atomic_int_set(&self->analysis_in_progress, TRUE);
        self->last_dispatched_pts_ns = current_time;

        g_async_queue_push(self->request_queue, wreq);
        GST_DEBUG_OBJECT(self, "Dispatched %d-frame request (inflight=%d)", n,
                         self->current_inflight);
      }
    } else {
      GST_WARNING_OBJECT(self, "Failed to map buffer for analysis.");
    }
  }

attach_meta:
  /* Attach pending description as metadata */
  g_mutex_lock(&self->pending_mutex);
  if ((self->output_mode & VLM_OUTPUT_META) && self->pending_description) {
    if (gst_buffer_is_writable(buf)) {
      gst_buffer_add_vlm_vision_meta(buf, self->pending_description);
    } else {
      GST_DEBUG_OBJECT(self, "Buffer not writable, skipping metadata for this frame.");
    }
  }
  g_mutex_unlock(&self->pending_mutex);

  return GST_FLOW_OK;
}

/* ─── Properties ─────────────────────────────────────────────────────── */

static void
gst_vlm_vision_set_property(GObject *object, guint prop_id,
                            const GValue *value, GParamSpec *pspec) {
  GstVlmVision *self = GST_VLM_VISION(object);

  switch (prop_id) {
    case PROP_BASE_URL:
      g_free(self->base_url);
      self->base_url = g_value_dup_string(value);
      break;
    case PROP_API_KEY:
      g_free(self->api_key);
      self->api_key = g_value_dup_string(value);
      break;
    case PROP_MODEL:
      g_free(self->model);
      self->model = g_value_dup_string(value);
      break;
    case PROP_SYSTEM_PROMPT:
      g_free(self->system_prompt);
      self->system_prompt = g_value_dup_string(value);
      break;
    case PROP_USER_PROMPT:
      g_free(self->user_prompt);
      self->user_prompt = g_value_dup_string(value);
      break;
    case PROP_PROFILE:
      g_free(self->profile_name);
      self->profile_name = g_value_dup_string(value);
      break;
    case PROP_ANALYSIS_INTERVAL:
      self->analysis_interval_sec = g_value_get_double(value);
      self->analysis_interval = GST_SECOND * self->analysis_interval_sec;
      break;
    case PROP_FRAMES_PER_REQUEST:
      self->frames_per_request = g_value_get_int(value);
      break;
    case PROP_REQUEST_MODE:
      self->request_mode = g_value_get_enum(value);
      break;
    case PROP_STOP_SEQUENCES:
      if (self->stop_sequences) g_strfreev(self->stop_sequences);
      self->stop_sequences = g_value_dup_boxed(value);
      break;
    case PROP_TEMPERATURE:
      self->temperature = g_value_get_double(value);
      break;
    case PROP_MAX_OUTPUT_TOKENS:
      self->max_output_tokens = g_value_get_int(value);
      break;
    case PROP_TOP_P:
      self->top_p = g_value_get_double(value);
      break;
    case PROP_OUTPUT_MODE:
      self->output_mode = g_value_get_flags(value);
      break;
    case PROP_TIMEOUT:
      self->timeout_sec = g_value_get_int(value);
      break;
    case PROP_MAX_INFLIGHT:
      self->max_inflight = g_value_get_int(value);
      break;
    case PROP_QUEUE_POLICY:
      self->queue_policy = g_value_get_enum(value);
      break;
    case PROP_TEMPLATE_BODY:
      g_free(self->template_body_path);
      self->template_body_path = g_value_dup_string(value);
      break;
    case PROP_TEMPLATE_HEADERS:
      g_free(self->template_headers_path);
      self->template_headers_path = g_value_dup_string(value);
      break;
    case PROP_TEMPLATE_RESPONSE:
      g_free(self->template_response_path);
      self->template_response_path = g_value_dup_string(value);
      break;
    case PROP_ERROR_POLICY:
      self->error_policy = g_value_get_enum(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_vlm_vision_get_property(GObject *object, guint prop_id,
                            GValue *value, GParamSpec *pspec) {
  GstVlmVision *self = GST_VLM_VISION(object);

  switch (prop_id) {
    case PROP_BASE_URL:
      g_value_set_string(value, self->base_url);
      break;
    case PROP_API_KEY:
      g_value_set_string(value, self->api_key);
      break;
    case PROP_MODEL:
      g_value_set_string(value, self->model);
      break;
    case PROP_SYSTEM_PROMPT:
      g_value_set_string(value, self->system_prompt);
      break;
    case PROP_USER_PROMPT:
      g_value_set_string(value, self->user_prompt);
      break;
    case PROP_PROFILE:
      g_value_set_string(value, self->profile_name);
      break;
    case PROP_ANALYSIS_INTERVAL:
      g_value_set_double(value, self->analysis_interval_sec);
      break;
    case PROP_FRAMES_PER_REQUEST:
      g_value_set_int(value, self->frames_per_request);
      break;
    case PROP_REQUEST_MODE:
      g_value_set_enum(value, self->request_mode);
      break;
    case PROP_STOP_SEQUENCES:
      g_value_set_boxed(value, self->stop_sequences);
      break;
    case PROP_TEMPERATURE:
      g_value_set_double(value, self->temperature);
      break;
    case PROP_MAX_OUTPUT_TOKENS:
      g_value_set_int(value, self->max_output_tokens);
      break;
    case PROP_TOP_P:
      g_value_set_double(value, self->top_p);
      break;
    case PROP_OUTPUT_MODE:
      g_value_set_flags(value, self->output_mode);
      break;
    case PROP_TIMEOUT:
      g_value_set_int(value, self->timeout_sec);
      break;
    case PROP_MAX_INFLIGHT:
      g_value_set_int(value, self->max_inflight);
      break;
    case PROP_QUEUE_POLICY:
      g_value_set_enum(value, self->queue_policy);
      break;
    case PROP_TEMPLATE_BODY:
      g_value_set_string(value, self->template_body_path);
      break;
    case PROP_TEMPLATE_HEADERS:
      g_value_set_string(value, self->template_headers_path);
      break;
    case PROP_TEMPLATE_RESPONSE:
      g_value_set_string(value, self->template_response_path);
      break;
    case PROP_ERROR_POLICY:
      g_value_set_enum(value, self->error_policy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

/* ─── class_init ─────────────────────────────────────────────────────── */

static void
gst_vlm_vision_class_init(GstVlmVisionClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

  GST_DEBUG_CATEGORY_INIT(gst_vlm_vision_debug_category, "vlmvision", 0,
                          "VLM Vision Plugin");

  static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
      "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS("video/x-raw, format={RGB, BGR, RGBA, BGRA}; image/jpeg"));

  static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
      "src", GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS("video/x-raw, format={RGB, BGR, RGBA, BGRA}; image/jpeg"));

  gst_element_class_add_pad_template(element_class,
      gst_static_pad_template_get(&sink_template));
  gst_element_class_add_pad_template(element_class,
      gst_static_pad_template_get(&src_template));

  gst_element_class_set_static_metadata(element_class,
      "VLM Vision Processor",
      "Filter/Analyzer/Video",
      "Processes video frames with OpenAI-compatible vision-language models",
      "https://github.com/Armaggheddon/GstVlmVision");

  gobject_class->dispose = gst_vlm_vision_dispose;
  gobject_class->set_property = gst_vlm_vision_set_property;
  gobject_class->get_property = gst_vlm_vision_get_property;

  base_transform_class->start = gst_vlm_vision_start;
  base_transform_class->stop = gst_vlm_vision_stop;
  base_transform_class->set_caps = gst_vlm_vision_set_caps;
  base_transform_class->transform_ip = gst_vlm_vision_transform_ip;

  /* ─── Install properties ──────────────────────────────────────────── */

  g_object_class_install_property(gobject_class, PROP_BASE_URL,
      g_param_spec_string("base-url", "Base URL",
          "Base URL for the OpenAI-compatible API endpoint",
          "https://api.openai.com",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_API_KEY,
      g_param_spec_string("api-key", "API Key",
          "API key for the endpoint (can be empty for local servers)",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_MODEL,
      g_param_spec_string("model", "Model",
          "Model name (e.g. 'gpt-4o', 'gemini-2.0-flash', 'llava-v1.6-34b')",
          "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_SYSTEM_PROMPT,
      g_param_spec_string("system-prompt", "System Prompt",
          "System prompt (can be empty)",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_USER_PROMPT,
      g_param_spec_string("user-prompt", "User Prompt",
          "Text prompt describing what to ask about the image(s)",
          "Describe what you see in this image.",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_PROFILE,
      g_param_spec_string("profile", "Capability Profile",
          "API compatibility profile (default: openai)",
          "openai",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_ANALYSIS_INTERVAL,
      g_param_spec_double("analysis-interval", "Analysis Interval",
          "Time interval in seconds between consecutive analysis calls",
          0.1, 3600.0, 5.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_FRAMES_PER_REQUEST,
      g_param_spec_int("frames-per-request", "Frames Per Request",
          "Number of frames to include per request (1 for now, multi-frame later)",
          1, 10, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_REQUEST_MODE,
      g_param_spec_enum("request-mode", "Request Mode",
          "API request mode (chat-completions for now)",
          GST_TYPE_VLM_REQUEST_MODE, VLM_REQUEST_MODE_CHAT_COMPLETIONS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_STOP_SEQUENCES,
      g_param_spec_boxed("stop-sequences", "Stop Sequences",
          "Strings that stop generation when encountered",
          G_TYPE_STRV,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_TEMPERATURE,
      g_param_spec_double("temperature", "Temperature",
          "Controls randomness (0.0 to 2.0)",
          0.0, 2.0, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_MAX_OUTPUT_TOKENS,
      g_param_spec_int("max-output-tokens", "Max Output Tokens",
          "Maximum number of tokens to generate",
          1, G_MAXINT, 800,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_TOP_P,
      g_param_spec_double("top-p", "Top P",
          "Cumulative probability cutoff for token selection (0.0 to 1.0)",
          0.0, 1.0, 0.8,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_OUTPUT_MODE,
      g_param_spec_flags("output-mode", "Output Mode",
          "How to emit results (bitmask: 1=meta, 2=signal, 4=bus, 8=raw-json)",
          GST_TYPE_VLM_OUTPUT_MODE,
          VLM_OUTPUT_META | VLM_OUTPUT_SIGNAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_TIMEOUT,
      g_param_spec_int("timeout", "Timeout",
          "HTTP request timeout in seconds",
          1, 600, 30,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_MAX_INFLIGHT,
      g_param_spec_int("max-inflight", "Max Inflight",
          "Maximum number of concurrent API requests",
          1, 16, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_QUEUE_POLICY,
      g_param_spec_enum("queue-policy", "Queue Policy",
          "What to do when max-inflight is reached",
          GST_TYPE_VLM_QUEUE_POLICY, VLM_QUEUE_POLICY_DROP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_TEMPLATE_BODY,
      g_param_spec_string("template-body", "Template Body",
          "Path to a file with a Go-template style body override",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_TEMPLATE_HEADERS,
      g_param_spec_string("template-headers", "Template Headers",
          "Path to a file with per-line header templates",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_TEMPLATE_RESPONSE,
      g_param_spec_string("template-response", "Template Response",
          "JSON path expression for extracting text from nonstandard responses",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property(gobject_class, PROP_ERROR_POLICY,
      g_param_spec_enum("error-policy", "Error Policy",
          "How to handle API errors (skip, bus, signal)",
          GST_TYPE_VLM_ERROR_POLICY, VLM_ERROR_POLICY_SKIP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  /* ─── Signals ─────────────────────────────────────────────────────── */

  g_signal_new("description-received",
          G_TYPE_FROM_CLASS(klass),
          G_SIGNAL_RUN_LAST,
          0, NULL, NULL, NULL,
          G_TYPE_NONE, 2, G_TYPE_STRING, GST_TYPE_BUFFER);

  g_signal_new("analysis-error",
          G_TYPE_FROM_CLASS(klass),
          G_SIGNAL_RUN_LAST,
          0, NULL, NULL, NULL,
          G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);

  /* Ensure metadata is registered */
  gst_vlm_vision_meta_get_info();
}

/* ─── init ───────────────────────────────────────────────────────────── */

static void
gst_vlm_vision_init(GstVlmVision *self) {
  self->request_queue = g_async_queue_new();
  self->result_queue = g_async_queue_new();

  self->base_url = g_strdup("https://api.openai.com");
  self->api_key = NULL;
  self->model = g_strdup("");
  self->system_prompt = NULL;
  self->user_prompt = g_strdup("Describe what you see in this image.");
  self->profile_name = g_strdup("openai");

  self->analysis_interval_sec = 5.0;
  self->analysis_interval = GST_SECOND * 5.0;
  self->frames_per_request = 1;
  self->request_mode = VLM_REQUEST_MODE_CHAT_COMPLETIONS;

  self->stop_sequences = NULL;
  self->temperature = 1.0;
  self->max_output_tokens = 800;
  self->top_p = 0.8;

  self->output_mode = VLM_OUTPUT_META | VLM_OUTPUT_SIGNAL;

  self->timeout_sec = 30;
  self->max_inflight = 1;
  self->queue_policy = VLM_QUEUE_POLICY_DROP;

  self->template_body_path = NULL;
  self->template_headers_path = NULL;
  self->template_response_path = NULL;
  self->error_policy = VLM_ERROR_POLICY_SKIP;

  self->worker_running = FALSE;
  self->worker_thread = NULL;
  self->result_source = NULL;
  self->pending_frames = NULL;

  gst_video_info_init(&self->input_video_info);
  self->input_is_jpeg = FALSE;
  self->analysis_in_progress = FALSE;
  self->last_analysis_time_ns = 0;
  self->frame_count_since_last_request = 0;
  self->pending_description = NULL;
  self->pending_raw_json = NULL;

  g_mutex_init(&self->inflight_mutex);
  g_mutex_init(&self->pending_mutex);
  self->current_inflight = 0;
  self->last_dispatched_pts_ns = GST_CLOCK_TIME_NONE;
  self->stopping = FALSE;

  self->serializer = NULL;
  self->response_parser = NULL;
}
