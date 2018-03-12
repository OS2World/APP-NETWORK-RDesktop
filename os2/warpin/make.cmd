/*
	Building System Load installation package for WARPIN.
*/

warpinPath = "%osdir%\install\WARPIN"
WPIFile = "rdesktop"
fileScriptInput = "script.inp"
fileScriptOutput = "script.out"

if RxFuncQuery('SysLoadFuncs') then
do
  call RxFuncAdd 'SysLoadFuncs', 'RexxUtil', 'SysLoadFuncs'
  call SysLoadFuncs
end

parse source os cmd scriptPath
savePath = directory( left( scriptPath, lastpos( "\", scriptPath ) - 1 ) )

/* Substitution versions of the components (from BLDLEVEL signatures) in   */
/* the script. Reads file fileScriptInput and makes file fileScriptOutput  */
/* Replaces all switches "<!-- BL:D:\path\program.exe -->" with version of */
/* D:\path\program.exe (bldlevel signature uses).                          */
if makeScript( fileScriptInput, fileScriptOutput ) = 0 then
  exit

/* Building installation package. */

call SysFileDelete WPIFile || ".wpi"

"@set beginlibpath=" || warpinPath

"@"warpinPath || "\WIC.EXE " || WPIFile || " -a " || ,
"1 -c. readme.os2 " || ,
"1 -c..\.. rdesktop.exe COPYING " || ,
"1 -c..\..\doc AUTHORS " || ,
"2 -c..\WPS rdesktop.dll " || ,
"2 -c..\WPS rdesktop.hlp " || ,
"-s " || fileScriptOutput

call SysFileDelete fileScriptOutput

call directory savePath

/* Exit */
return rc

makeScript: PROCEDURE
  fileInput = arg(1)
  fileOutput = arg(2)

/*  if RxFuncQuery( "gvLoadFuncs" ) then*/
  do
    call RxFuncAdd "gvLoadFuncs", "RXGETVER", "gvLoadFuncs"
    call gvLoadFuncs
  end

  if stream( fileInput, "c", "open read" ) \= "READY:" then
  do
    say "Cannot open input file: " || fileInput
    return 0
  end

  call SysFileDelete fileOutput
  if stream( fileOutput, "c", "open write" ) \= "READY:" then
  do
    say "Cannot open output file: " || fileOutput
    return 0
  end

  resOk = 1
  do while lines( fileInput ) \= 0
    parse value linein( fileInput ) with part1 "<!-- BL:" file " -->" part2

    if file \= "" then
    do
      rc = gvGetFromFile( file, "ver." )
      if rc \= "OK" then
      do
        say "File: " || file
        say rc
        resOk = 0
        leave
      end

      parse value ver._BL_REVISION || ".0" with ver.1 "." ver.2 "." ver.3 "." v
      verIns = ver.1 || "\" || ver.2 || "\" || ver.3
    end
    else
      verIns = ""

    call lineout fileOutput, part1 || verIns || part2
  end

  call stream fileInput, "c", "close"
  call stream fileOutput, "c", "close"
  call gvDropFuncs

  if \resOk then
    call SysFileDelete fileOutput
    
  return resOk
