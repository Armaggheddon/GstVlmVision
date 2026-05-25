#ifndef __HTTP_CLIENT_H__
#define __HTTP_CLIENT_H__

#include <glib.h>
#include <gst/gst.h>

typedef struct {
  gchar *data;
  gsize size;
  gint http_status;
} HttpResponse;

HttpResponse *http_client_send(const gchar *url, gchar **headers,
                               const gchar *body, gsize body_len,
                               gint timeout_seconds);
void http_response_free(HttpResponse *resp);

#endif /* __HTTP_CLIENT_H__ */
