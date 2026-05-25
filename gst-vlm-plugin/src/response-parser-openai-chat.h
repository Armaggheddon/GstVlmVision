#ifndef __RESPONSE_PARSER_OPENAI_CHAT_H__
#define __RESPONSE_PARSER_OPENAI_CHAT_H__

#include "vlm-backend.h"

VlmResponseParser *response_parser_openai_chat_new(void);
void response_parser_openai_chat_free(VlmResponseParser *p);

#endif /* __RESPONSE_PARSER_OPENAI_CHAT_H__ */
