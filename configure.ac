AC_INIT(MySQL-audit, 0.1)
AM_INIT_AUTOMAKE([no-define])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_LIBTOOL
AC_PROG_CC_C99
WITH_MYSQL()
MYSQL_USE_PLUGIN_API()
MYSQL_SUBST()

pkglibdir=/usr/lib/mysql/plugin
AC_SUBST(pkglibdir)

AM_CONFIG_HEADER(config.h)

AC_OUTPUT(Makefile tests/test.sh)
