#!/usr/bin/python

import dbus
import getopt
import os
import sys
import time

def usage():
	print >> sys.stderr, "usage: release-daemon.py"
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

	bus = dbus.SystemBus()
	proxy = bus.get_object('org.freedesktop.realmd',
	                       '/org/freedesktop/realmd')
	daemon = dbus.Interface(proxy, 'org.freedesktop.realmd.Daemon')
	properties = dbus.Interface(proxy, 'org.freedesktop.DBus.Properties')

	# A call to the provider
	props = properties.GetAll('org.freedesktop.realmd.Provider')

	print "releasing daemon"
	daemon.ReleaseDaemon()

	print "sleeping forever"
	time.sleep(100000000)

