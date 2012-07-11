include $(top_srcdir)/Makefile.decl

private_PROGRAMS = \
	realmd

REALMD_CONFIGS = \
	realmd-defaults.conf \
	realmd-redhat.conf

private_DATA = \
	net-ads-smb.conf \
	realmd-defaults.conf

polkit_policydir = $(POLKIT_ACTION_DIR)
polkit_policy_in_files = org.freedesktop.realmd.policy.in
polkit_policy_DATA = $(polkit_policy_in_files:.policy.in=.policy)
@INTLTOOL_POLICY_RULE@

realmd_SOURCES = \
	realm-ad-discover.c realm-ad-discover.h \
	realm-all-provider.c realm-all-provider.h \
	realm-command.c realm-command.h \
	realm-daemon.c realm-daemon.h \
	realm-debug.c realm-debug.h \
	realm-diagnostics.c realm-diagnostics.h \
	realm-discovery.c realm-discovery.h \
	realm-errors.c realm-errors.h \
	realm-ini-config.c realm-ini-config.h \
	realm-ipa-discover.c realm-ipa-discover.h \
	realm-kerberos.c realm-kerberos.h \
	realm-login-name.c realm-login-name.h \
	realm-network.c realm-network.h \
	realm-packages.c realm-packages.h \
	realm-provider.c realm-provider.h \
	realm-samba.c realm-samba.h \
	realm-samba-config.c realm-samba-config.h \
	realm-samba-enroll.c realm-samba-enroll.h \
	realm-samba-provider.c realm-samba-provider.h \
	realm-samba-winbind.c realm-samba-winbind.h \
	realm-service.c realm-service.h \
	realm-service-systemd.c realm-service-systemd.h \
	realm-service-upstart.c realm-service-upstart.h \
	realm-settings.c realm-settings.h \
	realm-sssd.c realm-sssd.h \
	realm-sssd-ad.c realm-sssd-ad.h \
	realm-sssd-ad-provider.c realm-sssd-ad-provider.h \
	realm-sssd-config.c realm-sssd-config.h \
	realm-sssd-ipa.c realm-sssd-ipa.h \
	realm-sssd-ipa-provider.c realm-sssd-ipa-provider.h \
	$(NULL)

realmd_CFLAGS = \
	-I$(top_srcdir)/dbus \
	-I$(top_builddir)/dbus \
	-DPRIVATE_DIR="\"$(privatedir)\"" \
	-DSYSCONF_DIR="\"$(sysconfdir)\"" \
	-DPROVIDER_DIR="\"$(privatedir)/provider.d\"" \
	$(PACKAGEKIT_CFLAGS) \
	$(POLKIT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(KRB5_CFLAGS) \
	$(NULL)

realmd_LDADD = \
	$(top_builddir)/dbus/librealm-dbus.a \
	$(PACKAGEKIT_LIBS) \
	$(POLKIT_LIBS) \
	$(GLIB_LIBS) \
	$(KRB5_LIBS) \
	$(NULL)

# Install and uninstall the config for this distro
install-data-local:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(privatedir)
	$(INSTALL_DATA) $(srcdir)/realmd-$(DISTRO).conf $(DESTDIR)$(privatedir)/realmd-distro.conf
uninstall-local:
	rm -f $(DESTDIR)$(privatedir)/realmd-distro.conf

# ------------------------------------------------------------------

EXTRA_DIST = \
	$(polkit_policy_in_files) \
	$(REALMD_CONFIGS) \
	$(private_DATA) \
	$(provider_DATA) \
	$(NULL)

CLEANFILES = \
	$(polkit_policy_DATA) \
	*.gcno \
	*.gcda \
	$(NULL)

DISTCLEANFILES = \
	$(BUILT_SOURCES) \
	$(NULL)
