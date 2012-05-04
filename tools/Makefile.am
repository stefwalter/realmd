include $(top_srcdir)/Makefile.decl

bin_PROGRAMS = \
	realm-enroll

realm_enroll_SOURCES = \
	realm-enroll.c \
	$(NULL)

realm_enroll_CFLAGS = \
	-I$(top_srcdir)/dbus \
	$(GLIB_CFLAGS) \
	$(KRB5_CFLAGS) \
	$(NULL)

realm_enroll_LDADD = \
	$(top_builddir)/dbus/librealm-dbus.a \
	$(GLIB_LIBS) \
	$(KRB5_LIBS) \
	$(NULL)
	