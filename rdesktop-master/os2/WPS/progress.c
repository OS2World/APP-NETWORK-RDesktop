#include <rdesktop.ih>
#include "progress.h";

#define TIMER_BAR_ID             1
// TIMER_BAR_TIMEOUT: Bar animation (image horizontal offset) delay [msec].
#define TIMER_BAR_TIMEOUT        20
#define TIMER_WAITWIN_ID         2
// TIMER_POPUP_TIMEOUT: Period to test the appearance of Rdesktop window [msec].
#define TIMER_WAITWIN_TIMEOUT    400
#define TIMER_POPUP_ID           3
// TIMER_POPUP_TIMEOUT: Delay before appearance of the progress window [msec].
#define TIMER_POPUP_TIMEOUT      1500

#define WM_PR_ADD_PID            (WM_USER + 1)
#define WM_PR_DELETE_PID         (WM_USER + 2)
#define WM_PR_SWITCH_TO          (WM_USER + 3)
#define WM_PR_DRAWBAR            (WM_USER + 4)

typedef struct _DLGINITDATA {
  USHORT     usSize;
  Rdesktop   *somSelf;
} DLGINITDATA, *PDLGINITDATA;

typedef struct _DLGDATA {
  USHORT     usSize;
  Rdesktop   *somSelf;
  ULONG      ulTimerBarId;
  ULONG      ulTimerWaitWinId;
  ULONG      ulTimerPopupId;
  PULONG     paulPID;
  ULONG      ulMaxPIDs;
  ULONG      cPID;
  HBITMAP    hbmBar;
  ULONG      ulBarStep;
} DLGDATA, *PDLGDATA;


// Progress bar.

static PFNWP ProgressBarWinProcOrg;

static MRESULT EXPENTRY _progressBarWinProc(HWND hwnd, ULONG msg,
                                            MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_PAINT:
      {
        HPS  hps = WinBeginPaint( hwnd, NULLHANDLE, NULL );

        WinSendMsg( WinQueryWindow( hwnd, QW_OWNER ), WM_PR_DRAWBAR,
                    MPFROMLONG( hwnd ), MPFROMLONG( hps ) );
        WinEndPaint( hps );
      }
      return (MRESULT)FALSE;
  }

  return ProgressBarWinProcOrg( hwnd, msg, mp1, mp2 );
}

// Progress dialog.

static BOOL _wmInitDlg(HWND hwnd, PDLGINITDATA pInitData)
{
  Rdesktop        *somSelf = pInitData->somSelf;
  RdesktopData    *somThis = RdesktopGetData( somSelf );
  HMODULE         hModule = utilGetModuleHandle();
  HAB             hab = WinQueryAnchorBlock( hwnd );
  PDLGDATA        pDlgData = (PDLGDATA)_wpAllocMem( somSelf, sizeof(DLGDATA),
                                                    NULL );
  HWND            hwndBar = WinWindowFromID( hwnd, IDD_ST_BAR );
  PSZ             pszObjTitle = _wpQueryTitle( somSelf );
  HPS             hps;
  CHAR            acBuf[128];

  if ( pDlgData == NULL )
  {
    WinDestroyWindow( hwnd );
    return FALSE;
  }

  WinSetWindowPtr( hwnd, QWL_USER, pDlgData );
  pDlgData->somSelf          = somSelf;
  pDlgData->ulTimerBarId     = 0;
  pDlgData->ulTimerWaitWinId = WinStartTimer( hab, hwnd, TIMER_WAITWIN_ID,
                                              TIMER_WAITWIN_TIMEOUT );
  pDlgData->ulTimerPopupId   = 0;
  pDlgData->paulPID          = NULL;
  pDlgData->ulMaxPIDs        = 0;
  pDlgData->cPID             = 0;
  hps = WinGetPS( hwndBar );
  pDlgData->hbmBar           = GpiLoadBitmap( hps, hModule,
                                              IDRES_PROGRESS_BAR_BMP, 0L, 0 );
  WinReleasePS( hps );

  // Progress bar will send WM_PR_DRAWBAR to the owner on WM_PAINT.
  ProgressBarWinProcOrg = WinSubclassWindow( hwndBar, _progressBarWinProc );

  // Set window title.
  if ( pszObjTitle != NULL &&
       utilLoadInsertStr( hModule, TRUE, IDS_PROGRESS_TITLE, 1, &pszObjTitle,
                          sizeof(acBuf), acBuf ) != 0 )
    WinSetWindowText( hwnd, acBuf );

  _hwndProgress = hwnd;
  return TRUE;
}

