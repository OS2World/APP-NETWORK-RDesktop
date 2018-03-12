#include <stdio.h>
#define INCL_WIN
#define INCL_DOSFILEMGR
#define INCL_DOSERRORS
#define INCL_GPI
#include <os2.h>
#include <util.h>
#include <diskdlg.h>
#include <debug.h>
#include <rdesktop.ih>
#include <rdrc.h>

#define WM_DISKDLG_SELPATH       (WM_USER + 1)

#define _FIRST_DRIVE             2

typedef struct _DLGDATA {
  USHORT               usSize;
  HWND                 hwndOwner;
  PDISKDLGINFO         pDlgInfo;
  ULONG                ulLevel;
  HBITMAP              hbmDrive;
  HBITMAP              hbmOpen;
  HBITMAP              hbmClosed;
  ULONG                ulMargin;
  BOOL                 fAutoSelPath;
} DLGDATA, *PDLGDATA;

static VOID _wmInitDlg(HWND hwnd, PDLGDATA pDlgData)
{
  HWND          hwndCBDrive = WinWindowFromID( hwnd, IDD_CB_DRIVE );
  HWND          hwndEFName = WinWindowFromID( hwnd, IDD_EF_NAME );
  HWND          hwndEFPath = WinWindowFromID( hwnd, IDD_EF_LOCAL_PATH );
  ULONG         ulIdx;
  ULONG         ulDiskCur, ulDiskMap;
  FSINFO        stFSInfo;
  CHAR          acBuf[261];

  WinSetWindowPtr( hwnd, QWL_USER, pDlgData );
  pDlgData->fAutoSelPath = FALSE;
  utilStrTrim( pDlgData->pDlgInfo->szName );
  utilStrTrim( pDlgData->pDlgInfo->szPath );

  // Set window title.
  if ( *pDlgData->pDlgInfo->szName != '\0' )
  {
    ULONG    cbBuf;

    cbBuf = WinQueryWindowText( hwnd, sizeof(acBuf) - 1, acBuf );
    if ( _snprintf( &acBuf[cbBuf], sizeof(acBuf) - cbBuf - 1, ": %s",
         pDlgData->pDlgInfo->szName ) != -1 )
      WinSetWindowText( hwnd, acBuf );
  }

  // Set remote disk name and local path at entry fields.
  WinSetFocus( HWND_DESKTOP,
               *pDlgData->pDlgInfo->szName != '\0' ? hwndEFPath : hwndEFName );
  WinSetWindowText( hwndEFName, pDlgData->pDlgInfo->szName );
  WinEnableWindow( hwndEFName, pDlgData->pDlgInfo->fEditableName );
  WinSetWindowText( hwndEFPath, pDlgData->pDlgInfo->szPath );

  // Drives list.

  // Query system existing drives.
  if ( DosQueryCurrentDisk( &ulDiskCur, &ulDiskMap ) != NO_ERROR )
    return;

  for( ulIdx = _FIRST_DRIVE; ulIdx < 26; ulIdx++ )
  {
    if ( (( ulDiskMap << ( 31 - ulIdx ) ) >> 31) == 0 )
      continue;

    // Drive letter.
    *((PULONG)acBuf) = (ULONG)'\0\0:A' + ulIdx;
    // Drive label.
    if ( ( ulIdx > 1 ) &&
         ( DosQueryFSInfo( ulIdx + 1, FSIL_VOLSER, &stFSInfo, sizeof(FSINFO) )
             == NO_ERROR ) )
      sprintf( &acBuf[2], " [%s]", stFSInfo.vol.szVolLabel );

    WinInsertLboxItem( hwndCBDrive, LIT_END, acBuf );
  }

  WinPostMsg( hwnd, WM_DISKDLG_SELPATH, 0, 0 );
}

static ULONG __addDir(HWND hwndLBDir, PSZ pszName, ULONG ulLevel)
{
  SHORT      sIdx;

  if ( ( *((PUSHORT)pszName) == (USHORT)'\0.' ) ||
       ( *((PUSHORT)pszName) == (USHORT)'..' && pszName[2] == '\0' ) )
    return 0;

  sIdx = WinInsertLboxItem( hwndLBDir, LIT_END, pszName );
  switch( sIdx )
  {
    case LIT_MEMERROR:
    case LIT_ERROR:
      return 0;
    default:
      WinSendMsg( hwndLBDir, LM_SETITEMHANDLE, MPFROMSHORT(sIdx),
                  MPFROMLONG(ulLevel) );
  }

  return 1;
}

