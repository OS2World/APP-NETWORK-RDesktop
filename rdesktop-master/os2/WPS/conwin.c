#include <rdesktop.ih>
#include <conwin.h>
#include <time.h>

#define WM_CON_ADDMESSAGE        (WM_USER + 1)
#define WM_CON_CLEAR             (WM_USER + 2)
// CON_CLEAR_ALL: mp1 for WM_CON_CLEAR
#define CON_CLEAR_ALL            -1
#define MAX_CON_RECORDS          1500

typedef struct _DLGDATA {
  USHORT     usSize;
  Rdesktop   *somSelf;
} DLGDATA, *PDLGDATA;

typedef struct _CONRECORD {
  MINIRECORDCORE	stRecCore;
  ULONG           ulLevel;
  ULONG           ulPID;
  PSZ             pszMsg;
  CTIME           stTime;
} CONRECORD, *PCONRECORD;

// Parameter for WM_CON_ADDMESSAGE.
typedef struct _ADDMESSAGE {
  ULONG           ulLevel;
  ULONG           ulPID;
  PSZ             pszMsg;
} ADDMESSAGE, *PADDMESSAGE;

typedef struct _CONDATA {
  ULONG           cRecords;
  HWND            hwndMenu;
  PCONRECORD      pMenuRecord;
} CONDATA, *PCONDATA;

