AC_PREREQ(2.61)
# Checks for programs.
AC_INIT([libjsmisc], [1.1], [support@mindbit.ro])

# Detect missing Autoconf Archive library in the system
m4_pattern_forbid([^AX_])

AC_CANONICAL_TARGET
AC_CANONICAL_HOST
AC_CANONICAL_SYSTEM
AX_COMPILER_VENDOR
AX_COMPILER_VERSION

AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_CC_C_O
AC_CHECK_SIZEOF([void *])
LT_INIT
AC_SUBST(MKDIR_P)

CFLAGS="-Wall $CFLAGS"

#
# additional configure arguments
#

AC_ARG_WITH(duktape,
    AC_HELP_STRING([--with-duktape=DIR], [search for Duktape headers in DIR]),
    [
        DUKTAPE="$withval"
    ]
)

#
# test for Duktape
#

if test -n "$DUKTAPE" ; then
    CPPFLAGS="-I$DUKTAPE $CPPFLAGS"
fi
AC_CHECK_HEADER(duktape.h,[],[],-)
if test "x$ac_cv_header_duktape_h" != xyes ; then
    AC_MSG_ERROR([duktape.h not found])
fi
LIBS="-lm $LIBS"
AC_CHECK_LIB([duktape],[duk_create_heap],[],[
    AC_MSG_ERROR([Duktape library not found])
])

#
# Debug mode
#

AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug], [Enable debugging mode]))

AS_IF([test "x$enable_debug" = "xyes"], [
     CFLAGS="$CFLAGS -DJS_DEBUG"
])

#
# Git versioning
#

AC_DEFINE_UNQUOTED(VERSION_STR,
    ["libjsmisc `git describe` ($host, $ax_cv_c_compiler_vendor cc $ax_cv_c_compiler_version, `expr $ac_cv_sizeof_void_p \* 8`-bit)"],
    [A string containing the version number, platform, and C compiler])

#
# Output
#

AC_OUTPUT
