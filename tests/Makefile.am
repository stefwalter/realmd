
TEST_CFLAGS = \
	-I$(top_srcdir) \
	-DTESTFILE_DIR="\"@abs_srcdir@/tests/files\"" \
	-DSYSCONF_DIR="\"/tmp/realmd-etc\"" \
	-DPRIVATE_DIR="\"@abs_srcdir@/tests/files\"" \
	$(GLIB_CFLAGS) \
	$(POLKIT_CFLAGS) \
	$(NULL)

TEST_LIBS = \
	$(GLIB_LIBS)

TEST_PROGS = \
	test-dn-util \
	test-ini-config \
	test-sssd-config \
	test-safe-format \
	test-login-name \
	test-settings \
	$(NULL)

TESTS += $(TEST_PROGS)
check_PROGRAMS += $(TEST_PROGS)

noinst_PROGRAMS +=  \
	frob-install-packages \
	$(NULL)

test_dn_util_SOURCES = \
	tests/test-dn-util.c \
	service/realm-dn-util.c \
	$(NULL)
test_dn_util_LDADD = $(TEST_LIBS)
test_dn_util_CFLAGS = $(TEST_CFLAGS)

test_ini_config_SOURCES = \
	tests/test-ini-config.c \
	service/realm-ini-config.c \
	service/realm-samba-config.c \
	service/realm-settings.c \
	$(NULL)
test_ini_config_LDADD = $(TEST_LIBS)
test_ini_config_CFLAGS = $(TEST_CFLAGS)

test_sssd_config_SOURCES = \
	tests/test-sssd-config.c \
	service/realm-ini-config.c \
	service/realm-sssd-config.c \
	service/realm-settings.c \
	$(NULL)
test_sssd_config_LDADD = $(TEST_LIBS)
test_sssd_config_CFLAGS = $(TEST_CFLAGS)

test_safe_format_SOURCES = \
	tests/test-safe-format.c \
	service/safe-format-string.c \
	$(NULL)
test_safe_format_LDADD = $(TEST_LIBS)
test_safe_format_CFLAGS = $(TEST_CFLAGS)

test_login_name_SOURCES = \
	tests/test-login-name.c \
	service/realm-login-name.c \
	$(NULL)
test_login_name_LDADD = $(TEST_LIBS)
test_login_name_CFLAGS = $(TEST_CFLAGS)

test_settings_SOURCES = \
	tests/test-settings.c \
	service/realm-settings.c \
	$(NULL)
test_settings_LDADD = $(TEST_LIBS)
test_settings_CFLAGS = $(TEST_CFLAGS)

frob_install_packages_SOURCES = \
	tests/frob-install-packages.c \
	service/realm-packages.c \
	service/realm-settings.c \
	service/realm-errors.c \
	$(NULL)
frob_install_packages_CFLAGS = \
	-I$(srcdir)/dbus \
	$(TEST_CFLAGS) \
	$(NULL)
frob_install_packages_LDADD  = \
	$(TEST_LIBS) \
	$(NULL)

EXTRA_DIST += \
	tests/files \
	$(PY_TESTS) \
	$(NULL)