static VOID _wmDestroy(HWND hwnd)
{
  PDLGDATA        pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  RdesktopData    *somThis = RdesktopGetData( pDlgData->somSelf );
  HAB             hab = WinQueryAnchorBlock( hwnd );

  _hwndProgress = NULLHANDLE;

  if ( pDlgData == NULL )
    return;

  if ( pDlgData->ulTimerBarId != 0 )
    WinStopTimer( hab, hwnd, pDlgData->ulTimerBarId );
  WinStopTimer( hab, hwnd, pDlgData->ulTimerWaitWinId );
  if ( pDlgData->ulTimerPopupId != 0 )
    WinStopTimer( hab, hwnd, pDlgData->ulTimerPopupId );
  GpiDeleteBitmap( pDlgData->hbmBar ); 
  utilWPFreeMem( pDlgData->somSelf, pDlgData->paulPID );
  _wpFreeMem( pDlgData->somSelf, (PBYTE)pDlgData );
}

static BOOL _wmTimer(HWND hwnd, ULONG ulTimerId)
{
  PDLGDATA   pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );

  if ( ulTimerId == pDlgData->ulTimerBarId )
  {
    // Progress bar animation.

    HWND     hwndBar = WinWindowFromID( hwnd, IDD_ST_BAR );

    pDlgData->ulBarStep++;
    WinInvalidateRect( hwndBar, NULL, FALSE );
    WinUpdateWindow( hwndBar );
  }
  else if ( ulTimerId == pDlgData->ulTimerWaitWinId )
  {
    // Chek window for runned processes (i.e. rdesktop.exe).

    ULONG           ulIdxSwEnt, ulIdxWaitPID;
    HAB             hab = WinQueryAnchorBlock( hwnd );
    ULONG           cSwEntry, cbSwBlock, ulPID;
    PSWBLOCK        pSwBlock;

    cSwEntry = WinQuerySwitchList( hab, NULL, 0 );
    if ( cSwEntry == 0 )
      return TRUE;
    cbSwBlock = cSwEntry * sizeof(SWENTRY);
    pSwBlock = (PSWBLOCK)_wpAllocMem( pDlgData->somSelf, cbSwBlock, NULL );
    if ( pSwBlock == NULL )
      return TRUE;

    cSwEntry = WinQuerySwitchList( hab, pSwBlock, cbSwBlock );

    for( ulIdxSwEnt = 0; ulIdxSwEnt < pSwBlock->cswentry; ulIdxSwEnt++ )
    {
      ulPID = pSwBlock->aswentry[ulIdxSwEnt].swctl.idProcess;
      for( ulIdxWaitPID = 0; ulIdxWaitPID < pDlgData->cPID; ulIdxWaitPID++ )
      {
        if ( ulPID == pDlgData->paulPID[ulIdxWaitPID] )
        {
          // Ok, window is created ==> rdesktop connected. Remove PID from the
          // list. If it was last PID, progress window will be removed.
          WinPostMsg( hwnd, WM_PR_DELETE_PID, MPFROMLONG(ulPID), 0 );
          break;
        }
      }
    }

    _wpFreeMem( pDlgData->somSelf, (PBYTE)pSwBlock );
    return TRUE;
  }
  else if ( ulTimerId == pDlgData->ulTimerPopupId )
    return (BOOL)WinSendMsg( hwnd, WM_PR_SWITCH_TO, 0, 0 );

  return FALSE; // Not our timer.
}

static VOID _wmCmdCancel(HWND hwnd)
{
  PDLGDATA   pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  ULONG      ulIdx, ulRC;
  PULONG     pulPID = pDlgData->paulPID;

  // Interrupt/break/kill all processes which still does not create window
  // (i.e. not connected). Usually I can just kill the process.
  for( ulIdx = 0; ulIdx < pDlgData->cPID; ulIdx++, pulPID++ )
  {
    ulRC = DosSendSignalException( *pulPID, XCPT_SIGNAL_INTR );
    if ( ulRC != NO_ERROR )
    {
      debug( "DosSendSignalException(%u, XCPT_SIGNAL_INTR), rc = %u",
             *pulPID, ulRC );

      ulRC = DosSendSignalException( *pulPID, XCPT_SIGNAL_BREAK );
      if ( ulRC != NO_ERROR )
      {
        debug( "DosSendSignalException(%u, XCPT_SIGNAL_BREAK), rc = %u",
               *pulPID, ulRC );
        ulRC = DosKillProcess( DCWA_PROCESSTREE, *pulPID );
        if ( ulRC != NO_ERROR )
          debug( "DosKillProcess(%u), rc = %u", *pulPID, ulRC );
      }
    }
  }  
}

