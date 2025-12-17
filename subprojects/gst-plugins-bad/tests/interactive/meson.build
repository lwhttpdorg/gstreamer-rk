fs = import('fs')

if not get_option('soundtouch').disabled() and soundtouch_dep.found()
  executable('pitch-test', 'pitch-test.c',
    include_directories: [configinc],
    dependencies: [gst_dep, gstcontroller_dep],
    install: false)

  fs.copyfile('../files/audio-8s-then-reverse.ogg', 'audio-8s-then-reverse.ogg')
  executable('pitch-playback-test', 'pitch-playback-test.c',
    include_directories: [configinc],
    dependencies: [gst_dep],
    install: false)
endif
