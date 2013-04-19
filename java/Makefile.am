# Makefile.am --- automake input file for systemtap per-method probing

#AM_CFLAGS = -Wall -Werror -W -Wformat=2 -Wno-format-nonliteral -g 
AM_CFLAGS = -g
AM_CFLAGS += -I$(builddir)/../includes/sys
AM_CFLAGS += -I$(srcdir)/../includes
AM_CFLAGS += -I$(srcdir)/java
#this could be configure variables, find jni_md.h dir and jni.h dir
AM_CFLAGS += -I/usr/lib/jvm/java-1.7.0-openjdk-1.7.0.9.x86_64/include/linux -I/usr/lib/jvm/java-1.7.0-openjdk-1.7.0.9.x86_64/include
#AM_CFLAGS += -I$(HAVE_JNI_H)
#AM_CFLAGS += -I$(HAVE_JNI_MD_H)
AM_CFLAGS += -DBINDIR='"$(bindir)"' -DSYSCONFDIR='"$(sysconfdir)"' -DPKGDATADIR='"${pkgdatadir}"' -DPKGLIBDIR='"$(pkglibexecdir)"' -DLOCALEDIR='"$(localedir)"'
AM_CFLAGS += -fPIC -shared

bin_PROGRAMS = 
noinst_PROGRAMS =
BUILT_SOURCES =
HelperSDT_SOURCES =

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
#javadir = 
#dist_java_JAVA = HelperSDT.java


#jar cvf HelperSDT.jar HelperSDT.class libHelperSDT.so

HelperSDT_SOURCES += java-build jar-build

java-build:
	$(JAVAC) $(srcdir)/HelperSDT.java

jar-build:
	$(JAR) cvf HelperSDT.jar HelperSDT.class libHelperSDT.so

#place/install libHelper.so into HAVE_HELPER dir
install_java_so:
	$(install_sh) -c -m 755 libHelperSDT.so /usr/lib/jvm/java-1.7.0-openjdk-1.7.0.9.x86_64/jre/lib/amd64/

#place/install HelperSDT.jar into HAVE_HELPER/../ext dir
install_java_jar:
	$(install_sh_DATA) HelperSDT.jar /usr/lib/jvm/java-1.7.0-openjdk-1.7.0.9.x86_64/jre/lib/ext
endif #HAVE_HELPER


# Arrange for the top-level git_version.h to be regenerated at every "make".
BUILT_SOURCES += git_version.stamp
git_version.stamp ../git_version.h:
	$(MAKE) -C .. $(notdir $@)
