include $(top_srcdir)/Makefile.decl

INCLUDES = \
	-I$(top_srcdir) \
	-DTESTFILE_DIR="\"@abs_srcdir@/files\"" \
	-DSYSCONF_DIR="\"/tmp/realmd-etc\"" \
	-DSERVICE_DIR="\"@abs_srcdir@/files\"" \
	$(NULL)

TEST_PROGS = \
	test-samba-config \
	$(NULL)

check_PROGRAMS = \
	$(TEST_PROGS)

noinst_PROGRAMS =  \
	frob-install-packages \
	$(NULL)

test_samba_config_SOURCES = \
	test-samba-config.c \
	$(top_srcdir)/service/realm-samba-config.c \
	$(top_srcdir)/service/realm-platform.c \
	$(NULL)

frob_install_packages_CFLAGS = \
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE \
	$(PACKAGEKIT_CFLAGS) \
	$(NULL)

frob_install_packages_LDADD = \
	$(PACKAGEKIT_LIBS) \
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