#include "gstvlmvisionmeta.h"

GType
gst_vlm_vision_meta_api_get_type(void) {
  static volatile GType type = 0;
  static const gchar *tags[] = {"description", "text", "vlm", NULL};

  if (g_once_init_enter(&type)) {
    GType _type = gst_meta_api_type_register("GstVlmVisionMetaAPI", tags);
    g_once_init_leave(&type, _type);
  }
  return type;
}

static gboolean
_gst_vlm_vision_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
  GstVlmVisionMeta *gmeta = (GstVlmVisionMeta *)meta;
  gmeta->description = NULL;
  return TRUE;
}

static void
_gst_vlm_vision_meta_free(GstVlmVisionMeta *meta) {
  g_free(meta->description);
}

static GstVlmVisionMeta *
_gst_vlm_vision_meta_copy(const GstVlmVisionMeta *meta, GstVlmVisionMeta *copy) {
  copy->description = g_strdup(meta->description);
  return copy;
}

const GstMetaInfo *
gst_vlm_vision_meta_get_info(void) {
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter(&meta_info)) {
    const GstMetaInfo *mi = gst_meta_register(
        GST_VLM_VISION_META_API_TYPE,
        "GstVlmVisionMeta",
        sizeof(GstVlmVisionMeta),
        (GstMetaInitFunction)_gst_vlm_vision_meta_init,
        (GstMetaFreeFunction)_gst_vlm_vision_meta_free,
        (GstMetaTransformFunction)NULL);
    g_once_init_leave(&meta_info, mi);
  }
  return meta_info;
}

GstVlmVisionMeta *
gst_buffer_add_vlm_vision_meta(GstBuffer *buffer, const gchar *description) {
  GstVlmVisionMeta *meta;

  g_return_val_if_fail(GST_IS_BUFFER(buffer), NULL);
  g_return_val_if_fail(description != NULL, NULL);

  const GstMetaInfo *info = gst_vlm_vision_meta_get_info();
  if (!info) {
    GST_ERROR("Failed to get GstVlmVisionMeta info");
    return NULL;
  }

  meta = (GstVlmVisionMeta *)gst_buffer_add_meta(buffer, info, NULL);
  if (!meta) {
    GST_ERROR("Failed to add GstVlmVisionMeta to buffer");
    return NULL;
  }

  meta->meta.flags = GST_META_FLAG_NONE;
  meta->description = g_strdup(description);

  return meta;
}
