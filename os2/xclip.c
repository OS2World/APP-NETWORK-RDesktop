/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Protocol services - Clipboard functions
   Copyright 2003-2008 Erik Forsberg <forsberg@cendio.se> for Cendio AB
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 2003-2008
   Copyright 2006-2011 Pierre Ossman <ossman@cendio.se> for Cendio AB

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "rdesktop.h"

/*
  - MSDN: Clipboard Formats
    http://msdn.microsoft.com/library/en-us/winui/winui/windowsuserinterface/dataexchange/clipboard/clipboardformats.asp
*/

#include <iconv.h>
#include <wchar.h>
#include "os2rd.h"
#include "debug.h"

// Remote clipboard's formats.
#define RDP_CF_TEXT         1
#define RDP_CF_BITMAP       2
#define RDP_CF_METAFILEPICT 3
#define RDP_CF_SYLK         4
#define RDP_CF_DIF          5
#define RDP_CF_TIFF         6
#define RDP_CF_OEMTEXT      7
#define RDP_CF_DIB          8
#define RDP_CF_PALETTE      9
#define RDP_CF_PENDATA      10
#define RDP_CF_RIFF         11
#define RDP_CF_WAVE         12
#define RDP_CF_UNICODETEXT  13
#define RDP_CF_ENHMETAFILE  14
#define RDP_CF_HDROP        15
#define RDP_CF_LOCALE       16
#define RDP_CF_DIBV5        17
#define RDP_CF_MAX          18

#define WIN_CLIP_CLASS         "rdesktopClipboard"

// rdesktop.c
extern RD_BOOL         g_rdpclip;
extern char            g_codepage[16];

HWND                   hwndClipWin = NULLHANDLE;

static HEV             hevRequest = NULLHANDLE;
static BOOL            fUseClipboardViewer = TRUE;
// Current known formats in remote clipboard.
static ULONG           aulRDCbFormats[8] = { 0 };
// Currently requested fromat from the server. When !=0 we wait answer.
static ULONG           ulRequestedRDFmt = 0;  // RDP_CF_xxxxx
static ULONG           fRequestedRDFmtChanged = FALSE;
// New formats in local clipboard, we need to send this list to server.
static ULONG           aHaveFmt[8] = { 0 }; // RDP_CF_xxxxx
static ULONG           cHaveFmt = 0;
// fSkipWMDrawClipboard is TRUE - at this momen we place remote clipbord's
// formats in local clipbord. No need send formats back to the server.
static BOOL            fSkipWMDrawClipboard = FALSE;
// Window's thread Id.
static TID             tidClipWin = -1;
// Clipboard window data.
static HAB             habClipWin;
static HMQ             hmqClipWin;
static BOOL            fMainThreadClipboardOpen = FALSE;
static ATOM            ulUnicodeLocalFmt; // "text/unicode" local format id.

// fAdd is TRUE: Register remote desktop format ulRDFmt (RDP_CF_xxxxx).
// fAdd is FALSE: Returns TRUE if format is registered.
// Returns FALSE if format is not registered.
static BOOL _DRCbFormat(ULONG ulRDFmt, BOOL fAdd)
{
  ULONG      ulIdx;

  for( ulIdx = 0; ulIdx < ARRAY_SIZE(aulRDCbFormats); ulIdx++ )
  {
    if ( aulRDCbFormats[ulIdx] == ulRDFmt )
      return TRUE;

    if ( fAdd && ( aulRDCbFormats[ulIdx] == 0 ) )
    {
      aulRDCbFormats[ulIdx] = ulRDFmt;
      return TRUE;
    }
  }

  return FALSE;
}


//           Clipboard window.
//           -----------------
// We use fake unvisible window in separate thread to handle local clipboard's
// events. The thread can be blocked for a long time until the data is obtained
// from the server (in WM_RENDERFMT handler).

/* This function is called for WM_RENDERFMT events.
   The WM_RENDERFMT event is sent from the requestor to the clipboard owner
   to request clipboard data.  */
