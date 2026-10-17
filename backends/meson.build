
pk_backend_modules = []

subdir('spawn')

packaging_backends = get_option('pk-backend')

if get_option('daemon-tests') and 'dummy' not in packaging_backends
    packaging_backends += ['dummy']
endif

foreach packaging_backend : packaging_backends
    subdir(packaging_backend)
endforeach
