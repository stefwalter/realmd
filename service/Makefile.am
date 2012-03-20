include $(top_srcdir)/Makefile.decl

servicedir = $(libdir)/identity-config
service_PROGRAMS = identity-config-service

identity_config_service_SOURCES = \
	ic-main.c

DBUS_XML_DEFINITIONS = \
	org.freedesktop.IdentityConfig.xml

ic-dbus-generated.c: $(DBUS_XML_DEFINITIONS) Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.IdentityConfig. \
		--generate-c-code ic-dbus-generated --c-namespace Ic \
		$(DBUS_XML_DEFINITIONS)
ic-dbus-generated.h: ic-dbus-generated.c

# ------------------------------------------------------------------


EXTRA_DIST = \
	org.freedesktop.IdentityConfig.xml \
	$(NULL)

CLEANFILES = \
	$(NULL)

DISTCLEANFILES = \
	$(NULL)
