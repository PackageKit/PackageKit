
# Integration test that drives a live packagekitd (dummy backend) over D-Bus. It
# needs the D-Bus/polkit policy installed to system paths, so we allow it to auto-SKIP
# in case those are not installed.
if get_option('daemon_tests')
  run_daemon_test = find_program('run-daemon-test.py')
  test(
    'pk-test-daemon',
    run_daemon_test,
    args: [
      '--daemon', packagekitd_exec,
      '--test-binary', pk_test_daemon,
    ],
    depends: [packagekitd_exec, pk_test_daemon, pk_backend_dummy],
    suite: 'daemon',
    is_parallel: false,
    timeout: 480,
  )
endif
