#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([m20], [0.1], [serge@vak.ru])
AM_INIT_AUTOMAKE(m20, 0.1, no-define)
AC_CONFIG_SRCDIR([as/dis.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile as/Makefile])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_UINT64_T
AC_CHECK_SIZEOF([long])
AC_CHECK_SIZEOF([long long])

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([memset strchr strrchr strtol])

AC_OUTPUT
