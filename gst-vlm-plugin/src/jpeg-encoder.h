#ifndef __JPEG_ENCODER_H__
#define __JPEG_ENCODER_H__

#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>

gboolean encode_frame_to_jpeg(GstObject *log_target,
                              const GstVideoInfo *video_info,
                              const guchar *frame_data, gsize frame_size,
                              guchar **jpeg_buffer, gsize *jpeg_size);

#endif /* __JPEG_ENCODER_H__ */
