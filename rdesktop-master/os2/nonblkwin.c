/*
  Implementation for the function nonblockingWin().
   
  This module allows to drag and resize windows without blocking in
  WinDispatchMsg().

  /Digi 2016/

*/

#define INCL_WIN
#include <os2.h>
#include "debug.h"

// Current window change mode (move or resize).
#define PRS_MOVE          0
#define PRS_TOP_LEFT      1
#define PRS_TOP           2
#define PRS_TOP_RIGHT     3
#define PRS_LEFT          4
#define PRS_RIGHT         5
#define PRS_BOTTOM_LEFT   6
#define PRS_BOTTOM        7
#define PRS_BOTTOM_RIGHT  8

static PFNWP           oldWndFrameProc;
static PFNWP           oldWndTitlebarProc;
static HWND            hwndMotion = NULLHANDLE; // Frame window.
static POINTL          ptMouseStartPos;
static RECTL           rcWinStart;
static LONG            lFrameCX, lFrameCY, lCornerCX, lCornerCY;
static LONG            lPtrResizeState = PRS_MOVE;

// System pointer IDs for each window move/resize mode.
static LONG aPRSToPointerId[9] =
{
  SPTR_ARROW,          // 0 PRS_MOVE
  SPTR_SIZENWSE,       // 1 PRS_TOP_LEFT
  SPTR_SIZENS,         // 2 PRS_TOP
  SPTR_SIZENESW,       // 3 PRS_TOP_RIGHT
  SPTR_SIZEWE,         // 4 PRS_LEFT
  SPTR_SIZEWE,         // 5 PRS_RIGHT
  SPTR_SIZENESW,       // 6 PRS_BOTTOM_LEFT
  SPTR_SIZENS,         // 7 PRS_BOTTOM
  SPTR_SIZENWSE        // 8 PRS_BOTTOM_RIGHT
};


static VOID _querySysValues()
{
  lFrameCX  = WinQuerySysValue( HWND_DESKTOP, SV_CXSIZEBORDER );
  lFrameCY  = WinQuerySysValue( HWND_DESKTOP, SV_CYSIZEBORDER );
  lCornerCX = WinQuerySysValue( HWND_DESKTOP, SV_CXICON ) / 2;
  lCornerCY = WinQuerySysValue( HWND_DESKTOP, SV_CYICON ) / 2;
}

// LONG _queryPtrResizeState(HWND hwnd, PPOINTL ppointl)
// Returns PRS_xxxxx value for the current mouse position in frame window.

static LONG _queryPtrResizeState(HWND hwnd, PPOINTL ppointl)
{
  RECTL      rectl;
  LONG       lResult;
  ULONG      ulStyle = WinQueryWindowULong( hwnd, QWL_STYLE );

  if ( (ulStyle & FS_SIZEBORDER) == 0 )
    // Non-resizable window.
    return PRS_MOVE;

  WinQueryWindowRect( hwnd, &rectl );
  WinInflateRect( 0, &rectl, -lFrameCX, -lFrameCY );

  if ( WinPtInRect( 0, &rectl, ppointl) )
    // Mouse not on border.
    return PRS_MOVE;

  WinInflateRect( 0, &rectl, -lCornerCX, -lCornerCY );

  if ( ppointl->y > rectl.yTop )
  {
    if ( ppointl->x < rectl.xLeft )
      lResult = PRS_TOP_LEFT;
    else if ( ppointl->x > rectl.xRight )
      lResult = PRS_TOP_RIGHT;
    else
      lResult = PRS_TOP;
  }
  else if ( ppointl->y > rectl.yBottom )
    lResult = ppointl->x < rectl.xLeft ? PRS_LEFT : PRS_RIGHT;
  else
  {
    if ( ppointl->x < rectl.xLeft )
      lResult = PRS_BOTTOM_LEFT;
    else if ( ppointl->x > rectl.xRight )
      lResult = PRS_BOTTOM_RIGHT;
    else
      lResult = PRS_BOTTOM;
  }
      
  return lResult;
}


