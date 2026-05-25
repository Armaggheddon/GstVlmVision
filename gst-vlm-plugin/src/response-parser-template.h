#ifndef __RESPONSE_PARSER_TEMPLATE_H__
#define __RESPONSE_PARSER_TEMPLATE_H__

#include "vlm-backend.h"

VlmResponseParser *response_parser_template_new(const gchar *text_path,
                                                const gchar *error_path,
                                                const gchar *raw_path);
void response_parser_template_free(VlmResponseParser *p);

#endif /* __RESPONSE_PARSER_TEMPLATE_H__ */