static VOID _wmRenderFmt(USHORT usFmt)
{
  ULONG      ulRC;
  ULONG      ulCount;

  debugCP();

  if ( fMainThreadClipboardOpen )
  {
    debug( "Clipboard opened in main thread - exit" );
    return;
  }

  ulRC = DosWaitEventSem( hevRequest, SEM_IMMEDIATE_RETURN );
  if ( ulRC != NO_ERROR )
  {
    if ( ulRC != ERROR_TIMEOUT )
      { debug( "#1 DosWaitEventSem(), rc = %u", ulRC ); }
    else
      { debug( "Error: Another clipboard request already was sent to the RDP "
             "server and not yet responded. Refusing this request." ); }
    return;
  }

  if ( usFmt == ulUnicodeLocalFmt )
    usFmt = CF_TEXT;

  switch( usFmt )
  {
    case CF_TEXT:
      // Select best format for text data from announced by server formats.
      if ( _DRCbFormat( RDP_CF_UNICODETEXT, FALSE ) )
        ulRequestedRDFmt = RDP_CF_UNICODETEXT;
      else if ( _DRCbFormat( RDP_CF_TEXT, FALSE ) )
        ulRequestedRDFmt = RDP_CF_TEXT;
      break;

    case CF_BITMAP:
      if ( _DRCbFormat( RDP_CF_DIBV5, FALSE ) )
        ulRequestedRDFmt = RDP_CF_DIBV5;
      else if ( _DRCbFormat( RDP_CF_DIB, FALSE ) )
        ulRequestedRDFmt = RDP_CF_DIB;
      break;
  }

  if ( ulRequestedRDFmt == 0 )
  {
    debug( "Unknown format was requested by system." );
    return;
  }

  ulRC = DosResetEventSem( hevRequest, &ulCount );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosResetEventSem(), rc = %u", ulRC );
    return;
  }

  debug( "Request server data for format %u...", ulRequestedRDFmt );
  // Signal to call cliprdr_send_data_request() from main thread in
  // xclipProcess().
  fRequestedRDFmtChanged = TRUE;

  // Waiting for the semaphore. It should be posted in ui_clip_handle_data(),
  // ui_clip_request_failed() or ui_clip_format_announce().
  debug( "Waiting for the event semaphore..." );
  ulRC = DosWaitEventSem( hevRequest, 60000 );
  if ( ulRC != NO_ERROR )
    { debug( "#2 DosWaitEventSem(), rc = %u", ulRC ); }

  memset( aulRDCbFormats, 0, sizeof(aulRDCbFormats) );
  debug( "done" );
}

/* Message WM_DRAWCLIPBOARD is sent to the clipboard viewer window whenever the
   contents of the clipboard change; that is, as a result of the
   WinCloseClipbrd function following a call to WinSetClipbrdData. */
static VOID _wmDrawClipboard()
{
  ULONG      ulFmt = 0;
  ULONG      cFmt = 0;
  ULONG      ulIdx;

  debugCP();

  if ( !WinOpenClipbrd( habClipWin ) )
  {
    debug( "WinOpenClipbrd() failed." );
    return;
  }

  memset( aHaveFmt, '\0', sizeof(aHaveFmt) );
  while( ( (ulFmt = WinEnumClipbrdFmts( habClipWin, ulFmt )) != 0 ) &&
         ( cFmt < ARRAY_SIZE(aHaveFmt) ) )
  {
    debug( "Local format: %u", ulFmt );

    if ( ulFmt == ulUnicodeLocalFmt )
      ulFmt = RDP_CF_UNICODETEXT;
    else
      switch( ulFmt )
      {
        case CF_TEXT:
          ulFmt = RDP_CF_UNICODETEXT;
          break;

        case CF_BITMAP:
          ulFmt = RDP_CF_DIBV5;
          break;

        default:
          debug( "  - unknown" );
          continue;
      }

    // Avoid duplicates.
    for( ulIdx = cFmt; ulIdx != 0; ulIdx-- )
      if ( aHaveFmt[ulIdx - 1] == ulFmt )
      {
        debug( "Remote format %lu already in announce list at #%lu",
               ulFmt, ulIdx - 1 );
        break;
      }

    if ( ulIdx == 0 )
    {
      debug( "Remote format to announce #%lu: %lu", cFmt, ulFmt );
      aHaveFmt[cFmt] = ulFmt;
      cFmt++;
    }
  }

  WinCloseClipbrd( habClipWin );

  cHaveFmt = cFmt;
  // Now xclipProcess() will detect that new formats are present (cHaveFmt<>0).

//  debug( "Formats: %u", cHaveFmt );
}

