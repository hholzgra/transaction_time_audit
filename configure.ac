AC_INIT([transaction_time_audit], [0.1], [hartmut@mariadb.com])
AC_PREREQ([2.63])

AM_INIT_AUTOMAKE([1.11 tar-pax no-define silent-rules -Wall -Werror])
AM_SILENT_RULES([yes])

AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC_C99

WITH_MYSQL()
MYSQL_USE_PLUGIN_API()
MYSQL_SUBST()

AM_PROG_AR
AC_PROG_LIBTOOL

pkglibdir=/usr/lib/mysql/plugin
AC_SUBST(pkglibdir)

AM_CONFIG_HEADER(config.h)

AC_OUTPUT(Makefile tests/test.sh)
