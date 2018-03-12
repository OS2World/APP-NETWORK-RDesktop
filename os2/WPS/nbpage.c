#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#define INCL_DOS
#define INCL_WIN
#define INCL_ERRORS
#include <os2.h>

// Make access to member vars available.
#include <rdesktop.ih>

#include <rdrc.h>
#include <util.h>
#include <diskdlg.h>

#define GET_NEW_ITEM_TEXT(__pszVar,__hwnd,__item) do { \
  if ( __pszVar != NULL ) _wpFreeMem( somSelf, __pszVar ); \
  __pszVar = _winGetText( somSelf, WinWindowFromID(__hwnd, __item) ); \
} while(0)

#define NEW_STRING(__pszNew,__pszStr) do { \
  __pszNew = (string)_wpAllocMem( somSelf, strlen( __pszStr ) + 1, NULL ); \
  if ( __pszNew != NULL ) strcpy( __pszNew, __pszStr ); \
} while(0)

// PSZ _winGetText(SOMObject *somSelf, HWND hwnd)
//
// Copies window text into a allocated buffer. Removes leading and trailing
// spaces from the text.
// Returns pointer to the new string. Pointer must be destroyed by _wpFreeMem().
PSZ _winGetText(SOMObject *somSelf, HWND hwnd)
{
  PSZ        pszText;
  ULONG      cbText;
  PCHAR      pcText, pcEnd;

  cbText = WinQueryWindowTextLength( hwnd );
  if ( cbText == 0 )
    return NULL;
  pcText = alloca( cbText + 1 );
  if ( pszText == NULL )
    return NULL;

  WinQueryWindowText( hwnd, cbText + 1, pcText );
  pcEnd = &pcText[cbText];
  // Remove leading and trailing spaces.
  while( ( pcEnd > pcText ) && isspace( *(pcEnd-1) ) ) pcEnd--;
  while( ( pcEnd > pcText ) && isspace( *pcText ) ) pcText++;
  if ( pcEnd == pcText )
    return NULL;

  cbText = pcEnd - pcText;
  pszText = (PSZ)_wpAllocMem( somSelf, cbText + 1, NULL );
  if ( pszText != NULL )
  {
    memcpy( pszText, pcText, cbText );
    pszText[cbText] = '\0';
  }

  return pszText;
}

// Page "Server".
// --------------

static const ULONG   aSizeModeRBId[] =
  { IDD_RB_ABSOLUTE, IDD_RB_PROPORTIONAL, IDD_RB_FULLSCREEN };

static VOID _serverUndo(HWND hwnd, RdesktopData *somThis)
{
  WinSetDlgItemText( hwnd, IDD_EF_HOST, _pszHost );
  WinSetDlgItemText( hwnd, IDD_EF_USER, _pszUser );
  WinSetDlgItemText( hwnd, IDD_EF_DOMAIN, _pszDomain );
  WinSetDlgItemText( hwnd, IDD_EF_PASSWORD, _pszPassword );
  WinEnableWindow( WinWindowFromID( hwnd, IDD_EF_PASSWORD ), !_fLoginPrompt );
  WinCheckButton( hwnd, IDD_CB_PROMPT, _fLoginPrompt );
  WinCheckButton( hwnd, aSizeModeRBId[_ulSizeMode % ARRAY_SIZE(aSizeModeRBId)],
                  1 );
  WinSendDlgItemMsg( hwnd, IDD_SB_WIDTH, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(_lWidth), MPFROMLONG(0) );
  WinSendDlgItemMsg( hwnd, IDD_SB_HEIGHT, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(_lHeight), MPFROMLONG(0) );
  WinSendDlgItemMsg( hwnd, IDD_SB_PROPSIZE, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(_ulProportionalSize), MPFROMLONG(0) );
  if ( _ulColorDepth > 4 )
    _ulColorDepth = 4;
  WinSendDlgItemMsg( hwnd, IDD_SB_DEPTH, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(_ulColorDepth), 0 );
}

static VOID _serverDefault(HWND hwnd)
{
  WinSetDlgItemText( hwnd, IDD_EF_HOST, "" );
  WinSetDlgItemText( hwnd, IDD_EF_USER, "" );
  WinSetDlgItemText( hwnd, IDD_EF_DOMAIN, "" );
  WinSetDlgItemText( hwnd, IDD_EF_PASSWORD, "" );
  WinCheckButton( hwnd, IDD_CB_PROMPT, DEF_PROMPT );
  WinSendDlgItemMsg( hwnd, aSizeModeRBId[DEF_SIZEMODE], BM_SETCHECK,
                     MPFROMSHORT( 1 ), 0 );
  WinSendDlgItemMsg( hwnd, IDD_SB_WIDTH, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(DEF_WIDTH), MPFROMLONG(0) );
  WinSendDlgItemMsg( hwnd, IDD_SB_HEIGHT, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(DEF_HEIGHT), MPFROMLONG(0) );
  WinSendDlgItemMsg( hwnd, IDD_SB_PROPSIZE, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(DEF_PROPSIZE), MPFROMLONG(0) );
  WinSendDlgItemMsg( hwnd, IDD_SB_DEPTH, SPBM_SETCURRENTVALUE,
                     MPFROMLONG(DEF_DEPTH), 0 );
}

static BOOL _serverInitDlg(HWND hwnd, MPARAM mp2)
{
  Rdesktop      *somSelf = PVOIDFROMMP(mp2);
  RdesktopData  *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );
  const PSZ     apszDepth[] = { " 8", "15", "16", "24", "32" };

  // Store object to window pointer.
  WinSetWindowPtr( hwnd, QWL_USER, somSelf );
  if ( somThis == NULL )
    return FALSE;

  // Setup controls.

  WinSendDlgItemMsg( hwnd, IDD_SB_WIDTH, SPBM_SETLIMITS,
                     MPFROMLONG(4096), MPFROMLONG(100));
  WinSendDlgItemMsg( hwnd, IDD_SB_HEIGHT, SPBM_SETLIMITS,
                     MPFROMLONG(4096), MPFROMLONG(100));
  WinSendDlgItemMsg( hwnd, IDD_SB_PROPSIZE, SPBM_SETLIMITS,
                     MPFROMLONG(100), MPFROMLONG(PROPSIZE_MIN));
  WinSendDlgItemMsg( hwnd, IDD_SB_DEPTH, SPBM_SETARRAY,
                     MPFROMP(apszDepth), MPFROMLONG(ARRAY_SIZE(apszDepth)) );

  // Initialize controls.
  _serverUndo( hwnd, somThis );

  return TRUE;
}

