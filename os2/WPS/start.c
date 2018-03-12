#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <process.h>
#define INCL_WIN
#define INCL_DOSFILEMGR
#define INCL_DOSERRORS
#define INCL_GPI
#define INCL_DOSPROCESS
#define INCL_DOSSEMAPHORES // SEM_INDEFINITE_WAIT
#define INCL_DOSEXCEPTIONS
#include <os2.h>
#include <util.h>
#include <rdesktop.ih>
#include <rdrc.h>
#include <debug.h>
#include <rwpipe.h>
#include <progress.h>

#define _CMD_MAX_LENGTH          4096

//     Logon dialog.
//     -------------

typedef struct _DLGDATA {
  USHORT               usSize;
  Rdesktop             *somSelf;
  RdesktopData         *somThis;
  CHAR                 szUser[136];
  CHAR                 szDomain[72];
  CHAR                 szPassword[136];
} DLGDATA, *PDLGDATA;

static VOID _wmInitDlg(HWND hwnd, PDLGDATA pDlgData)
{
  RdesktopData  *somThis = pDlgData->somThis;
  RECTL         rectDlg, rectParent;
  HWND          hwndEFUser = WinWindowFromID( hwnd, IDD_EF_USER );

  WinSetWindowPtr( hwnd, QWL_USER, pDlgData );

  // Place dialog at center of the desktop.
  WinQueryWindowRect( hwnd, &rectDlg );
  WinQueryWindowRect( WinQueryWindow( hwnd, QW_PARENT ), &rectParent );
  WinSetWindowPos( hwnd, HWND_TOP,
                   (rectParent.xRight - rectDlg.xRight) / 2,
                   (rectParent.yTop - rectDlg.yTop) / 2,
                   0, 0, SWP_MOVE | SWP_ACTIVATE );

  WinSetDlgItemText( hwnd, IDD_EF_HOST, _pszHost );
  WinSetWindowText( hwndEFUser, _pszUser );
  WinSetDlgItemText( hwnd, IDD_EF_DOMAIN, _pszDomain );

  WinSetFocus( HWND_DESKTOP,
               _pszUser == NULL
                 ? hwndEFUser : WinWindowFromID( hwnd, IDD_EF_PASSWORD ) );
}

static VOID _ctlEFUserChanged(HWND hwnd)
{
  CHAR       acBuf[256];
  PCHAR      pcBuf;
  BOOL       fEnable;

  if ( WinQueryDlgItemText( hwnd, IDD_EF_USER, sizeof(acBuf), acBuf ) == 0 )
    fEnable = FALSE;
  else
  {
    for( pcBuf = acBuf; isspace(*pcBuf); pcBuf++ );
    fEnable = *pcBuf != '\0';
  }

  WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ), fEnable );
}

static VOID _wmDestroy(HWND hwnd)
{
  PDLGDATA   pDlgData = WinQueryWindowPtr( hwnd, QWL_USER );

  WinQueryDlgItemText( hwnd, IDD_EF_USER,
                       sizeof(pDlgData->szUser), &pDlgData->szUser );
  WinQueryDlgItemText( hwnd, IDD_EF_DOMAIN,
                       sizeof(pDlgData->szDomain), &pDlgData->szDomain );
  WinQueryDlgItemText( hwnd, IDD_EF_PASSWORD,
                       sizeof(pDlgData->szPassword), &pDlgData->szPassword );
}

static MRESULT EXPENTRY _logonDlgProc(HWND hwnd, ULONG msg,
                                      MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      _wmInitDlg( hwnd, (PDLGDATA)mp2 );
      return (MRESULT)TRUE;

    case WM_DESTROY:
      _wmDestroy( hwnd );
      break;

    case WM_CONTROL:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDD_EF_USER:
          if ( SHORT2FROMMP( mp1 ) == EN_CHANGE )
            _ctlEFUserChanged( hwnd );
          break;
      }
      return (MRESULT)FALSE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


//     Program thread.
//     ---------------

typedef struct _PROGDATA {
  struct _PROGDATA     **ppPrev;
  struct _PROGDATA     *pNext;
  Rdesktop             *somSelf;
  RWPIPE               stRWPipe;
  USEITEM              stUseItem;
  VIEWITEM             stViewItem;
} PROGDATA, *PPROGDATA;

