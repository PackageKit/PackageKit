packagekit_py_sources = [
	'__init__.py',
	'backend.py',
	'progress.py',
	'package.py',
	'filter.py',
	'misc.py',
]

enums_py = custom_target(
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

install_data(
	packagekit_py_sources,
	install_dir: python_package_dir,
)

packagekit_test_py = custom_target(
	'packagekit.py',
	input: packagekit_py_sources,
	output: packagekit_py_sources,
	command: [
		'cp',
		'@INPUT@',
		'@OUTDIR@',
	],
	install: false,
)
