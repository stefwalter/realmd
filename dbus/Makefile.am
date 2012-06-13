include $(top_srcdir)/Makefile.decl

noinst_LIBRARIES = \
	librealm-dbus.a

dbusservicedir = $(DBUS_POLICY_DIR)
dbusservice_DATA = \
	org.freedesktop.realmd.conf

BUILT_SOURCES = \
	realm-dbus-generated.c realm-dbus-generated.h

librealm_dbus_a_SOURCES = \
	realm-dbus-constants.h \
	$(NULL)

nodist_librealm_dbus_a_SOURCES = \
	$(BUILT_SOURCES) \
	$(NULL)

librealm_dbus_a_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(NULL)

DBUS_XML_DEFINITIONS = \
	org.freedesktop.realmd.xml

realm-dbus-generated.c: org.freedesktop.realmd.xml
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.realmd. \
		--generate-c-code realm-dbus-generated --c-namespace RealmDbus $<
realm-dbus-generated.h: realm-dbus-generated.c

# ------------------------------------------------------------------

EXTRA_DIST = \
	$(DBUS_XML_DEFINITIONS) \
	$(dbusservice_DATA) \
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	*.gcno \
	*.gcda \
	$(NULL)

DISTCLEANFILES = \
	$(NULL)
