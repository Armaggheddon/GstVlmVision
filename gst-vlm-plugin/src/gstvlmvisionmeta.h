#ifndef __GST_VLM_VISION_META_H__
#define __GST_VLM_VISION_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_VLM_VISION_META_API_TYPE (gst_vlm_vision_meta_api_get_type())
#define GST_VLM_VISION_META_INFO (gst_vlm_vision_meta_get_info())

typedef struct _GstVlmVisionMeta GstVlmVisionMeta;

struct _GstVlmVisionMeta {
  GstMeta meta;
  gchar *description;
};

GType gst_vlm_vision_meta_api_get_type(void);
const GstMetaInfo *gst_vlm_vision_meta_get_info(void);
GstVlmVisionMeta *gst_buffer_add_vlm_vision_meta(GstBuffer *buffer, const gchar *description);

G_END_DECLS

#endif /* __GST_VLM_VISION_META_H__ */
