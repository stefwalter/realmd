#!/usr/bin/python

import dbus
import getopt
import gobject
import os
import sys

from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

def enroll_machine(realm, user, verbose):
	loop = gobject.MainLoop()

	bus = dbus.SystemBus()
	proxy = bus.get_object('org.freedesktop.realmd.ActiveDirectory',
	                       '/org/freedesktop/realmd/ActiveDirectory')
	kerberos = dbus.Interface(proxy, 'org.freedesktop.realmd.Kerberos')
	provider = dbus.Interface(proxy, 'org.freedesktop.realmd.Provider')

	def on_diagnostic_signal(data):
		sys.stderr.write(data)
	if verbose:
		provider.connect_to_signal("Diagnostics", on_diagnostic_signal)

	if not user:
		user = raw_input("User: ")
	if not user:
		sys.exit(0)
	if "@" not in user:
		user = "%s@%s" % (user, realm)

	ccache = "/tmp/my-strange-ccache"
	if os.path.exists(ccache):
		os.unlink(ccache)
	os.environ["KRB5CCNAME"] = "FILE:%s" % ccache

	ret = os.system("kinit %s" % user)
	if ret != 0:
		sys.exit(1)

	kerberos_cache = open(ccache, 'rb').read()

	def on_enroll_machine():
		print >> sys.stderr, "Enrolled in domain: %s" % realm
		sys.exit(0)

	def on_enroll_error(exc):
		print >> sys.stderr, "enroll-machine: %s" % str(exc)
		sys.exit(1)

	kerberos.EnrollMachineWithKerberosCache(realm, kerberos_cache,
	                                        reply_handler=on_enroll_machine,
	                                        error_handler=on_enroll_error,
	                                        timeout=300)

	loop.run()
	assert False, "not reached"

def usage():
	print >> sys.stderr, "usage: enroll-machine.py [-v] [-U username] realm"
	sys.exit(2)

if __name__ == '__main__':
	try:
		opts, args = getopt.getopt(sys.argv[1:], "U:v", ["verbose", "user="])
	except getopt.GetoptError, err:
		print str(err)
		usage()
		sys.exit(2)

	user = None
	verbose = False
	for o, a in opts:
		if o in ("-h", "--help"):
			usage()
		elif o in ("-U", "--user"):
			user = a
		elif o in ("-v", "--verbose"):
			verbose = True
		else:
			assert False, "unhandled option"

	if len(args) != 1:
		usage()

	enroll_machine(args[0], user, verbose)