MRESULT EXPENTRY nbPageServer(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  Rdesktop      *somSelf = WinQueryWindowPtr( hwnd, QWL_USER );
  RdesktopData  *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );

  switch( msg )
  {
    case WM_INITDLG:
      if ( _serverInitDlg( hwnd, mp2 ) )
        return (MRESULT)FALSE;
      break;

    case WM_DESTROY:
      if ( somThis != NULL )
      {
        CHAR           acBuf[8];

        GET_NEW_ITEM_TEXT( _pszHost, hwnd, IDD_EF_HOST );
        GET_NEW_ITEM_TEXT( _pszUser, hwnd, IDD_EF_USER );
        GET_NEW_ITEM_TEXT( _pszDomain, hwnd, IDD_EF_DOMAIN );
        GET_NEW_ITEM_TEXT( _pszPassword, hwnd, IDD_EF_PASSWORD );

        _fLoginPrompt = WinQueryButtonCheckstate( hwnd, IDD_CB_PROMPT ) != 0;

        _ulSizeMode =
           WinSendDlgItemMsg( hwnd, IDD_RB_FULLSCREEN,
                              BM_QUERYCHECK, 0, 0 ) != 0
           ? SIZEMODE_FULLSCREEN
           : ( WinSendDlgItemMsg( hwnd, IDD_RB_PROPORTIONAL,
                                    BM_QUERYCHECK, 0, 0 ) != 0
               ? SIZEMODE_PROPORTIONAL : SIZEMODE_ABSOLUTE );

        WinSendDlgItemMsg( hwnd, IDD_SB_WIDTH, SPBM_QUERYVALUE,
                           MPFROMP(&_lWidth), 0);
        WinSendDlgItemMsg( hwnd, IDD_SB_HEIGHT, SPBM_QUERYVALUE,
                           MPFROMP(&_lHeight), 0);
        WinSendDlgItemMsg( hwnd, IDD_SB_PROPSIZE, SPBM_QUERYVALUE,
                           MPFROMP(&_ulProportionalSize), 0);

        if ( WinSendDlgItemMsg( hwnd, IDD_SB_DEPTH, SPBM_QUERYVALUE,
               MPFROMP(&acBuf), MPFROM2SHORT(sizeof(acBuf),SPBQ_DONOTUPDATE) ) )
        {
          switch( *((PUSHORT)&acBuf[0]) )
          {
            case (USHORT)'8 ': _ulColorDepth = 0; break; //  8
            case (USHORT)'51': _ulColorDepth = 1; break; // 15
            case (USHORT)'61': _ulColorDepth = 2; break; // 16
            case (USHORT)'42': _ulColorDepth = 3; break; // 24
            case (USHORT)'23': _ulColorDepth = 4; break; // 32
          };
        }

        _wpSaveDeferred( somSelf );
      }
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDD_PB_UNDO:
          _serverUndo( hwnd, somThis );
          break;

        case IDD_PB_DEFAULT:
          _serverDefault( hwnd );
          break;
      }
      return (MRESULT)FALSE;

    case WM_CONTROL:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDD_CB_PROMPT:
          WinEnableWindow( WinWindowFromID( hwnd, IDD_EF_PASSWORD ),
                    WinQueryButtonCheckstate( hwnd, IDD_CB_PROMPT ) == 0 );
          break;
      }
      return (MRESULT)FALSE;

  } // end switch

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


// Page "Protocol" 1/2.
// --------------------

static VOID _protocol1VerSel(HWND hwnd, ULONG ulId)
{
  BOOL         fRDP5 = ulId == IDD_RB_RDP5;
  ULONG        ulIdx;
  const ULONG  aulCBId[] = {IDD_CB_NO_WALLPAPER, IDD_CB_NO_FULLWINDOWDRAG,
                            IDD_CB_NO_MENUANIMATIONS, IDD_CB_NO_THEMING,
//                            IDD_CB_NO_CURSOR_SHADOW, IDD_CB_NO_CURSORSETTINGS
                            IDD_CB_ANTIALIASING
                           };

  for( ulIdx = 0; ulIdx < ARRAY_SIZE(aulCBId); ulIdx++ )
    WinEnableWindow( WinWindowFromID( hwnd, aulCBId[ulIdx] ), fRDP5 );
}

static VOID _protocol1Set(HWND hwnd, PSZ pszClientHost, PSZ pszLocalCP,
                          ULONG ulEncryption, ULONG ulSwitches,
                          ULONG ulRDP5Perf )
{
  ULONG        ulVerId = (ulSwitches & SWFL_RDP5) != 0
                           ? IDD_RB_RDP5 : IDD_RB_RDP4;

  WinSetDlgItemText( hwnd, IDD_EF_CLIENT_HOST, pszClientHost );
  WinSetDlgItemText( hwnd, IDD_EF_LOCAL_CP, pszLocalCP );

  WinSendMsg( WinWindowFromID( hwnd, IDD_CB_ENCRYPTION ), LM_SELECTITEM,
              MPFROMSHORT( ulEncryption ), MPFROMLONG(TRUE) );

  WinSendDlgItemMsg( hwnd, ulVerId,
                     BM_SETCHECK, MPFROMSHORT( 1 ), 0 );
  _protocol1VerSel( hwnd, ulVerId );

  WinCheckButton( hwnd, IDD_CB_NO_WALLPAPER,
                  (ulRDP5Perf & PERFFL_NO_WALLPAPER) != 0 );
  WinCheckButton( hwnd, IDD_CB_NO_FULLWINDOWDRAG,
                  (ulRDP5Perf & PERFFL_NO_FULLWINDOWDRAG) != 0 );
  WinCheckButton( hwnd, IDD_CB_NO_MENUANIMATIONS,
                  (ulRDP5Perf & PERFFL_NO_MENUANIMATIONS) != 0 );
  WinCheckButton( hwnd, IDD_CB_NO_THEMING,
                  (ulRDP5Perf & PERFFL_NO_THEMING) != 0 );
/*  WinCheckButton( hwnd, IDD_CB_NO_CURSOR_SHADOW,
                  (ulRDP5Perf & PERFFL_NO_CURSOR_SHADOW) != 0 );*/
  WinCheckButton( hwnd, IDD_CB_ANTIALIASING,
                  (ulRDP5Perf & PERFFL_ANTIALIASING) != 0 );
}

static VOID _protocol1Undo(HWND hwnd, RdesktopData *somThis)
{
  _protocol1Set( hwnd, _pszClientHost, _pszLocalCP, _ulEncryption, _ulSwitches,
                 _ulRDP5Perf );
}

static BOOL _protocol1InitDlg(HWND hwnd, MPARAM mp2)
{
  Rdesktop      *somSelf = PVOIDFROMMP(mp2);
  RdesktopData  *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );
  HWND          hwndEncr = WinWindowFromID( hwnd, IDD_CB_ENCRYPTION );
  HMODULE       hModule = utilGetModuleHandle();
  CHAR          acBuf[128];
  ULONG         ulIdx;
  HAB           hab = WinQueryAnchorBlock( HWND_DESKTOP );
  const ULONG   aulEncrStrId[] = { IDS_ENC_FULL, IDS_ENC_NONE/*, IDS_ENC_LOGON*/ };
  // It seems, lonon-only encryption is bugly in rdesktop.

  // Store object to window pointer.
  WinSetWindowPtr( hwnd, QWL_USER, somSelf );
  if ( somThis == NULL )
    return FALSE;

  // List of enctryption modes.
  for( ulIdx = 0; ulIdx < ARRAY_SIZE(aulEncrStrId); ulIdx++ )
  {
    WinLoadString( hab, hModule, aulEncrStrId[ulIdx], sizeof(acBuf), acBuf );
    WinInsertLboxItem( hwndEncr, LIT_END, acBuf );
  }

  // Initialize controls.
  _protocol1Undo( hwnd, somThis );

  return TRUE;
}

