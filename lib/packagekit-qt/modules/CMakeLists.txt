 
## install the cmake files

file( GLOB cmakeFiles FindQPackageKit.cmake )
set(module_install_dir ${CMAKE_ROOT}/Modules )

install( FILES ${cmakeFiles} 
         DESTINATION ${module_install_dir}  )

