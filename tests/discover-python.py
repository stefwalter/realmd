#!/usr/bin/python

import dbus
import getopt
import gobject
import sys

from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

def discover_realm(string, verbose, cancel_after):
	loop = gobject.MainLoop()
	operation_id = "discover-python"

	bus = dbus.SystemBus()
	proxy = bus.get_object('org.freedesktop.realmd',
	                       '/org/freedesktop/realmd')
	provider = dbus.Interface(proxy, 'org.freedesktop.realmd.Provider')
	service = dbus.Interface(proxy, 'org.freedesktop.realmd.Service')

	def on_timeout_cancel():
		service.Cancel(operation_id)
	if cancel_after is not None:
		gobject.timeout_add_seconds(cancel_after, on_timeout_cancel)

	def on_diagnostic_signal(data, operation):
		sys.stderr.write(data)
	if verbose:
		service.connect_to_signal("Diagnostics", on_diagnostic_signal)

	def on_discover_realm(relevance, realms):
		if not realms:
			print >> sys.stderr, "discover-python: nothing discovered"
			sys.exit(1)
		for object_path in realms:
			props = dbus.Interface (bus.get_object ('org.freedesktop.realmd', object_path),
			                        'org.freedesktop.DBus.Properties')
			print props.Get('org.freedesktop.realmd.Realm', 'Name')
		sys.exit(0)

	def on_discover_error(exc):
		print >> sys.stderr, "discover-python: %s" % str(exc)
		sys.exit(1)

	provider.Discover(string, {"operation": operation_id },
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
		opts, args = getopt.getopt(sys.argv[1:], "v", ["verbose", "cancel="])
	except getopt.GetoptError, err:
		print str(err)
		usage()
		sys.exit(2)

	cancel_after = None
	verbose = False
	for o, a in opts:
		if o in ("-h", "--help"):
			usage()
		elif o in ("-v", "--verbose"):
			verbose = True
		elif o in ("-c", "--cancel"):
			cancel_after = int(a)
		else:
			assert False, "unhandled option"

	if len(args) != 1:
		usage()

	discover_realm(args[0], verbose, cancel_after)
