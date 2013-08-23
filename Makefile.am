SUBDIRS = src doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gsk-1.0.pc
EXTRA_DIST = gsk-1.0.pc.in
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

DEBARCH = @ARCH@
deb:
	test -r gsk-@VERSION@.tar.gz || $(MAKE) dist
	$(srcdir)/utils/pkgwrite --format=debian \
	  --tarball=gsk-@VERSION@.tar.gz \
	  --output=gsk-packages \
	  --pkgwriteinfo-file=pkgwriteinfo \
	  --arch=$(DEBARCH)

rpm:
	test -r gsk-@VERSION@.tar.gz || $(MAKE) dist
	$(srcdir)/utils/pkgwrite --format=redhat \
	  --tarball=gsk-@VERSION@.tar.gz \
	  --output=gsk-packages \
	  --pkgwriteinfo-file=pkgwriteinfo \
	  --arch=$(DEBARCH)

clean-packages:
	rm -f gsk-@VERSION@.tar.gz
	$(MAKE) deb
