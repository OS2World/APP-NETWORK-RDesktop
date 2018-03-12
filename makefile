#
# Rdesktop for OS/2.
# Andrey Vasilkin, 2016-2018.
#

NAME = rdesktop
VERSION = 1.8.3
DESCRIPTION = rdesktop for OS/2
MSGPREF = [rdesktop]
# DEBUGCODE = YES

EXEFILE = $(NAME).exe
DBGFILE = $(NAME).dbg
DEFFILE = $(NAME).def
RCFILE = $(NAME).rc
RESFILE = $(NAME).res
CLASSDLL = os2/rdesktop.dll
WPIFILE = os2/warpin/rdesktop.wpi


SRCS = asn.c bitmap.c cache.c channels.c cliprdr.c ctrl.c \
       iso.c licence.c lspci.c mcs.c mppc.c orders.c parallel.c \
       printer.c printercache.c pstcache.c rdesktop.c rdp.c rdp5.c rdpdr.c \
       rdpsnd.c rdpsnd_dsp.c seamless.c secure.c ssl.c tcp.c utils.c \
       os2/serial.c os2/xclip.c os2/os2seamless.c \
       os2/rdpsnd_dart.c os2/nonblkwin.c os2/disk.c os2/util.c \
       os2/libgiconv/geniconv.c os2/libgiconv/os2cp.c os2/libgiconv/os2iconv.c \
       os2/os2win.c os2/xkeymap.c

CFLAGS = -include ./os2/config.h -I. -idirafter /@unixroot/usr/include/os2tk45 \
         -DUSE_OS2_TOOLKIT_HEADERS -DWITH_RDPSND -Wall \
         -Wno-unused-variable -Wno-unused-but-set-variable

#         -DDEBUG_FILE=\"$(DBGFILE)\"
# -DWITH_DEBUG_SERIAL \
# -DWITH_DEBUG_SEAMLESS

ifeq ($(DEBUGCODE),YES)
SRCS += os2/debug.c
CFLAGS += -DDEBUG_FILE=\"$(DBGFILE)\"
DESCRIPTION += - debug
endif

OBJS = $(SRCS:.c=.o)

CC = @gcc
RC = @rc16
LIBS = -llibssl -llibcrypto -L./os2/lib -lmdm

LDFLAGS = $(DEFFILE)

all: $(EXEFILE) $(CLASSDLL) $(WPIFILE)

$(WPIFILE): os2/warpin/script.inp os2/warpin/readme.os2 rdesktop.exe COPYING \
            doc/AUTHORS os2/WPS/rdesktop.dll os2/WPS/rdesktop.hlp
	@echo $(MSGPREF) WPI file creation: $@
	@os2/warpin/make.cmd >nul

$(CLASSDLL):
	@echo @cd os2\\wps >makewps.cmd
	@echo @wmake all -h >>makewps.cmd
	@echo @cd ..\\.. >>makewps.cmd
	@makewps.cmd
	@cmd /c del makewps.cmd

$(EXEFILE): msgCompilation $(OBJS) $(DEFFILE) $(RESFILE) msgBuilding
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)
	@lxlite /CS $@ >nul
	$(RC) -n $(RESFILE) $@ >nul
	@echo $(MSGPREF) EXE created: $@

$(RESFILE): $(RCFILE)
	$(RC) -r -n $(RCFILE) >nul

$(RCFILE): ./os2/rdesktop.ico
	@echo ICON 1 .\\\\os2\\\\rdesktop.ico >$@

.c.obj: .AUTODEPEND
	$(CC) $(CFLAGS) -c $<

$(DEFFILE):
#	@rem @echo NAME $(NAME) windowapi >$@
	@cmd /c %unixroot%\\usr\\libexec\\bin\\date +"DESCRIPTION '@#Andrey Vasilkin:$(VERSION)#@##1## %F               %HOSTNAME%::::::@@$(DESCRIPTION)'" >>$@

msgCompilation :
	@echo $(MSGPREF) Compilation $(DESCRIPTION) ver. $(VERSION)...

msgBuilding :
	@echo $(MSGPREF) Building $(EXEFILE)...

clean :
	@echo $(MSGPREF) Cleaning up
	@rm -f $(OBJS) $(DBGFILE) $(EXEFILE) $(DEFFILE) $(RESFILE) $(RCFILE) $(WPIFILE) $(CLASSDLL)
	@echo @cd os2\\wps >makewps.cmd
	@echo @wmake clean -h >>makewps.cmd
	@makewps.cmd
	@cmd /c del makewps.cmd
