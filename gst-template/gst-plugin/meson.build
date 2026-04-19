plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-template-plugin')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer template Plug-ins')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://gstreamer.freedesktop.org')
configure_file(output : 'config.h', configuration : cdata)

gstaudio_dep = dependency('gstreamer-audio-1.0',
    fallback: ['gst-plugins-base', 'audio_dep'])

opencv_dep = dependency('opencv4', required: true)
# Use the newer version found in /opt/onnxruntime
ort_root = '/opt/onnxruntime'
ort_lib_dir = join_paths(ort_root, 'lib')
ort_inc = include_directories(join_paths(ort_root, 'include'))
ort_lib = cc.find_library('onnxruntime', dirs : ort_lib_dir, required: true)
onnxruntime_dep = declare_dependency(include_directories: ort_inc, dependencies: ort_lib)

# Plugin 1
plugin_sources = [
  'src/gstplugin.c'
  ]

gstpluginexample = library('gstplugin',
  plugin_sources,
  c_args: plugin_c_args,
  dependencies : [gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)

# Plugin 2 (audio filter example)
audiofilter_sources = [
  'src/gstaudiofilter.c',
  ]

gstaudiofilterexample = library('gstaudiofilterexample',
  audiofilter_sources,
  c_args: plugin_c_args,
  dependencies : [gst_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)

# The onnxinference Plugin
gstonnxinference_sources = [
  'src/gstonnxinference.cpp',
]

gstonnxinferenceexample = library('gstonnxinference',
  gstonnxinference_sources,
  c_args: plugin_c_args,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, opencv_dep, onnxruntime_dep],
  install : true,
  install_dir : plugins_install_dir,
)

# Shared metadata library for ONNX plugins
gstonnxmeta_lib = library('gstonnxmeta-shared',
  'src/gstonnxmeta.cpp',
  c_args: plugin_c_args,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep],
  install : false,
)

# The onnxpostprocess Plugin
gstonnxpostprocess_sources = [
  'src/gstonnxpostprocess.cpp',
]

gstonnxpostprocessexample = library('gstonnxpostprocess',
  gstonnxpostprocess_sources,
  c_args: plugin_c_args,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, opencv_dep, onnxruntime_dep],
  link_with: gstonnxmeta_lib,
  install : true,
  install_dir : plugins_install_dir,
)

# The onnxoverlay Plugin
gstonnxoverlay_sources = [
  'src/gstonnxoverlay.cpp',
]

gstonnxoverlayexample = library('gstonnxoverlay',
  gstonnxoverlay_sources,
  c_args: plugin_c_args,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, opencv_dep],
  link_with: gstonnxmeta_lib,
  install : true,
  install_dir : plugins_install_dir,
)
