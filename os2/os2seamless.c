/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   User interface services - Generic
   Copyright (C) Jay Sorg 2004-2007

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

#include "../rdesktop.h"
#include "debug.h"
#include "os2rd.h"

extern int             g_tcp_port_rdp; /* in tcp.c */
extern RD_BOOL         g_seamless_rdp;
extern char            g_seamless_spawn_cmd[];
extern RD_BOOL         g_seamless_persistent_mode;
extern int             g_height;

extern HWND            hwndFrame;
extern HAB             hab;


BOOL                   fSeamlessActive = FALSE; // We are currently in seamless mode.
static PSEAMLESSWIN    pSeamlessWindows = NULL;
static BOOL            fSeamlessStarted = FALSE;
static BOOL            fSeamlessHidden = FALSE; // Desktop is hidden on server.
static BOOL            fLastWinRemoved = FALSE;


static BOOL _swRemoveWindow(PSEAMLESSWIN pWin)
{
  PSEAMLESSWIN         pSw, *ppPrevNext = &pSeamlessWindows;

  for( pSw = pSeamlessWindows; pSw != NULL; pSw = pSw->pNext )
  {
    if ( pSw == pWin )
    {
      *ppPrevNext = pSw->pNext;

      if ( pSw->pcIconBuffer != NULL )
        debugFree( pSw->pcIconBuffer );

      debugFree( pSw );

      fLastWinRemoved = ( pSeamlessWindows == NULL );
      return TRUE;
    }
    ppPrevNext = &pSw->pNext;
  }

  return FALSE;
}

static PSEAMLESSWIN _swGetWindowById(ULONG ulId)
{
  PSEAMLESSWIN         pSw;

  if ( !fSeamlessActive )
    return NULL;

  for( pSw = pSeamlessWindows; pSw != NULL; pSw = pSw->pNext )
  {
    if ( pSw->ulId == ulId )
      return pSw;
  }
  return NULL;
}

/*static PSEAMLESSWIN _swGetWindowByHWnd(HWND hwnd)
{
  PSEAMLESSWIN         pSw;

  if ( !fSeamlessActive )
    return NULLHANDLE;

  for( pSw = pSeamlessWindows; pSw != NULL; pSw = pSw->pNext )
  {
    if ( ( pSw->hwnd == hwnd ) || ( pSw->hwndFrame == hwnd ) )
      return pSw;
  }
  return NULL;
}*/


/*
 *           RDesktop functions implemetation.
 */

void ui_seamless_hide_desktop()
{
  debugCP();

  if ( !g_seamless_rdp || !fSeamlessStarted )
    return;

  if ( fSeamlessActive )
    swSeamlessToggle();

  fSeamlessHidden = True;
}

void ui_seamless_unhide_desktop()
{
  debugCP();

  if ( !g_seamless_rdp || !fSeamlessStarted )
    return;

  fSeamlessHidden = False;
  swSeamlessToggle();
}

void ui_seamless_create_window(unsigned long id, unsigned long group,
                               unsigned long parent, unsigned long flags)
{
  ULONG                ulWinFlags = FCF_NOBYTEALIGN;
  PSEAMLESSWIN         pSw;

  // Ignore CREATEs for existing windows.
  if ( _swGetWindowById( id ) != NULL )
  {
    debug( "Window already exists" );
    return;
  }

  if ( parent == 0 )
    ulWinFlags |= FCF_TASKLIST;

  if ( (flags & SEAMLESSRDP_CREATE_MODAL) != 0 )
    ulWinFlags |= FCF_SYSMODAL;

  pSw = debugMAlloc( sizeof(SEAMLESSWIN) );
  if ( pSw == NULL )
  {
    debug( "Not enough memory" );
    return;
  }

  pSw->hwndFrame = WinCreateStdWindow( HWND_DESKTOP, 0, &ulWinFlags,
                                 WIN_CLIENT_CLASS, NULL, 0, 0, 1, &pSw->hwnd );

  if ( pSw->hwndFrame == NULLHANDLE )
  {
    debug( "WinCreateStdWindow() failed" );
    debugFree( pSw );
    return;
  }

  pSw->ulId         = id;
  pSw->ulParentId   = parent;
  pSw->ulGroup      = group;
  pSw->lState       = SEAMLESSRDP_NOTYETMAPPED;
  pSw->pNext        = pSeamlessWindows;
  pSw->ulIconSize   = 0;
  pSw->ulIconOffset = 0;
  pSw->pcIconBuffer = NULL;
  pSeamlessWindows  = pSw;

  if ( !WinSetWindowPtr( pSw->hwnd, 4, pSw ) )
    debug( "WinSetWindowPtr() failed" );
}

