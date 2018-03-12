#
# Open Watcom make file
#
# Usage:
#   wmake all       - builds the WPS class DLL
#   wmake install   - builds the WPS class DLL, installs it
#   wmake deinstall - deinstalls the WPS class DLL
#   wmake clean     - executes deinstall and removes compiled files

AUTOR = Andrey Vasilkin
VERSION = 1.0.1
COMMENT = Rdesktop WPS Class
MSGPREF = [WPS Class]

DLLFILE = rdesktop.dll
LNKFILE = rdesktop.lnk

INCPATH = $(%WATCOM)\H\OS2;$(%WATCOM)\H;D:\dev\OS2TK45\som\include;
INCPATH +=D:\dev\OS2TK45\h;

LIBPATH = %WATCOM%\lib386;%WATCOM%\lib386\os2
LIBS = somtk

SRCS = rdesktop.c nbpage.c diskdlg.c start.c util.c rwpipe_os2.c &
       conwin.c progress.c
# debug.c

OBJS = $(SRCS:.c=.obj)

CFLAGS = -zq -bd -sg -I -i=$(INCPATH) -d0 -w2
# CFLAGS+= -DDEBUG_FILE="d:\\r.dbg"
SCFLAGS = -maddstar

.SILENT

.EXTENSIONS : .ih .idl .res .rc .dlg .hlp .ipf

help: .symbolic
    @echo $(MSGPREF) The following pseudotargets are supported:
    @echo.
    @echo  all       - builds the WPS class DLL
    @echo  install   - builds the WPS class DLL, installs it
    @echo  deinstall - deinstalls the WPS class DLL
    @echo  clean     - executes deinstall and removes compiled files

all: rdesktop.dll .symbolic

install: all .symbolic
    -@call install.cmd I

deinstall: .symbolic
    -@call install.cmd D


$(DLLFILE): infCompiling $(OBJS) rdesktop.lnk rdesktop.res rdesktop.hlp
  @echo $(MSGPREF) Linking: $@
  wlink @$(LNKFILE)
  wrc -q $*.res $@

rdesktop.res: rdesktop.rc rdrc.h rdesktop.dlg rdesktop.ico progress_bar.bmp

rdesktop.ih rdesktop.h rdesktop.c: rdesktop.idl

rdesktop.obj: rdesktop.c rdesktop.ih rdesktop.h nbpage.h util.h

.c.obj:
    wcc386 $(CFLAGS) $[@

.idl.ih:
    sc -s"ih" $(SCFLAGS) $<

.idl.h:
    sc -s"h" $(SCFLAGS) $< 

.idl.c:
    sc -s"c" $(SCFLAGS) $< 

.rc.res:
  @echo $(MSGPREF) Compiling resource: $@
  wrc -r -q -i=$(INCPATH) $[*

.ipf.hlp:
  @echo $(MSGPREF) Compiling help: $@
  ipfc $< $*.hlp /W3 >nul

$(LNKFILE): .ALWAYS
  @echo $(MSGPREF) Creating file: $@
  @%create $@
  @%append $@ NAME $(DLLFILE)
  @%append $@ SYSTEM os2v2_dll INITINSTANCE TERMINSTANCE
  @%append $@ SEGMENT data nonshared loadoncall
  @%append $@ OPTION quiet,alignment=4
  @%append $@ OPTION manyautodata
  @%append $@ OPTION ELIMINATE
  @%append $@ OPTION QUIET
  @%append $@ OPTION OSNAME='OS/2 and eComStation'
!ifdef %unixroot
  @$(%unixroot)\usr\libexec\bin\date.exe +"OPTION DESCRIPTION '@$#$(AUTOR):$(VERSION)$#@$#$#1$#$# %F %T      $(%HOSTNAME)::ru:RUS:::@@$(COMMENT)'" >>$^@
!else
  @%append $@ OPTION DESCRIPTION '@$#$(AUTOR):$(VERSION)$#@$#$#1$#$#                          $(%HOSTNAME)::ru:RUS:0::@@$(COMMENT)'
!endif
!ifdef LIBPATH
  @%append $@ LIBPATH $(LIBPATH)
!endif
!ifdef LIBS
  @for %i in ($(LIBS)) do @%append $@ LIB %i
!endif
  @for %i in ($(OBJS)) do @%append $@ FILE %i
  @%append $@ EXPORT RdesktopCClassData
  @%append $@ EXPORT RdesktopClassData
  @%append $@ EXPORT RdesktopNewClass
  @%append $@ EXPORT M_RdesktopCClassData
  @%append $@ EXPORT M_RdesktopClassData
  @%append $@ EXPORT M_RdesktopNewClass

infCompiling: .SYMBOLIC
  @echo $(MSGPREF) Compiling: $(COMMENT) $(VERSION) ...

clean: .symbolic
  @echo $(MSGPREF) Cleaning up
  if exist $(LNKFILE) del $(LNKFILE)
  if exist $(DLLFILE) del $(DLLFILE)
  -@call install.cmd D
  if exist rdesktop.hlp del rdesktop.hlp
  @rm -f *.obj rdesktop.h *.ih *.err *.res
