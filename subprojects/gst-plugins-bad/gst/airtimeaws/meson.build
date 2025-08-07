project('gst-plugins-airtime-aws', 'c', 'cpp',
  version : '1.26.2.0',
  meson_version : '>= 1.1',
  default_options : [ 'warning_level=1',
                      'cpp_std=c++23',
                      'c_std=c17',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0].to_int()
gst_version_minor = version_arr[1].to_int()
gst_version_micro = version_arr[2].to_int()
if version_arr.length() == 4
  gst_version_nano = version_arr[3].to_int()
else
  gst_version_nano = 0
endif
gst_version_is_stable = gst_version_minor.is_even()
gst_version_is_dev = gst_version_minor.is_odd() and gst_version_micro < 90

gst_plugins_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')

if gst_version_is_stable
  gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
else
  gst_req = '>= ' + gst_version
endif

gst_dep = dependency('gstreamer-1.0', version : gst_req, fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req, fallback : ['gstreamer', 'gst_base_dep'])
json_glib_dep = dependency('json-glib-1.0', version : '>= 1.4.0')

aws_core_dep = dependency('aws-cpp-sdk-core', method : 'cmake')
aws_s3_dep = dependency('aws-cpp-sdk-s3', method : 'cmake')
aws_sts_dep = dependency('aws-cpp-sdk-sts', method : 'cmake')

boost_dep = dependency('boost', method : 'cmake')

cdata = configuration_data()
cdata.set_quoted('VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-airtimeaws-plugin')

# GStreamer package name and origin url
# gst_package_name = get_option('package-name')
#if gst_package_name == ''
  gst_package_name = ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer airtime aws plugins source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer airtime aws plugins git'
  else
    gst_package_name = 'GStreamer airtime aws plugins prerelease'
  endif
# endif
cdata.set_quoted('GST_PACKAGE_NAME', gst_package_name)
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https//www.airtimetools.com')
cdata.set_quoted('GST_LICENSE', 'LGPL')
configure_file(output : 'config.h', configuration : cdata)

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

# ----------------------------------------------------------------------------

# The gstairtimeaws plugin
gstairtimeaws_sources = [
  'gstairtimeawsplugin.c',
  'gstairtimeawsenv.cpp',
  'gstairtimes3error.cpp',
  'gstairtimes3src.cpp',
  'gstairtimes3srccontext.cpp',
  'gstairtimes3urichunkdownloadqueue.cpp',
  'gstairtimes3urichunkprocessor.cpp',
  'gstairtimes3urichunksourceaws.cpp',
  'gstairtimes3urichunksourcefake.cpp',
  'gstairtimes3uridownloadedchunknotifier.cpp',
  'gstairtimes3uriprovider.cpp',
  'gstairtimes3uriproviders.cpp',
  'gstairtimes3uriprovidersfactory.cpp'
  ]

gstairtimeaws = library('gstairtimeaws',
  gstairtimeaws_sources,
  c_args: gst_plugins_args,
  cpp_args: gst_plugins_args,
  include_directories : configinc,
  dependencies : [
    gst_dep, 
    gstbase_dep, 
    json_glib_dep, 
    aws_s3_dep, 
    aws_core_dep, 
    aws_sts_dep,
    boost_dep],
  install : true,
  install_dir : plugins_install_dir
)

