#!/usr/bin/python

import dbus
import getopt
import gobject
import sys

from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

def discover_realm(string, verbose):
	loop = gobject.MainLoop()

	bus = dbus.SystemBus()
	proxy = bus.get_object('org.freedesktop.realmd',
	                       '/org/freedesktop/realmd')
	provider = dbus.Interface(proxy, 'org.freedesktop.realmd.Provider')

	def on_diagnostic_signal(data):
		sys.stderr.write(data)
	if verbose:
		provider.connect_to_signal("Diagnostics", on_diagnostic_signal)

	def on_discover_realm(relevance, realm_info, discovered):
		kerberos = dbus.Interface(proxy, 'org.freedesktop.realmd.Kerberos')
		(bus_name, object_path, interface_name) = realm_info
		props = dbus.Interface (bus.get_object (bus_name, object_path),
		                        'org.freedesktop.DBus.Properties')
		print props.Get('org.freedesktop.realmd.KerberosRealm', 'Name')
		for (item, value) in discovered.items():
			if isinstance(value, dbus.Array):
				value = list(["%s" % v for v in value])
			print "\t%s = %s" % (item, value)
		sys.exit(0)

	def on_discover_error(exc):
		print >> sys.stderr, "discover-python: %s" % str(exc)
		sys.exit(1)

	provider.Discover(string,
	                  reply_handler=on_discover_realm,
	                  error_handler=on_discover_error,
	                  timeout=300)

	loop.run()
	assert False, "not reached"

def usage():
	print >> sys.stderr, "usage: discover-python.py [-v] string"
	sys.exit(2)

if __name__ == '__main__':
	try:
		opts, args = getopt.getopt(sys.argv[1:], "v", ["verbose"])
	except getopt.GetoptError, err:
		print str(err)
		usage()
		sys.exit(2)

	verbose = False
	for o, a in opts:
		if o in ("-h", "--help"):
			usage()
		elif o in ("-v", "--verbose"):
			verbose = True
		else:
			assert False, "unhandled option"

	if len(args) != 1:
		usage()

	discover_realm(args[0], verbose)
