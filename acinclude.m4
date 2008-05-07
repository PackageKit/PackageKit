dnl Macros to help with configuring Python extensions via autoconf.
dnl  Copyright (C) 1998,  James Henstridge <james@daa.com.au>
dnl
dnl  Distribute under the same rules as Autoconf itself.
dnl
dnl Used similar to AC_CHECK_LIB and associates.
dnl Swiped from http://www.initd.org/svn/psycopg/psycopg1/trunk/aclocal.m4

dnl PY_CHECK_MOD(MODNAME [,SYMBOL [,ACTION-IF-FOUND [,ACTION-IF-NOT-FOUND]]])
dnl Check if a module containing a given symbol is visible to python.
AC_DEFUN([PY_CHECK_MOD],
[AC_REQUIRE([AM_PATH_PYTHON])
py_mod_var=`echo $1['_']$2 | sed 'y%./+-%__p_%'`
AC_MSG_CHECKING(for ifelse([$3],[],,[$2 in ])python module $1)
AC_CACHE_VAL(py_cv_mod_$py_mod_var, [
if $PYTHON -c 'import $1 ifelse([$2],[],,[; $1.$2])' 1>&AC_FD_CC 2>&AC_FD_CC; then
  eval "py_cv_mod_$py_mod_var=yes"
else
  eval "py_cv_mod_$py_mod_var=no"
fi
])
py_val=`eval "echo \`echo '$py_cv_mod_'$py_mod_var\`"`
if test "x$py_val" != xno; then
  AC_MSG_RESULT(yes)
  ifelse([$3], [],, [$3
])dnl
else
  AC_MSG_RESULT(no)
  ifelse([$4], [],, [$4
])dnl
fi
])

