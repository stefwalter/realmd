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

DBUS_GENERATED = \
	xml/realm-dbus-generated-org.freedesktop.realmd.Kerberos.xml \
	xml/realm-dbus-generated-org.freedesktop.realmd.Provider.xml \
	xml/realm-dbus-generated-org.freedesktop.realmd.Service.xml

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files= $(DBUS_GENERATED)

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files=

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

EXTRA_DIST += \
	version.xml.in \
	version.xml \
	html.css

noinst_DATA = style.stamp

style.stamp: html.css html-build.stamp html.stamp
	@cp $(srcdir)/html.css $(builddir)/html/html.css
	@sed -i 's/href="style.css"/href="html.css"/' $(builddir)/html/*.html
	@touch $(builddir)/style.stamp

$(DBUS_GENERATED): $(top_srcdir)/dbus/org.freedesktop.realmd.xml
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.freedesktop.realmd. \
		--generate-docbook xml/realm-dbus-generated $<
