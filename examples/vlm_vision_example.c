/*
 * VLM Vision Plugin Example 1 -- Test source
 * Pipeline: videotestsrc ! videoconvert ! vlmvision ! videoconvert ! ximagesink
 *
 * Build: make all  (from examples/ directory)
 *
 * Run with environment variables:
 *   VLM_API_KEY=sk-... VLM_BASE_URL=https://api.openai.com \
 *     VLM_MODEL=gpt-4o ./bin/vlm_vision_example
 *
 * If no environment variables are set, falls back to a mock server at
 * http://127.0.0.1:8765 with a dummy key.
 */

#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* ── App data ─────────────────────────────────────────────────── */
typedef struct _AppData {
  GstElement *pipeline;
  GMainLoop *loop;
} AppData;

/* File-scoped copy so the signal handler can reach it. */
static AppData global_data = {NULL, NULL};

/* ── Helpers ──────────────────────────────────────────────────── */

/* ── SIGINT handler ───────────────────────────────────────────── */
static void
on_sigint(int sig)
{
  if (global_data.pipeline) {
    g_print("\n[SIGINT] Sending EOS for graceful shutdown...\n");
    gst_element_send_event(global_data.pipeline, gst_event_new_eos());
  }
}

/* ── "description-received" GObject signal callback ───────────── */
static void
on_description_received(GstElement *element, gchar *description,
                        GstBuffer *buffer, gpointer user_data)
{
  GstClockTime pts = GST_BUFFER_PTS(buffer);
  printf("=================================\n");
  printf("[signal] Frame PTS: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pts));
  printf("[signal] Description: %s\n", description);
  printf("=================================\n\n");
  fflush(stdout);
}

/* ── "analysis-error" GObject signal callback ──────────────────── */
static void
on_analysis_error(GstElement *element, gchar *message, gint http_status,
                  gpointer user_data)
{
  printf("*** [signal] ANALYSIS ERROR (HTTP %d): %s ***\n\n",
         http_status, message);
  fflush(stdout);
}

/* ── Bus watch ────────────────────────────────────────────────── */
static gboolean
bus_callback(GstBus *bus, GstMessage *message, gpointer user_data)
{
  AppData *data = (AppData *)user_data;

  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
      GError *err = NULL;
      gchar *debug = NULL;
      gst_message_parse_error(message, &err, &debug);
      g_printerr("ERROR from %s: %s\n",
                 GST_OBJECT_NAME(message->src), err->message);
      if (debug)
        g_printerr("  Debug info: %s\n", debug);
      g_error_free(err);
      g_free(debug);
      g_main_loop_quit(data->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_print("[bus] End of stream.\n");
      g_main_loop_quit(data->loop);
      break;
    case GST_MESSAGE_ELEMENT: {
      const GstStructure *s = gst_message_get_structure(message);
      if (s && gst_structure_has_name(s, "vlmvision-error")) {
        const gchar *msg = gst_structure_get_string(s, "message");
        gint http_status = 0;
        gst_structure_get_int(s, "http-status", &http_status);
        printf("*** [bus] VLM VISION ERROR (HTTP %d): %s ***\n",
               http_status, msg ? msg : "(unknown)");
        fflush(stdout);
      } else if (s && gst_structure_has_name(s, "vlmvision-result")) {
        const gchar *desc    = gst_structure_get_string(s, "description");
        const gchar *model   = gst_structure_get_string(s, "model");
        const gchar *json    = gst_structure_get_string(s, "raw-json");
        const gchar *err_str = gst_structure_get_string(s, "error");
        guint64 pts = 0;
        gst_structure_get_uint64(s, "pts", &pts);

        printf("--- [bus message: vlmvision-result] ---\n");
        printf("  PTS:        %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pts));
        printf("  Model:      %s\n", model ? model : "(unknown)");
        printf("  Description: %s\n", desc ? desc : "(null)");
        if (err_str && err_str[0])
          printf("  Error:      %s\n", err_str);
        if (json && json[0])
          printf("  Raw JSON:   %zu bytes\n", strlen(json));
        printf("--------------------------------------\n");
        fflush(stdout);
      }
      break;
    }
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->pipeline)) {
        GstState old, new_, pending;
        gst_message_parse_state_changed(message, &old, &new_, &pending);
        g_print("[bus] Pipeline: %s -> %s\n",
                gst_element_state_get_name(old),
                gst_element_state_get_name(new_));
      }
      break;
    default:
      break;
  }
  return TRUE;
}

