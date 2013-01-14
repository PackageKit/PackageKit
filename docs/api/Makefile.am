## Process this file with automake to produce Makefile.in

SUBDIRS = dbus

NULL =

# We require automake 1.6 at least.
AUTOMAKE_OPTIONS = 1.6

# The name of the module, e.g. 'glib'.
DOC_MODULE=PackageKit

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting the functions and macros.
DOC_SOURCE_DIR=../../lib/packagekit-glib2

# Extra options to pass to gtkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS=

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra options to supply to gtkdoc-mktmpl
MKTMPL_OPTIONS=

# Extra options to supply to gtkdoc-fixref. Not normally needed.
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB=$(top_srcdir)/lib/packagekit-glib2/pk-*.h
CFILE_GLOB=$(top_srcdir)/lib/packagekit-glib2/pk-*.c

# Header files to ignore when scanning.
IGNORE_HFILES =						\
	config.h					\
	pk-marshal.h					\
	$(NULL)

# Images to copy into HTML directory.
HTML_IMAGES =						\
	spec/pk-structure.png				\
	spec/pk-structure.svg				\
	spec/pk-faq-error-code.png			\
	spec/pk-faq-status.png				\
	spec/pk-faq-filter.png				\
	spec/pk-transactions.svg			\
	spec/pk-transactions-set-locale.png		\
	spec/pk-transactions-repair-required.png	\
	spec/pk-transactions-failure.png		\
	spec/pk-transactions-download.png		\
	spec/pk-transactions-sig-install.png		\
	spec/pk-transactions-success.png		\
	spec/pk-transactions-auto-untrusted.png		\
	spec/pk-transactions-trusted.png

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =						\
	version.xml					\
	dbus/org.freedesktop.PackageKit.ref.xml		\
	dbus/org.freedesktop.PackageKit.Transaction.ref.xml

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
expand_content_files=

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
# Only needed if you are using gtkdoc-scangobj to dynamically query widget
# signals and properties.
AM_CPPFLAGS =						\
	$(POLKIT_CFLAGS)				\
	$(DBUS_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	-I$(top_builddir)/lib/packagekit-glib2		\
	-I$(top_srcdir)/lib/packagekit-glib2		\
	$(NULL)

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(top_builddir)/lib/packagekit-glib2/libpackagekitprivate.a	\
	$(NULL)

GTKDOC_LIBS =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)					\
	$(POLKIT_LIBS)					\
	$(NULL)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

# Other files to distribute
EXTRA_DIST += version.xml.in

CLEANFILES =						\
	*.txt						\
	*.types						\
	*.args						\
	*.hierarchy					\
	*.interfaces					\
	*.prerequisites					\
	*.signals					\
	*.stamp						\
	tmpl

DISTCLEANFILES =					\
	org.freedesktop.PackageKit.ref.xml		\
	org.freedesktop.PackageKit.Transaction.ref.xml

-include $(top_srcdir)/git.mk
