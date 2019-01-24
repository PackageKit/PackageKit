subdir('test')
subdir(get_option('packaging_backend'))

if get_option('daemon_tests') and get_option('packaging_backend') != 'dummy'
  subdir('dummy')
endif
