subdir('test')

packaging_backends = get_option('packaging_backend')

if get_option('daemon_tests') and 'dummy' not in packaging_backends
   packaging_backends += ['dummy']
endif

foreach packaging_backend : packaging_backends
   subdir(packaging_backend)
endforeach