static VOID _wmXClipRDActivate(HWND hwnd, USHORT usActivate)
{
  HAB        hab = WinQueryAnchorBlock( hwnd );
  HWND       hwndOwner = WinQueryClipbrdOwner( hab );

  // A trick to avoid using "clipboard viewer" system service...

  if ( !usActivate )
  {
    // Before lose focus we requests all data in clipboard to provide data
    // rendering from the clipboard owner. Than we set our window as
    // clipboard owner.

    debugCP( "Activation" );
    if ( hwndOwner != hwnd )
    {
      ULONG    ulFmt = 0;

      if ( WinOpenClipbrd( hab ) )
      {
        while( (ulFmt = WinEnumClipbrdFmts( hab, ulFmt )) != 0 )
          WinQueryClipbrdData( hab, ulFmt );

        WinCloseClipbrd( hab );
      }

      WinSetClipbrdOwner( hab, hwnd );
    }

    return;
  }

  // We get a focus. If we are not a clipboard owner, we conclude that there
  // are new data in the clipboard.

  debugCP( "Deactivation" );
  if ( hwnd != hwndOwner )
    _wmDrawClipboard();
}

static MRESULT EXPENTRY wndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_DESTROYCLIPBOARD:
      debugCP( "WM_DESTROYCLIPBOARD" );
      // Make sure we're no longer the owner.
      if ( WinQueryClipbrdOwner( habClipWin ) == habClipWin )
        WinSetClipbrdOwner( habClipWin, NULLHANDLE );

      return (MRESULT)0;

    case WM_RENDERFMT:
      // Somebody in local system ask as to recevie a clipboard data.
      debugCP( "WM_RENDERFMT" );
      _wmRenderFmt( SHORT1FROMMP(mp1) );
      return (MRESULT)0;

    case WM_RENDERALLFMTS:
      // Shutdown. We cannot render clipboard data now.
      if ( !fMainThreadClipboardOpen )
      {
        WinOpenClipbrd( habClipWin );
        WinSetClipbrdOwner( habClipWin, NULLHANDLE );
        WinEmptyClipbrd( habClipWin );
        WinCloseClipbrd( habClipWin );
      }
      break;

    case WM_DRAWCLIPBOARD:
      // The contents of the clipboard was changed.
      if ( fSkipWMDrawClipboard )
      {
        debugCP( "WM_DRAWCLIPBOARD - skip" );
        break;
      }
      debugCP( "WM_DRAWCLIPBOARD" );
      _wmDrawClipboard();
      break;

    case WM_XCLIP_RDACTIVATE:
      if ( !fUseClipboardViewer )
        _wmXClipRDActivate( hwnd, SHORT1FROMMP(mp1) );
      return (MRESULT)0;
  }

  return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}

