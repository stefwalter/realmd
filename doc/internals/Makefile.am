
INTERNAL_DOCBOOK = doc/internals/realmd-internals.xml

INTERNAL_PARAMS = doc/internals/docbook-params.xsl

INTERNAL_STATIC = \
	$(NULL)

INTERNAL_INCLUDES = \
	$(NULL)

SVG_IMAGES = \
	doc/internals/discover-method.svg \
	doc/internals/join-method.svg \
	$(NULL)

EXTRA_DIST += \
	$(INTERNAL_DOCBOOK) \
	$(INTERNAL_PARAMS) \
	$(INTERNAL_INCLUDES) \
	$(SVG_IMAGES) \
	doc/internals/static \
	$(NULL)

CLEANFILES += \
	internals/realmd-internals.html \
	$(NULL)

internals/realmd-internals.html: $(INTERNAL_DOCBOOK) $(INTERNAL_INCLUDES) $(INTERNAL_STATIC)
	$(AM_V_GEN) mkdir -p internals && cp $(srcdir)/doc/internals/static/* internals/ && \
	$(XMLTO) html-nochunks -m $(srcdir)/$(INTERNAL_PARAMS) -o internals \
		--searchpath $(abs_builddir):$(abs_srcdir) $(srcdir)/$(INTERNAL_DOCBOOK)

render-images:
	for i in $(SVG_IMAGES:.svg=); do \
		inkscape --export-png=static/$$i.png $$i.svg; \
	done
