#ifndef __SERIALIZER_TEMPLATE_H__
#define __SERIALIZER_TEMPLATE_H__

#include "vlm-serializer.h"

VlmSerializer *serializer_template_new(const gchar *body_template_path,
                                       const gchar *headers_template_path,
                                       const gchar *url_template);
void serializer_template_free(VlmSerializer *s);

#endif /* __SERIALIZER_TEMPLATE_H__ */
