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

opencv_dep = dependency('opencv4', required: false)
if not opencv_dep.found()
  opencv_dep = dependency('opencv', required: true)
endif
inference_backend = get_option('inference_backend')

if inference_backend == 'onnxruntime'
  # Desktop/Raspberry Pi backend used by the existing project.
  ort_root = get_option('prefix') == '/usr/local' ? '/opt/onnxruntime' : get_option('prefix')
  ort_lib_dir = join_paths(ort_root, 'lib')
  ort_inc = include_directories(join_paths(ort_root, 'include'))
  ort_lib = cc.find_library('onnxruntime', dirs : ort_lib_dir, required: true)
  onnxruntime_dep = declare_dependency(include_directories: ort_inc, dependencies: ort_lib)
else
  cuda_dep = cc.find_library('cudart', dirs: ['/usr/local/cuda/lib64'], required: true)
  nvinfer_dep = cc.find_library('nvinfer', required: true)
  nvonnxparser_dep = cc.find_library('nvonnxparser', required: true)
  tensorrt_inc = include_directories('/usr/include/aarch64-linux-gnu', '/usr/local/cuda/include')
  tensorrt_dep = declare_dependency(
    include_directories: tensorrt_inc,
    dependencies: [cuda_dep, nvinfer_dep, nvonnxparser_dep])
endif

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
if inference_backend == 'onnxruntime'
  gstonnxinference_sources = ['src/gstonnxinference.cpp']
  inference_dep = onnxruntime_dep
else
  gstonnxinference_sources = ['src/gsttensorrtinference.cpp']
  inference_dep = tensorrt_dep
endif

gstonnxinferenceexample = library('gstonnxinference',
  gstonnxinference_sources,
  c_args: plugin_c_args,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, opencv_dep, inference_dep],
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

if inference_backend == 'onnxruntime'
  # Classification is intentionally omitted from the first Jetson/Nano image.
  gstonnxclassifier_sources = ['src/gstonnxclassifier.cpp']
  gstonnxclassifierexample = library('gstonnxclassifier',
    gstonnxclassifier_sources,
    c_args: plugin_c_args,
    cpp_args: plugin_c_args,
    dependencies : [gst_dep, gstbase_dep, opencv_dep, onnxruntime_dep],
    link_with: gstonnxmeta_lib,
    install : true,
    install_dir : plugins_install_dir,
  )
endif

# The onnxpostprocess Plugin
gstonnxpostprocess_sources = [
  'src/gstonnxpostprocess.cpp',
]

gstonnxpostprocessexample = library('gstonnxpostprocess',
  gstonnxpostprocess_sources,
  c_args: plugin_c_args,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, opencv_dep],
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


# ---------------------------------------------------------------------------
# ByteTrack – compiled inline as part of the tracker plugin.
# No external library required. Sources live in src/bytetrack/.
# Eigen3 is provided by the host package libeigen3-dev.
# ---------------------------------------------------------------------------
eigen_dep = dependency('eigen3', required: true)
bytetrack_inc = include_directories(
  'src/bytetrack',         # so #include "ByteTrack/xxx.h" resolves
)

# The onnxtracker Plugin (ByteTrack sources compiled inline)
gstonnxtracker_sources = [
  'src/gstonnxtracker.cpp',
  # ByteTrack algorithm sources – bundled, no external library needed
  'src/bytetrack/BYTETracker.cpp',
  'src/bytetrack/KalmanFilter.cpp',
  'src/bytetrack/Object.cpp',
  'src/bytetrack/Rect.cpp',
  'src/bytetrack/STrack.cpp',
  'src/bytetrack/lapjv.cpp',
]

gstonnxtrackerexample = library('gstonnxtracker',
  gstonnxtracker_sources,
  c_args: plugin_c_args,
  cpp_args: plugin_c_args + ['-std=c++17'],
  include_directories: bytetrack_inc,
  dependencies : [gst_dep, gstbase_dep, opencv_dep, eigen_dep],
  link_with: gstonnxmeta_lib,
  install : true,
  install_dir : plugins_install_dir,
)
