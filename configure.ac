AC_INIT(gluff.c)
PRODUCT=gluff
VERSION=1.12
AC_CONFIG_HEADER(config.h)
AC_PREFIX_DEFAULT(/opt/gluff)
AC_PROG_CC
AC_DEFINE_UNQUOTED(PRODUCT, "$PRODUCT")
AC_DEFINE_UNQUOTED(VERSION, "$VERSION")
AC_SUBST(PRODUCT)
AC_SUBST(VERSION)

LDFLAGS="$LDFLAGS -L/usr/lib/mysql -L/usr/local/lib"
CFLAGS="$CFLAGS -I/usr/local/include"
CPPFLAGS="$CPPFLAGS -I/usr/local/include"

opt_batch_limit=1000

dnl argument parsing for optional features
AC_ARG_WITH(batch-limit, AC_HELP_STRING([--with-batch-limit], [set limit for batch size]), opt_batch_limit=$withval)

if test "$opt_batch_limit" != "no"; then
   AC_MSG_NOTICE([Gluff: Setting batch limit to $opt_batch_limit. This requires an sqlite3 compiled with the SQLITE_ENABLE_UPDATE_DELETE_LIMIT option.])
   AC_DEFINE_UNQUOTED(BATCH_LIMIT, $opt_batch_limit)
else
   AC_MSG_WARN([Gluff: No limit on batch size. This could cause performance problems on a busy DHCP server.])
fi

dnl Checks for programs.
AC_PROG_CC
if test $CC = "gcc"; then
	CFLAGS="$CFLAGS -Wall"
fi
AC_PROG_INSTALL
AC_PROG_LN_S

dnl Checks for libraries.
AC_CHECK_LIB(sqlite3,sqlite3_open, ,AC_MSG_ERROR([Need libsqlite3.a]))
AC_CHECK_LIB(mysqlclient,mysql_init, ,AC_MSG_ERROR([Need libmysqlclient.a]))

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(limits.h unistd.h mysql/mysql.h sqlite3.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

dnl Checks for library functions.
AC_TYPE_SIGNAL

AC_OUTPUT(Makefile)