static void threadRead(PVOID pData)
{
  PPROGDATA  pProgData = (PPROGDATA)pData;
  RdesktopData  *somThis = RdesktopGetData( pProgData->somSelf );
  LONG		   cbRead;
  CHAR       acBuf[512];
  ULONG      ulPos = 0;
  PCHAR      pcEOL, pcNext;
  HAB        hab = WinInitialize( 0 );
  HMQ        hmq;

  // We must create message queue, even if thread does not need a message loop.

  if ( hab == NULLHANDLE )
    return;

  hmq = WinCreateMsgQueue( hab, 0 );
  if ( hmq == NULLHANDLE )
    return;

  WinCancelShutdown( hmq, TRUE );


  _wpRequestObjectMutexSem( pProgData->somSelf, SEM_INDEFINITE_WAIT );

  // Set "open" (in-use) state for the desktop icon.
  pProgData->stUseItem.type         = USAGE_OPENVIEW;
  pProgData->stViewItem.view        = OPEN_RDESKTOP;
  pProgData->stViewItem.handle      = pProgData->stRWPipe.pid;
  pProgData->stViewItem.ulViewState = VIEWSTATE_OPENING;
  _wpAddToObjUseList( pProgData->somSelf, &pProgData->stUseItem );

  // Insert PROGDATA to the linked list _pStartData.
  if ( _pStartData != NULL )
    ((PPROGDATA)_pStartData)->ppPrev = &pProgData->pNext;
  pProgData->pNext = (PPROGDATA)_pStartData;

  pProgData->ppPrev = (PPROGDATA *)&_pStartData;
  _pStartData = (PVOID)pProgData;

  _wpReleaseObjectMutexSem( pProgData->somSelf );

  _wpReleaseObjectMutexSem( pProgData->somSelf );

  // Read output data from rdesktop. rwpRead() returns 0 when program ends or
  // -1 on error.
  while( TRUE )
  {
    cbRead = rwpRead( &pProgData->stRWPipe, &acBuf[ulPos],
                      sizeof(acBuf) - ulPos );
    if ( cbRead <= 0 )
      break;

    ulPos += cbRead;
    while( TRUE )
    {
      pcEOL = memchr( acBuf, '\n', ulPos );
      if ( pcEOL == NULL )
      {
        if ( ulPos == sizeof(acBuf) )
        {
          debug( "Too long line received" );
          ulPos = 0;
        }
        break;
      }

      pcNext = &pcEOL[1];
      while( ( pcEOL > acBuf ) && isspace( *(pcEOL - 1) ) )
        pcEOL--;

      *pcEOL = '\0';

      // #winpos=x,y,width,heigh
      // Top-left window corner relative top-left desktop corner.
      if ( memcmp( acBuf, "#winpos=", 8 ) == 0 )
      {
        RECTL          rect;
        LONG           lX, lY, lW, lH;

        lX = strtol( &acBuf[8], &pcEOL, 10 );
        pcEOL++;
        lY = strtol( pcEOL, &pcEOL, 10 );
        pcEOL++;
        lW = strtol( pcEOL, &pcEOL, 10 );
        pcEOL++;
        lH = strtol( pcEOL, NULL, 10 );

        // Set coordinates of the window fully on the desktop. Do not store
        // window position if it out of the screen (on not current page of
        // xPager?).

        // Query size of the desktop.
        WinQueryWindowRect( HWND_DESKTOP, &rect );
        // rect.xRight - desktop width, rect.xTop - desktop height.

        if ( ( lX < rect.xRight ) && ( (lX + lW) >= rect.xRight )  )
          // Window crosses right border of the desktop.
          lX = rect.xRight - lW;
        if ( ( lX < 0 ) && ( (lX + lW) > 0 )  )
          // Window crosses left border of the desktop.
          lX = 0;
        if ( ( lX >= 0 ) && ( (lX + lW) <= rect.xRight ) )
          // Horizontal window coordinates fully on the desktop - store it.
          _lWinPosX = lX;

        if ( ( lY < rect.yTop ) && ( (lY + lH) >= rect.yTop )  )
          // Window crosses bottom border of the desktop.
          lY = rect.yTop - lH;
        if ( ( lY < 0 ) && ( (lY + lH) > 0 )  )
          // Window crosses top border of the desktop.
          lY = 0;
        if ( ( lY >= 0 ) && ( (lY + lH) <= rect.yTop ) )
          // Vertical window coordinates fully on the desktop - store it.
          _lWinPosY = lY;
      }
      else
        cwAddMessage2( pProgData->somSelf, pProgData->stRWPipe.pid, acBuf );

      ulPos -= pcNext - acBuf;
      memcpy( acBuf, pcNext, ulPos );
    }
  }

  WinLoadMessage( hab, utilGetModuleHandle(), IDM_RDESKTOP_EXIT,
                  sizeof(acBuf), acBuf );
  cwAddMessage( pProgData->somSelf, pProgData->stRWPipe.pid, CONREC_SHELL,
                acBuf );

  prStop( pProgData->somSelf, pProgData->stRWPipe.pid );

  // Remove in-use record.
  _wpRequestObjectMutexSem( pProgData->somSelf, SEM_INDEFINITE_WAIT );
  _wpDeleteFromObjUseList( pProgData->somSelf, &pProgData->stUseItem );

  // Remove PROGDATA from the linked list.
  if ( pProgData->pNext != NULL )
    pProgData->pNext->ppPrev = pProgData->ppPrev;
  *pProgData->ppPrev = pProgData->pNext;

  _wpReleaseObjectMutexSem( pProgData->somSelf );

  // Free resources.
  rwpClose( &pProgData->stRWPipe );
  _wpFreeMem( pProgData->somSelf, (PBYTE)pProgData );
  WinDestroyMsgQueue( hmq );
}