void threadClipWin(void *pData)
{
  QMSG       qmsg;

  habClipWin = WinInitialize( 0 );
  hmqClipWin = WinCreateMsgQueue( habClipWin, 0 );
  if ( hmqClipWin == NULLHANDLE )
  {
    debug( "WinCreateMsgQueue() failed" );
    return;
  }

  WinRegisterClass( habClipWin, WIN_CLIP_CLASS, wndProc, 0, 0 );
  hwndClipWin = WinCreateWindow( HWND_DESKTOP, WIN_CLIP_CLASS,
                                 NULL, 0, 0, 0, 0, 0, NULLHANDLE,
                                 HWND_BOTTOM, 1, NULL, NULL );

  debugCP( "-> win loop" );
  while( ( hevRequest != NULLHANDLE ) &&
         WinGetMsg( habClipWin, &qmsg, NULLHANDLE, 0, 0 ) &&
         ( qmsg.msg != WM_CLOSE ) )
    WinDispatchMsg( habClipWin, &qmsg );
  debugCP( "<- win loop" );

  WinDestroyWindow( hwndClipWin );
  WinDestroyMsgQueue( hmqClipWin );
}


//           Interface for rdesktop.
//           -----------------------

/* Called when the RDP server announces new clipboard data formats. */
void ui_clip_format_announce(uint8 *data, uint32 length)
{
  ULONG      ulFmt, ulFmtInfo;
  ULONG      ulTestFmtInfo, ulRC;

  debugCP();

  if ( hwndClipWin == NULLHANDLE )
  {
    debug( "No window" );
    return;
  }

  fMainThreadClipboardOpen = TRUE;

  // WM_RENDERFMT handler (_wmRenderFmt()) may be locked now, unlock it...
  DosPostEventSem( hevRequest );

  debug( "WinOpenClipbrd()..." );
  if ( !WinOpenClipbrd( habClipWin ) )
  {
    debug( "WinOpenClipbrd() failed" );
    fMainThreadClipboardOpen = FALSE;
    return;
  }

  debug( "WinSetClipbrdOwner(, NULLHANDLE)..." );
  WinSetClipbrdOwner( habClipWin, NULLHANDLE );
  debug( "WinEmptyClipbrd()..." );
  WinEmptyClipbrd( habClipWin );

  // Look for known formats. Store list in aucRDCbFormats[] and register
  // available formats.
  memset( aulRDCbFormats, 0, sizeof(aulRDCbFormats) );
  for( ; length != 0; length--, data++ )
  {
    switch( *data )
    {
      case RDP_CF_TEXT:
      case RDP_CF_OEMTEXT:
        ulFmt = CF_TEXT;
        ulFmtInfo = CFI_POINTER;
        break;

      case RDP_CF_UNICODETEXT:
        ulFmt = ulUnicodeLocalFmt;
        ulFmtInfo = CFI_POINTER;
        break;

      case RDP_CF_DIB:
      case RDP_CF_DIBV5:
        ulFmt = CF_BITMAP;
        ulFmtInfo = CFI_HANDLE;
        break;

      default:
        ulFmt = 0;
    }

    if ( ulFmt == 0 ) // Unhandled format.
      continue;

    debug( "Add format %u to the clipboard...", ulFmt );
    // Add format to the clipboard (only if new).
    if ( !WinQueryClipbrdFmtInfo( habClipWin, ulFmt, &ulTestFmtInfo ) &&
         !WinSetClipbrdData( habClipWin, 0, ulFmt, ulFmtInfo ) )
      debug( "WinSetClipbrdData(,,%u,%u) failed", ulFmt, ulFmtInfo );
    // Format set successfully, save original server's format.
    else if ( !_DRCbFormat( *data, TRUE ) )
      // List of original formats is full.
      break;
  }

  debug( "WinSetClipbrdOwner()..." );
  // Have formats in local clipboard - we should be a clipboard owner to
  // receive messages WM_RENDERFMT.
  if ( ( aulRDCbFormats[0] != 0 ) &&
       !WinSetClipbrdOwner( habClipWin, hwndClipWin ) )
    debug( "WinSetClipbrdOwner() failed" );

  debug( "Close clipboard..." );
  fSkipWMDrawClipboard = TRUE;
  debug( "WinCloseClipbrd()..." );
  WinCloseClipbrd( habClipWin );
  fSkipWMDrawClipboard = FALSE;

  debug( "Done" );
  fMainThreadClipboardOpen = FALSE;
}