static ULONG __addDirList(HWND hwndLBDir, PSZ pszPath)
{
  HDIR         hDir = HDIR_CREATE;
  FILEFINDBUF3 stFind;
  ULONG        cItems, ulRC;
  ULONG        ulLevel;
  ULONG        ulResult;
  
  cItems = (SHORT)WinSendMsg( hwndLBDir, LM_QUERYITEMCOUNT, 0, 0 );
  if ( cItems > 0 )
    ulLevel = (ULONG)WinSendMsg( hwndLBDir, LM_QUERYITEMHANDLE,
                                 MPFROMSHORT(cItems - 1), 0 ) + 1;

  cItems = 1;
  ulRC = DosFindFirst( pszPath, &hDir, MUST_HAVE_DIRECTORY, &stFind,
                       sizeof(stFind), &cItems, FIL_STANDARD );
  if ( ulRC == NO_ERROR )
  {
    ulResult = __addDir( hwndLBDir, stFind.achName, ulLevel );

    while( TRUE )
    {
      cItems = 1;
      ulRC = DosFindNext( hDir, &stFind, sizeof(stFind), &cItems );
      if ( ulRC != NO_ERROR )
        break;

      ulResult += __addDir( hwndLBDir, stFind.achName, ulLevel );
    }

    DosFindClose( hDir );
  }
  else
    ulResult = 0;

  return ulResult;
}

static VOID _ctlDriveSelect(HWND hwnd, HWND hwndCBDrive)
{
  PDLGDATA     pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  HWND         hwndLBDir;
  SHORT        sSelIdx;
  CHAR         acBuf[261];

  if ( pDlgData->fAutoSelPath )
    return;

  sSelIdx = (SHORT)WinSendMsg( hwndCBDrive, LM_QUERYSELECTION,
                               MPFROMSHORT(LIT_FIRST), 0 );

  if ( (SHORT)WinSendMsg( hwndCBDrive, LM_QUERYITEMTEXT,
                          MPFROM2SHORT(sSelIdx,sizeof(acBuf)), acBuf ) <= 0 )
    return;

  pDlgData->ulLevel = 0;

  hwndLBDir = WinWindowFromID( hwnd, IDD_LB_DIR );
  WinEnableWindowUpdate( hwndLBDir, FALSE );

  WinSendMsg( hwndLBDir, LM_DELETEALL, 0, 0 );

  *((PULONG)&acBuf[2]) = (ULONG)'\0\0\0\\';
  __addDir( hwndLBDir, acBuf, 0 );

  *((PULONG)&acBuf[2]) = (ULONG)'\0\0*\\';
  __addDirList( hwndLBDir, acBuf );

  WinEnableWindowUpdate( hwndLBDir, TRUE );
}

static VOID _ctlDirEnter(HWND hwnd, HWND hwndLBDir)
{
  PDLGDATA     pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  SHORT        sIdx;
  CHAR         acBuf[512];
  ULONG        cbBuf = 0;
  SHORT        sItem = (SHORT)WinSendMsg( hwndLBDir, LM_QUERYSELECTION,
                                          MPFROMSHORT( LIT_FIRST ), 0 );
  if ( sItem == LIT_NONE )
    return;

  pDlgData->ulLevel = (ULONG)WinSendMsg( hwndLBDir, LM_QUERYITEMHANDLE,
                                         MPFROMSHORT(sItem), 0 );
  // Make path at buffer.
  for( sIdx = 0; sIdx <= sItem; sIdx++ )
  {
    if ( ( (ULONG)WinSendMsg( hwndLBDir, LM_QUERYITEMHANDLE,
                              MPFROMSHORT(sIdx), 0 ) == pDlgData->ulLevel ) &&
         ( sItem != sIdx ) )
      continue;

    if ( ( cbBuf != 0 ) && ( acBuf[cbBuf-1] != '\\' ) )
    {
      acBuf[cbBuf] = '\\';
      cbBuf++;
    }

    cbBuf += (ULONG)WinSendMsg( hwndLBDir, LM_QUERYITEMTEXT,
                                MPFROM2SHORT( sIdx, sizeof(acBuf)-4 - cbBuf ),
                                MPFROMP( &acBuf[cbBuf] ) );
  }
  // Set path string to entry field.
  WinSetWindowText( WinWindowFromID( hwnd, IDD_EF_LOCAL_PATH ), acBuf );

  // Change list of directories.

  WinEnableWindowUpdate( hwndLBDir, FALSE );

  // Remove all items from this level (except selected item).
  for( sIdx = (SHORT)WinSendMsg( hwndLBDir, LM_QUERYITEMCOUNT, 0, 0 ) - 1;
       sIdx > 0; sIdx-- )
  {
    if ( (ULONG)WinSendMsg( hwndLBDir, LM_QUERYITEMHANDLE,
                              MPFROMSHORT(sIdx), 0 ) < pDlgData->ulLevel )
      break;

    if ( sItem != sIdx )
      WinSendMsg( hwndLBDir, LM_DELETEITEM, MPFROMSHORT( sIdx ), 0 );
  }

  *((PULONG)&acBuf[cbBuf]) = ( cbBuf != 0 ) && ( acBuf[cbBuf-1] != '\\' )
                             ? (ULONG)'\0\0*\\' : (ULONG)'\0\0\0*';
  __addDirList( hwndLBDir, acBuf );

  WinEnableWindowUpdate( hwndLBDir, TRUE );
}

