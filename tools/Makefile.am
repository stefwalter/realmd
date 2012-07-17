include $(top_srcdir)/Makefile.decl

bin_PROGRAMS = \
	realm

realm_SOURCES = \
	realm.c \
	realm-enroll.c \
	realm-discover.c \
	realm-logins.c \
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
