# Makefile.am --- automake input file for systemtap per-method probing

#AM_CFLAGS = -Wall -Werror -W -Wformat=2 -Wno-format-nonliteral -g 
AM_CFLAGS = -g
AM_CFLAGS += -I$(builddir)/../includes/sys
AM_CFLAGS += -I$(srcdir)/../includes
AM_CFLAGS += -I$(srcdir)/java
AM_CFLAGS += -I/usr/lib/jvm/java-1.7.0-openjdk-1.7.0.9.x86_64/include/linux -I/usr/lib/jvm/java-1.7.0-openjdk-1.7.0.9.x86_64/include
AM_CFLAGS += -DBINDIR='"$(bindir)"' -DSYSCONFDIR='"$(sysconfdir)"' -DPKGDATADIR='"${pkgdatadir}"' -DPKGLIBDIR='"$(pkglibexecdir)"' -DLOCALEDIR='"$(localedir)"'
AM_CFLAGS += -fPIC -shared

bin_PROGRAMS = 
noinst_PROGRAMS =

if HAVE_SELINUX
AM_CFLAGS += $(selinux_CFLAGS)
AM_LDFLAGS = $(selinux_LIBS)
endif #HAVE_SELINUX

if HAVE_HELPER

#build libHelperSDT.so
noinst_PROGRAMS += libHelperSDT.so
libHelperSDT_so_SOURCES = HelperSDT.c HelperSDT.h
libHelperSDT_so_LDFLAGS = -lc


#build HelperSDT.class

#jar cvf HelperSDT.jar HelperSDT.class libHelperSDT.so

#place/install libHelper.so into HAVE_HELPER dir

#place/install HelperSDT.jar into HAVE_HELPER/../ext diro

endif #HAVE_HELPER

BUILT_SOURCES = 

# Arrange for the top-level git_version.h to be regenerated at every "make".
BUILT_SOURCES += git_version.stamp
git_version.stamp ../git_version.h:
	$(MAKE) -C .. $(notdir $@)
