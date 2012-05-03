include $(top_srcdir)/Makefile.decl

noinst_LIBRARIES = \
	librealm-dbus.a

dbusservicedir = $(DBUS_POLICY_DIR)
dbusservice_DATA = \
	org.freedesktop.realmd.conf

BUILT_SOURCES = \
	realm-dbus-generated.c realm-dbus-generated.h \
	realm-dbus-systemd.c realm-dbus-systemd.h

librealm_dbus_a_SOURCES = \
	realm-dbus-constants.h \
	$(BUILT_SOURCES) \
	$(NULL)

DBUS_XML_DEFINITIONS = \
	org.freedesktop.realmd.xml \
	org.freedesktop.systemd1.xml

realm-dbus-generated.c: org.freedesktop.realmd.xml
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.realmd. \
		--generate-c-code realm-dbus-generated --c-namespace RealmDbus $<
realm-dbus-generated.h: realm-dbus-generated.c

realm-dbus-systemd.c: org.freedesktop.systemd1.xml
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.systemd1. \
		--generate-c-code realm-dbus-systemd --c-namespace RealmDbusSystemd $<
realm-dbus-systemd.h: realm-dbus-systemd.c

# ------------------------------------------------------------------

EXTRA_DIST = \
	$(DBUS_XML_DEFINITIONS) \
	$(dbusservice_DATA) \
	$(NULL)

CLEANFILES = \
	*.gcno \
	*.gcda \
	$(NULL)

DISTCLEANFILES = \
	$(NULL)