void ui_seamless_destroy_window(unsigned long id, unsigned long flags)
{
  PSEAMLESSWIN         pSw = _swGetWindowById( id );

  if ( pSw == NULL )
  {
    debug( "Window 0x%X not found", id );
    return;
  }

  if ( !WinDestroyWindow( pSw->hwndFrame ) )
    debug( "WinDestroyWindow() failed" );

  if ( !_swRemoveWindow( pSw ) )
    debug( "_swRemoveWindow() failed" );
}

void ui_seamless_destroy_group(unsigned long id, unsigned long flags)
{
  PSEAMLESSWIN         pNext, pSw = pSeamlessWindows;
  PSEAMLESSWIN         *ppPrevNext = &pSeamlessWindows;

  while( pSw != NULL )
  {
    pNext = pSw->pNext;

    if ( pSw->ulGroup == id )
    {
      *ppPrevNext = pSw->pNext;
      WinDestroyWindow( pSw->hwndFrame );
      debugFree( pSw );
    }
    else
      ppPrevNext = &pSw->pNext;

    pSw = pNext;
  }
}

void ui_seamless_seticon(unsigned long id, const char *format,
                         int width, int height, int chunk,
                         const char *data, int chunk_len)
{
  PSEAMLESSWIN         pSw = _swGetWindowById( id );

  if ( pSw == NULL )
  {
    debug( "Window 0x%X not found", id );
    return;
  }

  if ( chunk == 0 )
  {
    if ( pSw->ulIconSize != 0 )
      warning( "ui_seamless_seticon: New icon started before previous "
               "completed\n" );

    if ( strcmp( format, "RGBA" ) != 0 )
    {
      warning( "ui_seamless_seticon: Uknown icon format \"%s\"\n", format );
      return;
    }

    pSw->ulIconSize = width * height * 4;
    if ( pSw->ulIconSize > (32 * 32 * 4) )
    {
      warning( "ui_seamless_seticon: Icon too large (%d bytes)\n",
               pSw->ulIconSize );
      pSw->ulIconSize = 0;
      return;
    }

    pSw->ulIconOffset = 0;
    pSw->pcIconBuffer = debugMAlloc( pSw->ulIconSize );
    if ( pSw->pcIconBuffer == NULL )
    {
      debug( "Not enough memory" );
      pSw->ulIconSize = 0;
      return;
    }
  }
  else if ( pSw->ulIconSize == 0 )
    return;

  if ( chunk_len > (pSw->ulIconSize - pSw->ulIconOffset) )
  {
    warning( "ui_seamless_seticon: Too large chunk received (%d bytes > "
             "%d bytes)\n",	chunk_len, pSw->ulIconSize - pSw->ulIconOffset );
    if ( pSw->pcIconBuffer != NULL )
    {
      debugFree( pSw->pcIconBuffer );
      pSw->pcIconBuffer = NULL;
    }
    pSw->ulIconSize = 0;
    return;
  }

  memcpy( &pSw->pcIconBuffer[pSw->ulIconOffset], data, chunk_len );
  pSw->ulIconOffset += chunk_len;

  if ( pSw->ulIconOffset == pSw->ulIconSize )
  {
    debug( "Icon %u x %u obtained.", width, height );

    // We shuld make system icon here and set it for the window. But I can't
    // test it - rdesktop never call this routine on my systems!
    // seamlessrdpshell.exe on server side do not set icon.

    if ( pSw->pcIconBuffer != NULL )
      debugFree( pSw->pcIconBuffer );
    pSw->ulIconSize = 0;
  }
}

void ui_seamless_delicon(unsigned long id, const char *format,
                         int width, int height)
{
  debugCP();
}

