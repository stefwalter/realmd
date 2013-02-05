
all: html

html:
	SRCDIR=$(srcdir) BUILDDIR=$(builddir) python jinja2-build.py

clean:
	rm -rf $(builddir)/html/*

upload: all
	rsync -Hvax --exclude docs --exclude internals --exclude releases \
		html/* anarchy.freedesktop.org:/srv/www.freedesktop.org/www/software/realmd/

.PHONY: html