MRESULT EXPENTRY nbPageProtocol1(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  Rdesktop      *somSelf = WinQueryWindowPtr( hwnd, QWL_USER );
  RdesktopData  *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );

  switch( msg )
  {
    case WM_INITDLG:
      if ( _protocol1InitDlg( hwnd, mp2 ) )
        return (MRESULT)FALSE;
      break;

    case WM_DESTROY:
      if ( somThis != NULL )
      {
        GET_NEW_ITEM_TEXT( _pszClientHost, hwnd, IDD_EF_CLIENT_HOST );
        GET_NEW_ITEM_TEXT( _pszLocalCP, hwnd, IDD_EF_LOCAL_CP );

        _ulEncryption = (SHORT)WinSendMsg(
                              WinWindowFromID( hwnd, IDD_CB_ENCRYPTION ),
                              LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ), 0 );

        if ( WinQueryButtonCheckstate( hwnd, IDD_RB_RDP5 ) != 0 )
          _ulSwitches |= SWFL_RDP5;
        else
          _ulSwitches &= ~SWFL_RDP5;

        _ulRDP5Perf = 0;

        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_NO_WALLPAPER ) != 0 )
          _ulRDP5Perf |= PERFFL_NO_WALLPAPER;
        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_NO_FULLWINDOWDRAG ) != 0 )
          _ulRDP5Perf |= PERFFL_NO_FULLWINDOWDRAG;
        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_NO_MENUANIMATIONS ) != 0 )
          _ulRDP5Perf |= PERFFL_NO_MENUANIMATIONS;
        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_NO_THEMING ) != 0 )
          _ulRDP5Perf |= PERFFL_NO_THEMING;
/*        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_NO_CURSOR_SHADOW ) != 0 )
          _ulRDP5Perf |= PERFFL_NO_CURSOR_SHADOW;*/
        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_ANTIALIASING ) != 0 )
          _ulRDP5Perf |= PERFFL_ANTIALIASING;

        _wpSaveDeferred( somSelf );
      }
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDD_PB_UNDO:
          _protocol1Undo( hwnd, somThis );
          break;

        case IDD_PB_DEFAULT:
          _protocol1Set( hwnd, NULL, NULL, 0, DEF_SWITCHES, DEF_RDP5PERF );
          break;
      }
      return (MRESULT)FALSE;

    case WM_CONTROL:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDD_RB_RDP4:
        case IDD_RB_RDP5:
          _protocol1VerSel( hwnd, SHORT1FROMMP( mp1 ) );
          break;
      }
      return (MRESULT)FALSE;

  } // end switch

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


// Page "Protocol" 2/2.
// --------------------

static VOID _protocol2Set(HWND hwnd, PSZ pszClientHost, PSZ pszLocalCP,
                          ULONG ulSwitches)
{
  WinCheckButton( hwnd, IDD_CB_COMPRESSION,
                  (ulSwitches & SWFL_COMPRESSION) != 0 );
  WinCheckButton( hwnd, IDD_CB_PRES_BMP_CACHING,
                  (ulSwitches & SWFL_PRES_BMP_CACHING) != 0 );
  WinCheckButton( hwnd, IDD_CB_FORCE_BMP_UPD,
                  (ulSwitches & SWFL_FORCE_BMP_UPD) != 0 );
  WinCheckButton( hwnd, IDD_CB_NUMLOCK_SYNC,
                  (ulSwitches & SWFL_NUMLOCK_SYNC) != 0 );
  WinCheckButton( hwnd, IDD_CB_NO_MOTION_EVENTS,
                  (ulSwitches & SWFL_NO_MOTION_EVENTS) != 0 );
  WinCheckButton( hwnd, IDD_CB_KEEP_WIN_KEYS,
                  (ulSwitches & SWFL_KEEP_WIN_KEYS) != 0 );
}

static VOID _protocol2Undo(HWND hwnd, RdesktopData *somThis)
{
  _protocol2Set( hwnd, _pszClientHost, _pszLocalCP, _ulSwitches );
}

static BOOL _protocol2InitDlg(HWND hwnd, MPARAM mp2)
{
  Rdesktop      *somSelf = PVOIDFROMMP(mp2);
  RdesktopData  *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );

  // Store object to window pointer.
  WinSetWindowPtr( hwnd, QWL_USER, somSelf );
  if ( somThis == NULL )
    return FALSE;

  // Initialize controls.
  _protocol2Undo( hwnd, somThis );

  return TRUE;
}

MRESULT EXPENTRY nbPageProtocol2(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  Rdesktop      *somSelf = WinQueryWindowPtr( hwnd, QWL_USER );
  RdesktopData  *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );

  switch( msg )
  {
    case WM_INITDLG:
      if ( _protocol2InitDlg( hwnd, mp2 ) )
        return (MRESULT)FALSE;
      break;

    case WM_DESTROY:
      if ( somThis != NULL )
      {
        _ulSwitches &= ~(SWFL_COMPRESSION | SWFL_PRES_BMP_CACHING |
                         SWFL_FORCE_BMP_UPD | SWFL_NUMLOCK_SYNC |
                         SWFL_NO_MOTION_EVENTS | SWFL_KEEP_WIN_KEYS);

        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_COMPRESSION ) != 0 )
          _ulSwitches |= SWFL_COMPRESSION;
        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_PRES_BMP_CACHING ) != 0 )
          _ulSwitches |= SWFL_PRES_BMP_CACHING;
        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_FORCE_BMP_UPD ) != 0 )
          _ulSwitches |= SWFL_FORCE_BMP_UPD;
        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_NUMLOCK_SYNC ) != 0 )
          _ulSwitches |= SWFL_NUMLOCK_SYNC;
        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_NO_MOTION_EVENTS ) != 0 )
          _ulSwitches |= SWFL_NO_MOTION_EVENTS;
        if ( WinQueryButtonCheckstate( hwnd, IDD_CB_KEEP_WIN_KEYS ) != 0 )
          _ulSwitches |= SWFL_KEEP_WIN_KEYS;

        _wpSaveDeferred( somSelf );
      }
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDD_PB_UNDO:
          _protocol2Undo( hwnd, somThis );
          break;

        case IDD_PB_DEFAULT:
          _protocol2Set( hwnd, NULL, NULL, DEF_SWITCHES );
          break;
      }
      return (MRESULT)FALSE;

    case WM_CONTROL:
      return (MRESULT)FALSE;

  } // end switch

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


// Page "Rediredtion" 1/2.
// -----------------------