static BOOL _wmInitDlg(HWND hwnd, PDLGDATA pDlgData)
{
  Rdesktop        *somSelf = pDlgData->somSelf;
  HMODULE         hModule = utilGetModuleHandle();
  HWND            hwndCnt = WinWindowFromID( hwnd, IDD_CNT_RECORDS );
  PFIELDINFO      pFieldInfo;
  PFIELDINFO      pFldInf;
  CNRINFO         stCnrInf = { 0 };
  FIELDINFOINSERT	stFldInfIns = { 0 };
  CHAR            acBuf[64];
  PCONDATA        pConData = (PCONDATA)_wpAllocMem( somSelf, sizeof(CONDATA),
                                                    NULL );

  WinSetWindowPtr( hwnd, QWL_USER, somSelf );

  // Setup container.

  pConData->cRecords = 0;
  pConData->hwndMenu = WinLoadMenu( HWND_DESKTOP, hModule, IDMENU_CONSOLE );
  pConData->pMenuRecord = NULL;
  WinSetWindowPtr( hwndCnt, QWL_USER, pConData );

  pFldInf = (PFIELDINFO)WinSendMsg( hwndCnt, CM_ALLOCDETAILFIELDINFO,
                                    MPFROMLONG( 4 ), NULL );
  if ( pFldInf == NULL )
    return FALSE;
  pFieldInfo = pFldInf;

  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_OWNER;
  pFieldInfo->pTitleData = (string)NULL;
  pFieldInfo->offStruct = FIELDOFFSET( CONRECORD, ulLevel );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

  WinLoadString( NULLHANDLE, hModule, IDS_PID, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_ULONG | CFA_HORZSEPARATOR | CFA_RIGHT | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_CENTER;
  pFieldInfo->pTitleData = (string)_wpAllocMem( somSelf, strlen( acBuf ) + 1, NULL );
  strcpy( pFieldInfo->pTitleData, acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( CONRECORD, ulPID );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

  WinLoadString( NULLHANDLE, hModule, IDS_TIME, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_TIME | CFA_HORZSEPARATOR | CFA_CENTER | CFA_SEPARATOR;
  pFieldInfo->flTitle = CFA_CENTER;
  pFieldInfo->pTitleData = (string)_wpAllocMem( somSelf, strlen( acBuf ) + 1, NULL );
  strcpy( pFieldInfo->pTitleData, acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( CONRECORD, stTime );
  pFieldInfo = pFieldInfo->pNextFieldInfo;

  WinLoadString( NULLHANDLE, hModule, IDS_MESSAGE, sizeof(acBuf), acBuf );
  pFieldInfo->cb = sizeof(FIELDINFO);
  pFieldInfo->flData = CFA_STRING | CFA_HORZSEPARATOR | CFA_LEFT;
  pFieldInfo->flTitle = CFA_LEFT;
  pFieldInfo->pTitleData = (string)_wpAllocMem( somSelf, strlen( acBuf ) + 1, NULL );
  strcpy( pFieldInfo->pTitleData, acBuf );
  pFieldInfo->offStruct = FIELDOFFSET( CONRECORD, pszMsg );

  stFldInfIns.cb = sizeof(FIELDINFOINSERT);
  stFldInfIns.pFieldInfoOrder = (PFIELDINFO)CMA_FIRST;
  stFldInfIns.cFieldInfoInsert = 4;
  WinSendMsg( hwndCnt, CM_INSERTDETAILFIELDINFO, MPFROMP( pFldInf ),
              MPFROMP( &stFldInfIns ) );

  stCnrInf.cb = sizeof(CNRINFO);
  stCnrInf.flWindowAttr = CV_DETAIL | CA_DETAILSVIEWTITLES;
  WinSendMsg( hwndCnt, CM_SETCNRINFO, MPFROMP( &stCnrInf ),
              MPFROMLONG( CMA_FLWINDOWATTR ) );

  return TRUE;
}

static VOID _wmDestroy(HWND hwnd)
{
  Rdesktop     *somSelf = (Rdesktop *)WinQueryWindowPtr( hwnd, QWL_USER );
  HWND         hwndCnt = WinWindowFromID( hwnd, IDD_CNT_RECORDS );
  PFIELDINFO   pFldInf;
  PCONDATA     pConData = (PCONDATA)WinQueryWindowPtr( hwndCnt, QWL_USER );

  // Remove all records from the container.
  WinSendMsg( hwnd, WM_CON_CLEAR, MPFROMLONG(CON_CLEAR_ALL),
              MPFROMLONG(FALSE) );

  // Free titles strings of details view.
  pFldInf = (PFIELDINFO)WinSendMsg( hwndCnt, CM_QUERYDETAILFIELDINFO, 0,
                                    MPFROMSHORT( CMA_FIRST ) );
  while( ( pFldInf != NULL ) && ( (LONG)pFldInf != -1 ) )
  {
    utilWPFreeMem( somSelf, pFldInf->pTitleData );
    pFldInf = (PFIELDINFO)WinSendMsg( hwndCnt, CM_QUERYDETAILFIELDINFO,
                           MPFROMP( pFldInf ), MPFROMSHORT( CMA_NEXT ) );
  }

  if ( pConData != NULL )
  {
    WinDestroyWindow( pConData->hwndMenu );
    _wpFreeMem( somSelf, (PBYTE)pConData );
  }
}

static BOOL _wmDrawItem(HWND hwnd, POWNERITEM pItem)
{
  static const ULONG aulColors[] = {
    0x00FFFFFF, // CONREC_SHELL
    0x00CACACA, // CONREC_INFO
    0x00FAA632, // CONREC_WARNING
    0x00FE0202  // CONREC_ERROR
  };
  ULONG      ulLevel;
  RECTL      rectl;

  if ( ((PCNRDRAWITEMINFO)pItem->hItem)->pRecord == NULL )
    return FALSE;

  ulLevel = ((PCONRECORD)((PCNRDRAWITEMINFO)pItem->hItem)->pRecord)->ulLevel;

  rectl = pItem->rclItem;
  WinInflateRect( NULLHANDLE, &rectl, (rectl.xRight - rectl.xLeft) / -4, -1 );

  GpiCreateLogColorTable( pItem->hps, 0, LCOLF_RGB, 0, 0, NULL );
  WinFillRect( pItem->hps, &rectl, aulColors[ulLevel % ARRAY_SIZE(aulColors)] );
  return TRUE;
}

static VOID _wmConAddMessage(HWND hwnd, PADDMESSAGE pMessage)
{
  Rdesktop        *somSelf = (Rdesktop *)WinQueryWindowPtr( hwnd, QWL_USER );
  HWND            hwndCnt = WinWindowFromID( hwnd, IDD_CNT_RECORDS );
  PCONDATA        pConData = (PCONDATA)WinQueryWindowPtr( hwndCnt, QWL_USER );
  PCONRECORD      pRecord;
  RECORDINSERT    stRecIns;
  struct tm       *pTM;
  time_t          timeMsg = time( NULL );

  // Allocate a new record for the container.
  pRecord = (PCONRECORD)WinSendMsg( hwndCnt, CM_ALLOCRECORD,
                     MPFROMLONG( sizeof(CONRECORD) - sizeof(MINIRECORDCORE) ),
                     MPFROMLONG( 1 ) );
  if ( pRecord == NULL )
    return;

  // Fill record data.
  pRecord->ulLevel = pMessage->ulLevel;
  pRecord->ulPID   = pMessage->ulPID;
  pRecord->pszMsg  = (string)_wpAllocMem( somSelf,
                                         strlen( pMessage->pszMsg ) + 1, NULL );
  if ( pRecord->pszMsg != NULL )
    strcpy( pRecord->pszMsg, pMessage->pszMsg );

  pTM = localtime( &timeMsg );
  pRecord->stTime.hours      = pTM->tm_hour;
  pRecord->stTime.minutes    = pTM->tm_min;
  pRecord->stTime.seconds    = pTM->tm_sec;
  pRecord->stTime.ucReserved = 0;

  // Remove oldest record.
  if ( pConData->cRecords >= MAX_CON_RECORDS )
    WinSendMsg( hwnd, WM_CON_CLEAR,
                MPFROMLONG( (pConData->cRecords - MAX_CON_RECORDS) + 1 ),
                MPFROMLONG( FALSE ) );

  // Insert record into the container.
  stRecIns.cb = sizeof(RECORDINSERT);
  stRecIns.pRecordOrder = (PRECORDCORE)CMA_END;
  stRecIns.pRecordParent = NULL;
  stRecIns.zOrder = (USHORT)CMA_TOP;
  stRecIns.cRecordsInsert = 1;
  stRecIns.fInvalidateRecord = TRUE;
  WinSendMsg( hwndCnt, CM_INSERTRECORD, pRecord, &stRecIns );
  pConData->cRecords++;

  // Scroll container to the last record.
  WinSendMsg( hwndCnt, CM_SCROLLWINDOW, MPFROMSHORT( CMA_VERTICAL ),
              MPFROMLONG( 0xFFFF ) );

  // Select the record if message is error.
  if ( pMessage->ulLevel == CONREC_ERROR )
    WinSendMsg( hwndCnt, CM_SETRECORDEMPHASIS, pRecord,
                MPFROM2SHORT(1,CRA_CURSORED | CRA_SELECTED) );
}

static VOID _wmConClear(HWND hwnd, LONG cDelete, BOOL fInvalidate)
{
  Rdesktop   *somSelf = (Rdesktop *)WinQueryWindowPtr( hwnd, QWL_USER );
  HWND       hwndCnt = WinWindowFromID( hwnd, IDD_CNT_RECORDS );
  PCONDATA   pConData = (PCONDATA)WinQueryWindowPtr( hwndCnt, QWL_USER );
  PCONRECORD pRecord, pNext;

  pRecord = (PCONRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD, 0,
                                    MPFROM2SHORT( CMA_FIRST, CMA_ITEMORDER ) );
  while( ( pRecord != NULL ) && ( (LONG)pRecord != -1 ) && ( cDelete != 0 ) )
  {
    pNext = (PCONRECORD)WinSendMsg( hwndCnt, CM_QUERYRECORD, MPFROMP(pRecord),
                                    MPFROM2SHORT( CMA_NEXT, CMA_ITEMORDER ) );

    utilWPFreeMem( somSelf, pRecord->pszMsg );

    WinSendMsg( hwndCnt, CM_REMOVERECORD, MPFROMP( &pRecord ),
                MPFROM2SHORT( 1, CMA_FREE ) );

    if ( pConData->cRecords != 0 )
      pConData->cRecords--;

    pRecord = pNext;
    cDelete--;
  }

  if ( fInvalidate )
    WinSendMsg( hwndCnt, CM_INVALIDATERECORD, NULL,
                MPFROM2SHORT( 0, CMA_ERASE | CMA_REPOSITION ) );
}

static VOID _wmWindowPosChanged(HWND hwnd, LONG lCX, LONG lCY)
{
  HWND       hwndCnt = WinWindowFromID( hwnd, IDD_CNT_RECORDS );
  ULONG      ulBorderCX = WinQuerySysValue( HWND_DESKTOP, SV_CXSIZEBORDER );
  RECTL      rectl;

  rectl.xLeft   = 0;
  rectl.xRight  = lCX;
  rectl.yBottom = 0;
  rectl.yTop    = lCY;
  WinCalcFrameRect( hwnd, &rectl, TRUE );

  WinSetWindowPos( hwndCnt, HWND_TOP, ulBorderCX, rectl.yBottom,
                   lCX - (ulBorderCX * 2), rectl.yTop - rectl.yBottom,
                   SWP_SIZE | SWP_MOVE );
}

static VOID _cnCtxMenu(HWND hwnd, PCONRECORD pRecord)
{
  HWND       hwndCnt = WinWindowFromID( hwnd, IDD_CNT_RECORDS );
  PCONDATA   pConData = (PCONDATA)WinQueryWindowPtr( hwndCnt, QWL_USER );
  POINTL     pt;

  WinQueryMsgPos( WinQueryAnchorBlock( hwnd ), &pt );
  WinMapWindowPoints( HWND_DESKTOP, hwndCnt, &pt, 1 );

  // Enable/disable item "Copy" if menu was shown for item / not for any item.
  WinSendMsg( pConData->hwndMenu, MM_SETITEMATTR,
              MPFROM2SHORT( IDMI_CON_COPY, TRUE ),
              MPFROM2SHORT( MIA_DISABLED, pRecord == NULL ? MIA_DISABLED : 0 ) );

  WinPopupMenu( hwndCnt, hwnd, pConData->hwndMenu, pt.x, pt.y, 0,
                PU_MOUSEBUTTON1 | PU_KEYBOARD | PU_HCONSTRAIN | PU_VCONSTRAIN );

  pConData->pMenuRecord = pRecord;
}

static VOID _wmCmdCopy(HWND hwnd)
{
  HWND       hwndCnt = WinWindowFromID( hwnd, IDD_CNT_RECORDS );
  PCONDATA   pConData = (PCONDATA)WinQueryWindowPtr( hwndCnt, QWL_USER );
  PCONRECORD pRecord = pConData->pMenuRecord;
  HAB        hab = WinQueryAnchorBlock( hwnd );
  PSZ        pszText;
  ULONG      ulRC;
  BOOL       fSuccess;

  if ( pRecord == NULL )
    return;

  // Copy string to the clipboard.

  ulRC = DosAllocSharedMem( (PPVOID)&pszText, 0, strlen( pRecord->pszMsg ) + 1,
                            PAG_COMMIT | PAG_READ | PAG_WRITE |
                            OBJ_GIVEABLE | OBJ_GETTABLE | OBJ_TILE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosAllocSharedMem() failed, rc = %u", ulRC );
    return;
  }
  strcpy( pszText, pRecord->pszMsg );

  if ( !WinOpenClipbrd( hab ) )
  {
    debug( "WinOpenClipbrd() failed" );
    fSuccess = FALSE;
  }
  else
  {    
    WinEmptyClipbrd( hab );

    fSuccess = WinSetClipbrdData( hab, (ULONG)pszText, CF_TEXT, CFI_POINTER );
    if ( !fSuccess )
      debug( "WinOpenClipbrd() failed" );

    WinCloseClipbrd( hab );
  }

  if ( !fSuccess )
    DosFreeMem( pszText );
}

static MRESULT EXPENTRY _dlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_INITDLG:
      return (MRESULT)_wmInitDlg( hwnd, (PDLGDATA)mp2 );

    case WM_DESTROY:
      _wmDestroy( hwnd );
      break;

    case WM_DRAWITEM:
      if ( SHORT1FROMMP(mp1) == IDD_CNT_RECORDS )
        return (MRESULT)_wmDrawItem( hwnd, (POWNERITEM)mp2 );
      return (MRESULT)FALSE;

    case WM_WINDOWPOSCHANGED:
      if ( (((PSWP)mp1)->fl & SWP_SIZE) != 0 )
        _wmWindowPosChanged( hwnd, ((PSWP)mp1)->cx, ((PSWP)mp1)->cy );
      break;

    case WM_CON_ADDMESSAGE:
      _wmConAddMessage( hwnd, (PADDMESSAGE)mp1 );
      break;

    case WM_CON_CLEAR:
      _wmConClear( hwnd, (LONG)mp1, (BOOL)mp2 );
      break;

    case WM_CONTROL:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDD_CNT_RECORDS:
          switch( SHORT2FROMMP( mp1 ) )
          {
            case CN_CONTEXTMENU:
              _cnCtxMenu( hwnd, (PCONRECORD)mp2 );
              break;
          }
          return (MRESULT)FALSE;
      }
      break;

    case WM_COMMAND:
      switch( SHORT1FROMMP( mp1 ) )
      {
        case IDMI_CON_COPY:
          _wmCmdCopy( hwnd );
          break;

        case IDMI_CON_CLEAR:
          WinSendMsg( hwnd, WM_CON_CLEAR, MPFROMLONG( CON_CLEAR_ALL ),
                      MPFROMLONG( TRUE ) );
          break;
      }
      return (MRESULT)TRUE;
  }

  return WinDefDlgProc( hwnd, msg, mp1, mp2 );
}


VOID cwInit(Rdesktop *somSelf)
{
  RdesktopData *somThis = RdesktopGetData( somSelf );
  DLGDATA      stDlgData;

  if ( _hwndConsole != NULLHANDLE )
    return;

  stDlgData.usSize = sizeof(DLGDATA);
  stDlgData.somSelf = somSelf;

  _hwndConsole = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP, _dlgProc,
                           utilGetModuleHandle(), IDDLG_CONSOLE, &stDlgData ); 
  WinSetWindowPos( _hwndConsole, HWND_TOP, 0, 0, 600, 400, SWP_SIZE );
}

