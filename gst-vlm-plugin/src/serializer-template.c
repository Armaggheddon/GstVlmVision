#include "serializer-template.h"
#include "template-engine.h"
#include <gst/gst.h>
#include <string.h>
#include <stdio.h>

typedef struct {
  VlmSerializer base;
  gchar *body_template_path;
  gchar *headers_template_path;
  gchar *url_template_str;
  gchar *body_template_content;
  gchar **headers_template_lines;
} TemplateSerializer;

static gchar *
load_file_contents(const gchar *path) {
  if (!path) return NULL;
  gchar *contents = NULL;
  gsize len = 0;
  GError *err = NULL;
  if (g_file_get_contents(path, &contents, &len, &err)) {
    return contents;
  }
  GST_WARNING("Failed to load template file '%s': %s", path, err->message);
  g_error_free(err);
  return NULL;
}

static void
load_templates(TemplateSerializer *ts) {
  if (ts->body_template_path) {
    g_free(ts->body_template_content);
    ts->body_template_content = load_file_contents(ts->body_template_path);
  }
  if (ts->headers_template_path) {
    g_strfreev(ts->headers_template_lines);
    gchar *raw = load_file_contents(ts->headers_template_path);
    if (raw) {
      ts->headers_template_lines = g_strsplit(raw, "\n", -1);
      g_free(raw);
      /* Strip trailing empty lines */
      for (int i = 0; ts->headers_template_lines[i]; i++) {
        g_strstrip(ts->headers_template_lines[i]);
      }
    }
  }
}

static void
fill_context(VlmTemplateContext *ctx, VlmRequest *req, const gchar *base_url) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->base_url = base_url;
  ctx->model = req->model_name;
  ctx->system_prompt = req->system_prompt;
  ctx->user_prompt = req->user_prompt;
  ctx->frames = req->frames;
  ctx->num_frames = req->num_frames;
  ctx->temperature = req->temperature;
  ctx->max_tokens = req->max_tokens;
  ctx->top_p = req->top_p;
  ctx->stop_sequences = req->stop_sequences;
  ctx->schema_json = req->response_format;
  ctx->request_id = NULL;
}

static gchar *
template_build_url(VlmSerializer *self, const gchar *base_url, VlmRequest *req) {
  TemplateSerializer *ts = (TemplateSerializer *)self;

  VlmTemplateContext ctx;
  fill_context(&ctx, req, base_url);

  const gchar *tmpl = ts->url_template_str;
  if (!tmpl) tmpl = "{{BASE_URL}}/v1/chat/completions";

  return template_engine_substitute(tmpl, &ctx);
}

static gchar **
template_build_headers(VlmSerializer *self, const gchar *api_key) {
  TemplateSerializer *ts = (TemplateSerializer *)self;
  GPtrArray *arr = g_ptr_array_new();

  if (ts->headers_template_lines) {
    VlmTemplateContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.user_prompt = "";

    for (int i = 0; ts->headers_template_lines[i]; i++) {
      if (ts->headers_template_lines[i][0] == '\0') continue;
      gchar *sub = template_engine_substitute(ts->headers_template_lines[i], &ctx);
      g_ptr_array_add(arr, sub);
    }
  } else {
    g_ptr_array_add(arr, g_strdup("Content-Type: application/json"));
    if (api_key && api_key[0] != '\0') {
      g_ptr_array_add(arr, g_strdup_printf("Authorization: Bearer %s", api_key));
    }
  }

  g_ptr_array_add(arr, NULL);
  return (gchar **)g_ptr_array_free(arr, FALSE);
}

static gchar *
template_build_body(VlmSerializer *self, VlmRequest *req) {
  TemplateSerializer *ts = (TemplateSerializer *)self;

  if (!ts->body_template_content)
    return g_strdup("{}");

  VlmTemplateContext ctx;
  fill_context(&ctx, req, NULL);

  return template_engine_substitute(ts->body_template_content, &ctx);
}

VlmSerializer *
serializer_template_new(const gchar *body_template_path,
                        const gchar *headers_template_path,
                        const gchar *url_template) {
  TemplateSerializer *ts = g_new0(TemplateSerializer, 1);
  ts->base.name = "template";
  ts->base.build_url = template_build_url;
  ts->base.build_headers = template_build_headers;
  ts->base.build_body = template_build_body;

  ts->body_template_path = g_strdup(body_template_path);
  ts->headers_template_path = g_strdup(headers_template_path);
  ts->url_template_str = g_strdup(url_template);

  load_templates(ts);

  return (VlmSerializer *)ts;
}

void
serializer_template_free(VlmSerializer *s) {
  TemplateSerializer *ts = (TemplateSerializer *)s;
  g_free(ts->body_template_path);
  g_free(ts->headers_template_path);
  g_free(ts->url_template_str);
  g_free(ts->body_template_content);
  g_strfreev(ts->headers_template_lines);
  g_free(ts);
}
