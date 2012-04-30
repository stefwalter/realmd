include $(top_srcdir)/Makefile.decl

service_PROGRAMS = \
	realmd

REALMD_CONFIGS = \
	realmd-defaults.conf \
	realmd-redhat.conf

servicedir = $(libdir)/realmd
service_DATA = \
	ad-provider-smb.conf \
	realmd-defaults.conf

dbusservicedir = $(DBUS_POLICY_DIR)
dbusservice_DATA = \
	org.freedesktop.realmd.ActiveDirectory.conf

polkit_policydir = $(POLKIT_ACTION_DIR)
polkit_policy_in_files = org.freedesktop.realmd.policy.in
polkit_policy_DATA = $(polkit_policy_in_files:.policy.in=.policy)
@INTLTOOL_POLICY_RULE@

BUILT_SOURCES = \
	realm-dbus-generated.c realm-dbus-generated.h \
	realm-dbus-systemd.c realm-dbus-systemd.h

realmd_SOURCES = \
	realm-ad-discover.c realm-ad-discover.h \
	realm-ad-enroll.c realm-ad-enroll.h \
	realm-ad-provider.c realm-ad-provider.h \
	realm-command.c realm-command.h \
	realm-daemon.c realm-daemon.h \
	realm-debug.c realm-debug.h \
	realm-diagnostics.c realm-diagnostics.h \
	realm-discovery.c realm-discovery.h \
	realm-errors.c realm-errors.h \
	realm-packages.c realm-packages.h \
	realm-kerberos-provider.c realm-kerberos-provider.h \
	realm-samba-config.c realm-samba-config.h \
	realm-service.c realm-service.h \
	realm-winbind.c realm-winbind.h \
	$(BUILT_SOURCES) \
	$(NULL)

realmd_CFLAGS = \
	-DSERVICE_DIR=\""$(servicedir)"\" \
	-DSYSCONF_DIR=\""$(sysconfdir)"\" \
	$(PACKAGEKIT_CFLAGS) \
	$(POLKIT_CFLAGS) \
	$(NULL)

realmd_LDADD = \
	$(PACKAGEKIT_LIBS) \
	$(POLKIT_LIBS) \
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

# Install and uninstall the config for this distro
install-data-local:
	$(INSTALL_DATA) $(srcdir)/realmd-$(DISTRO).conf $(servicedir)/realmd-distro.conf
uninstall-local:
	rm -f $(servicedir)/realmd-distro.conf

# ------------------------------------------------------------------

EXTRA_DIST = \
	org.freedesktop.realmd.xml \
	$(REALMD_CONFIGS) \
	$(dbusservice_DATA) \
	$(service_DATA) \
	$(NULL)

CLEANFILES = \
	$(NULL)

DISTCLEANFILES = \
	$(NULL)
