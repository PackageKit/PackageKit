
pk_backend_modules = []

subdir('test')

packaging_backends = get_option('packaging_backend')

if get_option('daemon_tests') and 'dummy' not in packaging_backends
   packaging_backends += ['dummy']
endif

foreach packaging_backend : packaging_backends
   subdir(packaging_backend)
endforeach

# Symlink all backend modules into this directory, mirroring the installed
# layout, so the daemon and tests can load them straight from the build tree
ln = find_program('ln')
pk_backend_module_links = []
foreach module : pk_backend_modules
  pk_backend_module_links += custom_target(
    'link-' + module.name(),
    input: module,
    output: 'lib' + module.name() + '.so',
    command: [ln, '-sf', module.full_path(), '@OUTPUT@'],
    build_by_default: true,
  )
endforeach
