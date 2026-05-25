#ifndef __QUEUE_POLICY_H__
#define __QUEUE_POLICY_H__

#include <glib.h>

typedef enum {
  VLM_QUEUE_POLICY_DROP,
  VLM_QUEUE_POLICY_BLOCK,
} VlmQueuePolicy;

#endif /* __QUEUE_POLICY_H__ */
