#include "response-parser-openai-chat.h"
#include <json-c/json.h>
#include <string.h>

typedef struct {
  VlmResponseParser base;
} OpenAIChatResponseParser;

static gboolean
openai_chat_parse(VlmResponseParser *self, const gchar *body, gsize body_len,
                  gint http_status, VlmResult *result_out) {
  memset(result_out, 0, sizeof(VlmResult));
  result_out->http_status = http_status;
  result_out->raw_json = g_strdup(body);

  if (!body || body_len == 0) {
    result_out->error_message = g_strdup("Empty response body");
    return FALSE;
  }

  json_object *parsed = json_tokener_parse(body);
  if (!parsed) {
    result_out->error_message = g_strdup("Failed to parse response JSON");
    return FALSE;
  }

  /* Check for error response */
  json_object *jerror;
  if (json_object_object_get_ex(parsed, "error", &jerror)) {
    json_object *jmsg;
    if (json_object_object_get_ex(jerror, "message", &jmsg)) {
      result_out->error_message = g_strdup(json_object_get_string(jmsg));
    } else {
      result_out->error_message = g_strdup(json_object_to_json_string(jerror));
    }
    json_object_put(parsed);
    return FALSE;
  }

  /* Extract choices[0].message.content */
  json_object *jchoices;
  if (json_object_object_get_ex(parsed, "choices", &jchoices) &&
      json_object_is_type(jchoices, json_type_array) &&
      json_object_array_length(jchoices) > 0) {

    json_object *jchoice = json_object_array_get_idx(jchoices, 0);
    json_object *jmessage;
    if (json_object_object_get_ex(jchoice, "message", &jmessage)) {
      json_object *jcontent;
      if (json_object_object_get_ex(jmessage, "content", &jcontent)) {
        /* content may be NULL (e.g., tool call refusal) — check type */
        if (json_object_is_type(jcontent, json_type_string)) {
          result_out->text = g_strdup(json_object_get_string(jcontent));
        } else if (json_object_is_type(jcontent, json_type_null)) {
          result_out->text = g_strdup("");
        } else {
          result_out->text = g_strdup(json_object_to_json_string(jcontent));
        }
      } else {
        result_out->error_message = g_strdup("No 'content' field in message");
        json_object_put(parsed);
        return FALSE;
      }
    } else {
      json_object *jfinish;
      if (json_object_object_get_ex(jchoice, "finish_reason", &jfinish)) {
        result_out->text = g_strdup("");
        result_out->error_message = g_strdup_printf(
            "No message in choice (finish_reason: %s)",
            json_object_get_string(jfinish));
      } else {
        result_out->error_message = g_strdup("No 'message' field in choice");
      }
      json_object_put(parsed);
      return FALSE;
    }
  } else {
    result_out->error_message = g_strdup("No 'choices' array in response");
    json_object_put(parsed);
    return FALSE;
  }

  json_object_put(parsed);
  return TRUE;
}

VlmResponseParser *
response_parser_openai_chat_new(void) {
  OpenAIChatResponseParser *p = g_new0(OpenAIChatResponseParser, 1);
  p->base.name = "openai-chat-completions";
  p->base.parse = openai_chat_parse;
  return (VlmResponseParser *)p;
}

void
response_parser_openai_chat_free(VlmResponseParser *p) {
  g_free(p);
}
