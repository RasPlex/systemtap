# Makefile.am --- automake input file for systemtap-re2c

AUTOMAKE_OPTIONS=dejagnu

AM_CPPFLAGS = -I$(srcdir)/../includes -I$(builddir)/../includes/sys

clean-local:
	-rm -f tmp_regtest*

check_PROGRAMS =

check_PROGRAMS += regtest

check_SCRIPTS = regcomp.base/regtest.in.0 regcomp.base/regtest.in.1

regtest_SOURCES = regtest.cxx ../stapregex.cxx ../stapregex-tree.cxx \
	../stapregex-parse.cxx ../stapregex-dfa.cxx
regtest_SOURCES += ../translator-output.cxx ../util.cxx
regtest_CXXFLAGS = $(AM_CXXFLAGS) -DSTAPREGEX_STANDALONE

BUILT_SOURCES =

# Arrange for the top level git_version.h to be regenerated at every "make".
BUILT_SOURCES += git_version.stamp
git_version.stamp ../git_version.h:
	$(MAKE) -C .. $(notdir $@)

RUNTESTDEFAULTFLAGS = --tool regcomp --srcdir $$srcdir
