include $(top_srcdir)/Makefile.decl

INCLUDES = \
	-I$(top_srcdir) \
	$(NULL)

TEST_PROGS = \
	$(NULL)

check_PROGRAMS = \
	$(TEST_PROGS)

noinst_PROGRAMS =  \
	frob-install-packages \
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
	$(PY_TESTS) \
	$(NULL)

CLEANFILES = \
	$(NULL)

all-local: $(check_PROGRAMS)

check-local: test

check-memory: perform-memcheck

.PHONY: check-memory