include $(top_srcdir)/Makefile.decl

dbusservicedir = $(DBUS_POLICY_DIR)
dbusservice_DATA = \
	org.freedesktop.realmd.conf

servicedir       = $(DBUS_SERVICES_DIR)
service_in_files = \
	org.freedesktop.realmd.service.in
service_DATA     = $(service_in_files:.service.in=.service)

noinst_LIBRARIES = \
	librealm-dbus.a

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

DBUS_GENERATED = \
	realm-dbus-generated.c \
	realm-dbus-generated.h \
	$(NULL)

$(DBUS_GENERATED): org.freedesktop.realmd.xml
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.realmd. \
		--generate-c-code realm-dbus-generated \
		--c-namespace RealmDbus $<

# ------------------------------------------------------------------

EXTRA_DIST = \
	$(DBUS_XML_DEFINITIONS) \
	$(dbusservice_DATA) \
	$(provider_DATA) \
	$(service_in_files) \
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(DBUS_GENERATED) \
	$(service_DATA) \
	*.gcno \
	*.gcda \
	$(NULL)

DISTCLEANFILES = \
	$(NULL)
