include $(top_srcdir)/Makefile.decl

sbin_PROGRAMS = \
	realm

realm_SOURCES = \
	realm.c realm.h \
	realm-client.h \
	realm-client.c \
	realm-discover.c \
	realm-join.c \
	realm-leave.c \
	realm-logins.c \
	$(NULL)

AM_CFLAGS = \
	-I$(top_srcdir)/dbus \
	-I$(top_builddir)/dbus \
	-DLOCALEDIR=\""$(datadir)/locale"\" \
	$(GLIB_CFLAGS) \
	$(KRB5_CFLAGS) \
	$(NULL)

LDADD = \
	$(top_builddir)/dbus/librealm-dbus.a \
	$(GLIB_LIBS) \
	$(KRB5_LIBS) \
	$(NULL)
