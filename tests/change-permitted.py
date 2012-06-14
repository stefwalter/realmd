#!/usr/bin/python

import dbus
import getopt
import getpass
import gobject
import os
import sys

from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

def change_permitted(string, add_or_remove, logins):
	loop = gobject.MainLoop()

	bus = dbus.SystemBus()
	proxy = bus.get_object('org.freedesktop.realmd',
	                       '/org/freedesktop/realmd')
	provider = dbus.Interface(proxy, 'org.freedesktop.realmd.Provider')

	# Discover the realm
	(relevance, realms) = provider.Discover(string, "unused-operation-id", timeout=300)
	if not realms:
		print >> sys.stderr, "change-permitted.py: nothing discovered"
		sys.exit(1)

	(bus_name, object_path, interface_name) = realms[0]
	proxy = dbus.Interface (bus.get_object (bus_name, object_path), interface_name)
	realm = dbus.Interface(proxy, 'org.freedesktop.realmd.Kerberos')
	props = dbus.Interface(proxy, 'org.freedesktop.DBus.Properties')

	if add_or_remove:
		add = logins
		remove = []
	else:
		add = []
		remove = logins

	print "Previous: " + " ".join(props.Get(interface_name, 'PermittedLogins'))

	realm.ChangePermittedLogins(add, remove, "unused-operation-id")

	print "Changed: " + " ".join(props.Get(interface_name, 'PermittedLogins'))

def usage():
	print >> sys.stderr, "usage: change-permitted.py -a realm logins ..."
	print >> sys.stderr, "       change-permitted.py -r realm logins ..."
	sys.exit(2)

if __name__ == '__main__':
	try:
		opts, args = getopt.getopt(sys.argv[1:], "ahr", ["add", "help", "remove"])
	except getopt.GetoptError, err:
		print str(err)
		usage()
		sys.exit(2)

	add = False
	remove = False
	for o, a in opts:
		if o in ("-h", "--help"):
			usage()
		elif o in ("-a", "--add"):
			add = True
			remove = False
		elif o in ("-r", "--remove"):
			add = False
			remove = True
		else:
			assert False, "unhandled option"

	if len(args) < 2:
		usage()
	if not add and not remove:
		usage()

	change_permitted(args[0], add, args[1:])
