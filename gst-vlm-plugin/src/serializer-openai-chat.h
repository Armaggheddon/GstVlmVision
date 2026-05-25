#ifndef __SERIALIZER_OPENAI_CHAT_H__
#define __SERIALIZER_OPENAI_CHAT_H__

#include "vlm-serializer.h"

VlmSerializer *serializer_openai_chat_new(void);
void serializer_openai_chat_free(VlmSerializer *s);

#endif /* __SERIALIZER_OPENAI_CHAT_H__ */