static MRESULT EXPENTRY wndFrameProc(HWND hwnd, ULONG msg, MPARAM mp1,
                                     MPARAM mp2)
{
  switch( msg )
  {
    case WM_TRACKFRAME:
      // This message is sent to a window whenever it is to be moved or sized.
      // We catch this message to avoid window moves/resizes by system.
      return (MRESULT)TRUE;

    case WM_BUTTON1DOWN:
    case WM_BUTTON2DOWN:
      // Start move/resize window.
      {
        HWND         hwndParent = WinQueryWindow( hwnd, QW_PARENT );

        hwndMotion = hwnd;
        WinQueryPointerPos( HWND_DESKTOP, &ptMouseStartPos );

        WinQueryWindowRect( hwnd, &rcWinStart );
        WinMapWindowPoints( hwnd, hwndParent, (PPOINTL)&rcWinStart, 2 );

        if ( (SHORT2FROMMP(mp2) & KC_CTRL) == 0 )
          // A typical for PM processing when CTRL is pressed - do not move
          // window to the forefront.
          WinSetWindowPos( hwnd, HWND_TOP, 0, 0, 0, 0,
                           SWP_ZORDER | SWP_ACTIVATE );

        if ( !WinSetCapture( HWND_DESKTOP, hwnd ) )
          debug( "WinSetCapture() failed" );
      }
      return (MRESULT)TRUE;

    case WM_BUTTON1UP:
    case WM_BUTTON2UP:
      // End move/resize window.
      hwndMotion = NULLHANDLE;
      lPtrResizeState = PRS_MOVE;
      WinSetCapture( HWND_DESKTOP, NULLHANDLE );
      return (MRESULT)TRUE;

    case WM_MOUSEMOVE:
      {
        POINTL         ptMousePos;

        ptMousePos.x = (SHORT)SHORT1FROMMP(mp1);
        ptMousePos.y = (SHORT)SHORT2FROMMP(mp1);

        if ( hwndMotion != hwnd )
          // Window does not moves/resize now. Get mode for current mouse pos.
          lPtrResizeState = _queryPtrResizeState( hwnd, &ptMousePos );
        else
        {
          // Move/resize window.

          HWND         hwndParent = WinQueryWindow( hwnd, QW_PARENT );
          HWND         hwndClient = WinWindowFromID( hwnd, FID_CLIENT );
          RECTL        rectl;
          TRACKINFO    stTrackInfo;

          // Query minimum/maximum sizes for the window.
          WinSendMsg( hwnd, WM_QUERYTRACKINFO,
                      MPFROMLONG( TF_ALLINBOUNDARY ),
                      MPFROMP( &stTrackInfo ) );

          // Get mouse offset.
          WinQueryPointerPos( HWND_DESKTOP, &ptMousePos );
          ptMousePos.x -= ptMouseStartPos.x;
          ptMousePos.y -= ptMouseStartPos.y;

          // Query current window's rectangle.
          WinQueryWindowRect( hwnd, &rectl );
          WinMapWindowPoints( hwnd, hwndParent, (PPOINTL)&rectl, 2 );

          // Modify window's rectangle.
          switch( lPtrResizeState )
          {
            case PRS_TOP_LEFT:
              rectl.xLeft   = rcWinStart.xLeft + ptMousePos.x;

            case PRS_TOP:
              rectl.yTop    = rcWinStart.yTop + ptMousePos.y;
              break;

            case PRS_LEFT:
              rectl.xLeft   = rcWinStart.xLeft + ptMousePos.x;
              break;

            case PRS_TOP_RIGHT:
              rectl.yTop    = rcWinStart.yTop + ptMousePos.y;

            case PRS_RIGHT:
              rectl.xRight  = rcWinStart.xRight + ptMousePos.x;
              break;

            case PRS_BOTTOM_LEFT:
              rectl.xLeft   = rcWinStart.xLeft + ptMousePos.x;

            case PRS_BOTTOM:
              rectl.yBottom = rcWinStart.yBottom + ptMousePos.y;
              break;

            case PRS_MOVE:
              rectl.xLeft   = rcWinStart.xLeft + ptMousePos.x;
              rectl.yTop    = rcWinStart.yTop + ptMousePos.y;

            case PRS_BOTTOM_RIGHT:
              rectl.yBottom = rcWinStart.yBottom + ptMousePos.y;
              rectl.xRight  = rcWinStart.xRight + ptMousePos.x;
              break;
          }

          // Check minimum width.
          if ( ( rectl.xRight - rectl.xLeft ) < stTrackInfo.ptlMinTrackSize.x )
          {
            switch( lPtrResizeState )
            {
              case PRS_TOP_LEFT:
              case PRS_LEFT:
              case PRS_BOTTOM_LEFT:
                rectl.xLeft = rectl.xRight - stTrackInfo.ptlMinTrackSize.x;
              default:
                rectl.xRight = rectl.xLeft + stTrackInfo.ptlMinTrackSize.x;
            }
          }

          // Check minimum height.
          if ( ( rectl.yTop - rectl.yBottom ) < stTrackInfo.ptlMinTrackSize.y )
          {
            switch( lPtrResizeState )
            {
              case PRS_BOTTOM_LEFT:
              case PRS_BOTTOM:
              case PRS_BOTTOM_RIGHT:
                rectl.yBottom = rectl.yTop - stTrackInfo.ptlMinTrackSize.y;
              default:
                rectl.yTop = rectl.yBottom + stTrackInfo.ptlMinTrackSize.y;
            }
          }

          // Check maximum width.
          if ( ( rectl.xRight - rectl.xLeft ) > stTrackInfo.ptlMaxTrackSize.x )
          {
            switch( lPtrResizeState )
            {
              case PRS_TOP_LEFT:
              case PRS_LEFT:
              case PRS_BOTTOM_LEFT:
                rectl.xLeft = rectl.xRight - stTrackInfo.ptlMaxTrackSize.x;
              default:
                rectl.xRight = rectl.xLeft + stTrackInfo.ptlMaxTrackSize.x;
            }
          }

          // Check maximum height.
          if ( ( rectl.yTop - rectl.yBottom ) > stTrackInfo.ptlMaxTrackSize.y )
          {
            switch( lPtrResizeState )
            {
              case PRS_BOTTOM_LEFT:
              case PRS_BOTTOM:
              case PRS_BOTTOM_RIGHT:
                rectl.yBottom = rectl.yTop - stTrackInfo.ptlMaxTrackSize.y;
              default:
                rectl.yTop = rectl.yBottom + stTrackInfo.ptlMaxTrackSize.y;
            }
          }

          // Set new window size/position and update.
          WinSetWindowPos( hwnd, HWND_TOP, rectl.xLeft, rectl.yBottom,
                           rectl.xRight - rectl.xLeft,
                           rectl.yTop - rectl.yBottom,
                           SWP_SIZE | SWP_MOVE );
          WinInvalidateRect( hwndClient, NULL, FALSE );
          WinUpdateWindow( hwndClient );
        }
      }

      // Set cursor.
      WinSetPointer( HWND_DESKTOP,
                     WinQuerySysPointer( HWND_DESKTOP,
                                         aPRSToPointerId[lPtrResizeState],
                                         FALSE ) );
      return (MRESULT)TRUE;

    case WM_SYSVALUECHANGED:
      _querySysValues();
      break;
  }

  return oldWndFrameProc( hwnd, msg, mp1, mp2 );
}

static MRESULT EXPENTRY wndTitlebarProc(HWND hwnd, ULONG msg, MPARAM mp1,
                                        MPARAM mp2)
{
  switch( msg )
  {
    case WM_HITTEST:
      // Avoid system's move window process.
      return (MRESULT)HT_TRANSPARENT;
  }

  return oldWndTitlebarProc( hwnd, msg, mp1, mp2 );
}


VOID nonblockingWin(HWND hwndFrame, BOOL fResizable)
{
  _querySysValues();

  oldWndFrameProc    = WinSubclassWindow( hwndFrame, wndFrameProc );
  oldWndTitlebarProc = WinSubclassWindow(
                         WinWindowFromID( hwndFrame, FID_TITLEBAR ),
                         wndTitlebarProc );
}
