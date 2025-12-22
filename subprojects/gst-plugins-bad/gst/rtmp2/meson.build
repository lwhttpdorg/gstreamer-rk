rtmp2_sources = [
  'gstrtmp2.c',
  'gstrtmp2element.c',
  'gstrtmp2locationhandler.c',
  'gstrtmp2sink.c',
  'gstrtmp2src.c',
  'gstrtmp2serversrc.c',
  'gstrtmp2server.c',
  'rtmp/amf.c',
  'rtmp/rtmpchunkstream.c',
  'rtmp/rtmpclient.c',
  'rtmp/rtmpconnection.c',
  'rtmp/rtmphandshake.c',
  'rtmp/rtmpmessage.c',
  'rtmp/rtmputils.c',
  'server/rtmp2amf.c',
  'server/rtmp2chunk.c',
  'server/rtmp2chunk_v2.c',
  'server/rtmp2client.c',
  'server/rtmp2enhanced.c',
  'server/rtmp2flv.c',
  'server/rtmp2handshake.c',
]

rtmp2_headers = [
  'gstrtmp2locationhandler.h',
  'gstrtmp2elements.h',
  'gstrtmp2sink.h',
  'gstrtmp2src.h',
  'gstrtmp2serversrc.h',
  'gstrtmp2server.h',
]

doc_sources = []
foreach s: rtmp2_sources + rtmp2_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rtmp2': pathsep.join(doc_sources)
}

if get_option('rtmp2').disabled()
  subdir_done()
endif

gstrtmp2 = library('gstrtmp2',
  rtmp2_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrtmp2]
