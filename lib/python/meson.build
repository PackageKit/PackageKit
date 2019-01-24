python = import('python')
python_exec = python.find_installation()

python_package_dir = get_option('python-package-dir')
if python_package_dir == ''
	python_package_dir = python_exec.get_install_dir()
endif
python_package_dir = join_paths(python_package_dir, 'packagekit')

configuration_msg += '''
Python language version:   @0@
Python package dir:        @1@'''.format(
	python_exec.language_version(),
	python_package_dir,
)

enum_convertor = files('enum-convertor.py')
enums_py = custom_target (
	'enums.py',
	input: join_paths(meson.source_root(), 'lib', 'packagekit-glib2', 'pk-enum.c'),
	output: 'enums.py',
	command: [
		python_exec,
		enum_convertor,
		'@INPUT@',
	],
	capture: true,
	install: true,
	install_dir: python_package_dir,
)

subdir('packagekit')
