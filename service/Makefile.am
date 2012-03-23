include $(top_srcdir)/Makefile.decl

servicedir = $(libdir)/identity-config

service_PROGRAMS = \
	identity-config-service

service_DATA = \
	ad-provider-smb.conf

BUILT_SOURCES = \
	ic-dbus-generated.c ic-dbus-generated.h

identity_config_service_SOURCES = \
	ic-ad-discover.c ic-ad-discover.h \
	ic-ad-enroll.c ic-ad-enroll.h \
	ic-ad-provider.c ic-ad-provider.h \
	ic-ad-sssd.c ic-ad-sssd.h \
	ic-command.c ic-command.h \
	ic-debug.c ic-debug.h \
	ic-diagnostics.c ic-diagnostics.h \
	ic-discovery.c ic-discovery.h \
	ic-errors.c ic-errors.h \
	ic-main.c \
	ic-packages.c ic-packages.h \
	ic-kerberos-provider.c ic-kerberos-provider.h \
	ic-service.c ic-service.h \
	$(BUILT_SOURCES) \
	$(NULL)

identity_config_service_CFLAGS = \
	-DSERVICE_DIR=\""$(servicedir)"\" \
	$(PACKAGEKIT_CFLAGS) \
	$(NULL)

identity_config_service_LDADD = \
	$(PACKAGEKIT_LIBS) \
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