/* Called when the RDP server responds with clipboard data (after we've
   requested it in xclipProcess()). Converts obtained data to local format,
   puts it in local clipboard and posts event semaphore to unblock window's
   WM_RENDERFMT handler (see _wmRenderFmt() ). */
void ui_clip_handle_data(uint8 *data, uint32 length)
{
  ULONG      ulRC;
  ULONG      ulRDFmt = ulRequestedRDFmt;

  if ( ( ulRequestedRDFmt == 0 ) || fRequestedRDFmtChanged )
  {
    debug( "Requested format was not specified" );
    return;
  }
  ulRequestedRDFmt = 0;

  debugCP();

  if ( length != 0 )
  {
    debug( "remote fmt: %u, data: (0x%X), length: %u", ulRDFmt, data, length );
    // We don't need open clipboard here. Clipboard now must be opened
    // clipboard deta recipient.

    switch( ulRDFmt )
    {
      case RDP_CF_TEXT:
      case RDP_CF_UNICODETEXT:
        {
          PCHAR          pcRes;
          ULONG          cbRes = length + 2;

          // Allocate buffer for local data.
          ulRC = DosAllocSharedMem( (PVOID)&pcRes, NULL, cbRes,
                                    PAG_WRITE | PAG_COMMIT | OBJ_GIVEABLE );
          if ( ulRC != NO_ERROR )
          {
            debug( "DosAllocSharedMem(&pcRes,,,), rc = %u", ulRC );
            break;
          }
          memcpy( pcRes, data, length );

          if ( ulRDFmt == RDP_CF_UNICODETEXT )
          {
            // We have received unicode text - put it in the local clipboard as
            // "text/unicode" format.
            if ( !WinSetClipbrdData( habClipWin, (ULONG)pcRes,
                                     ulUnicodeLocalFmt, CFI_POINTER ) )
            {
              debug( "WinSetClipbrdData(,pcResUnicode,%lu,) failed",
                     ulUnicodeLocalFmt );
              // Do not free pcRes buffer - use it for CF_TEXT format.
            }
            else
            {
              debugCP( "WinSetClipbrdData() - Ok" );

              // Allocate a new buffer for local data in CF_TEXT format.
              ulRC = DosAllocSharedMem( (PVOID)&pcRes, NULL, cbRes,
                                        PAG_WRITE | PAG_COMMIT | OBJ_GIVEABLE );
              if ( ulRC != NO_ERROR )
              {
                debug( "DosAllocSharedMem(&pcRes,,,), rc = %u", ulRC );
                break;
              }
            }

            // Convert received unicode text to the local code page to put
            // as local format CF_TEXT.
            cbRes = utilStrIConv( WINDOWS_CODEPAGE, g_codepage, (PCHAR)data,
                                  length, pcRes, cbRes );
          }

          debugCP( "WinSetClipbrdData()..." );
          if ( !WinSetClipbrdData( habClipWin, (ULONG)pcRes, CF_TEXT,
                                   CFI_POINTER ) )
          {
            debugCP( "WinSetClipbrdData() failed" );
            DosFreeMem( (PVOID)&pcRes );
          }
          else
            debugCP( "WinSetClipbrdData() - Ok" );
        }
        break;

      case RDP_CF_DIB:
      case RDP_CF_DIBV5:
        {
          HPS          hps = WinBeginPaint( hwndClipWin, NULLHANDLE, NULL );
          HBITMAP      hbm = utilWinToSysBitmap( hps, (PCHAR)data, length );

          WinEndPaint( hps );
          if ( hbm == NULLHANDLE )
            error( "Cannot convert image for the local clipboard.\n" );
          else if ( !WinSetClipbrdData( habClipWin, hbm, CF_BITMAP,
                    CFI_HANDLE ) )
          {
            debug( "WinSetClipbrdData() failed" );
            GpiDeleteBitmap( hbm );
          }
        }
        break;
    } // switch( ulRDFmt )
  } // if ( length != 0 )

  // Unblock WM_RENDERFMT handler (waiting in _wmRenderFmt()).
  debug( "Post semaphore..." );
  ulRC = DosPostEventSem( hevRequest );
  if ( ulRC != NO_ERROR )
    debug( "DosPostEventSem(), rc = %u", ulRC );


  // 27.09.2016 Call ui_clip_sync() to send fake notice when PRIMARYCLIPBOARD
  // mode not used.
  // In PRIMARYCLIPBOARD mode it will set our window as clipboard viewer if
  // is no clipboard viewer in the system at this time.
  // I think this is correct behavior, if not - ui_clip_sync() calling should
  // be replaced with:
  //   if ( !fUseClipboardViewer )
  //     cliprdr_send_simple_native_format_announce( RDP_CF_UNICODETEXT );
  //
  // 05.03.2018 No need fake notice any more (with new WM_XCLIP_RDACTIVATE).

  // ui_clip_sync();
}