typedef struct _DISKRECORD {
  MINIRECORDCORE	stRecCore;
  PSZ             pszName;
  PSZ             pszPath;
} DISKRECORD, *PDISKRECORD;

static SHORT EXPENTRY _compDiskRecords(PRECORDCORE p1, PRECORDCORE p2,
                                       PVOID pStorage)
{
  switch( WinCompareStrings( WinQueryAnchorBlock( HWND_DESKTOP ), 0, 0,
                             ((PDISKRECORD)p1)->pszName,
                             ((PDISKRECORD)p2)->pszName, 0 ) )
  {
    case WCS_GT:
      return 1;

    case WCS_LT:
      return -1;
  }

  return 0;
}

static VOID _redirect1EnableBtnDelete(HWND hwnd)
{
  WinEnableWindow( WinWindowFromID( hwnd, IDD_PB_DISK_DELETE ),
    (PDISKRECORD)WinSendMsg( WinWindowFromID( hwnd, IDD_CNT_DISKS ),
                             CM_QUERYRECORDEMPHASIS, MPFROMLONG(CMA_FIRST),
                             MPFROMSHORT(CRA_SELECTED) ) != NULL
  );
}

static VOID _redirect1Add(HWND hwnd, Rdesktop *somSelf)
{
  HWND                 hwndCnt = WinWindowFromID( hwnd, IDD_CNT_DISKS );
  RECORDINSERT         stRecIns;
  PDISKRECORD          pRecord, pScanRec;
  DISKDLGINFO          stDlgInfo;

  stDlgInfo.fEditableName = TRUE;
  stDlgInfo.szName[0] = '\0';
  stDlgInfo.szPath[0] = '\0';
  if ( !diskDlg( hwnd, &stDlgInfo ) )
    return;

  // Allocate record for the container.
  pRecord = (PDISKRECORD)WinSendMsg( hwndCnt, CM_ALLOCRECORD,
                     MPFROMLONG( sizeof(DISKRECORD) - sizeof(MINIRECORDCORE) ),
                     MPFROMLONG( 1 ) );
  if ( pRecord == NULL )
    return;

  NEW_STRING( pRecord->pszName, stDlgInfo.szName );
  NEW_STRING( pRecord->pszPath, stDlgInfo.szPath );

  // Insert a new record to the container.
  stRecIns.cb = sizeof(RECORDINSERT);
  stRecIns.pRecordOrder = (PRECORDCORE)CMA_END;
  stRecIns.pRecordParent = NULL;
  stRecIns.zOrder = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert = 1;
  stRecIns.fInvalidateRecord = TRUE;
  WinSendMsg( hwndCnt, CM_INSERTRECORD, (PRECORDCORE)pRecord, &stRecIns );

  // Clear current selected-state emphasis.
  pScanRec = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORDEMPHASIS,
                            MPFROMLONG(CMA_FIRST), MPFROMSHORT(CRA_SELECTED) );
  while( ( pScanRec != NULL ) && ( (LONG)pScanRec != -1 ) )
  {
    WinSendMsg( hwndCnt, CM_SETRECORDEMPHASIS, (PRECORDCORE)pScanRec,
                MPFROM2SHORT( 0, CRA_SELECTED ) );
    pScanRec = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORDEMPHASIS,
                                 MPFROMP(pScanRec), MPFROMSHORT(CRA_SELECTED) );
  }
  // Select a new record.
  WinSendMsg( hwndCnt, CM_SETRECORDEMPHASIS, (PRECORDCORE)pRecord,
              MPFROM2SHORT( 1, CRA_CURSORED | CRA_SELECTED ) );
}

static VOID _redirect1ContainerEnter(HWND hwnd, Rdesktop *somSelf,
                                     PDISKRECORD pRecord)
{
  HWND         hwndCnt = WinWindowFromID( hwnd, IDD_CNT_DISKS );
  DISKDLGINFO  stDlgInfo;
  RECORDINSERT stRecIns;

  stDlgInfo.fEditableName = FALSE;
  strlcpy( stDlgInfo.szName, pRecord->pszName, DISKDLG_MAX_NAME );
  strlcpy( stDlgInfo.szPath, pRecord->pszPath, DISKDLG_MAX_PATH );
  if ( !diskDlg( hwnd, &stDlgInfo ) )
    return;

  // Store new data in the record.

  if ( pRecord->pszName != NULL )
    _wpFreeMem( somSelf, pRecord->pszName );
  if ( pRecord->pszPath != NULL )
    _wpFreeMem( somSelf, pRecord->pszPath );

  NEW_STRING( pRecord->pszName, stDlgInfo.szName );
  NEW_STRING( pRecord->pszPath, stDlgInfo.szPath );

  // Remove and insert record again to make changes visible. It is not enough
  // to update the container.

  stRecIns.pRecordOrder = (PRECORDCORE)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                                     MPFROMP(pRecord),
                                     MPFROM2SHORT( CMA_PREV, CMA_ITEMORDER ) );
  if ( ( stRecIns.pRecordOrder == NULL ) ||
       ( (ULONG)stRecIns.pRecordOrder == -1 ) )
    stRecIns.pRecordOrder = (PRECORDCORE)CMA_FIRST;

  WinSendMsg( hwndCnt, CM_REMOVERECORD, MPFROMP(&pRecord), MPFROM2SHORT(1,0) );

  stRecIns.cb = sizeof(RECORDINSERT);
  stRecIns.pRecordParent = NULL;
  stRecIns.zOrder = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert = 1;
  stRecIns.fInvalidateRecord = TRUE;
  WinSendMsg( hwndCnt, CM_INSERTRECORD, (PRECORDCORE)pRecord, &stRecIns );
}

static VOID _redirect1Delete(HWND hwnd, Rdesktop *somSelf)
{
  HWND        hwndCnt = WinWindowFromID( hwnd, IDD_CNT_DISKS );
  PDISKRECORD pRecord, pNext;

  pRecord = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORDEMPHASIS,
                            MPFROMLONG(CMA_FIRST), MPFROMSHORT(CRA_SELECTED) );

  while( ( pRecord != NULL ) && ( (LONG)pRecord != -1 ) )
  {
    pNext = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORDEMPHASIS,
                                 MPFROMP(pRecord), MPFROMSHORT(CRA_SELECTED) );

    utilWPFreeMem( somSelf, pRecord->pszName );
    utilWPFreeMem( somSelf, pRecord->pszPath );

    WinSendMsg( hwndCnt, CM_REMOVERECORD, MPFROMP(&pRecord),
                MPFROM2SHORT( 1, CMA_FREE | CMA_INVALIDATE ) );
    pRecord = pNext;
  }

  _redirect1EnableBtnDelete( hwnd );
}

