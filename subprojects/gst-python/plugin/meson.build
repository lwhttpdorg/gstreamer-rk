gstpython = library('gstpython',
    ['gstpythonplugin.c'],
    include_directories : [configinc],
    dependencies : [gst_dep, pygobject_dep, gstbase_dep, python_embed_dep, gmodule_dep, libdl],
    install : true,
    install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
)
plugins = [gstpython]

plugin_names = []
gst_plugins = []
foreach plugin: plugins
  dep = declare_dependency(link_with: plugin, variables: {'full_path': plugin.full_path()})
  meson.override_dependency(plugin.name(), dep)
  gst_plugins += [dep]
  if plugin.name().startswith('gst')
    plugin_names += [plugin.name().substring(3)]
  else
    plugin_names += [plugin.name()]
  endif
endforeach

summary({
    'Plugins': plugin_names,
}, list_sep: ', ')
