include $(top_srcdir)/Makefile.decl

service_PROGRAMS = \
	realmd

REALMD_CONFIGS = \
	realmd-defaults.conf \
	realmd-redhat.conf

servicedir = $(SERVICE_DIR)
service_DATA = \
	net-ads-smb.conf \
	realmd-defaults.conf

providerdir = $(SERVICE_DIR)/provider.d
provider_DATA = \
	org.freedesktop.realmd.Samba.provider

polkit_policydir = $(POLKIT_ACTION_DIR)
polkit_policy_in_files = org.freedesktop.realmd.policy.in
polkit_policy_DATA = $(polkit_policy_in_files:.policy.in=.policy)
@INTLTOOL_POLICY_RULE@

realmd_SOURCES = \
	realm-ad-discover.c realm-ad-discover.h \
	realm-command.c realm-command.h \
	realm-daemon.c realm-daemon.h \
	realm-debug.c realm-debug.h \
	realm-diagnostics.c realm-diagnostics.h \
	realm-discovery.c realm-discovery.h \
	realm-errors.c realm-errors.h \
	realm-ini-config.c realm-ini-config.h \
	realm-packages.c realm-packages.h \
	realm-platform.c realm-platform.h \
	realm-kerberos-provider.c realm-kerberos-provider.h \
	realm-samba-config.c realm-samba-config.h \
	realm-samba-enroll.c realm-samba-enroll.h \
	realm-samba-provider.c realm-samba-provider.h \
	realm-samba-winbind.c realm-samba-winbind.h \
	realm-service.c realm-service.h \
	realm-system.c realm-system.h \
	$(NULL)

realmd_CFLAGS = \
	-I$(top_srcdir)/dbus \
	-DSERVICE_DIR="\"$(servicedir)\"" \
	-DSYSCONF_DIR="\"$(sysconfdir)\"" \
	-DPROVIDER_DIR="\"$(providerdir)\"" \
	$(PACKAGEKIT_CFLAGS) \
	$(POLKIT_CFLAGS) \
	$(NULL)

realmd_LDADD = \
	$(top_builddir)/dbus/librealm-dbus.a \
	$(PACKAGEKIT_LIBS) \
	$(POLKIT_LIBS) \
	$(NULL)

# Install and uninstall the config for this distro
install-data-local:
	$(INSTALL_PROGRAM) -d $(servicedir)
	$(INSTALL_DATA) $(srcdir)/realmd-$(DISTRO).conf $(servicedir)/realmd-distro.conf
uninstall-local:
	rm -f $(servicedir)/realmd-distro.conf

# ------------------------------------------------------------------

EXTRA_DIST = \
	$(polkit_policy_in_files) \
	$(REALMD_CONFIGS) \
	$(service_DATA) \
	$(provider_DATA) \
	$(NULL)

CLEANFILES = \
	$(polkit_policy_DATA) \
	*.gcno \
	*.gcda \
	$(NULL)

DISTCLEANFILES = \
	$(NULL)