static VOID _redirect1Clean(HWND hwnd, Rdesktop *somSelf)
{
  HWND        hwndCnt = WinWindowFromID( hwnd, IDD_CNT_DISKS );
  PDISKRECORD pRecord;

  pRecord = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                                (PRECORDCORE)CMA_FIRST, 
                                MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER) );
  while( ( pRecord != NULL ) && ( (LONG)pRecord != -1 ) )
  {
    utilWPFreeMem( somSelf, pRecord->pszName );
    utilWPFreeMem( somSelf, pRecord->pszPath );
    pRecord = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                                  (PRECORDCORE)pRecord, 
                                  MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER) );
  }

  // Remove all records from container.
  WinSendMsg( hwndCnt, CM_REMOVERECORD, 0,
              MPFROM2SHORT( 0, CMA_FREE | CMA_INVALIDATE ) );
  WinEnableWindow( WinWindowFromID( hwnd, IDD_PB_DISK_DELETE ), FALSE );
}

static VOID _redirect1Undo(HWND hwnd, Rdesktop *somSelf)
{
  RdesktopData    *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );
  HWND            hwndCnt = WinWindowFromID( hwnd, IDD_CNT_DISKS );
  PDISKREDIR      pDiskRedir;
  RECORDINSERT    stRecIns;
  PDISKRECORD     pRecord, pRecords;
  ULONG           ulIdx;

  // Clear container.
  _redirect1Clean( hwnd, somSelf );

  if ( _seqDiskRedir._length != 0 )
  {
    // Allocate records for the container.
    pRecords = (PDISKRECORD)WinSendMsg( hwndCnt, CM_ALLOCRECORD,
                       MPFROMLONG( sizeof(DISKRECORD) - sizeof(MINIRECORDCORE) ),
                       MPFROMLONG( _seqDiskRedir._length ) );
    if ( pRecords == NULL )
      return;
    // Fill records.
    for( ulIdx = 0, pRecord = pRecords; ulIdx < _seqDiskRedir._length; ulIdx++ )
    {
      pDiskRedir = &_seqDiskRedir._buffer[ulIdx];

      NEW_STRING( pRecord->pszName, pDiskRedir->acName );
      NEW_STRING( pRecord->pszPath, pDiskRedir->pszPath );
      pRecord = (PDISKRECORD)pRecord->stRecCore.preccNextRecord;
    }

    // Insert records to the container.
    stRecIns.cb = sizeof(RECORDINSERT);
    stRecIns.pRecordOrder = (PRECORDCORE)CMA_END;
    stRecIns.pRecordParent = NULL;
    stRecIns.zOrder = (USHORT)CMA_TOP;
    stRecIns.cRecordsInsert = _seqDiskRedir._length;
    stRecIns.fInvalidateRecord = TRUE;
    WinSendMsg( hwndCnt, CM_INSERTRECORD, (PRECORDCORE)pRecords, &stRecIns );
  }

  WinSetDlgItemText( hwnd, IDD_EF_DISKCLIENT, _pszDiskClient );

  _redirect1EnableBtnDelete( hwnd );
}

static BOOL _redirect1InitDlg(HWND hwnd, MPARAM mp2)
{
  Rdesktop        *somSelf = PVOIDFROMMP(mp2);
  RdesktopData    *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );
  HWND            hwndCnt = WinWindowFromID( hwnd, IDD_CNT_DISKS );
  HMODULE         hModule = utilGetModuleHandle();
  PFIELDINFO      pFieldInfo;
  PFIELDINFO      pFldInf;
  CNRINFO         stCnrInf = { 0 };
  FIELDINFOINSERT	stFldInfIns = { 0 };
  CHAR            acBuf[64];

  // Store object to window pointer.
  WinSetWindowPtr( hwnd, QWL_USER, somSelf );
  if ( somThis == NULL )
    return FALSE;

  // Setup container.

  pFldInf = (PFIELDINFO)WinSendMsg( hwndCnt, CM_ALLOCDETAILFIELDINFO,
                                    MPFROMLONG( 2 ), NULL );
  if ( pFldInf == NULL )
    return FALSE;
  pFieldInfo = pFldInf;


  stCnrInf.pFieldInfoLast = pFieldInfo;

  WinLoadString( NULLHANDLE, hModule, IDS_NAME, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_FITITLEREADONLY | CFA_CENTER;
  pFieldInfo->pTitleData = (string)_wpAllocMem( somSelf, strlen( acBuf ) + 1, NULL );
  strcpy( pFieldInfo->pTitleData, acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( DISKRECORD, pszName );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

  WinLoadString( NULLHANDLE, hModule, IDS_LOCAL_PATH, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT;
  pFieldInfo->flTitle = CFA_FITITLEREADONLY | CFA_CENTER;
  pFieldInfo->pTitleData = (string)_wpAllocMem( somSelf, strlen( acBuf ) + 1, NULL );
  strcpy( pFieldInfo->pTitleData, acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( DISKRECORD, pszPath );

  stFldInfIns.cb = sizeof(FIELDINFOINSERT);
  stFldInfIns.pFieldInfoOrder = (PFIELDINFO)CMA_FIRST;
  stFldInfIns.cFieldInfoInsert = 2;
  WinSendMsg( hwndCnt, CM_INSERTDETAILFIELDINFO, MPFROMP( pFldInf ),
              MPFROMP( &stFldInfIns ) );

  stCnrInf.cb = sizeof(CNRINFO);
  stCnrInf.pSortRecord = _compDiskRecords;
  stCnrInf.flWindowAttr = CV_DETAIL | CA_DETAILSVIEWTITLES;
  WinSendMsg( hwndCnt, CM_SETCNRINFO, MPFROMP( &stCnrInf ),
              MPFROMLONG( CMA_PSORTRECORD | CMA_PFIELDINFOLAST |
                          CMA_FLWINDOWATTR ) );

  _redirect1Undo( hwnd, somSelf );

  return TRUE;
}

static BOOL _redirect1DiskDlgCheck(HWND hwnd, HWND hwndDlg,
                                   PDISKDLGINFO pDlgInf)
{
  HWND        hwndCnt = WinWindowFromID( hwnd, IDD_CNT_DISKS );
  PDISKRECORD pRecord;
  HAB         hab = WinQueryAnchorBlock( HWND_DESKTOP );

  if ( !pDlgInf->fEditableName )
    return TRUE;

  pRecord = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                                     (PRECORDCORE)CMA_FIRST, 
                                     MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER) );
  while( ( pRecord != NULL ) && ( (LONG)pRecord != -1 ) )
  {
    if ( WinCompareStrings( hab, 0, 0, pRecord->pszName, pDlgInf->szName, 0 )
           == WCS_EQ )
    {
      utilMessageBox( hwndDlg, NULL, IDM_DUPLICATE_NAME,
                      MB_APPLMODAL | MB_ICONHAND | MB_MOVEABLE | MB_OK );
      return FALSE;
    }

    pRecord = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                                       (PRECORDCORE)pRecord, 
                                       MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER) );
  }

  return TRUE;
}

