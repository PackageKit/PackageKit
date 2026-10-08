if get_option('docs')
  if not get_option('gobject_introspection')
    error('The API documentation requires -Dgobject_introspection=true')
  endif
  subdir('api')
endif