static VOID _wmPrDrawBar(HWND hwnd, HWND hwndBar, HPS hps)
{
  PDLGDATA          pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  POINTL            pt;
  BITMAPINFOHEADER2 stBmpInf;
  RECTL             rectl;
  HRGN              hrgn;
  LONG              lColor1 = SYSCLR_BUTTONLIGHT;
  LONG              lColor2 = SYSCLR_BUTTONDARK;

  stBmpInf.cbFix = sizeof(BITMAPINFOHEADER2);
  if ( !GpiQueryBitmapInfoHeader( pDlgData->hbmBar, &stBmpInf ) )
    return;

  WinQueryWindowRect( hwndBar, &rectl );
  if ( (rectl.yTop - rectl.yBottom - 4) > stBmpInf.cy )
    rectl.yTop = rectl.yBottom + stBmpInf.cy + 4;

  util3DFrame( hps, &rectl, lColor1, lColor2 );
  WinInflateRect( 0, &rectl, -1, -1 );
  util3DFrame( hps, &rectl, lColor2, lColor1 );
  WinInflateRect( 0, &rectl, -1, -1 );

  hrgn = GpiCreateRegion( hps, 1, &rectl );
  GpiSetClipRegion( hps, hrgn, NULL );

  pt.x = -( (pDlgData->ulBarStep * 2) % stBmpInf.cx );
  pt.y = rectl.yBottom;
  do
  {
    WinDrawBitmap( hps, pDlgData->hbmBar, NULL, &pt, 0, 0, DBM_NORMAL );
    pt.x += stBmpInf.cx;
  }
  while( pt.x < rectl.xRight );

  GpiSetClipRegion( hps, NULLHANDLE, NULL );
  GpiDestroyRegion( hps, hrgn );
}

static VOID _wmPrAddPID(HWND hwnd, ULONG ulPID)
{
  PDLGDATA        pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );

  // Expand PID list.
  if ( pDlgData->cPID == pDlgData->ulMaxPIDs )
  {
    PULONG pNewBuf = (PULONG)_wpAllocMem( pDlgData->somSelf,
                             (pDlgData->ulMaxPIDs + 8) * sizeof(ULONG), NULL );
    if ( pNewBuf == NULL )
      return;

    memcpy( pNewBuf, pDlgData->paulPID, pDlgData->ulMaxPIDs * sizeof(ULONG) );
    utilWPFreeMem( pDlgData->somSelf, pDlgData->paulPID );
    pDlgData->paulPID = pNewBuf;
    pDlgData->ulMaxPIDs += 8;
  }

  // Store PID.
  pDlgData->paulPID[pDlgData->cPID] = ulPID;
  pDlgData->cPID++;

  if ( pDlgData->ulTimerPopupId == 0 )
    pDlgData->ulTimerPopupId = WinStartTimer( WinQueryAnchorBlock( hwnd ), hwnd,
                                         TIMER_POPUP_ID, TIMER_POPUP_TIMEOUT );
}

static VOID _wmPrDeletePID(HWND hwnd, ULONG ulPID)
{
  PDLGDATA        pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  ULONG           ulIdx;

  // Remove given PID from the list.
  for( ulIdx = 0; ulIdx < pDlgData->cPID; ulIdx++ )
  {
    if ( pDlgData->paulPID[ulIdx] == ulPID )
    {
      pDlgData->cPID--;
      pDlgData->paulPID[ulIdx] = pDlgData->paulPID[pDlgData->cPID];
      break;
    }
  }

  // No PIDs left - destroy the window.
  if ( pDlgData->cPID == 0 )
    WinDestroyWindow( hwnd );
}

