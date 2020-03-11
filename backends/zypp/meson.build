add_languages('cpp')

zypp_dep = dependency('libzypp', version: '>=6.16.0')

# define if libzypp returns package size in bytes
zypp_args = []
if zypp_dep.version().version_compare('>=11.4.0')
  zypp_args = ['-DZYPP_RETURN_BYTES=1']
endif

shared_module(
  'pk_backend_zypp',
  'pk-backend-zypp.cpp',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    zypp_dep,
    gmodule_dep,
  ],
  cpp_args: [
    '-DPK_COMPILATION=1',
    '-DG_LOG_DOMAIN="PackageKit-Zypp"',
    '-Wall',
    '-Woverloaded-virtual',
    '-Wnon-virtual-dtor'
  ],
  c_args: [
    '-Wno-deprecated',
    '-D_FILE_OFFSET_BITS=64',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)
