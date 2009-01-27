all: packagekit-sharp.dll

generated/Client.cs: packagekit-api.xml $(CUSTOMS)
	gapi2-codegen --outdir=generated --customdir=$(srcdir) --generate packagekit-api.xml

packagekit-sharp.dll: $(srcdir)/generated/Client.cs $(SOURCES) $(CUSTOMS)
	$(MCS) -unsafe $(GLIBSHARP_LIBS) -target:library -out:packagekit-sharp.dll $(srcdir)/generated/*.cs $(SOURCES)

packagekit-api.xml: $(srcdir)/packagekit-api.raw $(srcdir)/PackageKit.metadata
	cp $(srcdir)/packagekit-api.raw packagekit-api.xml
	chmod +w $(builddir)/packagekit-api.xml
	gapi2-fixup --api=packagekit-api.xml --metadata=$(srcdir)/PackageKit.metadata

api:
	gapi2-parser packagekit-sharp-sources.xml


CUSTOMS =				\
	Client.custom			\
	PackageList.custom

SOURCES =

CLEANFILES =			\
	packagekit-sharp.dll		\
	*.mdb

EXTRA_DIST =				\
	packagekit-sharp-sources.xml	\
	$(SOURCES)			\
	$(CUSTOMS)			\
	packagekit-api.raw			\
	packagekit-api.xml			\
	$(srcdir)/generated/*.cs	\
	PackageKit.metadata

DIST_SUBDIRS =				\
	samples