VOID cwDone(Rdesktop *somSelf)
{
  RdesktopData *somThis = RdesktopGetData( somSelf );

  if ( _hwndConsole != NULLHANDLE )
  {
    WinDestroyWindow( _hwndConsole );
    _hwndConsole = NULLHANDLE;
  }
}

BOOL cwShow(Rdesktop *somSelf)
{
  RdesktopData *somThis = RdesktopGetData( somSelf );

  if ( _hwndConsole != NULLHANDLE )
  {
    // Set window title.
    HMODULE  hModule = utilGetModuleHandle();
    PSZ      pszObjTitle = _wpQueryTitle( somSelf );
    CHAR     acBuf[512];
    RECTL    rectWin = { 0 };
    ULONG    ulFlags;

    if ( pszObjTitle != NULL &&
         utilLoadInsertStr( hModule, TRUE, IDS_CONWIN_TITLE, 1, &pszObjTitle,
                            sizeof(acBuf), acBuf ) != 0 )
      WinSetWindowText( _hwndConsole, acBuf );

    // Show window.

    if ( !WinIsWindowVisible( _hwndConsole ) )
    {
      // Window is not visible - fit it in the desktop and move at mouse
      // position.

      HAB    hab = WinQueryAnchorBlock( _hwndConsole );
      RECTL  rectDT;
      POINTL pt;

      WinQueryPointerPos( HWND_DESKTOP, &pt );
      WinQueryWindowRect( _hwndConsole, &rectWin );
      WinQueryWindowRect( HWND_DESKTOP, &rectDT );

      // With and height is limited by desktop size.
      if ( (rectWin.xRight - rectWin.xLeft) > rectDT.xRight )
        rectWin.xRight = rectWin.xLeft + rectDT.xRight;
      if ( (rectWin.yTop - rectWin.yBottom) > rectDT.yTop )
        rectWin.yTop = rectDT.yTop + rectWin.yBottom;

      // Move window to the mouse's position.
      WinOffsetRect( hab, &rectWin,
                     pt.x - ( (rectWin.xRight - rectWin.xLeft) / 2 ),
                     pt.y - ( rectWin.yTop - rectWin.yBottom ) +
                     WinQuerySysValue( HWND_DESKTOP, SV_CYTITLEBAR ) / 2 +
                     WinQuerySysValue( HWND_DESKTOP, SV_CYSIZEBORDER ) );

      // Move window in desktop's boundaries.
      if ( rectWin.xRight > rectDT.xRight )
        WinOffsetRect( hab, &rectWin, rectDT.xRight - rectWin.xRight, 0 );
      if ( rectWin.xLeft < 0 )
        WinOffsetRect( hab, &rectWin, -rectWin.xLeft, 0 );
      if ( rectWin.yBottom < 0 )
        WinOffsetRect( hab, &rectWin, 0, -rectWin.yBottom );
      if ( rectWin.yTop > rectDT.yTop )
        WinOffsetRect( hab, &rectWin, 0, rectDT.yTop - rectWin.yTop );

      ulFlags = SWP_ACTIVATE | SWP_ZORDER | SWP_SHOW | SWP_MOVE | SWP_SIZE;
    }
    else
      // Window is visible - move it on top (do not change size and position).
      ulFlags = SWP_ACTIVATE | SWP_ZORDER;

    WinSetWindowPos( _hwndConsole, HWND_TOP, rectWin.xLeft, rectWin.yBottom,
                     rectWin.xRight - rectWin.xLeft,
                     rectWin.yTop - rectWin.yBottom, ulFlags );

    return TRUE;
  }

  return FALSE;
}

