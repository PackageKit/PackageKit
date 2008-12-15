SUBDIRS = src

WORKDIR := $(shell pwd)

rpm: dist
	mkdir -p $(WORKDIR)/rpm/SOURCES $(WORKDIR)/rpm/BUILD \
	$(WORKDIR)/rpm/RPMS $(WORKDIR)/rpm/SRPMS; \
	cp $(PACKAGE)-$(VERSION)*tar.gz $(WORKDIR)/rpm/SOURCES; \
	rpmbuild --define "_sourcedir $(WORKDIR)/rpm/SOURCES" \
	--define "_builddir $(WORKDIR)/rpm/BUILD" \
	--define "_srcrpmdir $(WORKDIR)/rpm/SRPMS" \
	--define "_rpmdir $(WORKDIR)/rpm/RPMS" -ba ruck.spec && \
	cp $(WORKDIR)/rpm/RPMS/noarch/$(PACKAGE)-$(VERSION)* .
