AC_PREREQ(2.63)

AC_INIT([realmd], [0.8.1],
        [http://bugs.freedesktop.org/enter_bug.cgi?product=realmd],
        [realmd])

# -----------------------------------------------------------------------------

AC_CONFIG_MACRO_DIR([build/m4])
AM_INIT_AUTOMAKE([1.11 tar-ustar foreign -Wno-portability])
AM_SANITY_CHECK
AM_CONFIG_HEADER(config.h)
AC_CONFIG_SRCDIR([service/realm-daemon.c])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_MAINTAINER_MODE([enable])

# -----------------------------------------------------------------------------
# Distro Detection

AC_ARG_WITH([distro],
            [AS_HELP_STRING([--with-distro],
                            [Configure for a specific distribution (eg: redhat)]
                           )],
            [DISTRO=$withval])

if test -z $DISTRO; then
	AC_CHECK_FILE(/etc/redhat-release, [DISTRO="redhat"])
	AC_CHECK_FILE(/etc/debian_version, [DISTRO="debian"])
	# Not customized for these yet
	dnl AC_CHECK_FILE(/etc/gentoo-release, [DISTRO="gentoo"])
	dnl AC_CHECK_FILE(/etc/slackware-version, [DISTRO="slackware"])
	dnl AC_CHECK_FILE(/etc/SuSE-release, [DISTRO="suse"])
fi

if test -z $DISTRO; then
	AC_MSG_ERROR([Couldn't detect the distro to configure for. Specify one with --with-distro])
fi

AC_SUBST(DISTRO)

# -----------------------------------------------------------------------------
# Basic tools

AC_GNU_SOURCE
AC_ISC_POSIX
AC_PROG_CC
AC_PROG_CPP
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB
IT_PROG_INTLTOOL([0.35.0])
AC_PATH_PROG(PKG_CONFIG, pkg-config, no)

GETTEXT_PACKAGE=realmd
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[The gettext domain name])
AM_GLIB_GNU_GETTEXT

# --------------------------------------------------------------------
# GLib

PKG_CHECK_MODULES(GLIB,
	glib-2.0 >= 2.32.0
	gio-2.0 >= 2.32.0
	gio-unix-2.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

GTK_DOC_CHECK(1.9)

AC_PATH_PROG([XSLTPROC], [xsltproc])
if test x$XSLTPROC = x; then
	AC_MSG_ERROR([xsltproc is required to build documentation])
fi

# --------------------------------------------------------------------
# PackageKit

PKG_CHECK_MODULES(PACKAGEKIT, packagekit-glib2)
AC_SUBST(PACKAGEKIT_CFLAGS)
AC_SUBST(PACKAGEKIT_LIBS)

# --------------------------------------------------------------------
# PolicyKit

PKG_CHECK_MODULES(POLKIT, polkit-gobject-1)
AC_SUBST(POLKIT_CFLAGS)
AC_SUBST(POLKIT_LIBS)

# -------------------------------------------------------------------
# Kerberos

if test "$KRB5_LIBS" != ""; then
	KRB5_PASSED_LIBS=$KRB5_LIBS
fi

if test "$KRB5_CFLAGS" != ""; then
	KRB5_PASSED_CFLAGS=$KRB5_CFLAGS
fi

AC_PATH_PROG(KRB5_CONFIG, krb5-config)
AC_MSG_CHECKING(for working krb5-config)
if test -x "$KRB5_CONFIG"; then
	KRB5_CFLAGS="`$KRB5_CONFIG --cflags`"
	KRB5_LIBS="`$KRB5_CONFIG --libs`"
	AC_MSG_RESULT(yes)
else
	if test x$KRB5_PASSED_LIBS = x; then
		AC_MSG_ERROR(no. Please install MIT kerberos devel package)
	fi
fi

if test x$KRB5_PASSED_LIBS != x; then
	KRB5_LIBS=$KRB5_PASSED_LIBS
fi

if test x$KRB5_PASSED_CFLAGS != x; then
	KRB5_CFLAGS=$KRB5_PASSED_CFLAGS
fi

AC_ARG_VAR([KRB5_CFLAGS], [C compiler flags for kerberos, overriding krb5-config])dnl
AC_ARG_VAR([KRB5_LIBS], [linker flags for kerberos, overriding krb5-config])dnl

AC_SUBST(KRB5_CFLAGS)
AC_SUBST(KRB5_LIBS)

# -------------------------------------------------------------------
# LDAP

AC_CHECK_LIB(ldap, ldap_search, , [ldap_invalid=yes])
AC_CHECK_LIB(lber, ber_pvt_opt_on, , [ldap_invalid=yes])
AC_CHECK_HEADERS([ldap.h], , [ldap_invalid=yes])

if test "$ldap_invalid" = "yes"; then
	AC_MSG_ERROR(["Couldn't find OpenLDAP headers or libraries"])
fi

LDAP_LIBS=""
AC_CHECK_LIB(resolv, res_query, LDAP_LIBS="-lresolv")
LDAP_LIBS="$LDAP_LIBS -lldap -llber"
LDAP_CFLAGS=""

AC_SUBST(LDAP_LIBS)
AC_SUBST(LDAP_CFLAGS)

# -------------------------------------------------------------------
# Directories

DBUS_SERVICES_DIR="${datadir}/dbus-1/system-services"
AC_SUBST(DBUS_SERVICES_DIR)

DBUS_POLICY_DIR="${sysconfdir}/dbus-1/system.d"
AC_SUBST(DBUS_POLICY_DIR)

POLKIT_ACTION_DIR="${datadir}/polkit-1/actions"
AC_SUBST(POLKIT_ACTION_DIR)

# --------------------------------------------------------------------
# Compilation options

AC_MSG_CHECKING([for debug mode])
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--enable-debug=no/default/yes],
                             [Turn on or off debugging])
             )

