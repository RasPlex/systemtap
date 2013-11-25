/* COVERAGE: link linkat symlink symlinkat readlink readlinkat */
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// To test for glibc support for linkat(), symlinkat(), readlinkat():
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
  int fd;
  char buf[128];

  fd = open("foobar",O_WRONLY|O_CREAT, S_IRWXU);
  close(fd);

  link("foobar", "foobar2");
  //staptest// link ("foobar", "foobar2") = 0

#if GLIBC_SUPPORT
  linkat(AT_FDCWD, "foobar", AT_FDCWD, "foobar3", 0);
  //staptest// linkat (AT_FDCWD, "foobar", AT_FDCWD, "foobar3", 0x0) = 0
#endif

  link("foobar", "foobar");
  //staptest// link ("foobar", "foobar") = -NNNN (EEXIST)

#if GLIBC_SUPPORT
  linkat(AT_FDCWD, "foobar", AT_FDCWD, "foobar", 0);
  //staptest// linkat (AT_FDCWD, "foobar", AT_FDCWD, "foobar", 0x0) = -NNNN (EEXIST)
#endif

  link("nonexist", "foo");
  //staptest// link ("nonexist", "foo") = -NNNN (ENOENT)

#if GLIBC_SUPPORT
  linkat(AT_FDCWD, "nonexist", AT_FDCWD, "foo", 0);
  //staptest// linkat (AT_FDCWD, "nonexist", AT_FDCWD, "foo", 0x0) = -NNNN (ENOENT)
#endif

  symlink("foobar", "Sfoobar");
  //staptest// symlink ("foobar", "Sfoobar") = 0

#if GLIBC_SUPPORT
  symlinkat("foobar", AT_FDCWD, "Sfoobar2");
  //staptest// symlinkat ("foobar", AT_FDCWD, "Sfoobar2") = 0
#endif

  readlink("Sfoobar", buf, sizeof(buf));
  //staptest// readlink ("Sfoobar", XXXX, 128) = 6

#if GLIBC_SUPPORT
  readlinkat(AT_FDCWD, "Sfoobar", buf, sizeof(buf));
  //staptest// readlinkat (AT_FDCWD, "Sfoobar", XXXX, 128) = 6
#endif

  return 0;
}
