# Makefile.am --- automake input file for systemtap per-method probing

JAVA_ARCH:=$(target_cpu)
# XXX: the following arch names are based generally off the rpmspec
# for java-1.7.0-openjdk, there might be omissions in some weird cases
override JAVA_ARCH:=$(JAVA_ARCH:x86_64=amd64)
override JAVA_ARCH:=$(JAVA_ARCH:i%86=i386)
override JAVA_ARCH:=$(JAVA_ARCH:sparcv9=sparc)
override JAVA_ARCH:=$(JAVA_ARCH:sparc64=sparcv9)
override JAVA_ARCH:=$(JAVA_ARCH:arm%=arm)

# XXX: perhaps autoconfigure the following?
JAVAC = javac
JAR = jar

BUILT_SOURCES = 
CLEANFILES =

#AM_CFLAGS = -Wall -Werror -W -Wformat=2 -Wno-format-nonliteral -g 
AM_CFLAGS = -g
AM_CFLAGS += -I$(builddir)/../includes/sys
AM_CFLAGS += -I$(srcdir)/../includes
AM_CFLAGS += -I$(srcdir)/java
AM_CFLAGS += -I$(JAVADIR)/include/linux -I$(JAVADIR)/include
AM_CFLAGS += -fPIC -shared

bin_PROGRAMS = 
noinst_DATA =
noinst_PROGRAMS =
pkglibexec_SCRIPTS = 

if HAVE_SELINUX
AM_CFLAGS += $(selinux_CFLAGS)
AM_LDFLAGS = $(selinux_LIBS)
endif #HAVE_SELINUX

if HAVE_JAVA

# only install if we need Byteman support!
pkglibexec_SCRIPTS += stapbm

# build libHelperSDT.so
noinst_PROGRAMS += libHelperSDT.so
libHelperSDT_so_SOURCES = HelperSDT.c HelperSDT.h
libHelperSDT_so_LDFLAGS = -lc

# build HelperSDT.class
noinst_DATA += HelperSDT.class
CLEANFILES += HelperSDT.class
HelperSDT.class: HelperSDT.java
	$(JAVAC) -d . $(srcdir)/HelperSDT.java

# jar cvf HelperSDT.jar HelperSDT.class libHelperSDT.so
noinst_DATA += HelperSDT.jar
CLEANFILES += HelperSDT.jar
HelperSDT.jar: HelperSDT.class
	$(JAR) cvf HelperSDT.jar HelperSDT.class

# place/install libHelperSDT.so and HelperSDT.jar
install-exec-local:
	$(INSTALL_PROGRAM) -D libHelperSDT.so $(DESTDIR)$(pkglibexecdir)/libHelperSDT_$(JAVA_ARCH).so
install-data-local: HelperSDT.jar
	$(INSTALL_DATA) -D HelperSDT.jar $(DESTDIR)$(pkglibexecdir)/HelperSDT.jar
uninstall-local:
	rm -rf $(DESTDIR)$(pkglibexecdir)/libHelperSDT_$(JAVA_ARCH).so
	rm -rf $(DESTDIR)$(pkglibexecdir)/HelperSDT.jar

endif #HAVE_JAVA

# Arrange for the top-level git_version.h to be regenerated at every "make".
BUILT_SOURCES += git_version.stamp
git_version.stamp ../git_version.h:
	$(MAKE) -C .. $(notdir $@)
