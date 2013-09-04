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

if HAVE_JAVA

# only install if we need Byteman support!
pkglibexec_SCRIPTS += stapbm

# build libHelperSDT.so
noinst_PROGRAMS += libHelperSDT.so
libHelperSDT_so_SOURCES = HelperSDT.c HelperSDT.h
libHelperSDT_so_LDFLAGS = -lc

# the full class name is:
HELPERSDT = org/systemtap/byteman/helper/HelperSDT

# build HelperSDT.class
noinst_DATA += $(HELPERSDT).class
CLEANFILES += $(HELPERSDT).class
$(HELPERSDT).class: $(srcdir)/$(HELPERSDT).java
	$(JAVAC) -d . $(srcdir)/$(HELPERSDT).java

# jar cvf HelperSDT.jar HelperSDT.class libHelperSDT.so
noinst_DATA += HelperSDT.jar
CLEANFILES += HelperSDT.jar
HelperSDT.jar: $(HELPERSDT).class
	$(JAR) cvf HelperSDT.jar $(HELPERSDT).class

# place/install libHelperSDT.so and HelperSDT.jar
install-exec-local:
	$(INSTALL_PROGRAM) -D libHelperSDT.so $(DESTDIR)$(pkglibexecdir)/libHelperSDT_$(JAVA_ARCH).so
install-data-local: HelperSDT.jar
	$(INSTALL_DATA) -D HelperSDT.jar $(DESTDIR)$(pkglibexecdir)/HelperSDT.jar
uninstall-local:
	rm -rf $(DESTDIR)$(pkglibexecdir)/libHelperSDT_$(JAVA_ARCH).so
	rm -rf $(DESTDIR)$(pkglibexecdir)/HelperSDT.jar

endif #HAVE_JAVA
