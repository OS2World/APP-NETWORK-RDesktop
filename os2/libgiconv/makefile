#
# Universal iconv implementation for OS/2.
#
# Makefile to build a library that uses kiconv.dll / iconv2.dll /
# iconv.dll or OS/2 Uni*() API.
# Set environment variable GENICONV=UCONV to use OS/2 Uni*() API only.
#
# Andrey Vasilkin, 2016.
#

LIBFILE = libgiconv.a
CC = gcc

all: $(LIBFILE) test.exe

CFLAGS = -DDEBUG

SRCS = geniconv.c os2cp.c os2iconv.c
# SRCS+= sys2utf8.c

OBJS = $(SRCS:.c=.o)

LIBS =
# -llibuls.lib
# -l$(LIBFILE)

test.exe: $(LIBFILE) test.o
	$(CC) $(LDFLAGS) $(OBJS) test.o -o $@ $(LIBS)

$(LIBFILE): $(OBJS)
	ar rc $@ $(OBJS)

.c.o: .AUTODEPEND
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJS) *.err $(LIBFILE) test.exe
