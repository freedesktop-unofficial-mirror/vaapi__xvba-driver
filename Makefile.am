AUTOMAKE_OPTIONS = foreign

SUBDIRS = debian.upstream src

DOCS = \
	AUTHORS \
	COPYING \
	NEWS \
	README

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = \
	aclocal.m4 compile config.guess config.sub \
	configure depcomp install-sh ltmain.sh     \
	Makefile.in missing

DEB_BUILDDIR = debian.build

deb:
	@[ -d debian ] || ln -s debian.upstream debian
	dpkg-buildpackage -rfakeroot -uc -us

deb.upstream: dist
	-mkdir -p $(DEB_BUILDDIR)
	cd $(DEB_BUILDDIR)				&& \
	rm -rf $(PACKAGE)-$(VERSION)			&& \
	tar zxvf ../$(PACKAGE)-$(VERSION).tar.gz	&& \
	cd $(PACKAGE)-$(VERSION)			&& \
	$(MAKE) deb -f Makefile.am

bindistdir = $(distdir).$(TARGET_ARCH)

bindist: bindist-gzip

bindist_conf_flags  =
bindist_conf_flags += --enable-debug
bindist_conf_flags += --enable-tracer

bindistdir: dist-gzip
	-rm -rf bindist-build; mkdir bindist-build
	-cd bindist-build && tar zxf ../$(distdir).tar.gz
	-cd bindist-build/$(distdir) && \
	./configure --prefix=/usr $(bindist_conf_flags) && \
	$(MAKE)

	-rm -rf $(bindistdir); mkdir $(bindistdir)
	$(MAKE) install-strip \
		DESTDIR=$(PWD)/$(bindistdir) \
		-C bindist-build/$(distdir)
	find $(bindistdir)/ -name "*.la" -exec rm -f {} \;
	-rm -rf bindist-build

	cp $(DOCS) $(bindistdir)/

bindist-gzip: bindistdir
	tar zcvf $(bindistdir).tar.gz $(bindistdir)
	rm -rf $(bindistdir)
