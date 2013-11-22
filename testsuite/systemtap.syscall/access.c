/* COVERAGE: access faccessat */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// To test for glibc support for faccessat():
//
// Since glibc 2.10:
//	_XOPEN_SOURCE >= 700 || _POSIX_C_SOURCE >= 200809L
// Before glibc 2.10:
//	_ATFILE_SOURCE

#define GLIBC_SUPPORT \
  (_XOPEN_SOURCE >= 700 || _POSIX_C_SOURCE >= 200809L \
   || defined(_ATFILE_SOURCE))

int main()
{
  int fd1;

  fd1 = creat("foobar1",S_IREAD|S_IWRITE);

  access("foobar1", F_OK);
  //staptest// access ("foobar1", F_OK) = 0

#if GLIBC_SUPPORT
  faccessat(AT_FDCWD, "foobar1", F_OK, 0);
  //staptest// faccessat (AT_FDCWD, "foobar1", F_OK) = 0
#endif

  access("foobar1", R_OK);
  //staptest// access ("foobar1", R_OK) = 0

#if GLIBC_SUPPORT
  faccessat(AT_FDCWD, "foobar1", R_OK, 0);
  //staptest// faccessat (AT_FDCWD, "foobar1", R_OK) = 0
#endif

  access("foobar1", W_OK);
  //staptest// access ("foobar1", W_OK) = 0

#if GLIBC_SUPPORT
  faccessat(AT_FDCWD, "foobar1", W_OK, 0);
  //staptest// faccessat (AT_FDCWD, "foobar1", W_OK) = 0
#endif

  access("foobar1", X_OK);
  //staptest// access ("foobar1", X_OK) = -NNNN (EACCES)

#if GLIBC_SUPPORT
  faccessat(AT_FDCWD, "foobar1", X_OK, 0);
  //staptest// faccessat (AT_FDCWD, "foobar1", X_OK) = -NNNN (EACCES)
#endif

  access("foobar1", R_OK|W_OK);
  //staptest// access ("foobar1", W_OK |R_OK) = 0

#if GLIBC_SUPPORT
  faccessat(AT_FDCWD, "foobar1", R_OK|W_OK, 0);
  //staptest// faccessat (AT_FDCWD, "foobar1", W_OK |R_OK) = 0
#endif

  access("foobar1", R_OK|W_OK|X_OK);
  //staptest// access ("foobar1", X_OK |W_OK |R_OK) = -NNNN (EACCES)

#if GLIBC_SUPPORT
  faccessat(AT_FDCWD, "foobar1", R_OK|W_OK|X_OK, 0);
  //staptest// faccessat (AT_FDCWD, "foobar1", X_OK |W_OK |R_OK) = -NNNN (EACCES)
#endif

  return 0;
}
