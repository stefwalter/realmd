SUBDIRS = manual

manual:
	make -C manual all
website:
	make -C website all

if ENABLE_GTK_DOC
upload: website manual
	rsync -Hvax manual/html/./ anarchy.freedesktop.org:/srv/www.freedesktop.org/www/software/realmd/docs/./

else
upload:
	@echo "*** gtk-doc must be installed (and --enable-gtk-doc) in order to make distcheck"
	@false
distcheck-hook:
	@echo "*** gtk-doc must be installed (and --enable-gtk-doc) in order to make distcheck"
	@false
endif
