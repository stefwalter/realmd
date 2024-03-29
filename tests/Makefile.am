include $(top_srcdir)/Makefile.decl

INCLUDES = \
	-I$(top_srcdir) \
	-DTESTFILE_DIR="\"@abs_srcdir@/files\"" \
	-DSYSCONF_DIR="\"/tmp/realmd-etc\"" \
	-DPRIVATE_DIR="\"@abs_srcdir@/files\"" \
	$(NULL)

AM_CFLAGS = \
	$(GLIB_CFLAGS)

LDADD = \
	$(GLIB_LIBS)

TEST_PROGS = \
	test-ini-config \
	test-sssd-config \
	test-login-name \
	test-samba-ou-format \
	$(NULL)

check_PROGRAMS = \
	$(TEST_PROGS)

noinst_PROGRAMS =  \
	frob-install-packages \
	frob-package-set \
	$(NULL)

test_ini_config_SOURCES = \
	test-ini-config.c \
	$(top_srcdir)/service/realm-ini-config.c \
	$(top_srcdir)/service/realm-samba-config.c \
	$(top_srcdir)/service/realm-settings.c \
	$(NULL)

test_sssd_config_SOURCES = \
	test-sssd-config.c \
	$(top_srcdir)/service/realm-ini-config.c \
	$(top_srcdir)/service/realm-sssd-config.c \
	$(top_srcdir)/service/realm-settings.c \
	$(NULL)

test_login_name_SOURCES = \
	test-login-name.c \
	$(top_srcdir)/service/realm-login-name.c \
	$(NULL)

test_samba_ou_format_SOURCES = \
	test-samba-ou-format.c \
	$(top_srcdir)/service/realm-samba-util.c \
	$(NULL)

frob_install_packages_CFLAGS = \
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE \
	$(PACKAGEKIT_CFLAGS) \
	$(NULL)

frob_install_packages_LDADD = \
	$(PACKAGEKIT_LIBS) \
	$(NULL)

frob_package_set_SOURCES = \
	frob-package-set.c \
	$(top_srcdir)/service/realm-packages.c \
	$(top_srcdir)/service/realm-settings.c \
	$(top_srcdir)/service/realm-diagnostics.c \
	$(NULL)

frob_package_set_CFLAGS = \
	-I$(top_srcdir)/dbus \
	$(PACKAGEKIT_CFLAGS) \
	$(AM_CFLAGS) \
	$(NULL)

frob_package_set_LDADD = \
	$(PACKAGEKIT_LIBS) \
	$(LDADD) \
	$(NULL)

PY_TESTS = \
	$(NULL)

PY_ENV = \
	$(NULL)

test-c: $(TEST_PROGS)
	@gtester --verbose -m $(TEST_MODE) --g-fatal-warnings $(TEST_PROGS)

test-py:
	@for py in $(PY_TESTS); do echo "TEST: $$py"; $(PY_ENV) python $(srcdir)/$$py; done

test: test-c test-py

EXTRA_DIST = \
	files \
	$(PY_TESTS) \
	$(NULL)

CLEANFILES = \
	*.gcno \
	*.gcda \
	$(NULL)

all-local: $(check_PROGRAMS)

check-local: test

check-memory: perform-memcheck

.PHONY: check-memory