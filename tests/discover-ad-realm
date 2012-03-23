#!/usr/bin/python

import dbus
import getopt
import gobject
import sys

from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

def usage():
	print >> sys.stderr, "usage: discover-ad-realm.py -v realm"
	sys.exit(2)

def main(argv):
	try:
		opts, args = getopt.getopt(argv[1:], "hv", ["help", "verbose"])
	except getopt.GetoptError, err:
		print >> sys.stderr, str(err)
		usage()

	verbose = False
	for o, a in opts:
		if o in ("-v", "--verbose"):
			verbose = True
		elif o in ("-h", "--help"):
			usage()
		else:
			assert False, "unhandled option"

	if len(args) != 1:
		usage()

	domain = args[0]

	loop = gobject.MainLoop()
	bus = dbus.SessionBus()
	proxy = bus.get_object('org.freedesktop.IdentityConfig.ActiveDirectory',
	                       '/org/freedesktop/IdentityConfig/ActiveDirectory')
	provider = dbus.Interface(proxy, 'org.freedesktop.IdentityConfig.Provider')

	if verbose:
		provider.connect_to_signal("Diagnostics", lambda data: sys.stderr.write(data))

	def on_discover_reply(match):
		if match == 0:
			print "%s is not an Active Directory domain" % (domain)
		else:
			print "%s is an Active Directory domain (certainty %d%%)" % (domain, match)
		loop.quit()

	def on_discover_error(error):
		print "Failed to discover: %s" % str(error)

	provider.DiscoverProvider(domain, reply_handler=on_discover_reply,
	                          error_handler=on_discover_error)

	loop.run()
	sys.exit(0)

if __name__ == "__main__":
	main(sys.argv)