/* Request failed. Unblock semaphore without placing data in the clipboard. */
void ui_clip_request_failed()
{
  ULONG      ulRC;

  debugCP();

  ulRC = DosPostEventSem( hevRequest );
  if ( ulRC != NO_ERROR )
    debug( "DosPostEventSem(), rc = %u", ulRC );
}

/* Server requested data from the local clipboard. */
void ui_clip_request_data(uint32 format)
{
  BOOL       fSuccess = FALSE;

  debug( "Request from server for format %u", format );

  fMainThreadClipboardOpen = TRUE;

  if ( !WinOpenClipbrd( habClipWin ) )
  {
    debug( "WinOpenClipbrd() failed." );
    fMainThreadClipboardOpen = FALSE;
    cliprdr_send_data( NULL, 0 );
    return;
  }

  switch( format )
  {
    case RDP_CF_TEXT:
    case RDP_CF_UNICODETEXT:
      {
        PSZ            pszClipText;
        ULONG          cbClipText;
        PSZ            pcRes;
        ULONG          cbRes;

        if ( format == RDP_CF_TEXT )
          debugCP( "RDP_CF_TEXT" );
        else
        {
          debugCP( "RDP_CF_UNICODETEXT" );

          pszClipText = (PSZ)WinQueryClipbrdData( habClipWin, ulUnicodeLocalFmt );
          if ( pszClipText != NULL )
          {
            // Unicode format requested and we have "text/unicode".
            debugCP( "We have \"text/unicode\" in local clipboard - use it" );

            cbRes = wcslen( (const wchar_t *)pszClipText ) * 2;
            cliprdr_send_data( (uint8 *)pszClipText, cbRes + 2 );
            fSuccess = TRUE;
            break;
          }
        }

        pszClipText = (PSZ)WinQueryClipbrdData( habClipWin, CF_TEXT );
        if ( pszClipText == NULL )
        {
          WinCloseClipbrd( habClipWin );
          break;
        }

        if ( format == RDP_CF_TEXT )
          cliprdr_send_data( (uint8 *)pszClipText, strlen( pszClipText ) + 1 );
        else
        {
          // Unicode format requested and we have data in CF_TEXT format.
          // Convert local CF_TEXT data to unicode and send it.

          cbClipText = strlen( pszClipText );
          cbRes = (cbClipText + 1) * 2;
          pcRes = debugMAlloc( cbRes );
          if ( pcRes == NULL )
            debug( "Not enough memory" );
          else
          {
            cbRes = utilStrIConv( g_codepage, WINDOWS_CODEPAGE, pszClipText,
                                  strlen( pszClipText ), pcRes, cbRes );

            cliprdr_send_data( (uint8 *)pcRes, cbRes + 2 );
            debugFree( pcRes );
            fSuccess = TRUE;
          }
        }
      }
      break;

    case RDP_CF_DIBV5:
      {
        HBITMAP hbm = (HBITMAP)WinQueryClipbrdData( habClipWin, CF_BITMAP );
        ULONG   cbDIB;
        PCHAR   pcDIB;

        debugCP( "RDP_CF_DIBV5" );

        if ( hbm == NULLHANDLE )
        {
          error( "Cannot get bitmap handle from the local clipboard.\n" );
          break;
        }

        cbDIB = utilSysBitmapToWinDIBV5( habClipWin, hbm, NULL, 0 );
        if ( cbDIB == 0 )
        {
          error( "Cannot convert image for the remote clipboard.\n" );
          break;
        }

        pcDIB = debugMAlloc( cbDIB );
        if ( pcDIB == NULL )
        {
          debug( "Not enough memory" );
          break;
        }

        cbDIB = utilSysBitmapToWinDIBV5( habClipWin, hbm, pcDIB, cbDIB );
        if ( cbDIB == 0 )
          error( "Cannot convert image for the remote clipboard.\n" );
        else
        {
          cliprdr_send_data( (uint8 *)pcDIB, cbDIB );
          fSuccess = TRUE;
        }

        debugFree( pcDIB );
      }
      break;
  }

  WinCloseClipbrd( habClipWin );
  fMainThreadClipboardOpen = FALSE;

  if ( !fSuccess )
    // No data available.
    cliprdr_send_data( NULL, 0 );

  // Make sure we're the clipboard viewer.
  ui_clip_sync();
}

