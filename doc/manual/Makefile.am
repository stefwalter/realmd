
NULL =

man8_MANS = \
	realm.8
man5_MANS = \
	realmd.conf.5

MAN_IN_FILES = \
	$(man8_MANS:.8=.xml) \
	$(man5_MANS:.5=.xml) \
	$(NULL)

DOCBOOK_FILE = realmd-docs.xml

CONTENT_INCLUDES = \
	realmd-guide-active-directory.xml \
	realmd-guide-freeipa.xml \
	realmd-guide-kerberos.xml \
	$(NULL)

STATIC_FILES = \
	static/gtk-doc.css \
	static/style.css \
	$(NULL)

DBUS_INTERFACE = $(top_srcdir)/dbus/org.freedesktop.realmd.xml
DBUS_ESCAPED = realmd-org.freedesktop.realmd.xml

DBUS_GENERATED = \
	realmd-org.freedesktop.realmd.Kerberos.xml \
	realmd-org.freedesktop.realmd.KerberosMembership.xml \
	realmd-org.freedesktop.realmd.Provider.xml \
	realmd-org.freedesktop.realmd.Realm.xml \
	realmd-org.freedesktop.realmd.Service.xml

XSLT_FILES = \
	escape-xml-to-text.xsl \
	gdbus-fix-bugs.xsl \
	gtk-doc.xsl \
	version-greater-or-equal.xsl \
	$(NULL)

EXTRA_DIST = \
	$(DOCBOOK_FILE) \
	$(CONTENT_INCLUDES) \
	$(MAN_IN_FILES) \
	$(XSLT_FILES) \
	static \
	$(NULL)

CLEANFILES = \
	$(DBUS_GENERATED) \
	$(DBUS_ESCAPED) \
	$(man8_MANS) \
	$(man5_MANS) \
	$(NULL)

.xml.8:
	$(AM_V_GEN) $(XSLTPROC) $(XSLTPROC_FLAGS) http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<
.xml.5:
	$(AM_V_GEN) $(XSLTPROC) $(XSLTPROC_FLAGS) http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

$(DBUS_GENERATED): $(DBUS_INTERFACE) gdbus-fix-bugs.xsl
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.realmd. \
		--generate-docbook realmd $<
	$(AM_V_GEN) for f in $(DBUS_GENERATED); do \
		$(XSLTPROC) --output $$f.tmp $(srcdir)/gdbus-fix-bugs.xsl $$f && mv $$f.tmp $$f; \
	done

# The above gdbus-codegen target cannot be run in parallel
.NOTPARALLEL:

$(DBUS_ESCAPED): $(DBUS_INTERFACE) $(srcdir)/escape-xml-to-text.xsl
	$(AM_V_GEN) $(XSLTPROC) --nonet --novalid --output $(DBUS_ESCAPED) \
		$(srcdir)/escape-xml-to-text.xsl $(DBUS_INTERFACE)
	@sed -i '/^[	 ]*$$/d' $(DBUS_ESCAPED)

html/index.html: $(DBUS_GENERATED) $(DOCBOOK_FILE) $(CONTENT_INCLUDES) $(MAN_IN_FILES) $(DBUS_ESCAPED) $(STATIC_FILES)
	$(AM_V_GEN) mkdir -p html && cp $(srcdir)/static/* html/
	$(AM_V_GEN) $(XMLTO) html -m $(srcdir)/gtk-doc.xsl -o html \
		--searchpath $(builddir):$(srcdir) $(srcdir)/$(DOCBOOK_FILE)

all-local: html/index.html

clean-local:
	@rm -rf *.tmp html

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(htmldir)
	$(INSTALL_DATA) $(builddir)/html/* $(DESTDIR)$(htmldir)

uninstall-local:
	rm -rf $(DESTDIR)$(htmldir)

dist-hook: html/index.html
	@mkdir $(distdir)/html
	@cp ./html/* $(distdir)/html

upload: all
	rsync -Hvax html/./ anarchy.freedesktop.org:/srv/www.freedesktop.org/www/software/realmd/docs/./
