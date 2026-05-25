project('gst-vlm-vision', 'c',
  version : '0.2.0',
  meson_version : '>=0.50.0',
  default_options : ['warning_level=1', 'c_std=c11'])

gst_version = '>=1.16.0'

# Dependencies
glib_dep = dependency('glib-2.0', version : '>=2.56')
gobject_dep = dependency('gobject-2.0')
gst_dep = dependency('gstreamer-1.0', version : gst_version)
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_version)
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_version)
curl_dep = dependency('libcurl', required : true)
jsonc_dep = dependency('json-c', required : true)
libjpeg_dep = dependency('libjpeg', required : false)

# GObject Introspection (optional)
gir_dep = dependency('gobject-introspection-1.0', required : false)
build_gir = gir_dep.found()

plugin_name = 'vlmvision'
plugin_so_name = 'gst' + plugin_name

plugin_sources = [
  'src/plugin.c',
  'src/gstvlmvisionmeta.c',
  'src/jpeg-encoder.c',
  'src/http-client.c',
  'src/serializer-openai-chat.c',
  'src/response-parser-openai-chat.c',
  'src/template-engine.c',
  'src/serializer-template.c',
  'src/response-parser-template.c',
  'src/gstvlmvision.c',
]

gst_vlmvision_lib = shared_module(plugin_so_name,
  plugin_sources,
  dependencies : [glib_dep, gobject_dep, gst_dep, gstbase_dep, gstvideo_dep, curl_dep, jsonc_dep, libjpeg_dep],
  install : true,
  install_dir : join_paths(get_option('libdir'), 'gstreamer-1.0'),
)

# Optional: generate GObject Introspection data
if build_gir
  gnome = import('gnome')
  gir_headers = ['src/gstvlmvision.h', 'src/gstvlmvisionmeta.h']
  gir_sources = plugin_sources

  gir_scanner_cflags = ['-DGST_PLUGIN_BUILD_STATIC']

  gnome.generate_gir(gst_vlmvision_lib,
    sources : gir_sources + gir_headers,
    nsversion : '1.0',
    namespace : 'GstVlmVision',
    symbol_prefix : 'gst_vlm_vision',
    identifier_prefix : 'GstVlmVision',
    export_packages : [plugin_so_name],
    includes : ['Gst-1.0', 'GstBase-1.0', 'GstVideo-1.0'],
    install : true,
    extra_args : [
      '--warn-all',
      '--library-path=' + meson.current_build_dir(),
    ] + gir_scanner_cflags
  )
else
  message('GObject introspection not found, not generating bindings')
endif

# Generate pkg-config file
pkg = import('pkgconfig')
pkg.generate(
  name : plugin_name,
  filebase : plugin_so_name,
  description : 'GStreamer plugin for OpenAI-compatible VLM vision processing',
  libraries_private : ['-L${pluginsdir}', '-l' + plugin_so_name],
  variables : [
    'pluginsdir=' + join_paths('${libdir}', 'gstreamer-1.0')
  ],
  requires : [
    'gstreamer-1.0',
    'gstreamer-base-1.0',
    'gstreamer-video-1.0',
  ],
)