MRESULT EXPENTRY nbPageRedirection1(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  Rdesktop      *somSelf = WinQueryWindowPtr( hwnd, QWL_USER );
  RdesktopData  *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );

  switch( msg )
  {
    case WM_INITDLG:
      if ( _redirect1InitDlg( hwnd, mp2 ) )
        return (MRESULT)FALSE;
      break;

    case WM_DESTROY:
      if ( somThis != NULL )
      {
        HWND        hwndCnt = WinWindowFromID( hwnd, IDD_CNT_DISKS );
        PFIELDINFO  pFldInf;
        PDISKRECORD pRecord;

        // Remove all disk redirections.
        _rdDiskRedirDelete( somSelf, NULL );

        // Fill new disk redirections.
        pRecord = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                                      (PRECORDCORE)CMA_FIRST, 
                                      MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER) );
        while( ( pRecord != NULL ) && ( (LONG)pRecord != -1 ) )
        {
          _rdDiskRedirAdd( somSelf, pRecord->pszName, pRecord->pszPath );
          pRecord = (PDISKRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                                        (PRECORDCORE)pRecord, 
                                        MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER) );
        }

        // Get a new displayed client name for disks.
        GET_NEW_ITEM_TEXT( _pszDiskClient, hwnd, IDD_EF_DISKCLIENT );

        // Clear container.
        _redirect1Clean( hwnd, somSelf );

        // Free titles strings of details view.
        pFldInf = (PFIELDINFO)WinSendMsg( hwndCnt, CM_QUERYDETAILFIELDINFO, 0,
                                          MPFROMSHORT( CMA_FIRST ) );
        while( ( pFldInf != NULL ) && ( (LONG)pFldInf != -1 ) )
        {
          utilWPFreeMem( somSelf, pFldInf->pTitleData );
          pFldInf = (PFIELDINFO)WinSendMsg( hwndCnt, CM_QUERYDETAILFIELDINFO,
                                 MPFROMP( pFldInf ), MPFROMSHORT( CMA_NEXT ) );
        }

        _wpSaveDeferred( somSelf );
      }
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDD_PB_DISK_ADD:
          _redirect1Add( hwnd, somSelf );
          break;

        case IDD_PB_DISK_DELETE:
          _redirect1Delete( hwnd, somSelf );
          break;

        case IDD_PB_UNDO:
          _redirect1Undo( hwnd, somSelf );
          break;
      }
      return (MRESULT)FALSE;

    case WM_CONTROL:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDD_CNT_DISKS:
          switch( SHORT2FROMMP( mp1 ) )
          {
            case CN_EMPHASIS:
              _redirect1EnableBtnDelete( hwnd );
              break;

            case CN_ENTER:
              _redirect1ContainerEnter( hwnd, somSelf,
                             (PDISKRECORD)((PNOTIFYRECORDENTER)mp2)->pRecord );
              break;
          }
          break;
      }
      return (MRESULT)FALSE;

    case WM_DISKDLG_CHECK:
      return (MRESULT)_redirect1DiskDlgCheck( hwnd, (HWND)mp1,
                                              (PDISKDLGINFO)mp2 );

  } // end switch

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


// Page "Rediredtion" 2/2.
// -----------------------

typedef struct _SERDEVRECORD {
  MINIRECORDCORE	stRecCore;
  PSZ             pszRemote;
  PSZ             pszLocal;
  CHAR            szRemote[8];
  CHAR            szLocal[8];
  ULONG           ulSerDevRedir;
} SERDEVRECORD, *PSERDEVRECORD;

static SHORT EXPENTRY _compSerDevRecords(PRECORDCORE p1, PRECORDCORE p2,
                                         PVOID pStorage)
{
  return (SHORT)SDR2Remote( ((PSERDEVRECORD)p1)->ulSerDevRedir ) -
         (SHORT)SDR2Remote( ((PSERDEVRECORD)p2)->ulSerDevRedir );
}

static SHORT __redirect2AddPort(HWND hwndCB, ULONG ulPort)
{
  SHORT      sIdx;
  CHAR       acBuf[8];

  sprintf( acBuf, "com%u", ulPort );
  sIdx = WinInsertLboxItem( hwndCB, LIT_END, acBuf );
  WinSendMsg( hwndCB, LM_SETITEMHANDLE, MPFROMSHORT(sIdx), MPFROMLONG(ulPort) );

  return sIdx;
}

// Fills lists (comboboxes) of remote and local ports.
static VOID __redirect2FillPortLists(HWND hwnd, Rdesktop *somSelf)
{
  HWND            hwndCnt = WinWindowFromID( hwnd, IDD_CNT_SERIALDEVICES );
  HWND            hwndCB;
  ULONG           ulPort;
  SHORT           sIdx;
  BOOL            fFound;
  CHAR            acBuf[32];
  PSERDEVRECORD   pRecord = (PSERDEVRECORD)WinSendMsg( hwndCnt,
                                 CM_QUERYRECORDEMPHASIS, MPFROMLONG(CMA_FIRST),
                                 MPFROMSHORT(CRA_CURSORED) );

  // Remote ports list.

  hwndCB = WinWindowFromID( hwnd, IDD_CB_SDREMOTE );
  WinSendMsg( hwndCB, LM_DELETEALL, 0, 0 );
  for( ulPort = 1; ulPort <= 16; ulPort++ )
  {
    sIdx = __redirect2AddPort( hwndCB, ulPort );
    if ( ( pRecord != NULL ) &&
         ( SDR2Remote( pRecord->ulSerDevRedir ) == ulPort ) )
      WinSendMsg( hwndCB, LM_SELECTITEM, MPFROMSHORT(sIdx), MPFROMSHORT(1) );
  }

  // Local ports list.

  hwndCB = WinWindowFromID( hwnd, IDD_CB_SDLOCAL );
  WinSendMsg( hwndCB, LM_DELETEALL, 0, 0 );
  WinLoadString( WinQueryAnchorBlock( HWND_DESKTOP ), utilGetModuleHandle(),
                 IDS_PORT_NONE, sizeof(acBuf), acBuf );
  WinSendMsg( hwndCB, LM_SETITEMHANDLE,
              MPFROMSHORT( WinInsertLboxItem( hwndCB, LIT_END, acBuf ) ), 0 );
  WinSendMsg( hwndCB, LM_SELECTITEM, MPFROMSHORT( 0 ), MPFROMSHORT( 1 ) );

  for( ulPort = 1; ulPort <= 16; ulPort++ )
  {
    // Looking for local port in the container's list.
    fFound = FALSE;
    pRecord = (PSERDEVRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                                      (PRECORDCORE)CMA_FIRST, 
                                      MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER) );
    while( ( pRecord != NULL ) && ( (LONG)pRecord != -1 ) )
    {
      if ( SDR2Local( pRecord->ulSerDevRedir ) == ulPort )
      {
        fFound = TRUE;
        break;
      }
      pRecord = (PSERDEVRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                                    (PRECORDCORE)pRecord, 
                                    MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER) );
    }

    if ( !fFound )
      // Local port ulPort not used.
      __redirect2AddPort( hwndCB, ulPort );
  }
}

