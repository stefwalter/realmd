## Process this file with automake to produce Makefile.in
include $(top_srcdir)/Makefile.decl

ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}

SUBDIRS = build dbus service tools tests doc po

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-debug=yes \
	--disable-coverage \
	--enable-strict \
	--disable-silent-rules \
	--enable-gtk-doc

dist-hook:
	@if test -d "$(srcdir)/.git"; \
	then \
		echo Creating ChangeLog && \
		( cd "$(top_srcdir)" && \
		  echo '# Generate automatically. Do not edit.'; echo; \
		  $(top_srcdir)/missing --run git log --stat --date=short ) > ChangeLog.tmp \
		&& mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
		|| ( rm -f ChangeLog.tmp ; \
		     echo Failed to generate ChangeLog >&2 ); \
	else \
		echo A git clone is required to generate a ChangeLog >&2; \
	fi

check-memory:
	@for subdir in $(SUBDIRS); do \
		test -d $(builddir)/$$subdir/tests && \
			make -C $(builddir)/$$subdir/tests check-memory; \
	done

upload-doc: all
	rsync -Hvax doc/html/./ anarchy.freedesktop.org:/srv/www.freedesktop.org/www/software/realmd/docs/./

upload-release: $(DIST_ARCHIVES)
	gpg --detach-sign --local-user 'stef@thewalter.net' $(DIST_ARCHIVES)
	scp $(DIST_ARCHIVES) $(DIST_ARCHIVES).sig anarchy.freedesktop.org:/srv/www.freedesktop.org/www/software/realmd/releases