BOOL startRD(Rdesktop *somSelf, PSZ pszMClass)
{
  RdesktopData  *somThis = RdesktopGetData( somSelf );
  PSZ           pszUser;
  PSZ           pszDomain;
  PSZ           pszPassword;
  PSZ           pszCmd;
  BOOL          fSuccess = FALSE;
  PPROGDATA     pProgData;

  pszCmd = strchr( _pszHost, ':' );
  if ( pszCmd == NULL )
    pszCmd = strchr( _pszHost, '\0' );

  if ( !utilVerifyDomainName( pszCmd - _pszHost, _pszHost ) )
  {
    cwAddMessageFmt( somSelf, 0, CONREC_ERROR, IDM_INVALID_HOSTNAME, 1,
                     &_pszHost );
    return FALSE;
  }

  if ( ( _pszUser == NULL ) || _fLoginPrompt )
  {
    // Dialog: request user information for authorization.

    DLGDATA    stDlgData;

    stDlgData.usSize = sizeof(DLGDATA);
    stDlgData.somSelf = somSelf;
    stDlgData.somThis = somThis;

    if ( WinDlgBox( HWND_DESKTOP, HWND_DESKTOP, _logonDlgProc,
                    utilGetModuleHandle(), IDDLG_LOGON, &stDlgData )
           != MBID_OK )
      return FALSE;

    pszUser     = stDlgData.szUser;
    STR_TRIM( pszUser );
    pszDomain   = stDlgData.szDomain;
    STR_TRIM( pszDomain );
    pszPassword = stDlgData.szPassword;
    STR_TRIM( pszPassword );
  }
  else
  {
    pszUser     = _pszUser;
    pszDomain   = _pszDomain;
    pszPassword = _pszPassword;
  }

  // Allocate buffer for command and switches.
  pszCmd = (PSZ)_wpAllocMem( somSelf, _CMD_MAX_LENGTH, NULL );

  fSuccess = startQueryCmd( somSelf, pszMClass, pszUser, pszDomain, pszPassword,
                            TRUE, _CMD_MAX_LENGTH, pszCmd ) != -1;

  if ( fSuccess )
  {
    // Run rdesktop.exe.

    pProgData = (PPROGDATA)_wpAllocMem( somSelf, sizeof(PROGDATA), NULL );
    fSuccess = rwpOpen( &pProgData->stRWPipe, pszCmd );
    if ( !fSuccess )
      cwAddMessageFmt( somSelf, 0, CONREC_ERROR, IDM_START_FAILED, 0, NULL );
  }

  _wpFreeMem( somSelf, pszCmd );

  if ( fSuccess )
  {
    CHAR     acDomainUser[256];
    PSZ      apszVal[2];

    // Make console message about runned rdesktop.exe.

    apszVal[0] = _pszHost;
    if ( pszDomain != NULL && *pszDomain != '\0' &&
         ( _snprintf( acDomainUser, sizeof(acDomainUser), "%s\\%s",
                      pszDomain, pszUser ) != -1 ) )
      apszVal[1] = acDomainUser;
    else
      apszVal[1] = pszUser;

    cwAddMessageFmt( somSelf, pProgData->stRWPipe.pid, CONREC_SHELL,
                     IDM_RDESKTOP_RUN, 2, apszVal );

    prStart( somSelf, pProgData->stRWPipe.pid );

    // Start thread to read rdesktop output.

    pProgData->somSelf = somSelf;
    if ( _beginthread( threadRead, NULL, 65535, pProgData ) != -1 )
      // Success.
      return TRUE;

    rwpClose( &pProgData->stRWPipe );
  }

  // Something is wrong...
  _wpFreeMem( pProgData->somSelf, (PBYTE)pProgData );
  return FALSE;
}