static MRESULT _wmMeasureItem(HWND hwnd, SHORT sItem)
{
  PDLGDATA   pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  HWND       hwndLB = WinWindowFromID( hwnd, IDD_LB_DIR );
  CHAR       acBuf[256];
  ULONG      cbBuf;
  POINTL     aptText[TXTBOX_COUNT] = { 0 };
  BOOL       fRC;
  HPS        hps;
  ULONG      ulWidth, ulHeight;
  ULONG      ulLevel = (ULONG)WinSendMsg( hwndLB, LM_QUERYITEMHANDLE,
                                          MPFROMSHORT(sItem), 0 );

  cbBuf = (ULONG)WinSendMsg( hwndLB, LM_QUERYITEMTEXT,
                      MPFROM2SHORT( sItem, sizeof(acBuf) ), MPFROMP( acBuf ) );

  hps = WinGetPS( hwndLB );
  fRC = GpiQueryTextBox( hps, cbBuf, acBuf, TXTBOX_COUNT, &aptText );
  WinReleasePS( hps );

  ulWidth = ( aptText[TXTBOX_BOTTOMRIGHT].x - aptText[TXTBOX_BOTTOMLEFT].x );
  ulHeight = aptText[TXTBOX_BOTTOMLEFT].y - aptText[TXTBOX_BOTTOMRIGHT].y;

  if ( pDlgData != NULL )
  {
    BITMAPINFOHEADER stBmpInf;

    // Get image size.
    stBmpInf.cbFix = sizeof(BITMAPINFOHEADER);
    GpiQueryBitmapParameters( ulLevel == 0
                                ? pDlgData->hbmDrive
                                : ulLevel <= pDlgData->ulLevel
                                    ? pDlgData->hbmOpen : pDlgData->hbmClosed,
                              &stBmpInf );

    ulWidth += (pDlgData->ulMargin * ulLevel) + stBmpInf.cx +
               pDlgData->ulMargin;

    if ( ulHeight < stBmpInf.cy )
      ulHeight = stBmpInf.cy;
  }

  return (MRESULT)( (ulWidth << 16) | ulHeight );
}

static VOID _wmDrawItem(HWND hwnd, POWNERITEM pItem)
{
  PDLGDATA   pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  CHAR       acBuf[256];
  ULONG      cbBuf;
  LONG       lBgColor, lColor;
  POINTL     pt;
  RECTL      rectl;
  ULONG      ulLevel = (ULONG)WinSendMsg( pItem->hwnd, LM_QUERYITEMHANDLE,
                                          MPFROMSHORT(pItem->idItem), 0 );
  BITMAPINFOHEADER stBmpInf = { 0 };
  HBITMAP    hBitmap = ulLevel == 0
                         ? pDlgData->hbmDrive
                         : ulLevel <= pDlgData->ulLevel
                             ? pDlgData->hbmOpen : pDlgData->hbmClosed;

  // Get image size.
  stBmpInf.cbFix = sizeof(BITMAPINFOHEADER);
  GpiQueryBitmapParameters( hBitmap, &stBmpInf );

  cbBuf = (ULONG)WinSendMsg( pItem->hwnd, LM_QUERYITEMTEXT,
              MPFROM2SHORT( pItem->idItem, sizeof(acBuf) ), MPFROMP( &acBuf ) );

  if ( pItem->fsState )
  {
    lBgColor = SYSCLR_MENUHILITEBGND;
    lColor = SYSCLR_MENUHILITE;
  }
  else
  {
    lBgColor = GpiQueryBackColor( pItem->hps );
    lColor = SYSCLR_MENUTEXT;
  }

  WinFillRect( pItem->hps, &pItem->rclItem, lBgColor );

  rectl = pItem->rclItem;
  rectl.xLeft += pDlgData->ulMargin * ulLevel;

  pt.x = rectl.xLeft;
  pt.y = rectl.yBottom + ( (rectl.yTop - rectl.yBottom) - stBmpInf.cy ) / 2;
  WinDrawBitmap( pItem->hps, hBitmap, NULL, &pt, lColor, lBgColor, DBM_NORMAL );

  rectl.xLeft += stBmpInf.cx + pDlgData->ulMargin;

  WinDrawText( pItem->hps, cbBuf, acBuf, &rectl, lColor, lBgColor,
               DT_LEFT | DT_VCENTER | DT_ERASERECT );

  pItem->fsState = pItem->fsStateOld = FALSE;
}

