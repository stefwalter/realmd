include $(top_srcdir)/Makefile.decl

servicedir = $(libdir)/identity-config
service_PROGRAMS = identity-config-service

BUILT_SOURCES = \
	ic-dbus-generated.c ic-dbus-generated.h

identity_config_service_SOURCES = \
	ic-ads-provider.c ic-ads-provider.h \
	ic-debug.c ic-debug.h \
	ic-diagnostics.c ic-diagnostics.h \
	ic-errors.c ic-errors.h \
	ic-main.c \
	ic-unix-process.c ic-unix-process.h \
	$(BUILT_SOURCES) \
	$(NULL)

DBUS_XML_DEFINITIONS = \
	org.freedesktop.IdentityConfig.xml

ic-dbus-generated.c: $(DBUS_XML_DEFINITIONS) Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.IdentityConfig. \
		--generate-c-code ic-dbus-generated --c-namespace IcDbus \
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
