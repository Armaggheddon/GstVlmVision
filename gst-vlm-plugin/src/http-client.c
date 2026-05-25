#include "http-client.h"
#include <curl/curl.h>
#include <string.h>

typedef struct {
  gchar *data;
  size_t size;
} MemoryChunk;

static size_t
write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  MemoryChunk *chunk = (MemoryChunk *)userp;

  gchar *ptr = g_realloc(chunk->data, chunk->size + realsize + 1);
  if (ptr == NULL) {
    GST_ERROR("Not enough memory (realloc returned NULL)");
    return 0;
  }

  chunk->data = ptr;
  memcpy(&(chunk->data[chunk->size]), contents, realsize);
  chunk->size += realsize;
  chunk->data[chunk->size] = 0;
  return realsize;
}

HttpResponse *
http_client_send(const gchar *url, gchar **headers,
                 const gchar *body, gsize body_len,
                 gint timeout_seconds) {
  CURL *curl;
  CURLcode res;
  struct curl_slist *header_list = NULL;
  MemoryChunk chunk;
  HttpResponse *response = NULL;

  chunk.data = g_malloc(1);
  chunk.data[0] = '\0';
  chunk.size = 0;

  curl = curl_easy_init();
  if (!curl) {
    g_free(chunk.data);
    GST_ERROR("Failed to initialize curl");
    return NULL;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);

  if (body && body_len > 0) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
  }

  if (headers) {
    for (gchar **h = headers; *h != NULL; h++) {
      header_list = curl_slist_append(header_list, *h);
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  }

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

  if (timeout_seconds > 0) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_seconds);
  }

  res = curl_easy_perform(curl);

  response = g_new0(HttpResponse, 1);
  response->data = chunk.data;
  response->size = chunk.size;

  if (res != CURLE_OK) {
    response->http_status = 0;
    GST_ERROR("curl_easy_perform() failed: %s", curl_easy_strerror(res));
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->http_status);
  }

  if (header_list)
    curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);

  return response;
}

void
http_response_free(HttpResponse *resp) {
  if (resp) {
    g_free(resp->data);
    g_free(resp);
  }
}