void ui_seamless_move_window(unsigned long id, int x, int y, int width,
                             int height, unsigned long flags)
{
  PSEAMLESSWIN         pSw;
  RECTL                rectl;

  if ( ( width == 0 ) || ( height == 0 ) )
    return;
	
  pSw = _swGetWindowById( id );
  if ( pSw == NULL )
  {
    debug( "Window 0x%X not found", id );
    return;
  }

  switch( pSw->lState )
  {
    case SEAMLESSRDP_MINIMIZED:
    case SEAMLESSRDP_MAXIMIZED:
      debug( "WTF?!" );
      return;
  }

  rectl.xLeft   = x;
  rectl.yBottom = g_height - y - height;
  rectl.xRight  = rectl.xLeft + width;
  rectl.yTop    = rectl.yBottom + height;
  WinCalcFrameRect( pSw->hwndFrame, &rectl, FALSE );

  if ( !WinSetWindowPos( pSw->hwndFrame, HWND_TOP, rectl.xLeft, rectl.yBottom,
                         rectl.xRight - rectl.xLeft, rectl.yTop - rectl.yBottom,
                         SWP_MOVE | SWP_SIZE | SWP_SHOW | SWP_ZORDER ) )
    debug( "WinSetWindowPos(%u,,,,,,) failed", hwndFrame );
}

void ui_seamless_restack_window(unsigned long id, unsigned long behind,
                                unsigned long flags)
{
  PSEAMLESSWIN         pSw;
  HWND                 hwndBehind;

  if ( behind != 0 )
  {
    pSw = _swGetWindowById( behind );
    if ( pSw == NULL )
    {
      debug( "Window 0x%X not found", behind );
      return;
    }

    hwndBehind = pSw->hwndFrame;
  }
  else
    hwndBehind = HWND_TOP;

  pSw = _swGetWindowById( id );
  if ( ( pSw == NULL ) ||
       !WinSetWindowPos( pSw->hwndFrame, hwndBehind, 0, 0, 0, 0, SWP_ZORDER ) )
  {
    debug( "WinSetWindowPos() failed" );
    return;
  }
}

void ui_seamless_settitle(unsigned long id, const char *title,
                          unsigned long flags)
{
  PSEAMLESSWIN         pSw = _swGetWindowById( id );

  if ( ( pSw != NULL ) && ( pSw->ulParentId == 0 ) )
  {
    ULONG    cbTitleIn = title == NULL ? 0 : strlen( title );
    ULONG    cbTitle   = MAX( cbTitleIn + 1, 24 );
    PSZ      pszTitle  = alloca( cbTitle );

    // Title passed in UTF-8 (not WINDOWS_CODEPAGE), convert it to the local CP.
    if ( utilStrIConv( "UTF-8", "", (PCHAR)title, cbTitleIn, pszTitle,
                       cbTitle ) == 0 )
      sprintf( pszTitle, "rdesktop 0x%X", (uint32)id );

    if ( !WinSetWindowText( pSw->hwndFrame, pszTitle ) )
      debug( "WinSetWindowText(%u, %s) failed", pSw->hwndFrame, pszTitle );
  }
}

void ui_seamless_setstate(unsigned long id, unsigned int state,
                          unsigned long flags)
{
  PSEAMLESSWIN         pSw = _swGetWindowById( id );
  ULONG                ulSwpFl;

  if ( pSw == NULL )
  {
    debug( "Window 0x%X not found", id );
    return;
  }

  switch( state )
  {
    case SEAMLESSRDP_NORMAL:
      ulSwpFl = SWP_SHOW | SWP_ACTIVATE;
      break;

    case SEAMLESSRDP_MAXIMIZED:
      ulSwpFl = SWP_MAXIMIZE;
      break;

    case SEAMLESSRDP_MINIMIZED:
      ulSwpFl = SWP_MINIMIZE;
      break;

    default:
      debug( "Invalid state %u", state );
      return;
  }

  if ( ( pSw->lState == SEAMLESSRDP_MINIMIZED ) &&
       ( state != SEAMLESSRDP_MINIMIZED ) )
    ulSwpFl |= SWP_RESTORE;

  pSw->lState = state;

  if ( !WinSetWindowPos( pSw->hwndFrame, HWND_TOP, 0, 0, 0, 0, ulSwpFl ) )
    debug( "WinSetWindowPos() failed" );
}

void ui_seamless_syncbegin(unsigned long flags)
{
  if ( !fSeamlessActive )
     return;

  /* Destroy all seamless windows */
  while( pSeamlessWindows != NULL )
  {
   WinDestroyWindow( pSeamlessWindows->hwndFrame );
     _swRemoveWindow( pSeamlessWindows );
  }
  fLastWinRemoved = FALSE;
}


void ui_seamless_ack(unsigned int serial)
{
//  debugPCP();
}

