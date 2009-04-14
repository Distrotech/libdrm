AM_CFLAGS = \
	-I $(top_srcdir)/shared-core \
	-I $(top_srcdir)/libdrm \
	-I $(top_srcdir)/libdrm/radeon


noinst_PROGRAMS = \
	dristat \
	drmstat

EXTRA_LTLIBRARIES = libdrmtest.la
libdrmtest_la_SOURCES = \
	drmtest.c \
	drmtest.h
libdrmtest_la_LIBADD = \
	$(top_builddir)/libdrm/libdrm.la

LDADD = libdrmtest.la $(top_builddir)/libdrm/radeon/libdrm_radeon.la

TESTS = auth \
	openclose \
	getversion \
	getclient \
	getstats \
	lock \
	setversion \
	updatedraw \
	gem_basic \
	gem_flink \
	gem_readwrite \
	gem_mmap \
	radeon_gem_mmap \
	radeon_gem_basic \
	radeon_bo_test

EXTRA_PROGRAMS = $(TESTS)
CLEANFILES = $(EXTRA_PROGRAMS) $(EXTRA_LTLIBRARIES)