/* ── main ─────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
  GstElement *pipeline, *source, *conv1, *vlm, *conv2, *sink;
  GstBus *bus;
  AppData data;

  /* ================================================================
   *  1.  Read environment / set defaults
   * ==============================================================*/
  const gchar *api_key  = getenv("VLM_API_KEY");
  const gchar *base_url = getenv("VLM_BASE_URL");
  const gchar *model    = getenv("VLM_MODEL");
  const gchar *profile  = getenv("VLM_PROFILE");

  if (!profile) profile = "openai";

  /*
   * If neither an API key nor a base URL was provided, point at a
   * local mock server so the pipeline can at least start up and
   * demonstrate the mechanics without real credentials.
   */
  if ((!api_key || api_key[0] == '\0') && (!base_url || base_url[0] == '\0')) {
    g_print("WARNING: No API key or base URL set.\n");
    g_print("         Falling back to mock server http://127.0.0.1:8765\n");
    g_print("         (start one with: python3 -m http.server 8765)\n");
    api_key  = "dummy-mock-key";
    base_url = "http://127.0.0.1:8765";
    if (!model) model = "mock-model";
  }

  if (!base_url || base_url[0] == '\0')
    base_url = "https://api.openai.com";

  /* ================================================================
   *  2.  Print configuration summary
   * ==============================================================*/
  g_print("=== VLM Vision Configuration ===\n");
  g_print("  Base URL:       %s\n", base_url);
  g_print("  Model:          %s\n", model);
  g_print("  Profile:        %s\n", profile);
  g_print("  System prompt:  You are a helpful video analysis assistant.\n");
  g_print("  User prompt:    Describe what you see in this video frame in detail.\n");
  g_print("  Output mode:    7 (meta | signal | bus)\n");
  g_print("  Timeout:        10 s\n");
  g_print("  Max inflight:   1\n");
  g_print("  Queue policy:   drop\n");
  g_print("=================================\n");

  /* ================================================================
   *  3.  Initialise GStreamer
   * ==============================================================*/
  gst_init(&argc, &argv);

  /* ================================================================
   *  4.  Create elements
   * ==============================================================*/
  source = gst_element_factory_make("videotestsrc",  "source");
  conv1  = gst_element_factory_make("videoconvert",  "conv1");
  vlm    = gst_element_factory_make("vlmvision",     "vlm-vision");
  conv2  = gst_element_factory_make("videoconvert",  "conv2");
  sink   = gst_element_factory_make("ximagesink", "sink");

  if (!vlm) {
    g_printerr("ERROR: 'vlmvision' element not found.\n");
    g_printerr("       Is GstVlmVision installed? Check GST_PLUGIN_PATH.\n");
    return 1;
  }
  if (!source || !conv1 || !conv2 || !sink) {
    g_printerr("ERROR: Failed to create a basic GStreamer element.\n");
    g_printerr("       Install gst-plugins-base and gst-plugins-good.\n");
    if (vlm)    gst_object_unref(vlm);
    if (source) gst_object_unref(source);
    if (conv1)  gst_object_unref(conv1);
    if (conv2)  gst_object_unref(conv2);
    if (sink)   gst_object_unref(sink);
    return 1;
  }

  /* ================================================================
   *  5.  Configure elements
   * ==============================================================*/
  /* videotestsrc: SMPTE colour bars */
  g_object_set(source, "pattern", 0, NULL);

  g_object_set(vlm,
      "api-key",        api_key,
      "base-url",       base_url,
      "model",          model,
      "profile",        profile,
      "system-prompt",  "You are a helpful video analysis assistant.",
      "user-prompt",    "Describe what you see in this video frame in detail.",
      "output-mode",    7,         /* meta=1 | signal=2 | bus=4 */
      "timeout",        10,
      "max-inflight",   1,
      "queue-policy",   0,         /* 0=drop */
      "error-policy",   1,         /* 1=bus */
      NULL);

  /* Connect signals. */
  g_signal_connect(vlm, "description-received",
                   G_CALLBACK(on_description_received), NULL);
  g_signal_connect(vlm, "analysis-error",
                   G_CALLBACK(on_analysis_error), NULL);

  /* ================================================================
   *  6.  Assemble pipeline
   * ==============================================================*/
  pipeline = gst_pipeline_new("vlm-vision-pipeline");
  data.pipeline = pipeline;
  data.loop     = NULL;

  gst_bin_add_many(GST_BIN(pipeline), source, conv1, vlm, conv2, sink, NULL);

  if (!gst_element_link_many(source, conv1, vlm, conv2, sink, NULL)) {
    g_printerr("ERROR: Failed to link elements.\n");
    gst_object_unref(pipeline);
    return 1;
  }

  /* ================================================================
   *  7.  Bus watch
   * ==============================================================*/
  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  gst_bus_add_watch(bus, bus_callback, &data);
  gst_object_unref(bus);

  /* ================================================================
   *  8.  Signal handler for Ctrl+C
   * ==============================================================*/
  global_data.pipeline = pipeline;
  global_data.loop     = NULL;
  signal(SIGINT, on_sigint);

  /* ================================================================
   *  9.  Run
   * ==============================================================*/
  data.loop = g_main_loop_new(NULL, FALSE);
  global_data.loop = data.loop;

  g_print("Starting pipeline. Press Ctrl+C to stop.\n");
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_main_loop_run(data.loop);

  /* ================================================================
   *  10. Cleanup
   * ==============================================================*/
  g_print("Cleaning up...\n");
  signal(SIGINT, SIG_DFL);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  g_main_loop_unref(data.loop);

  return 0;
}