if test "$enable_debug" != "no"; then
	AC_DEFINE_UNQUOTED(WITH_DEBUG, 1, [Print debug output])
	AC_DEFINE_UNQUOTED(_DEBUG, 1, [In debug mode])
	CFLAGS="$CFLAGS -g"
fi
if test "$enable_debug" = "yes"; then
	debug_status="yes"
	CFLAGS="$CFLAGS -O0"
elif test "$enable_debug" = "no"; then
	debug_status="no"
	CFLAGS="$CFLAGS -O2"
	AC_DEFINE_UNQUOTED(G_DISABLE_ASSERT, 1, [Disable glib assertions])
else
	debug_status="default"
fi

AC_MSG_RESULT($debug_status)

if test "$GCC" = "yes"; then
	CFLAGS="$CFLAGS \
		-Wall -Wmissing-declarations \
		-Wmissing-prototypes -Wnested-externs -Wpointer-arith \
		-Wdeclaration-after-statement -Wformat=2 -Winit-self \
		-Waggregate-return -Wmissing-format-attribute"

	for option in -Wmissing-include-dirs -Wundef; do
		SAVE_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $option"
		AC_MSG_CHECKING([whether gcc understands $option])
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
				  [has_option=yes],
				  [has_option=no])
		AC_MSG_RESULT($has_option)
		if test $has_option = no; then
			CFLAGS="$SAVE_CFLAGS"
		fi
	done

	# Only add this when optimizing is enabled (default)
	AC_MSG_CHECKING([whether optimization is enabled])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#if __OPTIMIZE__ == 0
	                                     #error No optimization
	                                     #endif
	                                   ]], [[]])],
	                  [has_optimization=yes],
	                  [has_optimization=no])
	if test $has_optimization = yes; then
		CFLAGS="$CFLAGS -Wp,-D_FORTIFY_SOURCE=2"
		AC_MSG_RESULT([yes, fortifying source])
	else
		AC_MSG_RESULT([no, not fortifying source])
	fi
fi

AC_ARG_ENABLE(strict, [
               AS_HELP_STRING([--enable-strict], [Strict code compilation])
             ])

AC_MSG_CHECKING([build strict])

if test "$enable_strict" = "yes"; then
	CFLAGS="$CFLAGS -Werror \
		-DG_DISABLE_DEPRECATED"
	TEST_MODE="thorough"
else
	CFLAGS="$CFLAGS -Wno-error"
	TEST_MODE="quick"
	enable_strict="no"
fi

AC_MSG_RESULT($enable_strict)
AC_SUBST(TEST_MODE)

AC_MSG_CHECKING([whether to build with gcov testing])
AC_ARG_ENABLE([coverage],
              AS_HELP_STRING([--enable-coverage],
                             [Whether to enable coverage testing ]),
              [], [enable_coverage=no])
AC_MSG_RESULT([$enable_coverage])

if test "$enable_coverage" = "yes"; then
	if test "$GCC" != "yes"; then
		AC_MSG_ERROR(Coverage testing requires GCC)
	fi

	AC_PATH_PROG(GCOV, gcov, no)
	if test "$GCOV" = "no" ; then
		AC_MSG_ERROR(gcov tool is not available)
	fi

	AC_PATH_PROG(LCOV, lcov, no)
	if test "$LCOV" = "no" ; then
		AC_MSG_ERROR(lcov tool is not installed)
	fi

	AC_PATH_PROG(GENHTML, genhtml, no)
	if test "$GENHTML" = "no" ; then
		AC_MSG_ERROR(lcov's genhtml tool is not installed)
	fi

	CFLAGS="$CFLAGS -O0 -g -fprofile-arcs -ftest-coverage"
	LDFLAGS="$LDFLAGS -lgcov"
fi

AM_CONDITIONAL([WITH_COVERAGE], [test "$enable_coverage" = "yes"])
AC_SUBST(LCOV)
AC_SUBST(GCOV)
AC_SUBST(GENHTML)

# ------------------------------------------------------------------------------
# Results

privatedir='${libdir}/realmd'
AC_SUBST(privatedir)

AC_CONFIG_FILES([
	Makefile
	build/Makefile
	dbus/Makefile
	doc/Makefile
	doc/version.xml
	po/Makefile.in
	po/Makefile
	service/Makefile
	tests/Makefile
	tools/Makefile
])
AC_OUTPUT

echo
echo "CFLAGS: $CFLAGS"
echo
echo "OPTIONS:"
echo "  Debug:                $debug_status"
echo "  Coverage:             $enable_coverage"
echo "  Strict:               $enable_strict"
echo
