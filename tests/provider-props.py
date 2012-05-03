#!/usr/bin/python

import dbus
import getopt
import os
import sys

def provider_props():
	bus = dbus.SystemBus()
	proxy = bus.get_object('org.freedesktop.realmd',
	                       '/org/freedesktop/realmd')
	properties = dbus.Interface(proxy, 'org.freedesktop.DBus.Properties')

	props = properties.GetAll('org.freedesktop.realmd.Provider')

	for (item, value) in props.items():
		if isinstance(value, dbus.Array):
			value = list(["%s" % (v, ) for v in value])
		print "\t%s = %s" % (item, value)

def usage():
	print >> sys.stderr, "usage: provider-props.py"
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

	provider_props()
