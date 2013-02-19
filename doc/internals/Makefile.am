
NULL =

DOCBOOK_FILE = realmd-internals.xml

DOCBOOK_PARAMS = docbook-params.xsl

STATIC_FILES = \
	$(NULL)

CONTENT_INCLUDES = \
	$(NULL)

SVG_IMAGES = \
	discover-method.svg \
	join-method.svg \
	$(NULL)

EXTRA_DIST = \
	$(DOCBOOK_FILE) \
	$(DOCBOOK_PARAMS) \
	$(CONTENT_INCLUDES) \
	$(SVG_IMAGES) \
	$(XSLT_FILES) \
	static \
	$(NULL)

CLEANFILES = \
	$(NULL)

html/realmd-internals.html: $(DOCBOOK_FILE) $(CONTENT_INCLUDES) $(STATIC_FILES)
	$(AM_V_GEN) mkdir -p html && cp $(srcdir)/static/* html/
	$(AM_V_GEN) $(XMLTO) html-nochunks -m $(DOCBOOK_PARAMS) -o html \
		--searchpath $(builddir):$(srcdir) $(srcdir)/$(DOCBOOK_FILE)

all-local: html/realmd-internals.html

clean-local:
	@rm -rf *.tmp html

render-images:
	for i in $(SVG_IMAGES:.svg=); do \
		inkscape --export-png=static/$$i.png $$i.svg; \
	done

upload: all
	rsync -Hvax html/./ anarchy.freedesktop.org:/srv/www.freedesktop.org/www/software/realmd/internals/./