// LONG startQueryCmd(Rdesktop *somSelf, PSZ pszMClass, PSZ pszUser,
//                    PSZ pszDomain, PSZ pszPassword, BOOL fSepZero,
//                    ULONG cbBuf, PCHAR pcBuf)
//
// When fSepZero is TRUE the name of the executable file and switches are
// separated by ZERO.
// If rdesktop.exe found the result begin with disk name, i.e. it contains the
// full path (E:\dir\rdesktop.exe...).
// Returns length of result string or -1 on error.

typedef struct _BLDRUNCMD {
  jmp_buf    env;
  PCHAR      pcCmd;
  PCHAR      pcEnd;
} BLDRUNCMD, *PBLDRUNCMD;

// '"' -> '\"', escape all previous '\' with '\'; last '\' -> '\\'; '\n' -> ' '
static PSZ _runSafeStr(ULONG cbBuf, PCHAR pcBuf, PSZ pszSrc)
{
  PCHAR      pcDst = pcBuf;
  CHAR       chSrc;
  BOOL       fLastIsSpace, fIsSpace;

  if ( cbBuf == 0 )
    return NULL;

  while( isspace( *pszSrc ) )
    pszSrc++;

  while( ( *pszSrc != '\0' ) && ( cbBuf > 1 ) )
  {
    fIsSpace = isspace( *pszSrc );
    chSrc = fIsSpace ? ' ' : *pszSrc; // EOL, TAB -> SPACE

    if ( *pszSrc == '"' )
    {
      // '"' -> '\"'; '\\"' -> '\\\\\"' (escape all prev. \)
      PCHAR  pcScanBack = pcDst - 1;

      while( ( pcScanBack >= pcBuf ) && ( *pcScanBack == '\\' ) && ( cbBuf > 1 ) )
      {
        *pcDst = '\\';
        pcDst++;
        cbBuf--;

        pcScanBack--;
      }

      if ( cbBuf > 2 )
      {
        *(PUSHORT)pcDst = (USHORT)'\"\\';
        pcDst += 2;
        cbBuf -= 2;
      }
    }
    else if ( !fIsSpace || !fLastIsSpace )
    {
      *pcDst = *pszSrc;
      pcDst++;
      cbBuf--;
    }

    fLastIsSpace = fIsSpace;
    pszSrc++;
  }

  if ( ( pcDst != pcBuf ) && ( *(pcDst - 1) == '\\' ) ) // Last '\' -> '\\'.
  {
    if ( cbBuf > 1 )  // Add '\'.
    {
      *pcDst = '\\';
      pcDst++;
      cbBuf--;
    }
    else              // No buffer space to escape '\' - remove last '\'.
    {
      pcDst--;
      cbBuf++;
    }
  }
  *pcDst = '\0';

  return pcBuf;
}