void ui_clip_sync(void)
{
  HWND                 hwndClipboardViewer;

  if ( fUseClipboardViewer )
  {
    hwndClipboardViewer = WinQueryClipbrdViewer( habClipWin );
    if ( hwndClipboardViewer == hwndClipWin )
      return;

    if ( hwndClipboardViewer == NULLHANDLE )
    {
      if ( !WinSetClipbrdViewer( habClipWin, hwndClipWin ) )
        debug( "WinSetClipbrdViewer() failed" );
      else
      {
        static BOOL    fFirstTime = TRUE;
        ULONG          ulFmt = 0;

        debug( "We are clipboard viewer now (fFirstTime = %d)", fFirstTime );
        if ( !fFirstTime )
          return;

        fFirstTime = FALSE;

        // Check current formats in local clipboard. If we have some to
        // announce - it will be detected on WM_DRAWCLIPBOARD after
        // WinSetClipbrdViewer() and list of formats will be sent in
        // xclipProcess(). But if we have not any formats for remote server
        // we send fake notice (RDP_CF_UNICODETEXT) now.
        // Without it server does not send notices for us (func.
        // ui_clip_format_announce() never called). 8-( )
        fMainThreadClipboardOpen = TRUE;
        if ( WinOpenClipbrd( habClipWin ) )
        {
          do
            ulFmt = WinEnumClipbrdFmts( habClipWin, ulFmt );
          while( (ulFmt != ulUnicodeLocalFmt) && (ulFmt != CF_TEXT) &&
                 (ulFmt != CF_BITMAP) && (ulFmt != 0) );
          WinCloseClipbrd( habClipWin );
        }
        fMainThreadClipboardOpen = FALSE;

        if ( ulFmt == 0 )
          // We haven't formats for the server, send fake notice.
          cliprdr_send_simple_native_format_announce( RDP_CF_UNICODETEXT );
      }
    }
    else
      debug( "Clipboard viewer already exists in system" );
  }
  else
  {
//  // We can't know witch formats in our clipboard (will be) - send fake notice.
//  debug( "Send fake notice..." );

    static BOOL         fFirstTime = TRUE;

    if ( fFirstTime )
    {
      cliprdr_send_simple_native_format_announce( RDP_CF_UNICODETEXT );
      fFirstTime = FALSE;
    }
    else
      // Announce current local formats.
      _wmDrawClipboard();
  }
}

