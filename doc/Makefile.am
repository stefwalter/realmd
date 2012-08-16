abs_top_builddir = @abs_top_builddir@

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
	realmd-org.freedesktop.realmd.Provider.xml \
	realmd-org.freedesktop.realmd.Service.xml

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files= $(DBUS_GENERATED) $(DBUS_ESCAPED)

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files=

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

EXTRA_DIST += \
	$(DBUS_GENERATED) \
	$(DBUS_ESCAPED) \
	escape-xml-to-text.xsl \
	version.xml.in \
	version.xml \
	html.css

CLEANFILES += \
	$(DBUS_GENERATED) \
	$(DBUS_ESCAPED) \
	style.stamp

noinst_DATA = style.stamp

style.stamp: html.css html-build.stamp html.stamp
	@cp $(srcdir)/html.css $(builddir)/html/html.css
	@sed -i 's/href="style.css"/href="html.css"/' $(builddir)/html/*.html
	@touch $(builddir)/style.stamp

$(DBUS_GENERATED): $(DBUS_INTERFACE)
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.realmd. \
		--generate-docbook realmd $<

$(DBUS_ESCAPED): $(DBUS_INTERFACE) $(srcdir)/escape-xml-to-text.xsl
	$(AM_V_GEN) xsltproc --output $(DBUS_ESCAPED) $(srcdir)/escape-xml-to-text.xsl $(DBUS_INTERFACE)
	@sed -i '/^[	 ]*$$/d' $(DBUS_ESCAPED)
