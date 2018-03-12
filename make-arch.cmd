@echo off
rem
rem  make-arch.cmd :
rem
rem  1. cleaning,
rem  2. archive sources (rdesktop-src-YYYYMMDD.zip),
rem  3. compiling,
rem  4. archive WPI (rdesktop-YYYYMMDD.zip),
rem  5. cleaning.
rem
rem  Requires:
rem    OS/2 Developer's toolkit, GCC, libc, libgcc1, openssl, Open Watcom,
rem    iconv2.dll, 7za, /usr/libexec/bin/date.exe

rem Query current date (variable %archdate%).
@%unixroot%\usr\libexec\bin\date +"set archdate=-%%Y%%m%%d" >archdate.cmd
call archdate.cmd
del archdate.cmd

set fnSrc=rdesktop-src%archdate%.zip
set fnWPI=rdesktop%archdate%.zip

rem Cleaning.
cd rdesktop-master
make clean >nul
cd ..
del %fnSrc% %fnWPI% 2>nul

rem Make archives of sources and binaries.

rem First archive - sources.
echo *** Packing sources: %fnSrc%.
7za.exe a -tzip -mx7 -r0 -x!*.zip %fnSrc% .\rdesktop-master make-arch.cmd >nul

rem Compiling the project...
cd rdesktop-master
make
cd os2\warpin

rem Second archive - WPI.
echo *** Packing WPI: %fnWPI%.
7za.exe a -tzip -mx7 -r0 ..\..\..\%fnWPI% readme.os2 rdesktop.wpi >nul

rem Cleaning and return to this script's directory.
echo *** Cleaning up.
cd ..\..
make clean >nul
cd ..