static VOID _runCmdAdd(PBLDRUNCMD pBld, PSZ pszFormat, ...)
{
  va_list	arglist;
  int     iRC;
  LONG    cBytes = pBld->pcEnd - pBld->pcCmd;

  if ( cBytes > 2 )
  {
    // New element starts with SPACE.
    *((PUSHORT)pBld->pcCmd) = '\0 ';
    cBytes--;
    pBld->pcCmd++;

    va_start( arglist, pszFormat ); 
    iRC = vsnprintf( pBld->pcCmd, cBytes, pszFormat, arglist ); 
    va_end( arglist );

    if ( iRC != -1 )
    {
      pBld->pcCmd += iRC;
      return;
    }
  }

  debug( "Not enough buffer space" );
  longjmp( pBld->env, 1 );
}

// Build command-line string to run rdesktop.
// fSepZero is TRUE: d:\\path\\rdesktop.exe\0switches\0\0
// fSepZero is FALSE: d:\\path\\rdesktop.exe switches\0
// Returns the number of characters written into the buffer pcBuf, not counting
// the leading null character(s), or value -1 if more than cbBuf characters
// were requested to be generated.

LONG startQueryCmd(Rdesktop *somSelf, PSZ pszMClass, PSZ pszUser,
                   PSZ pszDomain, PSZ pszPassword, BOOL fSepZero,
                   ULONG cbBuf, PCHAR pcBuf)
{
  const static ULONG aulDepth[] = { 8, 15, 16, 24, 32 };
  const static PSZ   aClpBrd[]  = { "PRIMARYCLIPBOARD", "CLIPBOARD", "off" };
  const static PSZ   aSound[]   = { "local", "remote", "off" };
  RdesktopData  *somThis = RdesktopGetData( somSelf );
  BOOL          fSuccess = FALSE;
  PSZ           pszPathName, pszEndOfExe;
  somId         Id;
  FILESTATUS3   stInfo;
  ULONG         ulIdx;
  BLDRUNCMD     stBld;
  CHAR          acSafe[512];

  if ( cbBuf <= _MAX_PATH )
    return -1;

  // Search rdesktop.exe.

  // Search rdesktop.exe in class DLL's directory.
  Id = somIdFromString( pszMClass );
  pszPathName = _somLocateClassFile( SOMClassMgrObject, Id,
                                Rdesktop_MajorVersion, Rdesktop_MinorVersion );
  SOMFree( Id );
  strcpy( pcBuf, pszPathName );
  stBld.pcCmd = strrchr( pcBuf, '\\' );
  if ( stBld.pcCmd != NULL )
  {
    strcpy( stBld.pcCmd + 1, "rdesktop.exe" );
    fSuccess = DosQueryPathInfo( pcBuf, FIL_STANDARD, &stInfo, sizeof(stInfo) )
                   == NO_ERROR &&
               ( (stInfo.attrFile & FILE_DIRECTORY) == 0 );
  }

  // Search rdesktop.exe in %PATH%.
  if ( !fSuccess )
  {
    _searchenv( "rdesktop.exe", "PATH", pcBuf ); 
    if ( pcBuf[0] == '\0' )
      strcpy( pcBuf, "rdesktop.exe" );
  }

  stBld.pcCmd = strchr( pcBuf, '\0' );
  stBld.pcEnd = &pcBuf[cbBuf];
  if ( setjmp( stBld.env ) != 0 )
  {
    debug( "Not enough buffer space" );
    return -1;
  }
  // Now we can use _runCmdAdd().

  pszEndOfExe = stBld.pcCmd;

  // Build switches.

  if ( pszUser != NULL && *pszUser != '\0' )
    _runCmdAdd( &stBld, "-u \"%s\"",
                _runSafeStr( sizeof(acSafe), acSafe, pszUser ) );
  if ( pszDomain != NULL && *pszDomain != '\0' )
    _runCmdAdd( &stBld, "-d \"%s\"",
                _runSafeStr( sizeof(acSafe), acSafe, pszDomain ) );
  if ( pszPassword != NULL && *pszPassword != '\0' )
    _runCmdAdd( &stBld, "-p \"%s\"",
                _runSafeStr( sizeof(acSafe), acSafe, pszPassword ) );

  if ( _ulSizeMode == SIZEMODE_FULLSCREEN )
    _runCmdAdd( &stBld, "-f" );
  else
  {
    PCHAR    pcPos;

    if ( _ulSizeMode == SIZEMODE_ABSOLUTE )
      pcPos = &acSafe[ sprintf( acSafe, "%dx%d", _lWidth, _lHeight ) ];
    else
      pcPos = &acSafe[ sprintf( acSafe, "%u%%", _ulProportionalSize ) ];

    if ( ( _lWinPosX != WINPOS_NOT_CHANGED ) &&
         ( _lWinPosY != WINPOS_NOT_CHANGED ) )
      pcPos += sprintf( pcPos, "+%d+%d",
                        _lWinPosX < 0 ? 0 : _lWinPosX,
                        _lWinPosY < 0 ? 0 : _lWinPosY );

    // Rdesktop for OS/2 feature: report position of the main window on exit.
    // Will print on stdout #winpos=nnn,nnn - position relative top-left corner
    // of the desktop.
    if ( fSepZero )
      strcpy( pcPos, "#winpos" );

    _runCmdAdd( &stBld, "-g%s", acSafe );
  }
  
/*  switch( _ulSizeMode )
  {
    case SIZEMODE_ABSOLUTE:
      _runCmdAdd( &stBld, "-g%dx%d%s", _lWidth, _lHeight,
                  fSepZero ? "#winpos" : "" );
      break;

    case SIZEMODE_PROPORTIONAL:
      _runCmdAdd( &stBld, "-g%u%%", _ulProportionalSize,
                  fSepZero ? "#winpos" : "" );
      break;

    case SIZEMODE_FULLSCREEN:
      _runCmdAdd( &stBld, "-f" );
      break;
  }*/

  if ( _pszClientHost != NULL )
    _runCmdAdd( &stBld, "-n \"%s\"",
                _runSafeStr( sizeof(acSafe), acSafe, _pszClientHost ) );
  if ( _pszLocalCP != NULL )
    _runCmdAdd( &stBld, "-L \"%s\"",
                _runSafeStr( sizeof(acSafe), acSafe, _pszLocalCP ) );

  if ( (_ulSwitches & SWFL_COMPRESSION) != 0 )
    _runCmdAdd( &stBld, "-z" );
  if ( (_ulSwitches & SWFL_PRES_BMP_CACHING) != 0 )
    _runCmdAdd( &stBld, "-P" );
  if ( (_ulSwitches & SWFL_FORCE_BMP_UPD) != 0 )
    _runCmdAdd( &stBld, "-b" );
  if ( (_ulSwitches & SWFL_NUMLOCK_SYNC) != 0 )
    _runCmdAdd( &stBld, "-N" );
  if ( (_ulSwitches & SWFL_NO_MOTION_EVENTS) != 0 )
    _runCmdAdd( &stBld, "-m" );
  if ( (_ulSwitches & SWFL_KEEP_WIN_KEYS) != 0 )
    _runCmdAdd( &stBld, "-K" );

  if ( (_ulSwitches & SWFL_RDP5) != 0 )
    _runCmdAdd( &stBld, "-x %X", _ulRDP5Perf );

  switch( _ulEncryption )
  {
    case ENC_LOGON:
      _runCmdAdd( &stBld, "-E" );
      break;

    case ENC_NONE:
      _runCmdAdd( &stBld, "-e" );
      break;
  }

  if ( _seqDiskRedir._length != 0 )
  {
    PDISKREDIR      pDiskRedir = &_seqDiskRedir._buffer[0];

    if ( _pszDiskClient != NULL )
      _runCmdAdd( &stBld, "-r clientname=\"%s\"",
                  _runSafeStr( sizeof(acSafe), acSafe, _pszDiskClient ) );

    for( ulIdx = 0; ulIdx < _seqDiskRedir._length; ulIdx++, pDiskRedir++ )
      _runCmdAdd( &stBld, "-r disk:\"%s\"=\"%s\"",
                  _runSafeStr( sizeof(acSafe), acSafe, pDiskRedir->acName ),
                  pDiskRedir->pszPath );
  }

  for( ulIdx = 0; ulIdx < _cSerDevRedir; ulIdx++ )
    _runCmdAdd( &stBld, "-r comport:com%u=com%u",
                SDR2Remote( _aulSerDevRedir[ulIdx] ),
                SDR2Local( _aulSerDevRedir[ulIdx] ) );

  _runCmdAdd( &stBld, "-r clipboard:%s -r sound:%s -T \"%s\" -a%u -%c %s",
              aClpBrd[_ulClipboard % ARRAY_SIZE(aClpBrd)],
              aSound[_ulSound % ARRAY_SIZE(aSound)],
              _runSafeStr( sizeof(acSafe), acSafe, _wpQueryTitle( somSelf ) ),
              aulDepth[_ulColorDepth % ARRAY_SIZE(aulDepth)],
              (_ulSwitches & SWFL_RDP5) != 0 ? '5' : '4',
              _pszHost );

  if ( fSepZero )
  {
    // Command and arguments must be splitted by zero.
    *pszEndOfExe = '\0';

    // Additional zero at end.
    if ( stBld.pcEnd == stBld.pcCmd )
      longjmp( stBld.env, 1 );
    stBld.pcCmd[1] = '\0';
  }

  return stBld.pcCmd - pcBuf;
}

