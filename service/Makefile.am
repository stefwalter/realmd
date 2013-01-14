include $(top_srcdir)/Makefile.decl

private_PROGRAMS = \
	realmd

REALMD_CONFIGS = \
	realmd-defaults.conf \
	realmd-redhat.conf \
	realmd-debian.conf \
	$(NULL)

private_DATA = \
	net-ads-smb.conf \
	realmd-defaults.conf

polkit_policydir = $(POLKIT_ACTION_DIR)
polkit_policy_in_files = org.freedesktop.realmd.policy.in
polkit_policy_DATA = $(polkit_policy_in_files:.policy.in=.policy)
@INTLTOOL_POLICY_RULE@

realmd_SOURCES = \
	realm-adcli-enroll.c realm-adcli-enroll.h \
	realm-all-provider.c realm-all-provider.h \
	realm-command.c realm-command.h \
	realm-daemon.c realm-daemon.h \
	realm-diagnostics.c realm-diagnostics.h \
	realm-discovery.c realm-discovery.h \
	realm-errors.c realm-errors.h \
	realm-example.c realm-example.h \
	realm-example-provider.c realm-example-provider.h \
	realm-ini-config.c realm-ini-config.h \
	realm-invocation.c realm-invocation.h \
	realm-ipa-discover.c realm-ipa-discover.h \
	realm-kerberos.c realm-kerberos.h \
	realm-kerberos-discover.c realm-kerberos-discover.h \
	realm-kerberos-membership.c realm-kerberos-membership.h \
	realm-kerberos-provider.c realm-kerberos-provider.h \
	realm-login-name.c realm-login-name.h \
	realm-network.c realm-network.h \
	realm-packages.c realm-packages.h \
	realm-provider.c realm-provider.h \
	realm-samba.c realm-samba.h \
	realm-samba-config.c realm-samba-config.h \
	realm-samba-enroll.c realm-samba-enroll.h \
	realm-samba-provider.c realm-samba-provider.h \
	realm-samba-util.c realm-samba-util.h \
	realm-samba-winbind.c realm-samba-winbind.h \
	realm-service.c realm-service.h \
	realm-settings.c realm-settings.h \
	realm-sssd.c realm-sssd.h \
	realm-sssd-ad.c realm-sssd-ad.h \
	realm-sssd-provider.c realm-sssd-provider.h \
	realm-sssd-config.c realm-sssd-config.h \
	realm-sssd-ipa.c realm-sssd-ipa.h \
	$(NULL)

realmd_CFLAGS = \
	-I$(top_srcdir)/dbus \
	-I$(top_builddir)/dbus \
	-DPRIVATE_DIR="\"$(privatedir)\"" \
	-DSYSCONF_DIR="\"$(sysconfdir)\"" \
	-DPROVIDER_DIR="\"$(privatedir)/provider.d\"" \
	-DLOCALEDIR=\""$(datadir)/locale"\" \
	-DSTATE_DIR="\"$(localstatedir)/realmd\"" \
	$(PACKAGEKIT_CFLAGS) \
	$(POLKIT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(KRB5_CFLAGS) \
	$(LDAP_CFLAGS) \
	$(NULL)

realmd_LDADD = \
	$(top_builddir)/dbus/librealm-dbus.a \
	$(PACKAGEKIT_LIBS) \
	$(POLKIT_LIBS) \
	$(GLIB_LIBS) \
	$(KRB5_LIBS) \
	$(LDAP_LIBS) \
	$(NULL)

# Install and uninstall the config for this distro
install-data-local:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(privatedir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(localstatedir)/realmd
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