void ui_seamless_begin(RD_BOOL hidden)
{
  if ( !g_seamless_rdp || fSeamlessStarted )
    return;

  fSeamlessStarted = TRUE;
  fSeamlessHidden = (BOOL)hidden;
  fLastWinRemoved = FALSE;

  if ( !hidden )
    swSeamlessToggle();

  if ( g_seamless_spawn_cmd[0] != '\0' )
  {
    seamless_send_spawn( g_seamless_spawn_cmd );
    g_seamless_spawn_cmd[0] = 0;
  }

  seamless_send_persistent( g_seamless_persistent_mode );
}

void ui_seamless_end()
{
  /* Destroy all seamless windows */
  while( pSeamlessWindows != NULL )
  {
    WinDestroyWindow( pSeamlessWindows->hwndFrame );
    _swRemoveWindow( pSeamlessWindows );
  }

  fSeamlessActive  = FALSE;
  fSeamlessStarted = FALSE;
  fSeamlessHidden  = FALSE;
  fLastWinRemoved = FALSE;
}


/*
 *           Module public routines.
 */

VOID swInvalidate(PRECTL prectl)
{
  PSEAMLESSWIN         pSw;
  RECTL                rectl;

  if ( !fSeamlessActive )
    return;

  for( pSw = pSeamlessWindows; pSw != NULL; pSw = pSw->pNext )
  {
    rectl = *prectl;
    WinMapWindowPoints( HWND_DESKTOP, pSw->hwnd, (PPOINTL)&rectl, 2 );
    WinInvalidateRect( pSw->hwnd, &rectl, FALSE );
  }
}

VOID swUpdate()
{
  PSEAMLESSWIN         pSw;

  if ( !fSeamlessActive )
    return;

  for( pSw = pSeamlessWindows; pSw != NULL; pSw = pSw->pNext )
    WinUpdateWindow( pSw->hwnd );
}

VOID swFocus(HWND hwnd, BOOL fSet)
{
  PSEAMLESSWIN         pSw;

  if ( !fSeamlessActive )
    return;

  pSw = (PSEAMLESSWIN)WinQueryWindowPtr( hwnd, 4 );
  if ( pSw == NULL )
    return;

  if ( fSet )
  {
    if ( pSw->lState == SEAMLESSRDP_MINIMIZED )
    {
      pSw->lState = SEAMLESSRDP_NORMAL;
      seamless_send_state( pSw->ulId, SEAMLESSRDP_NORMAL, 0 );
    }

//  seamless_send_zchange( pSw->ulId, 0, 0 );
    seamless_send_focus( pSw->ulId, 0 );
  }
}

VOID swMoved(HWND hwnd)
{
/*  PSEAMLESSWIN         pSw = (PSEAMLESSWIN)WinQueryWindowPtr( hwnd, 4 );
  RECTL                rectl;

  if ( !fSeamlessActive || ( pSw == NULL ) )
    return;

  if ( pSw->lSkipMoveEvent > 0 )
  {
    pSw->lSkipMoveEvent--;
    return;
  }

  WinQueryWindowRect( pSw->hwnd, &rectl );
  WinMapWindowPoints( pSw->hwnd, HWND_DESKTOP, (PPOINTL)&rectl, 2 );

  seamless_send_position( pSw->ulId, rectl.xLeft, g_height - rectl.yTop,
                          rectl.xRight - rectl.xLeft,
                          rectl.yTop - rectl.yBottom, 0 );*/
}

VOID swRestored(HWND hwnd)
{
  PSEAMLESSWIN         pSw;

  if ( !fSeamlessActive )
    return;

  pSw = (PSEAMLESSWIN)WinQueryWindowPtr( hwnd, 4 );
  if ( ( pSw == NULL ) || ( pSw->lState != SEAMLESSRDP_MINIMIZED ) )
    return;

  pSw->lState = SEAMLESSRDP_NORMAL;
  seamless_send_state( pSw->ulId, SEAMLESSRDP_NORMAL, 0 );
}

BOOL swSeamlessToggle()
{
  if ( !g_seamless_rdp || !fSeamlessStarted || fSeamlessHidden )
    return FALSE;

  if ( fSeamlessActive )
  {
    /* Deactivate */
    while( pSeamlessWindows != NULL )
    {
      WinDestroyWindow( pSeamlessWindows->hwndFrame );
      _swRemoveWindow( pSeamlessWindows );
    }
    ui_create_window();
  }
  else
  {
    /* Activate */
    ui_destroy_window();
    seamless_send_sync();
  }

  fSeamlessActive = !fSeamlessActive;
  fLastWinRemoved = FALSE;
  return TRUE;
}

BOOL swNoWindowsLeft()
{
  return fSeamlessStarted && fSeamlessActive && fLastWinRemoved;
}