static const ULONG   aClpbrRBId[] =
  { IDD_RB_CBPRIMARY, IDD_RB_CBPASSIVE, IDD_RB_CBOFF };

static const ULONG   aSoundRBId[] =
  { IDD_RB_SNDLOCAL, IDD_RB_SNDREMOTE, IDD_RB_SNDOFF };

static VOID _redirect2Undo(HWND hwnd, Rdesktop *somSelf)
{
  RdesktopData    *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );
  HWND            hwndCnt = WinWindowFromID( hwnd, IDD_CNT_SERIALDEVICES );
  ULONG           ulIdx;
  RECORDINSERT    stRecIns;
  PSERDEVRECORD   pRecord, pRecords;

  WinCheckButton( hwnd, aClpbrRBId[_ulClipboard % ARRAY_SIZE(aClpbrRBId)], 1 );
  WinCheckButton( hwnd, aSoundRBId[_ulSound % ARRAY_SIZE(aSoundRBId)], 1 );

  WinSendMsg( hwndCnt, CM_REMOVERECORD, 0, MPFROM2SHORT( 0, CMA_FREE ) );

  if ( _cSerDevRedir != 0 )
  {
    // Allocate records for the container.
    pRecords = (PSERDEVRECORD)WinSendMsg( hwndCnt, CM_ALLOCRECORD,
                   MPFROMLONG( sizeof(SERDEVRECORD) - sizeof(MINIRECORDCORE) ),
                   MPFROMLONG( _cSerDevRedir ) );
    if ( pRecords != NULL )
    {
      // Fill records.
      for( ulIdx = 0, pRecord = pRecords; ulIdx < _cSerDevRedir; ulIdx++ )
      {
        pRecord->ulSerDevRedir = _aulSerDevRedir[ulIdx];
        sprintf( pRecord->szRemote, "com%u", SDR2Remote( _aulSerDevRedir[ulIdx] ) );
        sprintf( pRecord->szLocal, "com%u", SDR2Local( _aulSerDevRedir[ulIdx] ) );
        pRecord->pszRemote = pRecord->szRemote;
        pRecord->pszLocal = pRecord->szLocal;

        pRecord = (PSERDEVRECORD)pRecord->stRecCore.preccNextRecord;
      }

      // Insert records to the container.
      stRecIns.cb = sizeof(RECORDINSERT);
      stRecIns.pRecordOrder = (PRECORDCORE)CMA_END;
      stRecIns.pRecordParent = NULL;
      stRecIns.zOrder = (USHORT)CMA_TOP;
      stRecIns.cRecordsInsert = _cSerDevRedir;
      stRecIns.fInvalidateRecord = TRUE;
      WinSendMsg( hwndCnt, CM_INSERTRECORD, (PRECORDCORE)pRecords, &stRecIns );
    } // if ( pRecords != NULL )
  }

  __redirect2FillPortLists( hwnd, somSelf );
}

