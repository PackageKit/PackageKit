python = import('python')
python_exec = python.find_installation()

enum_convertor = files('enum-convertor.py')

python_package_dir = get_option('pythonpackagedir')
if python_package_dir == ''
  python_package_dir = python_exec.get_install_dir()
endif
python_package_dir = join_paths(python_package_dir, 'packagekit')

subdir('packagekit')