VOID cwAddMessage(Rdesktop *somSelf, ULONG pid, ULONG ulLevel, PSZ pszMsg)
{
  RdesktopData *somThis = RdesktopGetData( somSelf );
  ADDMESSAGE   stMessage;

  if ( ( _hwndConsole == NULLHANDLE ) || ( *pszMsg == '\0' ) )
    return;

  stMessage.ulPID = pid;
  stMessage.ulLevel = ulLevel;
  stMessage.pszMsg = pszMsg;
  WinSendMsg( _hwndConsole, WM_CON_ADDMESSAGE, MPFROMP( &stMessage ), 0 );

  if ( ulLevel == CONREC_ERROR )
    cwShow( somSelf );
}

// VOID cwAddMessage2(Rdesktop *somSelf, ULONG pid, PSZ pszMsg)
// Adds a rdesktop's message. Automatically detects the type of message.

typedef struct _RDESKTOPPMSG {
  ULONG      ulLevel;
  PSZ        pszMsg;
} RDESKTOPPMSG, *PRDESKTOPPMSG;

static RDESKTOPPMSG aMsgList[] =
{
  { CONREC_ERROR,   "ERROR: connect:" },
  { CONREC_ERROR,   "ERROR: Oops." },
  { CONREC_ERROR,   "ERROR: Channel table full, increase MAX_CHANNELS" },
  { CONREC_ERROR,   "Error creating ctrl socket:" },
  { CONREC_ERROR,   "Error binding ctrl socket:" },
  { CONREC_ERROR,   "Error listening on socket:" },
  { CONREC_ERROR,   "server: accept()" },
  { CONREC_ERROR,   "Error creating ctrl client socket: socket()" },
  { CONREC_ERROR,   "Error connecting to ctrl socket: connect()" },
  { CONREC_INFO,    "ERROR: get bitmap" },
  { CONREC_WARNING, "lseek" },
  { CONREC_WARNING, "write" },
  { CONREC_WARNING, "ftruncate" },
  { CONREC_WARNING, "opendir" },
  { CONREC_WARNING, "open" },
  { CONREC_WARNING, "fcntl" },
  { CONREC_WARNING, "ERROR: Maximum number of open files (" },
  { CONREC_WARNING, "close" },
  { CONREC_WARNING, "read" },
  { CONREC_WARNING, "stat" },
  { CONREC_WARNING, "rename" },
  { CONREC_WARNING, "NotifyInfo" },
  { CONREC_WARNING, "statfs" },
/*  { CONREC_WARNING, "ERROR: Bad packet header" },
  { CONREC_WARNING, "ERROR: expected DT, got" },
  { CONREC_WARNING, "ERROR: expected CC, got" },*/
  { CONREC_ERROR,   "Failed to connect," },
  { CONREC_WARNING, "Failed to negotiate protocol, retrying with plain RDP." },
  { CONREC_ERROR,   "ERROR: Expected RDP_NEG_RSP, got type =" },
  { CONREC_ERROR,   "ERROR: Unexpected protocol in negotiation response," },
  { CONREC_ERROR,   "Tried to reconnect for" },
//  { CONREC_WARNING, "ERROR: token len" },
  { CONREC_ERROR,   "ERROR: lspci protocol error: Invalid line" },
/*  { CONREC_WARNING, "ERROR: MCS connect:" },
  { CONREC_WARNING, "ERROR: expected AUcf, got" },
  { CONREC_WARNING, "ERROR: AUrq:" },
  { CONREC_WARNING, "ERROR: expected CJcf, got" },
  { CONREC_WARNING, "ERROR: CJrq:" },
  { CONREC_WARNING, "ERROR: expected data, got" },
  { CONREC_WARNING, "ERROR: error getting brush data, style" },
  { CONREC_WARNING, "ERROR: bad ROP2" },
  { CONREC_WARNING, "ERROR: polygon parse error" },
  { CONREC_WARNING, "ERROR: polygon2 parse error" },
  { CONREC_WARNING, "ERROR: polyline parse error" },
  { CONREC_WARNING, "ERROR: order parsing failed" },
  { CONREC_WARNING, "ERROR: error decompressed packet size exceeds max" },
  { CONREC_WARNING, "ERROR: error while decompressing packet" },
  { CONREC_WARNING, "ERROR: Protocol error in server redirection, unexpected data" },*/
  { CONREC_ERROR,   "ERROR: error while decompressing packet" },
  { CONREC_ERROR,   "ERROR: invalid irp device 0x" },
  { CONREC_ERROR,   "ERROR: IRP for bad device" },
  { CONREC_INFO,    "ERROR: RDPSND: Extra RDPSND_NEGOTIATE in the middle of a session" },
  { CONREC_INFO,    "ERROR: RDPSND: Extra RDPSND_REC_NEGOTIATE in the middle of a session" },
//  { CONREC_WARNING, "ERROR: RDPSND: Invalid format index" },
//  { CONREC_WARNING, "ERROR: RDPSND: Multiple RDPSND_REC_START" },
//  { CONREC_WARNING, "ERROR: RDPSND: Device not accepting format" },
//  { CONREC_WARNING, "ERROR: RDPSND: Split at packet header. Things will go south from here..." },
//  { CONREC_WARNING, "ERROR: channel_register" },
//  { CONREC_WARNING, "ERROR: No space to queue audio packet" },
//  { CONREC_WARNING, "ERROR: RSA magic 0x" },
  { CONREC_ERROR,   "ERROR: Bad server public key size (" },
  { CONREC_ERROR,   "ERROR: random len" },
  { CONREC_ERROR,   "ERROR: Server didn't send enough X509 certificates" },
  { CONREC_ERROR,   "ERROR: Couldn't load CA Certificate from server" },
  { CONREC_ERROR,   "ERROR: Couldn't load Certificate from server" },
  { CONREC_ERROR,   "ERROR: Security error CA Certificate invalid" },
  { CONREC_ERROR,   "ERROR: Bad server public key size (" },
  { CONREC_ERROR,   "ERROR: Problem extracting RSA exponent, modulus" },
  { CONREC_ERROR,   "ERROR: Failed to extract public key from certificate" },
  { CONREC_ERROR,   "ERROR: SSL_write:" },
  { CONREC_ERROR,   "ERROR: send:" },
  { CONREC_ERROR,   "ERROR: Remote peer initiated ssl shutdown." },
  { CONREC_ERROR,   "ERROR: SSL_read:" },
  { CONREC_ERROR,   "ERROR: recv:" },
  { CONREC_ERROR,   "ERROR: Connection closed" },
//  { CONREC_WARNING, "ERROR: tcp_tls_connect: SSL_CTX_new() failed to create TLS v1.0 context" },
//  { CONREC_WARNING, "ERROR: tcp_tls_connect: SSL_new() failed" },
//  { CONREC_WARNING, "ERROR: tcp_tls_connect: SSL_set_fd() failed" },
  { CONREC_ERROR,   "ERROR: tcp_tls_get_server_pubkey: SSL_get_peer_certificate() failed" },
  { CONREC_ERROR,   "ERROR: tcp_tls_get_server_pubkey: X509_get_pubkey() failed" },
  { CONREC_ERROR,   "ERROR: tcp_tls_get_server_pubkey: i2d_PublicKey() failed" },
  { CONREC_ERROR,   "ERROR: getaddrinfo:" },
  { CONREC_ERROR,   "ERROR: socket:" },
  { CONREC_ERROR,   "ERROR: connect:" },
  { CONREC_ERROR,   "ERROR: Cannot start thread." },
  { CONREC_WARNING, "share name " },
  { CONREC_WARNING, "WARNING: " },
  { CONREC_WARNING, "ERROR: " },
  { CONREC_ERROR,   "Usage: " },
  { CONREC_WARNING, "disconnect: Disconnect initiated by administration tool." },
  { CONREC_ERROR,   "disconnect: " },
  { CONREC_ERROR,   "Killed by SIGSEGV" },
  { 0, NULL }
};

