#include "serializer-openai-chat.h"
#include <glib/gbase64.h>
#include <json-c/json.h>
#include <string.h>

typedef struct {
  VlmSerializer base;
} OpenAIChatSerializer;

static gchar *
openai_chat_build_url(VlmSerializer *self, const gchar *base_url, VlmRequest *req) {
  if (!base_url) base_url = "";
  gsize len = strlen(base_url);
  if (len > 0 && base_url[len - 1] == '/')
    return g_strdup_printf("%sv1/chat/completions", base_url);
  return g_strdup_printf("%s/v1/chat/completions", base_url);
}

static gchar **
openai_chat_build_headers(VlmSerializer *self, const gchar *api_key) {
  GPtrArray *arr = g_ptr_array_new();

  g_ptr_array_add(arr, g_strdup("Content-Type: application/json"));

  if (api_key && api_key[0] != '\0') {
    gchar *auth = g_strdup_printf("Authorization: Bearer %s", api_key);
    g_ptr_array_add(arr, auth);
  }

  g_ptr_array_add(arr, NULL);
  return (gchar **)g_ptr_array_free(arr, FALSE);
}

static gchar *
openai_chat_build_body(VlmSerializer *self, VlmRequest *req) {
  json_object *jroot = json_object_new_object();
  json_object *jmessages = json_object_new_array();

  /* model */
  json_object_object_add(jroot, "model",
      json_object_new_string(req->model_name ? req->model_name : ""));

  /* messages array */
  if (req->system_prompt && req->system_prompt[0] != '\0') {
    json_object *jsys = json_object_new_object();
    json_object_object_add(jsys, "role", json_object_new_string("system"));
    json_object_object_add(jsys, "content", json_object_new_string(req->system_prompt));
    json_object_array_add(jmessages, jsys);
  }

  json_object *juser = json_object_new_object();
  json_object_object_add(juser, "role", json_object_new_string("user"));

  json_object *jcontent = json_object_new_array();

  /* images */
  for (int i = 0; i < req->num_frames; i++) {
    gchar *b64 = g_base64_encode(req->frames[i].data, req->frames[i].size);
    gchar *data_url = g_strdup_printf("data:image/jpeg;base64,%s", b64);

    json_object *jimg = json_object_new_object();
    json_object_object_add(jimg, "type", json_object_new_string("image_url"));
    json_object *jimg_url = json_object_new_object();
    json_object_object_add(jimg_url, "url", json_object_new_string(data_url));
    json_object_object_add(jimg, "image_url", jimg_url);
    json_object_array_add(jcontent, jimg);

    g_free(data_url);
    g_free(b64);
  }

  /* text prompt */
  if (req->user_prompt && req->user_prompt[0] != '\0') {
    json_object *jtext = json_object_new_object();
    json_object_object_add(jtext, "type", json_object_new_string("text"));
    json_object_object_add(jtext, "text", json_object_new_string(req->user_prompt));
    json_object_array_add(jcontent, jtext);
  }

  json_object_object_add(juser, "content", jcontent);
  json_object_array_add(jmessages, juser);
  json_object_object_add(jroot, "messages", jmessages);

  /* temperature */
  if (req->temperature >= 0.0) {
    json_object_object_add(jroot, "temperature",
        json_object_new_double(req->temperature));
  }

  /* max_tokens */
  if (req->max_tokens > 0) {
    json_object_object_add(jroot, "max_tokens",
        json_object_new_int(req->max_tokens));
  }

  /* top_p */
  if (req->top_p >= 0.0) {
    json_object_object_add(jroot, "top_p",
        json_object_new_double(req->top_p));
  }

  /* stop */
  if (req->stop_sequences && req->stop_sequences[0] != NULL) {
    json_object *jstop = json_object_new_array();
    for (int i = 0; req->stop_sequences[i] != NULL; i++) {
      json_object_array_add(jstop, json_object_new_string(req->stop_sequences[i]));
    }
    json_object_object_add(jroot, "stop", jstop);
  }

  /* response_format */
  if (req->response_format && req->response_format[0] != '\0') {
    if (strcmp(req->response_format, "json_object") == 0) {
      json_object *jrf = json_object_new_object();
      json_object_object_add(jrf, "type", json_object_new_string("json_object"));
      json_object_object_add(jroot, "response_format", jrf);
    } else {
      /* Treat as JSON schema string */
      json_object *jschema = json_tokener_parse(req->response_format);
      if (jschema) {
        json_object *jrf = json_object_new_object();
        json_object_object_add(jrf, "type", json_object_new_string("json_schema"));
        json_object_object_add(jrf, "json_schema", jschema);
        json_object_object_add(jroot, "response_format", jrf);
      }
    }
  }

  const gchar *json_str = json_object_to_json_string_ext(jroot, JSON_C_TO_STRING_PLAIN);
  gchar *result = g_strdup(json_str);
  json_object_put(jroot);
  return result;
}

VlmSerializer *
serializer_openai_chat_new(void) {
  OpenAIChatSerializer *s = g_new0(OpenAIChatSerializer, 1);
  s->base.name = "openai-chat-completions";
  s->base.build_url = openai_chat_build_url;
  s->base.build_headers = openai_chat_build_headers;
  s->base.build_body = openai_chat_build_body;
  return (VlmSerializer *)s;
}

void
serializer_openai_chat_free(VlmSerializer *s) {
  g_free(s);
}
