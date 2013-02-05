if ENABLE_DOC

SUBDIRS = manual internals

EXTRA_DIST = \
	version.xml.in \
	version.xml \
	website/Makefile.in

DISTCLEANFILES = \
	website/Makefile

upload:
	make -C manual upload
	make -C internals upload
	make -C website upload

else
upload:
	@echo "*** doc must be enabled  (and --enable-doc) in order to make upload"
	@false
distcheck-hook:
	@echo "*** doc must be enabled (and --enable-doc) in order to make distcheck"
	@false
endif