void ui_clip_set_mode(const char *optarg)
{
  g_rdpclip = True;

  if ( str_startswith( optarg, "PRIMARYCLIPBOARD" ) )
    fUseClipboardViewer = TRUE;
  else if ( str_startswith( optarg, "CLIPBOARD" ) )
    fUseClipboardViewer = FALSE;
  else
  {
    warning( "Invalid clipboard mode '%s'.\n", optarg );
    g_rdpclip = False;
  }

  debug( "fUseClipboardViewer: %d", fUseClipboardViewer );
}


//           Interface for os2win.c.
//           -----------------------

VOID xclipInit()
{
  ULONG      ulRC;

  if ( !g_rdpclip || ( hevRequest != NULLHANDLE ) )
    return;

  ulRC = DosCreateEventSem( NULL, &hevRequest, 0, TRUE );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosCreateEventSem(), rc = %u", ulRC );
    return;
  }

  tidClipWin = _beginthread( threadClipWin, NULL, 65535, NULL );
  if ( tidClipWin == -1 )
  {
    debug( "_beginthread() failed" );
    DosCloseEventSem( hevRequest );
    hevRequest = NULLHANDLE;
  }
  else if ( !cliprdr_init() )
    debug( "cliprdr_init() failed" );
  else
    ulUnicodeLocalFmt = WinAddAtom( WinQuerySystemAtomTable(),
                                      "text/unicode" );
}

VOID xclipDone()
{
  if ( hevRequest != NULLHANDLE )
  {
    WinPostMsg( hwndClipWin, WM_CLOSE, 0, 0 );
    DosPostEventSem( hevRequest );
    DosSleep( 1 );
    DosCloseEventSem( hevRequest );
    hevRequest = NULLHANDLE;
    WinDeleteAtom( WinQuerySystemAtomTable(), ulUnicodeLocalFmt );
  }
}

/* Called from ui_select(). */
VOID xclipProcess()
{
  if ( fRequestedRDFmtChanged )
  {
    debug( "Format requested from clipboard window: %u", ulRequestedRDFmt );

    // Clipboard window procedure requested (remote) clipboard data on event
    // WM_RENDERFMT. We will send this request to the server here.
    if ( ulRequestedRDFmt != 0 )
    {
      debug( "Call cliprdr_send_data_request(%u)...", ulRequestedRDFmt );
      cliprdr_send_data_request( ulRequestedRDFmt );
      // Request sent, wait RDP server response with clipboard data
      // ( ui_clip_handle_data() ) or fail signal ( ui_clip_request_failed() ).
      // Sometimes we have a subsequent call ui_clip_format_announce() instead
      // ui_clip_handle_data() or ui_clip_request_failed().
    }
    else
    {
      ULONG  ulRC = DosPostEventSem( hevRequest );

      debug( "WTF?! Post semaphore..." );
      if ( ulRC != NO_ERROR )
        debug( "DosPostEventSem(), rc = %u", ulRC );
    }
    fRequestedRDFmtChanged = FALSE;
  }

  if ( cHaveFmt != 0 )
  {
    // Clipboard window procedure filled the list of formats in local clipboard
    // on event WM_DRAWCLIPBOARD. We will send it to the server here.
    PBYTE    pbBuf = debugCAlloc( cHaveFmt, 36 );
    ULONG    ulIdx;

    if ( pbBuf == NULL )
      debug( "Not enough memory" );
    else
    {
      // Make format descriptors [ uint32 format + 32-byte description ].
      for( ulIdx = 0; ulIdx < cHaveFmt; ulIdx++ )
        *((PULONG)&pbBuf[ulIdx * 36]) = aHaveFmt[ulIdx];

      debug( "Call cliprdr_send_native_format_announce() for %u formats...",
             cHaveFmt );
      cliprdr_send_native_format_announce( (uint8 *)pbBuf, cHaveFmt * 36 );
      debugFree( pbBuf );
    }
    cHaveFmt = 0;
  }
}