// Switch to (last) runned rdesktop.exe.
BOOL startSwithTo(Rdesktop *somSelf)
{
  RdesktopData  *somThis = RdesktopGetData( somSelf );
  BOOL          fSuccess = FALSE;

  if ( _pStartData != NULL )
  {
    HAB        hab = WinQueryAnchorBlock(HWND_DESKTOP);
    ULONG      cItems = WinQuerySwitchList( hab, NULL, 0 );
    ULONG      ulBufSize = ( cItems * sizeof(SWENTRY) ) + sizeof(HSWITCH);
    PSWBLOCK   pSwBlk = (PSWBLOCK)_wpAllocMem( somSelf, ulBufSize, NULL );
    ULONG      ulIdx;

    cItems = WinQuerySwitchList( hab, pSwBlk, ulBufSize );

    for( ulIdx = 0; ulIdx < pSwBlk->cswentry; ulIdx++ )
    {
      if ( pSwBlk->aswentry[ulIdx].swctl.idProcess ==
           ((PPROGDATA)_pStartData)->stRWPipe.pid )
      {
        fSuccess = WinSwitchToProgram( pSwBlk->aswentry[ulIdx].hswitch ) == 0;
        break;
      }
    }

    _wpFreeMem( somSelf, (PBYTE)pSwBlk );
  }

  return fSuccess;
}

