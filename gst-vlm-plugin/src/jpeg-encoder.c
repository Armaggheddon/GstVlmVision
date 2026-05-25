#include "jpeg-encoder.h"
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

typedef struct {
  unsigned char *data;
  unsigned long size;
  unsigned long allocated_size;
} JPEGDynamicBuffer;

struct my_error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

static void
my_error_exit(j_common_ptr cinfo) {
  struct my_error_mgr *myerr = (struct my_error_mgr *)cinfo->err;
  (*cinfo->err->output_message)(cinfo);
  longjmp(myerr->setjmp_buffer, 1);
}

static void
jpeg_init_destination(j_compress_ptr cinfo) {
  JPEGDynamicBuffer *dest = (JPEGDynamicBuffer *)cinfo->client_data;
  dest->data = g_malloc(16384);
  dest->allocated_size = 16384;
  dest->size = 0;

  cinfo->dest->next_output_byte = dest->data;
  cinfo->dest->free_in_buffer = dest->allocated_size;
}

static boolean
jpeg_empty_output_buffer(j_compress_ptr cinfo) {
  JPEGDynamicBuffer *dest = (JPEGDynamicBuffer *)cinfo->client_data;
  unsigned int new_size = dest->allocated_size * 2;
  unsigned char *new_buffer = g_realloc(dest->data, new_size);

  if (!new_buffer) {
    (*cinfo->err->error_exit)((j_common_ptr)cinfo);
    return FALSE;
  }

  dest->data = new_buffer;
  cinfo->dest->next_output_byte = dest->data + dest->allocated_size;
  cinfo->dest->free_in_buffer = dest->allocated_size;
  dest->allocated_size = new_size;

  return TRUE;
}

static void
jpeg_term_destination(j_compress_ptr cinfo) {
  JPEGDynamicBuffer *dest = (JPEGDynamicBuffer *)cinfo->client_data;
  dest->size = dest->allocated_size - cinfo->dest->free_in_buffer;
}

