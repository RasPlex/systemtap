# Makefile.am --- automake input file for systemtap per-method probing

# TODOXXX autoconfigure these as far as possible
JAVADIR=@JAVADIR@
# JAVADIR = /usr/lib/jvm/java-1.7.0-openjdk-1.7.0.17.x86_64
# JAVADIR = /usr/lib/jvm/java-1.7.0-openjdk-1.7.0.9.x86_64
javahelperdir = $(JAVADIR)/jre/lib/amd64
javaextdir = $(JAVADIR)/jre/lib/ext
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
noinst_PROGRAMS =
bin_SCRIPTS = stapbm

if HAVE_SELINUX
AM_CFLAGS += $(selinux_CFLAGS)
AM_LDFLAGS = $(selinux_LIBS)
endif #HAVE_SELINUX

if HAVE_JAVA_HELPER

# build libHelperSDT.so
noinst_PROGRAMS += libHelperSDT.so
libHelperSDT_so_SOURCES = HelperSDT.c HelperSDT.h
libHelperSDT_so_LDFLAGS = -lc

# build HelperSDT.class
noinst_PROGRAMS += HelperSDT.class
CLEANFILES += HelperSDT.class
HelperSDT.class:
	$(JAVAC) -d . $(srcdir)/HelperSDT.java

# jar cvf HelperSDT.jar HelperSDT.class libHelperSDT.so
noinst_PROGRAMS += HelperSDT.jar
CLEANFILES += HelperSDT.jar
HelperSDT.jar: HelperSDT.class libHelperSDT.so
	$(JAR) cvf HelperSDT.jar HelperSDT.class libHelperSDT.so

# place/install libHelper.so into HAVE_JAVA_HELPER dir
# place/install HelperSDT.jar into HAVE_JAVA_HELPER/../ext diro
install-exec-local:
	$(INSTALL_DATA) -D libHelperSDT.so $(javahelperdir)/libHelperSDT.so
install-data-local: HelperSDT.jar
	$(INSTALL_DATA) -D HelperSDT.jar $(javaextdir)/HelperSDT.jar

uninstall-local:
	rm -rf $(javahelperdir)/libHelperSDT.so
	rm -rf $(javahelperdir)/HelperSDT.jar

endif #HAVE_JAVA_HELPER

# Arrange for the top-level git_version.h to be regenerated at every "make".
BUILT_SOURCES += git_version.stamp
git_version.stamp ../git_version.h:
	$(MAKE) -C .. $(notdir $@)
