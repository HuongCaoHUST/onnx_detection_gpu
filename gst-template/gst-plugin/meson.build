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
onnxruntime_dep = dependency('onnxruntime', required: true)

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

# The onnxpostprocess Plugin
gstonnxpostprocess_sources = [
  'src/gstonnxpostprocess.cpp',
]

gstonnxpostprocessexample = library('gstonnxpostprocess',
  gstonnxpostprocess_sources,
  c_args: plugin_c_args,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, opencv_dep, onnxruntime_dep],
  install : true,
  install_dir : plugins_install_dir,
)
