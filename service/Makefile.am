include $(top_srcdir)/Makefile.decl

servicedir = $(libdir)/realmd

service_PROGRAMS = \
	realmd

service_DATA = \
	ad-provider-smb.conf

BUILT_SOURCES = \
	realm-dbus-generated.c realm-dbus-generated.h

realmd_SOURCES = \
	realm-ad-discover.c realm-ad-discover.h \
	realm-ad-enroll.c realm-ad-enroll.h \
	realm-ad-provider.c realm-ad-provider.h \
	realm-ad-sssd.c realm-ad-sssd.h \
	realm-command.c realm-command.h \
	realm-debug.c realm-debug.h \
	realm-diagnostics.c realm-diagnostics.h \
	realm-discovery.c realm-discovery.h \
	realm-errors.c realm-errors.h \
	realm-packages.c realm-packages.h \
	realm-kerberos-provider.c realm-kerberos-provider.h \
	realm-service.c realm-service.h \
	$(BUILT_SOURCES) \
	$(NULL)

realmd_CFLAGS = \
	-DSERVICE_DIR=\""$(servicedir)"\" \
	$(PACKAGEKIT_CFLAGS) \
	$(NULL)

realmd_LDADD = \
	$(PACKAGEKIT_LIBS) \
	$(NULL)

DBUS_XML_DEFINITIONS = \
	org.freedesktop.realmd.xml

realm-dbus-generated.c: $(DBUS_XML_DEFINITIONS) Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.realmd. \
		--generate-c-code realm-dbus-generated --c-namespace RealmDbus \
		$(DBUS_XML_DEFINITIONS)
realm-dbus-generated.h: realm-dbus-generated.c

# ------------------------------------------------------------------

EXTRA_DIST = \
	org.freedesktop.realmd.xml \
	$(NULL)

CLEANFILES = \
	$(NULL)

DISTCLEANFILES = \
	$(NULL)
