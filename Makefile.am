ACLOCAL_AMFLAGS = -I m4

pkglib_LTLIBRARIES = transaction_time_audit.la

transaction_time_audit_la_SOURCES = transaction_time_audit.c

noinst_HEADERS = transaction_time_audit.h


transaction_time_audit_la_CFLAGS = @MYSQL_CFLAGS@ -I/usr/include/mysql/private
transaction_time_audit_la_CXXFLAGS = @MYSQL_CXXFLAGS@ -I/usr/include/mysql/private
transaction_time_audit_la_LDFLAGS = -module -avoid-version

test: all
	cd tests; . test.sh

pdf: manual.pdf

manual.pdf: manual.xml
	docbook2pdf manual.xml

html: manual.html

manual.html: manual.xml
	docbook2html -u manual.xml

EXTRA_DIST=tests/test.sh.in tests/install_plugins.inc tests/uninstall_plugins.inc
