include $(top_srcdir)/Makefile.decl

bin_PROGRAMS = \
	realm-discover \
	realm-enroll

realm_enroll_SOURCES = \
	realm-enroll.c \
	$(NULL)

AM_CFLAGS = \
	-I$(top_srcdir)/dbus \
	-I$(top_builddir)/dbus \
	$(GLIB_CFLAGS) \
	$(KRB5_CFLAGS) \
	$(NULL)

LDADD = \
	$(top_builddir)/dbus/librealm-dbus.a \
	$(GLIB_LIBS) \
	$(KRB5_LIBS) \
	$(NULL)
	
