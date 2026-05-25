#include "template-engine.h"
#include <glib/gbase64.h>
#include <json-c/json.h>
#include <string.h>
#include <stdio.h>

/* Simple string replacement — replaces ALL occurrences of `from` with `to`.
 * Returns a newly allocated string. */
static gchar *
str_replace(const gchar *str, const gchar *from, const gchar *to) {
  if (!str || !from || !to) return g_strdup(str ? str : "");

  GString *result = g_string_new("");
  const gchar *p = str;
  gsize from_len = strlen(from);

  while (*p) {
    const gchar *match = strstr(p, from);
    if (match) {
      g_string_append_len(result, p, match - p);
      g_string_append(result, to);
      p = match + from_len;
    } else {
      g_string_append(result, p);
      break;
    }
  }

  return g_string_free(result, FALSE);
}

/* Builds image array JSON for {{IMAGE_ARRAY_JSON}} */
static gchar *
build_image_array_json(VlmFramePayload *frames, gint num_frames) {
  json_object *jarr = json_object_new_array();
  for (int i = 0; i < num_frames; i++) {
    gchar *b64 = g_base64_encode(frames[i].data, frames[i].size);
    gchar *data_url = g_strdup_printf("data:image/jpeg;base64,%s", b64);

    json_object *jimg = json_object_new_object();
    json_object_object_add(jimg, "type", json_object_new_string("image_url"));
    json_object *jimg_url = json_object_new_object();
    json_object_object_add(jimg_url, "url", json_object_new_string(data_url));
    json_object_object_add(jimg, "image_url", jimg_url);
    json_object_array_add(jarr, jimg);

    g_free(data_url);
    g_free(b64);
  }
  const gchar *s = json_object_to_json_string_ext(jarr, JSON_C_TO_STRING_PLAIN);
  gchar *result = g_strdup(s);
  json_object_put(jarr);
  return result;
}

static gchar *
build_stop_sequences_json(gchar **stop_sequences) {
  if (!stop_sequences || !stop_sequences[0]) return g_strdup("[]");
  json_object *jarr = json_object_new_array();
  for (int i = 0; stop_sequences[i] != NULL; i++) {
    json_object_array_add(jarr, json_object_new_string(stop_sequences[i]));
  }
  const gchar *s = json_object_to_json_string_ext(jarr, JSON_C_TO_STRING_PLAIN);
  gchar *result = g_strdup(s);
  json_object_put(jarr);
  return result;
}

gchar *
template_engine_substitute(const gchar *template_str, const VlmTemplateContext *ctx) {
  if (!template_str) return NULL;

  gchar *result = g_strdup(template_str);

  /* Base URL */
  if (ctx->base_url) {
    gchar *tmp = str_replace(result, "{{BASE_URL}}", ctx->base_url);
    g_free(result);
    result = tmp;
  }

  /* Model */
  if (ctx->model) {
    gchar *tmp = str_replace(result, "{{MODEL}}", ctx->model);
    g_free(result);
    result = tmp;
  }

  /* System prompt */
  if (ctx->system_prompt) {
    gchar *tmp = str_replace(result, "{{SYSTEM_PROMPT}}", ctx->system_prompt);
    g_free(result);
    result = tmp;
  }

  /* User prompt */
  if (ctx->user_prompt) {
    gchar *tmp = str_replace(result, "{{USER_PROMPT}}", ctx->user_prompt);
    g_free(result);
    result = tmp;
  }

  /* Single image base64 */
  if (ctx->frames && ctx->num_frames > 0) {
    gchar *b64 = g_base64_encode(ctx->frames[0].data, ctx->frames[0].size);
    gchar *tmp = str_replace(result, "{{IMAGE_0_BASE64}}", b64);
    g_free(result);
    result = tmp;

    gchar *data_url = g_strdup_printf("data:image/jpeg;base64,%s", b64);
    tmp = str_replace(result, "{{IMAGE_0_DATA_URL}}", data_url);
    g_free(result);
    result = tmp;

    g_free(data_url);
    g_free(b64);
  }

  /* Image array JSON */
  if (ctx->frames && ctx->num_frames > 0) {
    gchar *arr = build_image_array_json(ctx->frames, ctx->num_frames);
    gchar *tmp = str_replace(result, "{{IMAGE_ARRAY_JSON}}", arr);
    g_free(result);
    result = tmp;
    g_free(arr);
  }

  /* temperature */
  if (ctx->temperature >= 0.0) {
    gchar temp_str[64];
    snprintf(temp_str, sizeof(temp_str), "%g", ctx->temperature);
    gchar *tmp = str_replace(result, "{{TEMPERATURE}}", temp_str);
    g_free(result);
    result = tmp;
  }

  /* max_tokens */
  if (ctx->max_tokens > 0) {
    gchar tok_str[64];
    snprintf(tok_str, sizeof(tok_str), "%d", ctx->max_tokens);
    gchar *tmp = str_replace(result, "{{MAX_TOKENS}}", tok_str);
    g_free(result);
    result = tmp;
  }

  /* top_p */
  if (ctx->top_p >= 0.0) {
    gchar top_str[64];
    snprintf(top_str, sizeof(top_str), "%g", ctx->top_p);
    gchar *tmp = str_replace(result, "{{TOP_P}}", top_str);
    g_free(result);
    result = tmp;
  }

  /* schema JSON */
  if (ctx->schema_json) {
    gchar *tmp = str_replace(result, "{{SCHEMA_JSON}}", ctx->schema_json);
    g_free(result);
    result = tmp;
  }

  /* request_id */
  if (ctx->request_id) {
    gchar *tmp = str_replace(result, "{{REQUEST_ID}}", ctx->request_id);
    g_free(result);
    result = tmp;
  }

  /* stop_sequences JSON */
  if (ctx->stop_sequences && ctx->stop_sequences[0]) {
    gchar *ss = build_stop_sequences_json(ctx->stop_sequences);
    gchar *tmp = str_replace(result, "{{STOP_SEQUENCES_JSON}}", ss);
    g_free(result);
    result = tmp;
    g_free(ss);
  }

  return result;
}