static VOID _wmPrSwitchTo(HWND hwnd)
{
  PDLGDATA        pDlgData = (PDLGDATA)WinQueryWindowPtr( hwnd, QWL_USER );
  HAB             hab = WinQueryAnchorBlock( hwnd );
  POINTL          pt = { 0 };
  ULONG           ulFlags = SWP_ACTIVATE | SWP_ZORDER;

  if ( !WinIsWindowVisible( hwnd ) )
  {
    // Show window at center of the desktop.
    RECTL           rectDT, rectWin;

    WinQueryWindowRect( hwnd, &rectWin );
    WinQueryWindowRect( HWND_DESKTOP, &rectDT );
    pt.x = ( rectDT.xRight - ( rectWin.xRight - rectWin.xLeft ) ) / 2;
    pt.y = ( rectDT.yTop -   ( rectWin.yTop - rectWin.yBottom ) ) / 2;
    ulFlags = SWP_ACTIVATE | SWP_ZORDER | SWP_SHOW | SWP_MOVE;
  }

  // Stop one-event timer for show (popup) window after creation.
  if ( pDlgData->ulTimerPopupId != 0 )
  {
    WinStopTimer( hab, hwnd, pDlgData->ulTimerPopupId );
    pDlgData->ulTimerPopupId = 0;
  }

  // Start animation timer.
  if ( pDlgData->ulTimerBarId == 0 )
    pDlgData->ulTimerBarId = WinStartTimer( hab, hwnd, TIMER_BAR_ID,
                                            TIMER_BAR_TIMEOUT );

  WinSetWindowPos( hwnd, HWND_TOP, pt.x, pt.y, 0, 0, ulFlags );
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_wmInitDlg( hwnd, (PDLGINITDATA)mp2 );

    case WM_DESTROY:
      _wmDestroy( hwnd );
      break;

    case WM_TIMER:
      if ( _wmTimer( hwnd, SHORT1FROMMP(mp1) ) )
        return (MRESULT)TRUE;
      break;

    case WM_PR_DRAWBAR:
      _wmPrDrawBar( hwnd, (HWND)mp1, (HPS)mp2 );
      return (MRESULT)TRUE;

    case WM_PR_ADD_PID:
      _wmPrAddPID( hwnd, LONGFROMMP(mp1) );
      return (MRESULT)TRUE;

    case WM_PR_DELETE_PID:
      _wmPrDeletePID( hwnd, LONGFROMMP(mp1) );
      return (MRESULT)TRUE;

    case WM_PR_SWITCH_TO:
      _wmPrSwitchTo( hwnd );
      return (MRESULT)TRUE;

    case WM_COMMAND:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case MBID_CANCEL:
          _wmCmdCancel( hwnd );
          break;
      }
      return (MRESULT)TRUE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


VOID prInit(Rdesktop *somSelf)
{
}

VOID prDone(Rdesktop *somSelf)
{
  RdesktopData *somThis = RdesktopGetData( somSelf );

  if ( _hwndProgress != NULLHANDLE )
    WinDestroyWindow( _hwndProgress );
}

// Should be called from the main thread only.
BOOL prStart(Rdesktop *somSelf, ULONG ulPID)
{
  RdesktopData *somThis = RdesktopGetData( somSelf );

  if ( _hwndProgress == NULLHANDLE )
  {
    DLGINITDATA  stInitData;
    HWND         hwndProgress;

    stInitData.usSize = sizeof(DLGDATA);
    stInitData.somSelf = somSelf;

    hwndProgress = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, _dlgProc,
                          utilGetModuleHandle(), IDDLG_PROGRESS, &stInitData );
    if ( hwndProgress == NULLHANDLE )
    {
      _wpReleaseObjectMutexSem( somSelf );
      return FALSE;
    }
  }

  WinSendMsg( _hwndProgress, WM_PR_ADD_PID, MPFROMLONG(ulPID), 0 );

  return TRUE;
}

// Can be called from any thread.
VOID prStop(Rdesktop *somSelf, ULONG ulPID)
{
  RdesktopData *somThis = RdesktopGetData( somSelf );

  if ( _hwndProgress != NULLHANDLE )
    WinSendMsg( _hwndProgress, WM_PR_DELETE_PID, MPFROMLONG(ulPID), 0 );
}

BOOL prSwitchTo(Rdesktop *somSelf)
{
  RdesktopData *somThis = RdesktopGetData( somSelf );

  if ( _hwndProgress != NULLHANDLE )
  {
    WinSendMsg( _hwndProgress, WM_PR_SWITCH_TO, 0, 0 );
    return TRUE;
  }

  return FALSE;
}