VOID startKillAllSessions(Rdesktop *somSelf)
{
  RdesktopData  *somThis = RdesktopGetData( somSelf );
  PPROGDATA     pScan;
  ULONG         ulRC;
  BOOL          fHaveThreads;

  // Kill all rdesktop.exe sessions.

  _wpRequestObjectMutexSem( somSelf, SEM_INDEFINITE_WAIT );

  if ( _pStartData == NULL )
  {
    _wpReleaseObjectMutexSem( somSelf );
    return;
  }

  for( pScan = (PPROGDATA)_pStartData; pScan != NULL; pScan = pScan->pNext )
  {
    ulRC = DosSendSignalException( pScan->stRWPipe.pid, XCPT_SIGNAL_BREAK );
    if ( ulRC != NO_ERROR )
    {
      ulRC = DosKillProcess( DCWA_PROCESSTREE, pScan->stRWPipe.pid );
      if ( ulRC != NO_ERROR )
        debug( "DosKillProcess(%u), rc = %u", pScan->stRWPipe.pid, ulRC );
    }
  }

  _wpReleaseObjectMutexSem( somSelf );

  // Wait to end all threads.
  do
  {
    DosSleep( 10 );
    _wpRequestObjectMutexSem( somSelf, SEM_INDEFINITE_WAIT );
    fHaveThreads = _pStartData != NULL;
    _wpReleaseObjectMutexSem( somSelf );
  }
  while( fHaveThreads );
}
