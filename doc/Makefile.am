if ENABLE_DOC

SUBDIRS = manual

EXTRA_DIST = \
	version.xml.in \
	version.xml \
	website/Makefile.in

DISTCLEANFILES = \
	website/Makefile

upload: website manual
	make -C manual upload
	make -C website upload

else
upload:
	@echo "*** doc must be enabled  (and --enable-doc) in order to make upload"
	@false
distcheck-hook:
	@echo "*** doc must be enabled (and --enable-doc) in order to make distcheck"
	@false
endif
