#!/usr/bin/python

import dbus
import getopt
import os
import sys

def service_list():
	bus = dbus.SystemBus()
	proxy = bus.get_object('org.freedesktop.realmd',
	                       '/org/freedesktop/realmd')
	properties = dbus.Interface(proxy, 'org.freedesktop.DBus.Properties')

	props = properties.GetAll('org.freedesktop.realmd.Service')

	providers = props["Providers"]
	for (name, interface, path) in providers:
		print "%s\n\ttype: %s\n\tpath: %s" % (name, interface, path)

def usage():
	print >> sys.stderr, "usage: service-list.py"
	sys.exit(2)

if __name__ == '__main__':
	try:
		opts, args = getopt.getopt(sys.argv[1:], "", [])
	except getopt.GetoptError, err:
		print str(err)
		usage()
		sys.exit(2)

	for o, a in opts:
		assert False, "unhandled option"

	if len(args) != 0:
		usage()

	service_list()
