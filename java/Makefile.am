# Makefile.am --- automake input file for systemtap per-method probing

# XXX: perhaps autoconfigure the following?
JAVAC = javac
JAR = jar

BUILT_SOURCES = 
CLEANFILES =

#AM_CFLAGS = -Wall -Werror -W -Wformat=2 -Wno-format-nonliteral -g 
AM_CFLAGS = -g
AM_CFLAGS += -I$(builddir)/../includes/sys
AM_CFLAGS += -I$(srcdir)/../includes
AM_CFLAGS += -I$(srcdir)/java # XXX?
AM_CFLAGS += -I$(JAVADIR)/include/linux -I$(JAVADIR)/include
AM_CFLAGS += -DBINDIR='"$(bindir)"' -DSYSCONFDIR='"$(sysconfdir)"' -DPKGDATADIR='"${pkgdatadir}"' -DPKGLIBDIR='"$(pkglibexecdir)"' -DLOCALEDIR='"$(localedir)"'
AM_CFLAGS += -fPIC -shared

bin_PROGRAMS = 
noinst_DATA =
noinst_PROGRAMS =
pkglibexec_SCRIPTS = stapbm

if HAVE_SELINUX
AM_CFLAGS += $(selinux_CFLAGS)
AM_LDFLAGS = $(selinux_LIBS)
endif #HAVE_SELINUX

if HAVE_JAVA

# build libHelperSDT.so
noinst_PROGRAMS += libHelperSDT.so
libHelperSDT_so_SOURCES = HelperSDT.c HelperSDT.h
libHelperSDT_so_LDFLAGS = -lc

# build HelperSDT.class
noinst_DATA += HelperSDT.class
CLEANFILES += HelperSDT.class
HelperSDT.class:
	$(JAVAC) -d . $(srcdir)/HelperSDT.java

# build getarch.class
noinst_DATA += libHelperSDT_getarch.class
CLEANFILES += libHelperSDT_getarch.class
libHelperSDT_getarch.class:
	$(JAVAC) -d . $(srcdir)/libHelperSDT_getarch.java

# jar cvf HelperSDT.jar HelperSDT.class libHelperSDT.so
noinst_DATA += HelperSDT.jar
CLEANFILES += HelperSDT.jar
HelperSDT.jar: HelperSDT.class libHelperSDT.so
	$(JAR) cvf HelperSDT.jar HelperSDT.class libHelperSDT.so

# place/install libHelperSDT.so and HelperSDT.jar
install-exec-local:
	$(INSTALL_DATA) -D libHelperSDT.so $(DESTDIR)$(pkglibexecdir)/libHelperSDT.so
install-data-local: HelperSDT.jar
	$(INSTALL_DATA) -D libHelperSDT_getarch.class $(DESTDIR)$(pkglibexecdir)/libHelperSDT_getarch.class
	$(INSTALL_DATA) -D HelperSDT.jar $(DESTDIR)$(pkglibexecdir)/HelperSDT.jar

uninstall-local:
	rm -rf $(DESTDIR)$(pkglibexecdir)/libHelperSDT.so
	rm -rf $(DESTDIR)$(pkglibexecdir)/libHelperSDT_getarch.class
	rm -rf $(DESTDIR)$(pkglibexecdir)/HelperSDT.jar

endif #HAVE_JAVA

# Arrange for the top-level git_version.h to be regenerated at every "make".
BUILT_SOURCES += git_version.stamp
git_version.stamp ../git_version.h:
	$(MAKE) -C .. $(notdir $@)
