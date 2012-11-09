SUBDIRS = manual

EXTRA_DIST = \
	version.xml.in \
	version.xml \
	website/Makefile.in

DISTCLEANFILES = \
	website/Makefile

if ENABLE_GTK_DOC
upload: website manual
	make -C manual upload
	make -C website upload

else
upload:
	@echo "*** gtk-doc must be installed (and --enable-gtk-doc) in order to make distcheck"
	@false
distcheck-hook:
	@echo "*** gtk-doc must be installed (and --enable-gtk-doc) in order to make distcheck"
	@false
endif
