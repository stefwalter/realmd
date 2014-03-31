
dbusservicedir = $(DBUS_POLICY_DIR)
dbusservice_DATA = \
	dbus/org.freedesktop.realmd.conf

servicedir       = $(DBUS_SERVICES_DIR)
service_in_files = \
	dbus/org.freedesktop.realmd.service.in
service_DATA     = $(service_in_files:.service.in=.service)

noinst_LIBRARIES += \
	librealm-dbus.a

BUILT_SOURCES += \
	realm-dbus-generated.c \
	realm-dbus-generated.h \
	$(NULL)

librealm_dbus_a_SOURCES = \
	dbus/realm-dbus-constants.h \
	$(NULL)

nodist_librealm_dbus_a_SOURCES = \
	$(BUILT_SOURCES) \
	$(NULL)

librealm_dbus_a_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(NULL)

DBUS_XML_DEFINITIONS = \
	dbus/org.freedesktop.realmd.xml

DBUS_GENERATED = \
	realm-dbus-generated.c \
	realm-dbus-generated.h \
	$(NULL)

$(DBUS_GENERATED): dbus/org.freedesktop.realmd.xml
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.realmd. \
		--generate-c-code realm-dbus-generated \
		--c-namespace RealmDbus $<

systemdunit_in_files = dbus/realmd.service.in

if WITH_SYSTEMD
systemdunitdir       = $(SYSTEMD_UNIT_DIR)
systemdunit_DATA     = $(systemdunit_in_files:.service.in=.service)
endif

# ------------------------------------------------------------------

EXTRA_DIST += \
	$(DBUS_XML_DEFINITIONS) \
	$(dbusservice_DATA) \
	$(provider_DATA) \
	$(service_in_files) \
	$(systemdunit_in_files) \
	$(NULL)

CLEANFILES += \
	$(DBUS_GENERATED) \
	$(service_DATA) \
	$(systemdunit_DATA) \
	$(NULL)