// Chooses drive from drives list and fills list of directories by
// local path specified for this dialog.
static VOID _wmDiskDlgSelPath(HWND hwnd)
{
  PDLGDATA   pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  HWND       hwndCBDrive = WinWindowFromID( hwnd, IDD_CB_DRIVE );
  HWND       hwndLBDir = WinWindowFromID( hwnd, IDD_LB_DIR );
  PSZ        pszPath = pDlgData->pDlgInfo->szPath;
  ULONG      cbPath = strlen( pszPath );
  PCHAR      pcEnd;
  SHORT      sIdx;
  CHAR       acBuf[512];
  SHORT      cbBuf;
  ULONG      cItems;

  if ( ( cbPath < 2 ) || ( pszPath[1] != ':' ) )
    return;

  for( sIdx = 0; ; sIdx++ )
  {
    cbBuf = (SHORT)WinSendMsg( hwndCBDrive, LM_QUERYITEMTEXT,
                               MPFROM2SHORT( sIdx, sizeof(acBuf) ),
                               MPFROMP( acBuf ) );
    if ( cbBuf == 0 )
      return;

    if ( acBuf[0] == toupper( pszPath[0] ) )
      break;
  }
 
  pDlgData->fAutoSelPath = TRUE;
  WinSendMsg( hwndCBDrive, LM_SELECTITEM, MPFROMSHORT(sIdx), MPFROMLONG(TRUE) );

  // Add drive (D:\) to the list.
  *((PULONG)&acBuf[1]) = '\0\0\\:';
  cItems = __addDir( hwndLBDir, acBuf, 0 );
  pDlgData->ulLevel = 0;
  // Go to the begin of first directory name.
  pcEnd = &pszPath[2];

  if ( cItems != 0 )
  {
    // Add directories from the path.
    while( TRUE )
    {
      pszPath = pcEnd;
      while( *pszPath == '\\' )
        pszPath++;

      pcEnd = strchr( pszPath, '\\' );
      if ( pcEnd == NULL )
      {
        pcEnd = strchr( pszPath, '\0' );
        if ( pcEnd == pszPath ) // End of path
          break;
      }

      cbPath = pcEnd - pDlgData->pDlgInfo->szPath;
      memcpy( acBuf, pDlgData->pDlgInfo->szPath, cbPath );
      acBuf[cbPath] = '\0';

      cItems = __addDirList( hwndLBDir, acBuf );
      pDlgData->ulLevel++;

      while( *pcEnd == '\\' )
        pcEnd++;
    }

    if ( cItems != 0 )
    {
      // Add list of directories in last (open) directory from the path.
      strcat( acBuf, pDlgData->ulLevel == 0 ? "*" : "\\*" );
      __addDirList( hwndLBDir, acBuf );
      // Select last (open) directory.
      WinSendMsg( hwndLBDir, LM_SELECTITEM, MPFROMSHORT(pDlgData->ulLevel),
                  MPFROMLONG(TRUE) );
    }
  }

  pDlgData->fAutoSelPath = FALSE;
}

