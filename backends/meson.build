configuration_msg += '''
Backend:                   @0@'''.format(get_option('packaging_backend'))

subdir(get_option('packaging_backend'))

if get_option('daemon_tests') and get_option('packaging_backend') != 'dummy'
	subdir('dummy')
endif
