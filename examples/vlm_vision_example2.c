/*
 * VLM Vision Plugin Example 2 -- URI source
 * Pipeline: uridecodebin ! videoconvert ! vlmvision ! videoconvert ! ximagesink
 *
 * Build: make all  (from examples/ directory)
 *
 * Run:
 *   VLM_API_KEY=sk-... ./bin/vlm_vision_example2 [video-uri]
 *
 * If no URI is given, the Sintel trailer is used.
 * Environment variables: VLM_API_KEY, VLM_BASE_URL, VLM_MODEL, VLM_PROFILE
 */

#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static GMainLoop *loop = NULL;
static GstElement *pipeline = NULL;

/* ── SIGINT handler ───────────────────────────────────────────── */
static void
on_sigint(int sig)
{
  if (pipeline) {
    g_print("\n[SIGINT] Sending EOS for graceful shutdown...\n");
    gst_element_send_event(pipeline, gst_event_new_eos());
  }
}

/* ── Bus callback ─────────────────────────────────────────────── */
static gboolean
bus_callback(GstBus *bus, GstMessage *message, gpointer user_data)
{
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
      GError *err = NULL;
      gchar *debug = NULL;
      gst_message_parse_error(message, &err, &debug);
      g_printerr("ERROR from %s: %s\n",
                 GST_OBJECT_NAME(message->src), err->message);
      if (debug) g_printerr("  Debug info: %s\n", debug);
      g_error_free(err);
      g_free(debug);
      g_main_loop_quit(loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_print("[bus] End of stream.\n");
      g_main_loop_quit(loop);
      break;
    case GST_MESSAGE_ELEMENT: {
      const GstStructure *s = gst_message_get_structure(message);
      if (!s) break;
      if (gst_structure_has_name(s, "vlmvision-result")) {
        const gchar *desc = gst_structure_get_string(s, "description");
        guint64 pts = 0;
        gst_structure_get_uint64(s, "pts", &pts);
        printf("--- [bus] ---\n");
        printf("  PTS:  %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pts));
        printf("  Desc: %s\n", desc ? desc : "(null)");
        printf("-------------\n");
        fflush(stdout);
      } else if (gst_structure_has_name(s, "vlmvision-error")) {
        const gchar *msg = gst_structure_get_string(s, "message");
        gint hs = 0;
        gst_structure_get_int(s, "http-status", &hs);
        printf("*** [bus] ERROR (HTTP %d): %s ***\n", hs, msg ? msg : "?");
        fflush(stdout);
      }
      break;
    }
    default:
      break;
  }
  return TRUE;
}

/* ── "description-received" signal callback ────────────────────── */
static void
on_description_received(GstElement *element, gchar *description,
                        GstBuffer *buffer, gpointer user_data)
{
  GstClockTime pts = GST_BUFFER_PTS(buffer);
  printf("=================================\n");
  printf("[signal] PTS: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pts));
  printf("[signal] %s\n", description);
  printf("=================================\n\n");
  fflush(stdout);
}

/* ── "analysis-error" signal callback ──────────────────────────── */
static void
on_analysis_error(GstElement *element, gchar *message, gint http_status,
                  gpointer user_data)
{
  printf("*** [signal] ANALYSIS ERROR (HTTP %d): %s ***\n\n",
         http_status, message);
  fflush(stdout);
}

/* ── main ─────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
  const gchar *uri =
    "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm";
  if (argc > 1) uri = argv[1];

  /* ── config from environment ────────────────────────────────── */
  const gchar *api_key  = getenv("VLM_API_KEY");
  const gchar *base_url = getenv("VLM_BASE_URL");
  const gchar *model    = getenv("VLM_MODEL");
  const gchar *profile  = getenv("VLM_PROFILE");

  if (!profile)  profile  = "openai";
  if (!model)    model    = "";
  if (!base_url) base_url = "https://api.openai.com";
  if (!api_key)  api_key  = "";

  g_print("=== VLM Vision Example 2 ===\n");
  g_print("  URI:      %s\n", uri);
  g_print("  Profile:  %s\n", profile);
  g_print("  Model:    %s\n", model);
  g_print("  Base URL: %s\n", base_url);
  g_print("  Interval: 3.0 s\n");
  g_print("============================\n");

  gst_init(&argc, &argv);

  /* ── build pipeline string ──────────────────────────────────── */
  gchar *pipe_str = g_strdup_printf(
      "uridecodebin uri=%s name=src ! videoconvert ! "
      "vlmvision name=vlm api-key=\"%s\" base-url=\"%s\" model=\"%s\" "
      "profile=\"%s\" user-prompt=\"Describe what you see in this video scene.\" "
      "analysis-interval=3.0 error-policy=1 output-mode=6 timeout=10 ! "
      "videoconvert ! ximagesink",
      uri, api_key, base_url, model, profile);

  pipeline = gst_parse_launch(pipe_str, NULL);
  g_free(pipe_str);

  if (!pipeline) {
    g_printerr("ERROR: Failed to build pipeline.\n");
    return 1;
  }

  /* ── connect signals ────────────────────────────────────────── */
  GstElement *vlm = gst_bin_get_by_name(GST_BIN(pipeline), "vlm");
  if (vlm) {
    g_signal_connect(vlm, "description-received",
                     G_CALLBACK(on_description_received), NULL);
    g_signal_connect(vlm, "analysis-error",
                     G_CALLBACK(on_analysis_error), NULL);
    gst_object_unref(vlm);
  }

  /* ── bus watch ──────────────────────────────────────────────── */
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  gst_bus_add_watch(bus, bus_callback, NULL);
  gst_object_unref(bus);

  /* ── run ────────────────────────────────────────────────────── */
  loop = g_main_loop_new(NULL, FALSE);
  signal(SIGINT, on_sigint);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_main_loop_run(loop);

  /* ── cleanup ────────────────────────────────────────────────── */
  signal(SIGINT, SIG_DFL);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  g_main_loop_unref(loop);

  return 0;
}