static BOOL _cmdPBOk(HWND hwnd)
{
  PDLGDATA    pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  ULONG       ulRC;
  FILESTATUS3 stInfo;

  if ( ( WinQueryDlgItemText( hwnd, IDD_EF_NAME, DISKDLG_MAX_NAME,
                              pDlgData->pDlgInfo->szName ) == 0 ) ||
       ( WinQueryDlgItemText( hwnd, IDD_EF_LOCAL_PATH, DISKDLG_MAX_PATH,
                              pDlgData->pDlgInfo->szPath ) == 0 ) )
    return FALSE;

  utilStrTrim( pDlgData->pDlgInfo->szName );
  utilStrTrim( pDlgData->pDlgInfo->szPath );

  ulRC = DosQueryPathInfo( pDlgData->pDlgInfo->szPath, FIL_STANDARD, &stInfo,
                           sizeof(stInfo) );
  if ( ( ulRC != NO_ERROR ) || (stInfo.attrFile & FILE_DIRECTORY) == 0 )
  {
    if ( utilMessageBox( hwnd, NULL, IDM_PATH_NOT_EXIST,
                         MB_APPLMODAL | MB_ICONQUESTION | MB_MOVEABLE |
                         MB_YESNO | MB_DEFBUTTON2 ) == MBID_NO )
      return FALSE;
  }

  return (BOOL)WinSendMsg( pDlgData->hwndOwner, WM_DISKDLG_CHECK,
                           MPFROMLONG(hwnd), MPFROMP(pDlgData->pDlgInfo) );
}

static VOID _ctlEFChanged(HWND hwnd)
{
  CHAR       acBuf[361];
  BOOL       fOkBtnEnable =
   ( WinQueryDlgItemTextLength( hwnd, IDD_EF_NAME ) != 0 ) &&
   ( WinQueryDlgItemText( hwnd, IDD_EF_LOCAL_PATH, DISKDLG_MAX_PATH, acBuf )
       > 1 ) && isalpha( acBuf[0] ) && acBuf[1] == ':';

  WinEnableWindow( WinWindowFromID( hwnd, MBID_OK ), fOkBtnEnable );
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      _wmInitDlg( hwnd, (PDLGDATA)mp2 );
      return (MRESULT)TRUE;

    case WM_MEASUREITEM:
      if ( SHORT1FROMMP(mp1) != IDD_LB_DIR )
        break;
      return _wmMeasureItem( hwnd, SHORT1FROMMP(mp2) );

    case WM_DRAWITEM:
      _wmDrawItem( hwnd, (POWNERITEM)PVOIDFROMMP( mp2 ) );
      return (MRESULT)TRUE;

    case WM_DISKDLG_SELPATH:
      _wmDiskDlgSelPath( hwnd );
      return (MRESULT)TRUE;

    case WM_CONTROL:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDD_CB_DRIVE:
          if ( SHORT2FROMMP( mp1 ) == LN_SELECT )
            _ctlDriveSelect( hwnd, (HWND)mp2 );
          break;

        case IDD_LB_DIR:
          if ( SHORT2FROMMP( mp1 ) == LN_ENTER )
            _ctlDirEnter( hwnd, (HWND)mp2 );
          break;

        case IDD_EF_NAME:
        case IDD_EF_LOCAL_PATH:
          if ( SHORT2FROMMP( mp1 ) == EN_CHANGE )
            _ctlEFChanged( hwnd );
          break;
      }
      return (MRESULT)FALSE;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case MBID_OK:
          if ( !_cmdPBOk( hwnd ) )
            return (MRESULT)FALSE;
          break;
      }
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}

BOOL diskDlg(HWND hwndOwner, PDISKDLGINFO pDlgInfo)
{
  DLGDATA    stDlgData;
  HWND       hwnd;
  ULONG      ulRC;

  stDlgData.usSize        = sizeof(DLGDATA);
  stDlgData.hwndOwner     = hwndOwner;
  stDlgData.pDlgInfo      = pDlgInfo;
  stDlgData.ulLevel       = 0;
  stDlgData.hbmDrive      = WinGetSysBitmap( HWND_DESKTOP, SBMP_DRIVE );
  stDlgData.hbmOpen       = WinGetSysBitmap( HWND_DESKTOP, SBMP_TREEMINUS );
  stDlgData.hbmClosed     = WinGetSysBitmap( HWND_DESKTOP, SBMP_TREEPLUS );
  stDlgData.ulMargin      = 5;

  hwnd = WinLoadDlg( HWND_DESKTOP, hwndOwner, _dlgProc, utilGetModuleHandle(),
                     IDDLG_DISK, &stDlgData );

  if ( hwnd == NULLHANDLE )
    return FALSE;

  ulRC = WinProcessDlg( hwnd );
  WinDestroyWindow( hwnd );

  return ulRC == MBID_OK;
}