VOID cwAddMessage2(Rdesktop *somSelf, ULONG pid, PSZ pszMsg)
{
#define _MSG_UNABLE_RESOLV      ": unable to resolve host"
#define _MSG_UNABLE_RESOLV_LEN  24

  ULONG          ulLevel = CONREC_INFO;
  ULONG          cbMsg = STR_LEN( pszMsg );
  ULONG          cbScanMsg;
  PRDESKTOPPMSG  pScanMsg;

  // Search message in the defined list and get message level.
  for( pScanMsg = aMsgList; pScanMsg->pszMsg != NULL; pScanMsg++ )
  {
    cbScanMsg = strlen( pScanMsg->pszMsg );
    if ( ( cbScanMsg <= cbMsg ) &&
         ( memcmp( pScanMsg->pszMsg, pszMsg, cbScanMsg ) == 0 ) )
    {
      ulLevel = pScanMsg->ulLevel;
      break;
    }
  }

  // Check for the message "ERROR: hostname: unable to resolve host".
  if ( ( cbMsg >= _MSG_UNABLE_RESOLV_LEN ) &&
       ( memcmp( _MSG_UNABLE_RESOLV, &pszMsg[cbMsg - _MSG_UNABLE_RESOLV_LEN],
                 _MSG_UNABLE_RESOLV_LEN ) == 0 ) )
    ulLevel = CONREC_ERROR;

  cwAddMessage( somSelf, pid, ulLevel, pszMsg );
}

VOID cwAddMessageFmt(Rdesktop *somSelf, ULONG pid, ULONG ulLevel,
                     ULONG ulMsgId, ULONG cArg, PSZ *apszArg)
{
  CHAR       acBuf[256];

  if ( utilLoadInsertStr( utilGetModuleHandle(), FALSE, ulMsgId, cArg,
                          apszArg, sizeof(acBuf), acBuf ) != 0 )
    cwAddMessage( somSelf, pid, ulLevel, acBuf );
}
