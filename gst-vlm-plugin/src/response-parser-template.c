#include "response-parser-template.h"
#include <json-c/json.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
  VlmResponseParser base;
  gchar *text_path;
  gchar *error_path;
  gchar *raw_path;
} TemplateResponseParser;

/* Walk a JSON path like $.choices[0].message.content.
 * Returns the found json_object (caller does NOT own the reference),
 * or NULL. */
static json_object *
json_path_walk(json_object *root, const gchar *path) {
  if (!root || !path) return NULL;

  /* Skip leading "$." or "$" */
  const gchar *p = path;
  if (*p == '$') {
    p++;
    if (*p == '.') p++;
  }
  if (*p == '\0') return root;

  /* Tokenize the remaining path */
  gchar *path_copy = g_strdup(p);
  gchar *saveptr;
  gchar *token = strtok_r(path_copy, ".", &saveptr);

  json_object *current = root;
  while (token && current) {
    /* Check for array index: token[42] */
    gchar *bracket = strchr(token, '[');
    if (bracket) {
      *bracket = '\0';
      gchar *key = token;
      gchar *idx_str = bracket + 1;
      gchar *end = strchr(idx_str, ']');
      if (end) *end = '\0';

      /* Navigate to object key first */
      if (key[0] != '\0') {
        json_object *child;
        if (!json_object_object_get_ex(current, key, &child)) {
          current = NULL;
          break;
        }
        current = child;
      }

      /* Then array index */
      if (json_object_is_type(current, json_type_array)) {
        int idx = atoi(idx_str);
        int len = json_object_array_length(current);
        if (idx >= 0 && idx < len) {
          current = json_object_array_get_idx(current, idx);
        } else {
          current = NULL;
        }
      } else {
        current = NULL;
      }
    } else {
      /* Simple object key */
      json_object *child;
      if (!json_object_object_get_ex(current, token, &child)) {
        current = NULL;
        break;
      }
      current = child;
    }

    token = strtok_r(NULL, ".", &saveptr);
  }

  g_free(path_copy);
  return current;
}

static gchar *
json_path_get_string(json_object *root, const gchar *path) {
  if (!path) return NULL;
  json_object *found = json_path_walk(root, path);
  if (!found) return NULL;
  if (json_object_is_type(found, json_type_string))
    return g_strdup(json_object_get_string(found));
  return g_strdup(json_object_to_json_string(found));
}

static gboolean
template_parse(VlmResponseParser *self, const gchar *body, gsize body_len,
               gint http_status, VlmResult *result_out) {
  TemplateResponseParser *tp = (TemplateResponseParser *)self;
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

  /* Check error path */
  if (tp->error_path) {
    gchar *err = json_path_get_string(parsed, tp->error_path);
    if (err) {
      result_out->error_message = err;
      json_object_put(parsed);
      return FALSE;
    }
  }

  /* Check for standard error message */
  json_object *jerr;
  if (!tp->error_path &&
      json_object_object_get_ex(parsed, "error", &jerr)) {
    json_object *jmsg;
    if (json_object_object_get_ex(jerr, "message", &jmsg)) {
      result_out->error_message = g_strdup(json_object_get_string(jmsg));
    }
    json_object_put(parsed);
    return FALSE;
  }

  /* Extract text */
  const gchar *text_path = tp->text_path ? tp->text_path
                                         : "$.choices[0].message.content";
  result_out->text = json_path_get_string(parsed, text_path);

  /* Extract raw if path specified */
  if (tp->raw_path) {
    gchar *raw = json_path_get_string(parsed, tp->raw_path);
    if (raw) {
      g_free(result_out->raw_json);
      result_out->raw_json = raw;
    }
  }

  if (!result_out->text) {
    result_out->error_message = g_strdup_printf(
        "No value found at path '%s'", text_path);
    json_object_put(parsed);
    return FALSE;
  }

  json_object_put(parsed);
  return TRUE;
}

VlmResponseParser *
response_parser_template_new(const gchar *text_path,
                             const gchar *error_path,
                             const gchar *raw_path) {
  TemplateResponseParser *tp = g_new0(TemplateResponseParser, 1);
  tp->base.name = "template";
  tp->base.parse = template_parse;

  tp->text_path = g_strdup(text_path);
  tp->error_path = g_strdup(error_path);
  tp->raw_path = g_strdup(raw_path);

  return (VlmResponseParser *)tp;
}

void
response_parser_template_free(VlmResponseParser *p) {
  TemplateResponseParser *tp = (TemplateResponseParser *)p;
  g_free(tp->text_path);
  g_free(tp->error_path);
  g_free(tp->raw_path);
  g_free(tp);
}
