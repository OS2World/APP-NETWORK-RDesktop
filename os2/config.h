#ifndef _CONFIG_H_
#define _CONFIG_H_

#define HAVE_SYS_SELECT_H 1
#define HAVE_SYSEXITS_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_ICONV 1
#define HAVE_DIRFD 1
#define HAVE_LOCALE_H 1
#define HAVE_LANGINFO_H 1
#define HAVE_ICONV 1
#define HAVE_ICONV_H 1
#define HAVE_SYS_FILIO_H 1
#define HAVE_DIRFD 1
#define HAVE_DECL_DIRFD 1
#define PACKAGE_VERSION "1.8.3"

#define WITH_RDPSND 1
#define RDPSND_DART 1

#define ICONV_CONST const

// Lost define for termios.h
#define IMAXBEL		0x00002000	/* ring bell on input queue full */

#endif // _CONFIG_H_
