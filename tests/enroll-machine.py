#!/usr/bin/python

import dbus
import getopt
import getpass
import gobject
import os
import sys

from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

def do_with_credential_cache (realm, realm_name, principal, enroll):
	ccache = "/tmp/my-strange-ccache"
	if os.path.exists(ccache):
		os.unlink(ccache)
	os.environ["KRB5CCNAME"] = "FILE:%s" % ccache

	print "kinit %s" % principal
	ret = os.system("kinit %s" % principal)
	if ret != 0:
		sys.exit(1)

	kerberos_cache = open(ccache, 'rb').read()

	def on_enroll_machine():
		action = enroll and "Enrolled in" or "Unenrolled from"
		print >> sys.stderr, "%s domain: %s" % (action, realm_name)
		sys.exit(0)

	def on_enroll_error(exc):
		print >> sys.stderr, "enroll-machine.py: %s" % str(exc)
		sys.exit(1)

	if enroll:
		realm.EnrollWithCredentialCache(kerberos_cache, { }, "unused-operation-id",
		                                reply_handler=on_enroll_machine,
		                                error_handler=on_enroll_error,
		                                timeout=300)
	else:
		realm.UnenrollWithCredentialCache(kerberos_cache, { }, "unused-operation-id",
		                                  reply_handler=on_enroll_machine,
		                                  error_handler=on_enroll_error,
		                                  timeout=300)

def do_with_password (realm, realm_name, principal, enroll):
	password = getpass.getpass("Password for %s: " % principal)
	if not password:
		sys.exit(1)

	def on_enroll_machine():
		action = enroll and "Enrolled in" or "Unenrolled from"
		print >> sys.stderr, "%s domain: %s" % (action, realm_name)
		sys.exit(0)

	def on_enroll_error(exc):
		print >> sys.stderr, "enroll-machine.py: %s" % str(exc)
		sys.exit(1)

	if enroll:
		realm.EnrollWithPassword(principal, password, { }, "unused-operation-id",
		                         reply_handler=on_enroll_machine,
		                         error_handler=on_enroll_error,
		                         timeout=300)
	else:
		realm.UnenrollWithPassword(principal, password, { }, "unused-operation-id",
		                           reply_handler=on_enroll_machine,
		                           error_handler=on_enroll_error,
		                           timeout=300)


def enroll_machine(string, user, enroll, verbose, lazy):
	loop = gobject.MainLoop()

	bus = dbus.SystemBus()
	proxy = bus.get_object('org.freedesktop.realmd',
	                       '/org/freedesktop/realmd')
	provider = dbus.Interface(proxy, 'org.freedesktop.realmd.Provider')

	# Discover the realm
	(relevance, realms) = provider.Discover(string, "unused-operation-id", timeout=300)
	if not realms:
		print >> sys.stderr, "enroll-machine.py: nothing discovered"
		sys.exit(1)

	(bus_name, object_path, interface_name) = realms[0]
	realm = dbus.Interface (bus.get_object (bus_name, object_path), interface_name)

	props = dbus.Interface (realm, 'org.freedesktop.DBus.Properties')
	realm_name = props.Get(interface_name, 'Name')

	def on_diagnostic_signal(data, operation_id):
		sys.stderr.write(data)
	if verbose:
		diags = dbus.Interface (realm, 'org.freedesktop.realmd.Diagnostics')
		diags.connect_to_signal("Diagnostics", on_diagnostic_signal)

	if not user:
		user = raw_input("User: ")
	if not user:
		sys.exit(0)
	if "@" not in user:
		user = "%s@%s" % (user, realm_name)

	if lazy:
		do_with_password(realm, realm_name, user, enroll)
	else:
		do_with_credential_cache(realm, realm_name, user, enroll)

	loop.run()
	assert False, "not reached"

def usage():
	print >> sys.stderr, "usage: enroll-machine.py [-lv] [-U username] realm"
	print >> sys.stderr, "       enroll-machine.py -u [-lv] [-U username] realm"
	sys.exit(2)

if __name__ == '__main__':
	try:
		opts, args = getopt.getopt(sys.argv[1:], "luU:v", ["lazy", "user=", "unenroll", "verbose"])
	except getopt.GetoptError, err:
		print str(err)
		usage()
		sys.exit(2)

	lazy = False
	user = None
	enroll = True
	verbose = False
	for o, a in opts:
		if o in ("-h", "--help"):
			usage()
		elif o in ("-l", "--lazy"):
			lazy = True
		elif o in ("-u", "--unenroll"):
			enroll = False
		elif o in ("-U", "--user"):
			user = a
		elif o in ("-v", "--verbose"):
			verbose = True
		else:
			assert False, "unhandled option"

	if len(args) != 1:
		usage()

	enroll_machine(args[0], user, enroll, verbose, lazy)