gboolean
encode_frame_to_jpeg(GstObject *log_target,
                     const GstVideoInfo *video_info,
                     const guchar *frame_data, gsize frame_size,
                     guchar **jpeg_buffer, gsize *jpeg_size) {
  struct jpeg_compress_struct cinfo;
  struct my_error_mgr jerr;
  JSAMPROW row_pointer[1];
  int row_stride;
  JPEGDynamicBuffer dest_buffer = {NULL, 0, 0};
  struct jpeg_destination_mgr dest_mgr;
  memset(&dest_mgr, 0, sizeof(dest_mgr));
  dest_mgr.init_destination = jpeg_init_destination;
  dest_mgr.empty_output_buffer = jpeg_empty_output_buffer;
  dest_mgr.term_destination = jpeg_term_destination;

  if (!frame_data || frame_size == 0) {
    GST_ERROR_OBJECT(log_target, "Invalid frame data");
    return FALSE;
  }

  if (video_info->width <= 0 || video_info->height <= 0) {
    GST_ERROR_OBJECT(log_target, "Invalid video dimensions: %dx%d",
                     video_info->width, video_info->height);
    return FALSE;
  }

  GstVideoFormat format = video_info->finfo->format;
  const char *format_name = gst_video_format_to_string(format);
  GST_DEBUG_OBJECT(log_target, "Encoding video format %s to JPEG, dimensions: %dx%d",
                   format_name, video_info->width, video_info->height);

  gsize expected_size = video_info->size;
  if (frame_size < expected_size) {
    GST_ERROR_OBJECT(log_target, "Buffer too small: got %zu bytes, expected %zu bytes",
                     frame_size, expected_size);
    return FALSE;
  }

  memset(&cinfo, 0, sizeof(cinfo));
  memset(&jerr, 0, sizeof(jerr));
  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = my_error_exit;
  jpeg_create_compress(&cinfo);

  dest_buffer.data = NULL;
  dest_buffer.size = 0;
  dest_buffer.allocated_size = 0;

  dest_mgr.init_destination = jpeg_init_destination;
  dest_mgr.empty_output_buffer = jpeg_empty_output_buffer;
  dest_mgr.term_destination = jpeg_term_destination;

  cinfo.client_data = &dest_buffer;
  cinfo.dest = &dest_mgr;

  cinfo.image_width = video_info->width;
  cinfo.image_height = video_info->height;

  int n_components;
  J_COLOR_SPACE color_space;

  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
      n_components = 3;
      color_space = JCS_RGB;
      break;
    case GST_VIDEO_FORMAT_BGR:
      n_components = 3;
      color_space = JCS_RGB;
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
      n_components = 3;
      color_space = JCS_RGB;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      GST_ERROR_OBJECT(log_target,
                       "YUV formats not directly supported. Add videoconvert before this element.");
      return FALSE;
    default:
      GST_ERROR_OBJECT(log_target, "Unsupported video format: %s", format_name);
      return FALSE;
  }

  cinfo.input_components = n_components;
  cinfo.in_color_space = color_space;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 85, TRUE);

  row_stride = GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0);
  GST_DEBUG_OBJECT(log_target, "Video stride: %d, components: %d", row_stride, n_components);

  guchar *rgb_row = NULL;
  if (format == GST_VIDEO_FORMAT_BGR || format == GST_VIDEO_FORMAT_BGRA ||
      format == GST_VIDEO_FORMAT_RGBA) {
    rgb_row = g_malloc(video_info->width * 3);
  }

  if (setjmp(jerr.setjmp_buffer)) {
    g_free(rgb_row);
    jpeg_destroy_compress(&cinfo);
    if (dest_buffer.data) g_free(dest_buffer.data);
    *jpeg_buffer = NULL;
    *jpeg_size = 0;
    GST_ERROR_OBJECT(log_target, "libjpeg internal error");
    return FALSE;
  }

  jpeg_start_compress(&cinfo, TRUE);

  while (cinfo.next_scanline < cinfo.image_height) {
    const guchar *src_row = frame_data + (cinfo.next_scanline * row_stride);

    if (format == GST_VIDEO_FORMAT_BGR) {
      for (int i = 0; i < video_info->width; i++) {
        rgb_row[i * 3 + 0] = src_row[i * 3 + 2];  /* R <- B */
        rgb_row[i * 3 + 1] = src_row[i * 3 + 1];  /* G <- G */
        rgb_row[i * 3 + 2] = src_row[i * 3 + 0];  /* B <- R */
      }
      row_pointer[0] = rgb_row;
    } else if (format == GST_VIDEO_FORMAT_RGBA) {
      for (int i = 0; i < video_info->width; i++) {
        rgb_row[i * 3 + 0] = src_row[i * 4 + 0];  /* R */
        rgb_row[i * 3 + 1] = src_row[i * 4 + 1];  /* G */
        rgb_row[i * 3 + 2] = src_row[i * 4 + 2];  /* B */
      }
      row_pointer[0] = rgb_row;
    } else if (format == GST_VIDEO_FORMAT_BGRA) {
      for (int i = 0; i < video_info->width; i++) {
        rgb_row[i * 3 + 0] = src_row[i * 4 + 2];  /* R <- B */
        rgb_row[i * 3 + 1] = src_row[i * 4 + 1];  /* G <- G */
        rgb_row[i * 3 + 2] = src_row[i * 4 + 0];  /* B <- R */
      }
      row_pointer[0] = rgb_row;
    } else {
      row_pointer[0] = (JSAMPROW)src_row;
    }

    if (jpeg_write_scanlines(&cinfo, row_pointer, 1) != 1) {
      GST_ERROR_OBJECT(log_target, "Error writing JPEG scanline");
      if (rgb_row) g_free(rgb_row);
      jpeg_destroy_compress(&cinfo);
      if (dest_buffer.data) g_free(dest_buffer.data);
      return FALSE;
    }
  }

  if (rgb_row) g_free(rgb_row);

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  *jpeg_buffer = dest_buffer.data;
  *jpeg_size = dest_buffer.size;

  GST_DEBUG_OBJECT(log_target, "Successfully encoded JPEG image (%lu bytes)", dest_buffer.size);
  return TRUE;
}
