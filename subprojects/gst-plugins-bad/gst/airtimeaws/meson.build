if get_option('airtimeaws').disabled()
  subdir_done()
endif

aws_core_dep = dependency('aws-cpp-sdk-core', method : 'cmake', required : false, include_type: 'system')
aws_s3_dep = dependency('aws-cpp-sdk-s3', method : 'cmake', required : false, include_type: 'system')
aws_sts_dep = dependency('aws-cpp-sdk-sts', method : 'cmake', required : false, include_type: 'system')

if not aws_core_dep.found() or not aws_s3_dep.found() or not aws_sts_dep.found()
   message('Skipping airtimeaws plugin: AWS SDK dependencies not found')
   subdir_done()
endif

json_dep = dependency('json-glib-1.0', version : '>=1.6.6', fallback : ['json-glib', 'json_glib_dep'], required: true, include_type: 'system')
boost_dep = dependency('boost', method : 'cmake', required : false, include_type: 'system')

airtimeaws_sources = [
  'gstairtimeawsenv.cpp',
  'gstairtimeawsplugin.c',
  'gstairtimes3error.cpp',
  'gstairtimes3init.cpp',
  'gstairtimes3src.cpp',
  'gstairtimes3srccontext.cpp',
  'gstairtimes3urichunkdownloadqueue.cpp',
  'gstairtimes3urichunkprocessor.cpp',
  'gstairtimes3urichunksourceaws.cpp',
  'gstairtimes3urichunksourcedirectory.cpp',
  'gstairtimes3uridownloadedchunknotifier.cpp',
  'gstairtimes3uriprovider.cpp',
  'gstairtimes3uriproviders.cpp',
  'gstairtimes3uriprovidersfactory.cpp',
  'gstairtimes3utils.cpp',
]

if boost_dep.found()
  airtimeaws_sources += 'gstairtimes3urichunksourcefake.cpp'
else
  airtimeaws_sources += 'gstairtimes3urichunksourcefake_disabled.cpp'
endif

airtimeaws_headers = [
  'gstairtimeawsenv.hpp',
  'gstairtimes3bufferfillstatus.hpp',
  'gstairtimes3error.hpp',
  'gstairtimes3init.hpp',
  'gstairtimes3src.h',
  'gstairtimes3srccontext.h',
  'gstairtimes3srccontext.hpp',
  'gstairtimes3urichunkdownloadqueue.hpp',
  'gstairtimes3urichunkprocessor.hpp',
  'gstairtimes3urichunksource.hpp',
  'gstairtimes3urichunksourceaws.hpp',
  'gstairtimes3urichunksourcedirectory.hpp',
  'gstairtimes3urichunkspec.hpp',
  'gstairtimes3uridownloadedchunknotifier.hpp',
  'gstairtimes3uriobjectmetadata.hpp',
  'gstairtimes3uriprovider.hpp',
  'gstairtimes3uriproviderconfig.hpp',
  'gstairtimes3uriproviders.hpp',
  'gstairtimes3uriprovidersfactory.hpp',
  'gstairtimes3utils.hpp',
  'gstairtimescopedhelpers.hpp'
]

if boost_dep.found()
  airtimeaws_headers += 'gstairtimes3urichunksourcefake.hpp'
endif

doc_sources = []
foreach s: airtimeaws_sources + airtimeaws_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'airtimeaws': pathsep.join(doc_sources)
}

gstairtimeaws = library('gstairtimeaws',
  airtimeaws_sources,
  c_args : gst_plugins_bad_args,
  cpp_args: ['-std=c++23'],
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, aws_core_dep, aws_s3_dep, aws_sts_dep, json_dep, boost_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstairtimeaws]
