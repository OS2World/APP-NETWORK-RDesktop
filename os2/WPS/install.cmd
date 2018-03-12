/*
 *  Rdesktop WPS Class (De)Installation Script
 *
 *  This script is to be called by makefile
 *  Syntax: install I|D
 */

call RxFuncAdd "SysLoadFuncs", "RexxUtil", "SysLoadFuncs"
call SysLoadFuncs

ClassName      = "Rdesktop"
ClassDll       = "rdesktop.dll"
ObjectLocation = "<WP_DESKTOP>"

/* Uncomment next two lines to create/delete object. */
/*
ObjectId       = "<RDESKTOP_WPS>"
ObjectName     = "Rdesktop WPS Object"
*/

parse upper arg Action

/* check if DLL exists */
ClassDllFullname = stream( ClassDll, "C", "QUERY EXISTS" )
if ClassDllFullname = "" then
do
  say "Class DLL "ClassDll" not found."
  return 2
end

select
  when Action = "I" then
  do
    /* register WPS class DLL */
    call charout, "Registering WPS Class """ClassName""" of "ClassDll"..."
    if SysRegisterObjectClass( ClassName, ClassDllFullname ) then
      say " Ok."
    else
    do
      say " ERROR!"
      leave
    end

    if symbol( "ObjectId" ) = "VAR" then
    do
      /* create WPS object of class */
      call charout, "Creating WPS object """ObjectName""" ..."
  /*          if (SysCreateObject( ClassName,  ObjectName,  ObjectLocation, ,
                           'OBJECTID='ObjectId';' || ,
                           'host=1.2.3.4;user=myUser;domain=myDomain;' || ,
                           'password=myPswd;prompt=NO;' || ,
                           'sizemode=fullscreen;width=100;height=200;' || ,
                           'proportionalsize=50;depth=8;' || ,
                           'clienthost=clnthst;localcodepage=CP866;' || ,
                           'switches=RDP5,NUMLOCK_SYNC,KEEP_WIN_KEYS;' || ,
                           'drp5perf=NO_MENUANIMATIONS,NO_CURSORSETTINGS;' || ,
                           'encryption=LOGON;clipboard=OFF;sound=OFF;' || ,
                           'disk="MyHome"="D:\home Dir",MyDoc=D:\docDir;', ,
                           'U')) then*/
      if SysCreateObject( ClassName,  ObjectName,  ObjectLocation, ,
                          "OBJECTID="ObjectId";", "U" ) then
        say " Ok."
      else
      do
        say " ERROR!"
        leave
      end

      /* open properties */
      call charout, "Opening object properties ..."
      if SysSetObjectData( ObjectId, "OPEN=SETTINGS" ) then
         say " Ok."
      else
         say " ERROR!"
    end
  end

  when Action = "D" then
  do
    if symbol( "ObjectId" ) = "VAR" then
    do
      /* destroy object if it exists */
      if SysSetObjectData( ObjectId, ";" ) then
      do
        call charout, "Destroying WPS object """ObjectName""" ..."
        if SysDestroyObject( ObjectId ) then
          say " Ok."
        else
        do
          say " ERROR!"
          leave
        end
      end
      else
        say "WPS object does not exist."
    end

    /* check if WPS class is registered */
    fDeregister = 0
    List.0 = 0
    call SysQueryClassList "List."
    if List.0 = 0  then
      fDeregister = 1
    else
    do i = List.0 to 1 by -1
      parse var List.i ThisClass .
      if ThisClass = ClassName then
      do
        fDeregister = 1
        leave
      end
    end

    if fDeregister then
    do
      call charout, "Deregistering WPS class """ClassName""" ..."
      if SysDeregisterObjectClass( ClassName ) then
      do
        say " Ok."
        say "Restart desktop..."

        cntPMShell = 0

        "@plist y|RXQUEUE"

        do while queued() \= 0
          parse pull CPU pid Thr Pri Sess Type Name
          if Name = "" then
            iterate

          if FILESPEC( "name", Name ) = "PMSHELL.EXE" then
          do
            cntPMShell = cntPMShell + 1
            if cntPMShell == 2 then
            do
              "@pkill "pid
              leave
            end
          end
        end

      end
      else
        say " Error."
    end
    else
      say "WPS class is not registered."
  end

  /* --------------------------------------------- */

  otherwise
  do
     say "Usage: install.cmd <I|D>"
     return 1
  end
end

return 0