static BOOL _redirect2InitDlg(HWND hwnd, MPARAM mp2)
{
  Rdesktop        *somSelf = PVOIDFROMMP(mp2);
  RdesktopData    *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );
  HWND            hwndCnt = WinWindowFromID( hwnd, IDD_CNT_SERIALDEVICES );
  HMODULE         hModule = utilGetModuleHandle();
  PFIELDINFO      pFieldInfo;
  PFIELDINFO      pFldInf;
  CNRINFO         stCnrInf = { 0 };
  FIELDINFOINSERT	stFldInfIns = { 0 };
  CHAR            acBuf[64];

  // Store object to window pointer.
  WinSetWindowPtr( hwnd, QWL_USER, somSelf );
  if ( somThis == NULL )
    return FALSE;

  // Setup container.

  pFldInf = (PFIELDINFO)WinSendMsg( hwndCnt, CM_ALLOCDETAILFIELDINFO,
                                    MPFROMLONG( 2 ), NULL );
  if ( pFldInf == NULL )
    return FALSE;
  pFieldInfo = pFldInf;


  stCnrInf.pFieldInfoLast = pFieldInfo;

  WinLoadString( NULLHANDLE, hModule, IDS_REMOTE, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_FITITLEREADONLY | CFA_CENTER;
  pFieldInfo->pTitleData = (string)_wpAllocMem( somSelf, strlen( acBuf ) + 1, NULL );
  strcpy( pFieldInfo->pTitleData, acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( SERDEVRECORD, pszRemote );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

  WinLoadString( NULLHANDLE, hModule, IDS_LOCAL, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT;
  pFieldInfo->flTitle = CFA_FITITLEREADONLY | CFA_CENTER;
  pFieldInfo->pTitleData = (string)_wpAllocMem( somSelf, strlen( acBuf ) + 1, NULL );
  strcpy( pFieldInfo->pTitleData, acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( SERDEVRECORD, pszLocal );

  stFldInfIns.cb = sizeof(FIELDINFOINSERT);
  stFldInfIns.pFieldInfoOrder = (PFIELDINFO)CMA_FIRST;
  stFldInfIns.cFieldInfoInsert = 2;
  WinSendMsg( hwndCnt, CM_INSERTDETAILFIELDINFO, MPFROMP( pFldInf ),
              MPFROMP( &stFldInfIns ) );

  stCnrInf.cb = sizeof(CNRINFO);
  stCnrInf.pSortRecord = _compSerDevRecords;
  stCnrInf.flWindowAttr = CV_DETAIL | CA_DETAILSVIEWTITLES;
  WinSendMsg( hwndCnt, CM_SETCNRINFO, MPFROMP( &stCnrInf ),
              MPFROMLONG( CMA_PSORTRECORD | CMA_PFIELDINFOLAST |
                          CMA_FLWINDOWATTR ) );

  _redirect2Undo( hwnd, somSelf );

  return TRUE;
}

static VOID _redirect2BtnSet(HWND hwnd, Rdesktop *somSelf)
{
  HWND            hwndCnt = WinWindowFromID( hwnd, IDD_CNT_SERIALDEVICES );
  HWND            hwndCBRemote = WinWindowFromID( hwnd, IDD_CB_SDREMOTE );
  SHORT           sRemoteIdx;
  ULONG           ulRemotePort;
  HWND            hwndCBLocal = WinWindowFromID( hwnd, IDD_CB_SDLOCAL );
  SHORT           sLocalIdx;
  ULONG           ulLocalPort;
  PSERDEVRECORD   pRecord;

  sRemoteIdx = (SHORT)WinSendMsg( hwndCBRemote, LM_QUERYSELECTION,
                                  MPFROMSHORT(LIT_FIRST), 0 );
  ulRemotePort = WinQueryWindowULong( hwndCBRemote, QWL_USER );

  sLocalIdx = (SHORT)WinSendMsg( hwndCBLocal, LM_QUERYSELECTION,
                                 MPFROMSHORT(LIT_FIRST), 0 );
  ulLocalPort = WinQueryWindowULong( hwndCBLocal, QWL_USER );

  // Value "none" selected for the local port. Remove redrection record.

  pRecord = (PSERDEVRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
              (PRECORDCORE)CMA_FIRST, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER) );
  while( ( pRecord != NULL ) && ( (LONG)pRecord != -1 ) )
  {
    if ( SDR2Remote( pRecord->ulSerDevRedir ) == ulRemotePort )
    {
      WinSendMsg( hwndCnt, CM_REMOVERECORD, MPFROMP( &pRecord ),
                  MPFROM2SHORT( 1, CMA_FREE | CMA_INVALIDATE ) );
      break;
    }
    pRecord = (PSERDEVRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                (PRECORDCORE)pRecord, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER) );
  }

  if ( ulLocalPort != 0 )
  {
    // Insert the new redirection record to the container.
    RECORDINSERT       stRecIns;

    pRecord = (PSERDEVRECORD)WinSendMsg( hwndCnt, CM_ALLOCRECORD,
                   MPFROMLONG( sizeof(SERDEVRECORD) - sizeof(MINIRECORDCORE) ),
                   MPFROMLONG( 1 ) );
    if ( pRecord != NULL )
    {
      // Fill the record.
      pRecord->ulSerDevRedir = Ports2SDR( ulRemotePort, ulLocalPort );
      sprintf( pRecord->szRemote, "com%u", ulRemotePort );
      sprintf( pRecord->szLocal, "com%u", ulLocalPort );
      pRecord->pszRemote = pRecord->szRemote;
      pRecord->pszLocal = pRecord->szLocal;

      // Insert the record to the container.
      stRecIns.cb = sizeof(RECORDINSERT);
      stRecIns.pRecordOrder = (PRECORDCORE)CMA_END;
      stRecIns.pRecordParent = NULL;
      stRecIns.zOrder = (USHORT)CMA_TOP;
      stRecIns.cRecordsInsert = 1;
      stRecIns.fInvalidateRecord = TRUE;
      WinSendMsg( hwndCnt, CM_INSERTRECORD, (PRECORDCORE)pRecord, &stRecIns );
      WinSendMsg( hwndCnt, CM_SETRECORDEMPHASIS, (PRECORDCORE)pRecord,
                  MPFROM2SHORT( 1, CRA_CURSORED | CRA_SELECTED ) );
    } // if ( pRecord != NULL )
  }

  __redirect2FillPortLists( hwnd, somSelf );
}

// Set the port number (from list item handle) into the memory of the reserved
// window words for combobox.
static VOID _redirect2PortSelected(HWND hwnd, USHORT usItemId)
{
  HWND       hwndCB = WinWindowFromID( hwnd, usItemId );
  SHORT      sIdx = (SHORT)WinSendMsg( hwndCB, LM_QUERYSELECTION,
                                       MPFROMSHORT(LIT_FIRST), 0 );
  ULONG      ulPort = (ULONG)WinSendMsg( hwndCB, LM_QUERYITEMHANDLE,
                                         MPFROMSHORT(sIdx), 0 );

  WinSetWindowULong( hwndCB, QWL_USER, ulPort );
}

static VOID _redirect2PortCntrCursored(HWND hwnd, PNOTIFYRECORDEMPHASIS pInfo)
{
  HWND          hwndCB = WinWindowFromID( hwnd, IDD_CB_SDREMOTE );
  SHORT         cItems = (SHORT)WinSendMsg( hwndCB, LM_QUERYITEMCOUNT, 0, 0 );
  SHORT         sIdx;

  for( sIdx = 0; sIdx < cItems; sIdx++ )
  {
    if ( (ULONG)WinSendMsg( hwndCB, LM_QUERYITEMHANDLE, MPFROMSHORT(sIdx), 0 )
           == SDR2Remote( ((PSERDEVRECORD)pInfo->pRecord)->ulSerDevRedir ) )
    {
      WinSendMsg( hwndCB, LM_SELECTITEM, MPFROMSHORT( sIdx ), MPFROMSHORT(1) );
      break;
    }
  }
}

MRESULT EXPENTRY nbPageRedirection2(HWND hwnd, ULONG msg, MPARAM mp1,
                                    MPARAM mp2)
{
  Rdesktop      *somSelf = WinQueryWindowPtr( hwnd, QWL_USER );
  RdesktopData  *somThis = somSelf == NULL ? NULL : RdesktopGetData( somSelf );

  switch( msg )
  {
    case WM_INITDLG:
      if ( _redirect2InitDlg( hwnd, mp2 ) )
        return (MRESULT)FALSE;
      break;

    case WM_DESTROY:
      if ( somThis != NULL )
      {
        PSERDEVRECORD   pRecord;
        HWND            hwndCnt = WinWindowFromID(hwnd, IDD_CNT_SERIALDEVICES);

        _ulClipboard = WinSendDlgItemMsg( hwnd, IDD_RB_CBPRIMARY, BM_QUERYCHECK,
                                          0, 0 ) != 0
                       ? 0 : ( WinSendDlgItemMsg( hwnd, IDD_RB_CBPASSIVE,
                                                  BM_QUERYCHECK, 0, 0 ) != 0
                               ? 1 : 2 );

        _ulSound = WinSendDlgItemMsg( hwnd, IDD_RB_SNDLOCAL, BM_QUERYCHECK,
                                      0, 0 ) != 0
                       ? 0 : ( WinSendDlgItemMsg( hwnd, IDD_RB_SNDREMOTE,
                                                  BM_QUERYCHECK, 0, 0 ) != 0
                               ? 1 : 2 );

        // Store list of serial devices redirections.
        _cSerDevRedir = 0;
        pRecord = (PSERDEVRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                    (PRECORDCORE)CMA_FIRST, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER) );
        while( ( pRecord != NULL ) && ( (LONG)pRecord != -1 ) )
        {
          _aulSerDevRedir[_cSerDevRedir++] = pRecord->ulSerDevRedir;
          pRecord = (PSERDEVRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD,
                      (PRECORDCORE)pRecord, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER) );
        }

        _wpSaveDeferred( somSelf );
      }
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDD_PB_SDSET:
          _redirect2BtnSet( hwnd, somSelf );
          break;

        case IDD_PB_UNDO:
          _redirect2Undo( hwnd, somSelf );
          break;
      }
      return (MRESULT)FALSE;

    case WM_CONTROL:
      switch( SHORT1FROMMP(mp1) )
      {
        case IDD_CNT_SERIALDEVICES:
          if ( ( SHORT2FROMMP(mp1) == CN_EMPHASIS ) &&
               ( (((PNOTIFYRECORDEMPHASIS)mp2)->fEmphasisMask & CRA_CURSORED)
                 != 0 ) )
            _redirect2PortCntrCursored( hwnd, (PNOTIFYRECORDEMPHASIS)mp2 );
          break;

        case IDD_CB_SDREMOTE:
        case IDD_CB_SDLOCAL:
          if ( SHORT2FROMMP(mp1) == LN_SELECT )
            _redirect2PortSelected( hwnd, SHORT1FROMMP(mp1) );
          break;
      }
      return (MRESULT)FALSE;
  } // end switch

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}
