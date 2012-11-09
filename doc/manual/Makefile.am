abs_top_builddir = @abs_top_builddir@

NULL =

# We require automake 1.6 at least.
AUTOMAKE_OPTIONS = 1.6

# The name of the module, e.g. 'glib'.
DOC_MODULE=realmd

# The top-level SGML file. You can change this if you want to.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting the functions and macros.
# e.g. DOC_SOURCE_DIR=../../../gtk
DOC_SOURCE_DIR=.

# Extra options to supply to gtkdoc-mkdb.
# e.g. MKDB_OPTIONS=--sgml-mode --output-format=xml
MKDB_OPTIONS=--sgml-mode --output-format=xml

DBUS_INTERFACE = $(top_srcdir)/dbus/org.freedesktop.realmd.xml
DBUS_ESCAPED = realmd-org.freedesktop.realmd.xml

DBUS_GENERATED = \
	realmd-org.freedesktop.realmd.Kerberos.xml \
	realmd-org.freedesktop.realmd.KerberosMembership.xml \
	realmd-org.freedesktop.realmd.Provider.xml \
	realmd-org.freedesktop.realmd.Realm.xml \
	realmd-org.freedesktop.realmd.Service.xml

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files= \
	$(DBUS_GENERATED) \
	$(DBUS_ESCAPED) \
	html.css \
	realm.xml \
	realmd-guide-active-directory.xml \
	realmd-guide-configuring.xml \
	realmd-guide-freeipa.xml \
	realmd-guide-kerberos.xml \
	$(NULL)

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files=

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

XSLTPROC_FLAGS = \
	--nonet \
	--stringparam man.output.quietly 1 \
	--stringparam funcsynopsis.style ansi \
	--stringparam man.th.extra1.suppress 1 \
	--stringparam man.authors.section.enabled 0 \
	--stringparam man.copyright.section.enabled 0

.xml.8:
	$(AM_V_GEN) $(XSLTPROC) $(XSLTPROC_FLAGS) http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

man_MANS = \
	realm.8

xml_files = $(man_MANS:.8=.xml)

EXTRA_DIST += \
	$(DBUS_GENERATED) \
	$(DBUS_ESCAPED) \
	$(xml_files) \
	escape-xml-to-text.xsl \
	html.css

CLEANFILES += \
	$(DBUS_GENERATED) \
	$(DBUS_ESCAPED) \
	$(man_MANS) \
	style.stamp

noinst_DATA = style.stamp

style.stamp: html.css html-build.stamp
	@cp $(srcdir)/html.css $(builddir)/html/html.css
	@sed -i 's/href="style.css"/href="html.css"/' $(builddir)/html/*.html
	@touch $(builddir)/style.stamp

$(DBUS_GENERATED): $(DBUS_INTERFACE)
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.realmd. \
		--generate-docbook realmd $<

$(DBUS_ESCAPED): $(DBUS_INTERFACE) $(srcdir)/escape-xml-to-text.xsl
	$(AM_V_GEN) $(XSLTPROC) --nonet --novalid --output $(DBUS_ESCAPED) \
		$(srcdir)/escape-xml-to-text.xsl $(DBUS_INTERFACE)
	@sed -i '/^[	 ]*$$/d' $(DBUS_ESCAPED)

upload: all
	rsync -Hvax html/./ anarchy.freedesktop.org:/srv/www.freedesktop.org/www/software/realmd/docs/./
