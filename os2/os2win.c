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
#include "../scancodes.h"

#include "debug.h"
#include "os2rd.h"
#include <sys/socket.h>
#include <signal.h>
#include "pmwinmou.h"

//#undef debug(s,...)
//#define debug(s,...) printf(__FILE__"/%s(): "#s"\n", __func__, ##__VA_ARGS__)

#define _KBD_FLAG_NEED_SYNC      0x80000000

VOID disk_init(); // disk.c
VOID disk_done(); // disk.c

extern int g_tcp_port_rdp; /* in tcp.c */
// rdesktop.c
extern char g_keymapname[PATH_MAX];
extern int g_server_depth;
extern RD_BOOL g_owncolmap;
extern RD_BOOL g_grab_keyboard;
extern RD_BOOL g_fullscreen;
extern char g_title[];
extern int g_sizeopt;
extern int g_width;
extern int g_height;
extern int g_xpos;
extern int g_ypos;
extern int g_pos;
extern RD_BOOL g_seamless_rdp;
extern RD_BOOL g_rdpclip;
extern RD_BOOL g_sendmotion;
extern RD_BOOL g_ownbackstore;	   // TRUE: -B was not specified.
extern RD_BOOL g_numlock_sync;     // TRUE: -N was specified.
extern RD_BOOL g_hide_decorations; // TRUE: -D was specified.
extern uint32 g_embed_wnd;         // -X n
// os2seamless.c
extern BOOL fSeamlessActive;

// Shared data.
HAB                    hab = NULLHANDLE;
HWND                   hwndFrame = NULLHANDLE;
HWND                   hwnd = NULLHANDLE;

// Private data.
static HMQ             hmq;
static HPOINTER        hptrCursor = NULLHANDLE;
static HPAL            hpalCurrent = NULLHANDLE;
static UCHAR           aucKeysPressed[4] = { 0 };
static HPS             hpsMem = NULLHANDLE;
static PFNWP           oldWndFrameProc;
static SHORT           sHSlider, sVSlider;
static PVOID           pTempMem = NULL;
static ULONG           cbTempMem = 0;
static ULONG           ulSyncKbdFlags = ~_KBD_FLAG_NEED_SYNC;
 // ulSyncKbdFlags:
 //   KBD_FLAG_SCROLL      0x0001
 //   KBD_FLAG_NUMLOCK     0x0002
 //   KBD_FLAG_CAPITAL     0x0004
 //   _KBD_FLAG_NEED_SYNC  0x80000000
 //
 // On start it equals ~_KBD_FLAG_NEED_SYNC - no "need sync." flag but some
 // value not equals keyboard state. In this case syncronization will be send
 // from _wmChar(), when user pressed some key first time. After, all
 // syncronizations will be send from _wmActivate().


#define _AMOUSE_DLL                        "AMouDll.dll"
#define _AMOUSE_WINREGISTERFORWHEELMSG     "WinRegisterForWheelMsg"
static HMODULE         hmAMouDll = NULLHANDLE;
static BOOL _stdcall (*pfnWinRegisterForWheelMsg)(HWND hwnd, ULONG flWindow);


typedef struct _WINDATA {
  BYTE       bData[1];
} WINDATA, *PWINDATA;

static LONG aRDOpCodesToROP[16] =
{// GpiBitBlt(,,,,lRop,)  | NN | Logic           | rdesktop const.
    ROP_ZERO,            //  0   BLACKNESS
    ROP_NOTSRCERASE,     //  1   NOT (src | dst)
    0x22,                //  2   NOT (src) & dst
    ROP_NOTSRCCOPY,      //  3   NOT (src)
    ROP_SRCERASE,        //  4   src & NOT (dst)
    ROP_DSTINVERT,       //  5   NOT (dst)
    ROP_SRCINVERT,       //  6   src ^ dst         ROP2_XOR
    0x77,                //  7   NOT (src & dst)
    ROP_SRCAND,          //  8   src & dst         ROP2_AND
    0x99,                //  9   NOT (src) ^ dst   ROP2_NXOR
    0xAA,                // 10   dst
    ROP_MERGEPAINT,      // 11   NOT (src) | dst
    ROP_SRCCOPY,         // 12   src               ROP2_COPY
    0xDD,                // 13   src | NOT (dst)
    ROP_SRCPAINT,        // 14   src | dst         ROP2_OR
    ROP_ONE              // 15   WHITENESS
};

static LONG aRDOpCodesToMix[16] =
{// GpiSetMix(,lMixMode)  | NN | Logic           | rdesktop const.
    FM_ZERO,             //  0   BLACKNESS
    FM_NOTMERGESRC,      //  1   NOT (src | dst)
    FM_SUBTRACT,         //  2   NOT (src) & dst
    FM_NOTCOPYSRC,       //  3   NOT (src)
    FM_MASKSRCNOT,       //  4   src & NOT (dst)
    FM_INVERT,           //  5   NOT (dst)
    FM_XOR,              //  6   src ^ dst         ROP2_XOR
    FM_NOTMASKSRC,       //  7   NOT (src & dst)
    FM_AND,              //  8   src & dst         ROP2_AND
    FM_NOTXORSRC,        //  9   NOT (src) ^ dst   ROP2_NXOR -- not supported?
    FM_LEAVEALONE,       // 10   dst
    FM_MERGENOTSRC,      // 11   NOT (src) | dst
    FM_OVERPAINT,        // 12   src               ROP2_COPY
    FM_MERGESRCNOT,      // 13   src | NOT (dst)
    FM_OR,               // 14   src | dst         ROP2_OR
    FM_ONE               // 15   WHITENESS
};

static LONG aRDHatchToPatSym[6] = {
  PATSYM_HORIZ,     // 0 bsHorizontal
  PATSYM_VERT,      // 1 bsVertical
  PATSYM_DIAG1,     // 2 bsFDiagonal
  PATSYM_DIAG3,     // 3 bsBDiagonal
  PATSYM_HATCH,     // 4 bsCross
  PATSYM_DIAGHATCH  // 5 bsDiagCross
};

#define RDOpCodesToROP(oc) \
 ( ((oc) >= 0) && ((oc) < ARRAY_SIZE(aRDOpCodesToROP) ) ? \
                  aRDOpCodesToROP[oc] : ROP_SRCCOPY )

#define RDOpCodesToMix(oc) \
 ( ((oc) >= 0) && ((oc) < ARRAY_SIZE(aRDOpCodesToMix) ) ? \
                  aRDOpCodesToMix[oc] : ROP_SRCCOPY )

#define COL15R(c) ( ((c >> 7) & 0xF8) | ((c >> 12) & 0x07) )
#define COL15G(c) ( ((c >> 2) & 0xF8) | ((c >> 8)  & 0x07) )
#define COL15B(c) ( ((c << 3) & 0xF8) | ((c >> 2)  & 0x07) )
#define COL16R(c) ( ((c >> 8) & 0xF8) | ((c >> 13) & 0x07) )
#define COL16G(c) ( ((c >> 3) & 0xFC) | ((c >> 9)  & 0x03) )
#define COL16B(c) ( ((c << 3) & 0xF8) | ((c >> 2)  & 0x07) )
#define COL24B(c) ( (c & 0xFF0000) >> 16 )
#define COL24G(c) ( (c & 0x00FF00) >> 8 )
#define COL24R(c) ( c & 0x0000FF )

static LONG _RDColToRGB(int iClr)
{
  switch( g_server_depth )
  {
    case 8:  return iClr;
    case 15: return (COL15R(iClr) << 16) | COL15G(iClr) << 8 | COL15B(iClr);
    case 16: return (COL16R(iClr) << 16) | COL16G(iClr) << 8 | COL16B(iClr);
    case 24:
    case 32: return (COL24R(iClr) << 16) | COL24G(iClr) << 8 | COL24B(iClr);
  }
  return 0;
}

static VOID _sigBreak(int sinno)
{
  if ( hab != NULLHANDLE )
  {
    debug( "Post WM_QUIT to a message queue" );
    WinPostQueueMsg( hmq, WM_QUIT, 0, 0 );
  }
}

static PVOID _getTempMem(ULONG ulSize)
{
  if ( ulSize > cbTempMem )
  {
    PVOID    pNew = debugReAlloc( pTempMem, ulSize );

    if ( pNew == NULL )
    {
      debug( "Not enough memory" );
      return NULL;
    }

    pTempMem = pNew;
    cbTempMem = ulSize;
  }

  return pTempMem;
}

static VOID _rectInclPoint(PRECTL prectl, PPOINTL ppointl)
{
  POINTL     pointl;

  if ( prectl->xLeft > ppointl->x )   prectl->xLeft   = ppointl->x;
  if ( prectl->yBottom > ppointl->y ) prectl->yBottom = ppointl->y;

  pointl.x = ppointl->x + 1;
  pointl.y = ppointl->y + 1;
  if ( prectl->xRight < pointl.x )  prectl->xRight  = pointl.x;
  if ( prectl->yTop < pointl.y )    prectl->yTop    = pointl.y;
}

static VOID _rectFromRDCoord(PRECTL prectl, int x, int y, int cx, int cy)
{
  prectl->xLeft   = x;
  prectl->yBottom = g_height - y - cy;
  prectl->xRight  = prectl->xLeft + cx;
  prectl->yTop    = prectl->yBottom + cy;
}

/* Convert DWORDs from 15-bit (555) to 16-bit colors (556). */
static VOID __cpy15to16(PBYTE pbDst, PBYTE pbSrc, ULONG cbLine)
{
  USHORT     usCol;

  for( ; cbLine >= 2; cbLine -= 2, pbSrc += 2, pbDst += 2 )
  {
    usCol = *(PUSHORT)pbSrc;
    *((PUSHORT)pbDst) = ( (usCol & 0x7FE0) << 1 ) | (usCol & 0x001F);
  }
}

static HBITMAP _createSysBitmap(HPS hps, ULONG ulInBPP, ULONG ulCX, ULONG ulCY,
                                PBYTE pbData, BOOL fVFlip)
{
  ULONG                ulBPP = ulInBPP == 15 ? 16 : ulInBPP;
  PBITMAPINFOHEADER2   pbmih = alloca( ulBPP == 8
                                  ? sizeof(BITMAPINFO2) + (sizeof(RGB2) << 8)
                                  : ulBPP == 1
                                    ? sizeof(BITMAPINFO2) + (sizeof(RGB2) << 1)
                                    : sizeof(BITMAPINFO2) );
  PBITMAPINFO2         pbmi = NULL;
  PBYTE                pbImage = NULL;
  HBITMAP              hbm;
  ULONG                cbSrcLine, cbDstLine = ( (ulBPP*ulCX + 31) / 32 ) * 4;

  if ( pbmih == NULL )
  {
    debug( "Not enough stack size" );
    return NULLHANDLE;
  }
  memset( pbmih, 0, sizeof(BITMAPINFOHEADER2) );

  if ( pbData != NULL )
  {
    register ULONG     ulIdx;
    register PBYTE     pbImageScan;
    LONG               lDataLineOffs;

    pbmi           = (PBITMAPINFO2)pbmih;
    pbmih->cbImage = cbDstLine * ulCY;

    // Copy the bitmap data (if specified).

    if ( ulBPP != 1 )
    {
      cbSrcLine = ( (ulBPP * ulCX + 15) / 16 ) * 2;

      if ( fVFlip )
      {
        pbData = &pbData[pbmih->cbImage - cbSrcLine]; // Last line in source.
        lDataLineOffs = -cbSrcLine;
      }
      else
        lDataLineOffs = cbSrcLine;

      if ( cbSrcLine == lDataLineOffs )
        pbImage = pbData;        // Good. We don't need to convert the image.
      else
      {
        pbImage = _getTempMem( pbmih->cbImage );
        if ( pbImage == NULL )
        {
          debug( "Not enough memory" );
          return NULLHANDLE;
        }

        ulIdx = 0, pbImageScan = pbImage;
        if ( ulInBPP == 15 )
        {
          for( ; ulIdx < ulCY;
               ulIdx++, pbImageScan += cbDstLine, pbData += lDataLineOffs )
            __cpy15to16( pbImageScan, pbData, cbSrcLine );
        }
        else
          for( ; ulIdx < ulCY;
               ulIdx++, pbImageScan += cbDstLine, pbData += lDataLineOffs )
            memcpy( pbImageScan, pbData, cbSrcLine );

        if ( ulBPP == 8 )
        {
          if ( hpalCurrent == NULLHANDLE )
            debug( "Warinig! palette was not specified!" );
          else
            GpiQueryPaletteInfo( hpalCurrent, NULLHANDLE, 0, 0, 256,
                                 (PULONG)&pbmi->argbColor );
        }
      }
    } // if ( ulBPP != 1 )
    else
    {
      BYTE     bPadMask;

      cbSrcLine = (ulCX + 7) / 8;
      bPadMask = 0xFF << ((cbSrcLine * 8) - ulCX);

      pbImage = _getTempMem( pbmih->cbImage );
      if ( pbImage == NULL )
      {
        debug( "Not enough memory" );
        return NULLHANDLE;
      }

      if ( fVFlip )
      {
        pbData = &pbData[(ulCY - 1) * cbSrcLine]; // Last line in source.
        lDataLineOffs = -cbSrcLine;
      }
      else
        lDataLineOffs = cbSrcLine;

      for( ulIdx = 0, pbImageScan = pbImage; ulIdx < ulCY;
           ulIdx++, pbImageScan += cbDstLine, pbData += lDataLineOffs )
      {
        memcpy( pbImageScan, pbData, cbSrcLine );
        if ( (ulCX & 1) != 0 )
          pbImageScan[cbSrcLine - 1] &= bPadMask;
        memset( &pbImageScan[cbSrcLine], 0, cbDstLine - cbSrcLine );
      }

      *((PULONG)&pbmi->argbColor[0]) = 0x00000000;
      *((PULONG)&pbmi->argbColor[1]) = 0x00FFFFFF;
    } // if ( ulBPP != 1 ) else
  } // if ( pbData != NULL )

  // Create a system bitmap object
  pbmih->cbFix           = sizeof(BITMAPINFOHEADER2);
  pbmih->cx              = ulCX;
  pbmih->cy              = ulCY;
  pbmih->cPlanes         = 1;
  pbmih->cBitCount       = ulBPP;

  hbm = GpiCreateBitmap( hps, pbmih, pbImage == NULL ? 0 : CBM_INIT, pbImage,
                         pbmi );

  if ( ( hbm == GPI_ERROR ) || ( hbm == 0 ) )
  {
    debug( "GpiCreateBitmap() failed" );
    return NULLHANDLE;
  }

  return hbm;
}

static RD_HBITMAP _rdbmpCreate(ULONG ulBPP, ULONG ulCX, ULONG ulCY,
                               PBYTE pbData)
{
  HPS        hpsMem;
  HDC        hdcMem = DevOpenDC( hab, OD_MEMORY, "*", 0, NULL, NULLHANDLE );
  SIZEL      sizel = { ulCX, ulCY };
  HBITMAP    hbmMem;

  if ( hdcMem == NULLHANDLE )
  {
    debug( "Cannot open memory device context" );
    return NULL;
  }

  // Create a new memory presentation space.
  hpsMem = GpiCreatePS( hab, hdcMem, &sizel,
                        PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC );
  if ( hpsMem == NULLHANDLE )
  {
    debug( "GpiCreatePS() failed. Memory PS was not created." );
    DevCloseDC( hdcMem );
    return NULL;
  }

  // Create a bitmap object.
  hbmMem = _createSysBitmap( hpsMem, ulBPP, ulCX, ulCY, pbData, TRUE );
  if ( hbmMem == NULLHANDLE )
  {
    debug( "_createSysBitmap() failed" );
    GpiDestroyPS( hpsMem );
    DevCloseDC( hdcMem );
    return NULL;
  }

  // Set bitmap object for the memory presentation space.
  if ( !GpiSetBitmapId( hpsMem, hbmMem, 1 ) )
    debug( "GpiSetBitmapId() failed" );
  if ( GpiSetBitmap( hpsMem, hbmMem ) == HBM_ERROR )
    debug( "GpiSetBitmap() failed" );

  // Return handle of created presentation space.
  return (RD_HBITMAP)hpsMem;
}

static VOID _rdbmpDestroy(RD_HBITMAP hRDBmp)
{
  HPS        hpsMem = (HPS)hRDBmp;
  HBITMAP    hbmMem = GpiSetBitmap( hpsMem, NULLHANDLE );
  HDC        hdcMem = GpiQueryDevice( hpsMem );

  if ( !GpiDestroyPS( hpsMem ) )
    debug( "GpiDestroyPS() failed" );

  if ( DevCloseDC( hdcMem ) == DEV_ERROR )
    debug( "DevCloseDC() failed" );

  if ( ( hbmMem != NULLHANDLE ) && !GpiDeleteBitmap( hbmMem ) )
    debug( "GpiDeleteBitmap() failed" );
}

static BOOL _rdbrushSet(HPS hps, int opcode, BRUSH *pRDBrush)
{
  HBITMAP    hbm = NULLHANDLE;
  POINTL     pointl;

  if ( pRDBrush != NULL )
  {
    switch( pRDBrush->style )
    {
      case 0:  // Solid
        GpiSetPattern( hps, PATSYM_SOLID );
        break;

      case 2:  // Hatch
        if ( !GpiSetPattern( hps, aRDHatchToPatSym[ pRDBrush->pattern[0] ] ) )
          debug( "GpiSetPattern() failed" );
        break;

      case 3:  // Pattern
        if ( pRDBrush->bd == NULL )             // rdp4 brush
        {
          //debug( "Brush: rdp4" );
          hbm = _createSysBitmap( hps, 1, 8, 8, (PBYTE)pRDBrush->pattern, TRUE );
        }
        else if ( pRDBrush->bd->colour_code > 1 ) // > 1 bpp
        {
          //debug( "Brush: colour_code: %u ", brush->bd->colour_code );
          hbm = _createSysBitmap( hps, g_server_depth, 8, 8,
                                  (PBYTE)pRDBrush->bd->data, FALSE );
        }
        else
        {
          //debug( "Brush: colour_code is 1" );
          hbm = _createSysBitmap( hps, 1, 8, 8, (PBYTE)pRDBrush->bd->data,
                                  FALSE );
        }

        // Set created bitmap as current pattern.
        if ( hbm == NULLHANDLE )
          debug( "_createSysBitmap() failed" );
        else if ( !GpiSetBitmapId( hps, hbm, 2 ) )
          debug( "GpiSetBitmapId() failed" );
        else if ( !GpiSetPatternSet( hps, 2 ) )
          debug( "GpiSetPatternSet() failed" );
        break;

      default:
        debug( "Unsupported brush style: %u", pRDBrush->style );
        return FALSE;
    }

    // Set pattern origin point.
    pointl.x = pRDBrush->xorigin;
    pointl.y = 7 - pRDBrush->yorigin;
    GpiSetPatternRefPoint( hps, &pointl );
  }

  // Set mix mode.
  if ( opcode >= 0 && opcode < ARRAY_SIZE(aRDOpCodesToMix) )
  {
    GpiSetMix( hps, aRDOpCodesToMix[opcode] );
    GpiSetBackMix( hps, aRDOpCodesToMix[opcode] );
  }

  return TRUE;
}

static VOID _rdbrushReset(HPS hps)
{
  LONG       lSet = GpiQueryPatternSet( hps );

  // Return to defaults.
  GpiSetMix( hps, FM_DEFAULT );
  GpiSetBackMix( hps, FM_DEFAULT );
  GpiSetPattern( hps, PATSYM_DEFAULT );
  GpiSetPatternSet( hps, LCID_DEFAULT );

  if ( lSet == 2 )
  {
    HBITMAP  hbm = GpiQueryBitmapHandle( hps, lSet );

    if ( hbm == GPI_ERROR )
      debug( "GpiQueryBitmapHandle() failed" );
    else if ( hbm != NULLHANDLE )
    {
      if ( !GpiDeleteSetId( hps, 2 ) )    // Free tag "2".
        debug( "GpiDeleteSetId() failed" );

      if ( !GpiDeleteBitmap( hbm ) )
        debug( "GpiDeleteBitmap() failed" );
    }
  }
}

static VOID _invalidateRect(PRECTL prectl)
{
  swInvalidate( prectl );

  WinOffsetRect( hab, prectl, -sHSlider, -sVSlider );
  WinInvalidateRect( hwnd, prectl, FALSE );
}

static VOID _toggleFullscreen()
{
  // Toggle fullscreen only when remode and local desktops have some size.
  if ( ( WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN ) != g_width ) ||
       ( WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN ) != g_height ) )
    return;

  if ( !swSeamlessToggle() )
  {
    ui_destroy_window();
    g_fullscreen = !g_fullscreen;
    ui_create_window();
  }
}



/*
 *           Window events.
 */

MRESULT EXPENTRY wndFrameProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_QUERYTRACKINFO:
      {
        MRESULT        mr = oldWndFrameProc( hwnd, msg, mp1, mp2 );
        PTRACKINFO     pTrackInfo = (PTRACKINFO)PVOIDFROMMP(mp2);
        RECTL          rectl;

        rectl.xLeft   = 0;
        rectl.yBottom = 0;
        rectl.xRight  = g_width;
        rectl.yTop    = g_height;
        WinCalcFrameRect( hwndFrame, &rectl, FALSE );

        pTrackInfo->ptlMaxTrackSize.x = rectl.xRight - rectl.xLeft;
        pTrackInfo->ptlMaxTrackSize.y = rectl.yTop - rectl.yBottom;
        return mr;
      }
  }

  return oldWndFrameProc( hwnd, msg, mp1, mp2 );
}


static VOID _wmPaint(HWND hwnd)
{
  POINTL               aPoints[3];
  HPS                  hps;
  HPS                  hpsMicro = (HPS)WinQueryWindowULong( hwnd, 0 );
  PSEAMLESSWIN         pSw = (PSEAMLESSWIN)WinQueryWindowPtr( hwnd, 4 );

  if ( hpsMem == NULLHANDLE )
  {
    debug( "No memory presentation space" );
    return;
  }

  /* IBM OS/2 DEVELOPER'S TOOLKIT, Presentation Manager Programming Reference:
     "Each time the application receives a WM_PAINT message, it should pass the
     handle of the micro presentation space as an argument to the WinBeginPaint
     function; this prevents the system from returning a cached-micro
     presentation space. The system modifies the visible region of the supplied
     micro presentation space and returns the presentation space to the
     application. This method enables the application to use the same
     presentation space for all drawing in a specified window." */
  /* if hpsMicro is NULLHANDLE (-B): Obtain a cache presentation space. */
  hps = WinBeginPaint( hwnd, hpsMicro, (PRECTL)&aPoints );
  if ( hps == NULLHANDLE )
    debug( "WinBeginPaint() failed" );
  else
  {
    aPoints[2] = aPoints[0];

    if ( pSw != NULL )
    {
      // Seamless window is "viewport" for remote desktop - source rectangle
      // position is a window's position.
      if ( !WinMapWindowPoints( pSw->hwnd, HWND_DESKTOP, &aPoints[2], 1 ) )
        debug( "WinMapWindowPoints() failed" );
    }
    else
    {
      // The main window may have scroll bars...
      aPoints[2].x += sHSlider;
      aPoints[2].y += sVSlider;
    }

    if ( GpiBitBlt( hps, hpsMem, 3, aPoints, ROP_SRCCOPY, 0 ) ==
         GPI_ERROR )
      debug( "GpiBitBlt() failed" );
  }
  WinEndPaint( hps );
}

/*
  static VOID _wmSize(HWND hwnd, USHORT usCX, USHORT usCY)

  - Creates/destroys scroll bars for the main window.
  - Creates a micro presentation space for any window (if a switch -B was not
    specified in the command line).

  IBM OS/2 DEVELOPER'S TOOLKIT, Presentation Manager Programming Reference:
    "The micro presentation space allows access to only a subset of the
    operating system graphics functions, but it uses less memory and is
    _faster_ than a normal presentation space."
*/
static VOID _wmSize(HWND hwnd, USHORT usCX, USHORT usCY)
{
  PSEAMLESSWIN         pSw = (PSEAMLESSWIN)WinQueryWindowPtr( hwnd, 4 );
  HPS                  hpsMicroOld = (HPS)WinQueryWindowULong( hwnd, 0 );
  HWND                 hwndFrame = WinQueryWindow( hwnd, QW_PARENT );
  HDC                  hdc;
  SIZEL                sizel;
  HPS                  hpsMicro;

  if ( // Main window.
       ( pSw == NULL ) &&
       // Window not minimized now (style flag WS_MINIMIZED NOT set now).
       ( ( WinQueryWindowULong( hwndFrame, QWL_STYLE ) & WS_MINIMIZED ) == 0 ) )
  {
    // Scroll bars for the main window.

    BOOL       fSBRemoved = FALSE;
    HWND       hwndHScroll = WinWindowFromID( hwndFrame, FID_HORZSCROLL );
    HWND       hwndVScroll = WinWindowFromID( hwndFrame, FID_VERTSCROLL );
    RECTL      rectl;

    // Create or destroy scroll bars.

    if ( usCX < g_width )
    {
      if ( hwndHScroll == NULLHANDLE )
        hwndHScroll = WinCreateWindow( hwndFrame, WC_SCROLLBAR, NULL,
                                       SBS_HORZ | WS_VISIBLE, 0, 0, 10, 10,
                                       hwndFrame, HWND_TOP, FID_HORZSCROLL,
                                       NULL, NULL );
    }
    else if ( hwndHScroll != NULLHANDLE )
    {
      WinDestroyWindow( hwndHScroll );
      hwndHScroll = NULLHANDLE;
      sHSlider = 0;
      fSBRemoved = TRUE;
    }

    if ( usCY < g_height )
    {
      if ( hwndVScroll == NULLHANDLE )
        hwndVScroll = WinCreateWindow( hwndFrame, WC_SCROLLBAR, NULL,
                                       SBS_VERT | WS_VISIBLE, 0, 0, 10, 10,
                                       hwndFrame, HWND_TOP, FID_VERTSCROLL,
                                       NULL, NULL );
    }
    else if ( hwndVScroll != NULLHANDLE )
    {
      WinDestroyWindow( hwndVScroll );
      hwndVScroll = NULLHANDLE;
      sVSlider = 0;
      fSBRemoved = TRUE;
    }

    if ( fSBRemoved )
      WinSendMsg( hwndFrame, WM_UPDATEFRAME, MPFROMLONG(FCF_BORDER), 0 );

    // Setup scroll bars.

    WinQueryWindowRect( hwnd, &rectl );
    usCX = rectl.xRight;
    usCY = rectl.yTop;

    if ( hwndHScroll != NULLHANDLE )
    {
      SHORT    sSBLastPos = g_width - rectl.xRight;

      if ( sHSlider > sSBLastPos )
        sHSlider = sSBLastPos;

      WinSendMsg( hwndHScroll, SBM_SETSCROLLBAR, MPFROMSHORT(sHSlider),
                  MPFROM2SHORT(0, sSBLastPos) );
      WinSendMsg( hwndHScroll, SBM_SETTHUMBSIZE,
                  MPFROM2SHORT(rectl.xRight, g_width), 0 );
    }

    if ( hwndVScroll != NULLHANDLE )
    {
      SHORT    sSBLastPos = g_height - rectl.yTop;

      if ( sVSlider > sSBLastPos )
        sVSlider = sSBLastPos;

      WinSendMsg( hwndVScroll, SBM_SETSCROLLBAR,
                  MPFROMSHORT(sSBLastPos - sVSlider),
                  MPFROM2SHORT(0, sSBLastPos) );
      WinSendMsg( hwndVScroll, SBM_SETTHUMBSIZE,
                  MPFROM2SHORT(rectl.yTop, g_height), 0 );
    }
  } // if ( pSw == NULL )

  // Create a micro presentation space for the window.

  if ( !g_ownbackstore || ( hpsMicroOld != NULLHANDLE ) )
    // The switch -B was specified or micro presentation space already created.
    return;

  hdc = WinOpenWindowDC( hwnd );
  sizel.cx = usCX;
  sizel.cy = usCY;

  hpsMicro = GpiCreatePS( hab, hdc, &sizel,
                          PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC );
  GpiCreateLogColorTable( hpsMicro, 0, LCOLF_RGB, 0, 0, NULL );

  // Destroy old PS if it was be created.
/*  if ( hpsMicroOld != NULLHANDLE )
    GpiDestroyPS( hpsMicroOld );*/

  // Store the handle of micro presentation space into the memory of the
  // reserved window words.
  WinSetWindowULong( hwnd, 0, (ULONG)hpsMicro );

  rdp_send_client_window_status( WinIsWindowShowing( hwnd ) );
}

static VOID _wmMove(HWND hwnd)
{
  swMoved( hwnd );
}

static VOID _wmMouseMove(HWND hwnd, POINTS points)
{
  PSEAMLESSWIN         pSw = (PSEAMLESSWIN)WinQueryWindowPtr( hwnd, 4 );

  if ( pSw != NULL )
  {
    POINTL   pointl = { 0 };

    WinMapWindowPoints( pSw->hwnd, HWND_DESKTOP, &pointl, 1 );
    points.x += pointl.x;
    points.y += pointl.y;
  }
  else
  {
    points.x += sHSlider;
    points.y += sVSlider;
  }

  points.y = g_height - points.y - 1;

  if ( points.x < 0 )
    points.x = 0;
  if ( points.y < 0 )
    points.y = 0;

  WinSetPointer( HWND_DESKTOP, hptrCursor );

  if ( g_sendmotion )
    rdp_send_input( time( NULL ), RDP_INPUT_MOUSE, MOUSE_FLAG_MOVE,
                    points.x, points.y );
}

static VOID _wmMouseButton(HWND hwnd, ULONG ulButton, POINTS points,
                           BOOL fPressed)
{
  static uint16        au16SysBtnToRDFl[5] =
                { MOUSE_FLAG_BUTTON1, MOUSE_FLAG_BUTTON2, MOUSE_FLAG_BUTTON3,
                  MOUSE_FLAG_BUTTON4, MOUSE_FLAG_BUTTON5 };
  static LONG          lButtonsDown = 0;
  uint16               u16Fl = au16SysBtnToRDFl[ulButton];
  PSEAMLESSWIN         pSw;

  if ( !fPressed && ( ulButton > 2 ) )
    // Wheel event & released "button" - skip.
    return;

  pSw = (PSEAMLESSWIN)WinQueryWindowPtr( hwnd, 4 );
  if ( pSw != NULL )
  {
    POINTL   pointl = { 0 };

    WinMapWindowPoints( pSw->hwnd, HWND_DESKTOP, &pointl, 1 );
    points.x += pointl.x;
    points.y += pointl.y;
  }
  else
  {
    points.x += sHSlider;
    points.y += sVSlider;
  }

  points.y = g_height - points.y - 1;

  if ( points.x < 0 )
    points.x = 0;
  if ( points.y < 0 )
    points.y = 0;

  if ( fPressed )
  {
    u16Fl |= MOUSE_FLAG_DOWN;

    if ( ulButton <= 2 ) // Not wheel event
    {
      if ( lButtonsDown == 0 )
        WinSetCapture( HWND_DESKTOP, hwnd );
      lButtonsDown++;
    }
  }
  else if ( ( lButtonsDown > 0 ) && ( ulButton <= 2 ) )
  {
    lButtonsDown--;
    if ( lButtonsDown == 0 )
      WinSetCapture( HWND_DESKTOP, NULLHANDLE );
  }

  rdp_send_input( time( NULL ), RDP_INPUT_MOUSE, u16Fl, points.x, points.y );
}

static VOID _kbdSync()
{
  ULONG      ulSyncKbdFlagsNew;

  ulSyncKbdFlags &= ~_KBD_FLAG_NEED_SYNC;

  if ( !g_numlock_sync )
    return;

  ulSyncKbdFlagsNew = ui_get_numlock_state( 0 );

  if ( ( WinGetKeyState( HWND_DESKTOP, VK_SCRLLOCK ) & 0x0001 ) != 0 )
    ulSyncKbdFlagsNew |= KBD_FLAG_SCROLL;

  if ( ( WinGetKeyState( HWND_DESKTOP, VK_CAPSLOCK ) & 0x0001 ) != 0 )
    ulSyncKbdFlagsNew |= KBD_FLAG_CAPITAL;

  if ( ulSyncKbdFlagsNew != ulSyncKbdFlags )
  {
    // Send RDP_INPUT_SYNCHRONIZE only if it realy needed.
    ulSyncKbdFlags = ulSyncKbdFlagsNew;
    rdp_send_input( time( NULL ), RDP_INPUT_SYNCHRONIZE, 0, ulSyncKbdFlags, 0 );
  }
}

static VOID _wmChar(HWND hwnd, MPARAM mp1, MPARAM mp2)
{
  static BOOL          fPrtScrPress = FALSE;
  static struct _EXT {
    UCHAR    ucOSScancode;
    ULONG    ulRDScancode;
  } aExtKeys[] = {
    { 0x68, 0x52 | SCANCODE_EXTENDED }, // Insert
    { 0x60, 0x47 | SCANCODE_EXTENDED }, // Home
    { 0x62, 0x49 | SCANCODE_EXTENDED }, // PgUp
    { 0x69, 0x53 | SCANCODE_EXTENDED }, // Delete
    { 0x65, 0x4F | SCANCODE_EXTENDED }, // End
    { 0x67, 0x51 | SCANCODE_EXTENDED }, // PgDn
    { 0x61, 0x48 | SCANCODE_EXTENDED }, // Up
    { 0x63, 0x4B | SCANCODE_EXTENDED }, // Left
    { 0x66, 0x50 | SCANCODE_EXTENDED }, // Down
    { 0x64, 0x4D | SCANCODE_EXTENDED }, // Right
    { 0x5A, 0x1C | SCANCODE_EXTENDED }, // Numpad Enter
    { 0x5B, 0x1D | SCANCODE_EXTENDED }, // Right Ctrl
    { 0x7E, 0x5B | SCANCODE_EXTENDED }, // Left Win
    { 0x7F, 0x5C | SCANCODE_EXTENDED }, // Right Win
    { 0x7C, 0x5D | SCANCODE_EXTENDED }, // Menu (Win kbd)
    { 0x5E, 0xB8 },                     // Right Alt
    { 0x5C, 0x35 | SCANCODE_EXTENDED }  // </> (Num pad)
  };

  ULONG      ulFlags = SHORT1FROMMP(mp1);      // WM_CHAR flags
  ULONG      ulVirtualKey = SHORT2FROMMP(mp2); // Virtual key code VK_*
  //ULONG      ulCharCode = SHORT1FROMMP(mp2);   // Character code
  UCHAR      ucScanCode = CHAR4FROMMP(mp1);    // Scan code
  time_t     timeEvent = time( NULL );
  ULONG      ulIdx;

  if ( (ulFlags & KC_SCANCODE) == 0 )
    return;

//  printf( "Scan code: 0x%X\n", ucScanCode );

  // Register pressed keys.

  // Remove released key.
  for( ulIdx = 0; ulIdx < ARRAY_SIZE(aucKeysPressed); ulIdx++ )
  {
    if ( aucKeysPressed[ulIdx] == ucScanCode )
    {
      if ( (ulFlags & KC_KEYUP) != 0 )
        switch( ulIdx )
        {
          case 0: aucKeysPressed[0] = aucKeysPressed[1];
          case 1: aucKeysPressed[1] = aucKeysPressed[2];
          case 2: aucKeysPressed[2] = aucKeysPressed[3];
          case 3: aucKeysPressed[3] = 0;
        }

      break;
    }
  }

  if ( ( (ulFlags & KC_KEYUP) == 0 ) && ( ulIdx == ARRAY_SIZE(aucKeysPressed) ) )
  {
    // Register pressed unregistered key.
    *((PULONG)&aucKeysPressed) = (*(PULONG)&aucKeysPressed << 8) | ucScanCode;

    // Ctrl-Alt-Enter should toggle fullscreen. 
    if ( ( ucScanCode == 0x5A ) || ( ucScanCode == 0x1C ) ) // Enter.
    {
      ULONG  ulCtrlAlt = 0;

      for( ulIdx = 0; ulIdx < ARRAY_SIZE(aucKeysPressed); ulIdx++ )
        switch( aucKeysPressed[ulIdx] )
        {
          case 0x1D: // Left Ctrl
          case 0x5B: // Right Ctrl
            ulCtrlAlt |= 0x01;
            break;

          case 0x38: // Left Alt
          case 0x5E: // AltGr (Right Alt)
            ulCtrlAlt |= 0x02;
            break;
        }

      if ( ulCtrlAlt == 0x03 )
      {
        // Ctrl-Alt-Enter pressed.
        _toggleFullscreen();
        return;
      }
    }
  }

  // Send scan code.

  /* OS/2  Name              Output sequence
     0x5D  PrtScr            E0 2A E0 37, repeat: E0 37, break: E0 B7 E0 AA
     0x54  Alt + PrtScr      54
     0x5F  Pause/Break       E1 1D 45 E1 9D C5, no break code
     0x6E  Ctrl+Pause/Break  E0 46 E0 C6, no break code                    */

  if ( ucScanCode == 0x5D ) // PrtScr|SysRq
  {
    if ( (ulFlags & KC_KEYUP) == 0 )
    {
      if ( !fPrtScrPress )
      {
        rdp_send_scancode( timeEvent, 0, 0x2A | SCANCODE_EXTENDED );
        fPrtScrPress = TRUE;
      }
      rdp_send_scancode( timeEvent, 0, 0x37 | SCANCODE_EXTENDED );
    }
    else
    {
      rdp_send_scancode( timeEvent, 0, 0xB7 | SCANCODE_EXTENDED );
      rdp_send_scancode( timeEvent, 0, 0xAA | SCANCODE_EXTENDED );
      fPrtScrPress = FALSE;
    }
    return;
  }

  fPrtScrPress = FALSE;

  if ( ucScanCode == 0x5F ) // Pause/Break (no break code).
  {
    static UCHAR   auchPauseBreak[6] = { 0xE1,0x1D,0x45,0xE1,0x9D,0xC5 };

    if ( (ulFlags & KC_KEYUP) == 0 )
      for( ulIdx = 0; ulIdx < 6; ulIdx++ )
        rdp_send_scancode( timeEvent, 0, auchPauseBreak[ulIdx] );
    return;
  }

  if ( ucScanCode == 0x6E ) // Ctrl + Pause/Break (no break code).
  {
    rdp_send_scancode( timeEvent, 0, 0x46 | SCANCODE_EXTENDED );
    rdp_send_scancode( timeEvent, 0, 0xC6 | SCANCODE_EXTENDED );
    return;
  }

/* 15.12.2016 - replaced with _kbdSync() call (see below). 
  if ( g_numlock_sync &&
       ( ucScanCode == 0x0045 ) && ( (ulFlags & KC_KEYUP) != 0 ) )
    // NumLock syncronization enabled and NumLock released,
    rdp_send_input( 0, RDP_INPUT_SYNCHRONIZE, 0, ui_get_numlock_state(0), 0 );
*/

/*  if ( ucScanCode == 0x5E )                   // Right Alt (AltGr).
  {
    debug( "Right Alt (%s) -> Ctrl (0x1D) + Right Alt",
           (ulFlags & KC_KEYUP) == 0 ? "pressed" : "released" );
    rdp_send_scancode( timeEvent,
                       (ulFlags & KC_KEYUP) == 0 ?
                         RDP_KEYPRESS : RDP_KEYRELEASE,
                       0x1D ); // Ctrl
    // ucScanCode = 0xB8; // RAlt, Will be changed 0x5E->0xB8 with aExtKeys.
  }*/

  // Translate some OS/2 specified scan codes to windows scan codes.
  for( ulIdx = 0; ulIdx < ARRAY_SIZE(aExtKeys); ulIdx++ )
  {
    if ( aExtKeys[ulIdx].ucOSScancode == ucScanCode )
    {
/*      debug( "Convert scancode 0x%X to 0x%X",
             ucScanCode, aExtKeys[ulIdx].ulRDScancode );*/
      ucScanCode = aExtKeys[ulIdx].ulRDScancode;
      break;
    }
  }

  if (
       ( ( ulSyncKbdFlags & _KBD_FLAG_NEED_SYNC ) != 0 )
     ||
       (
         ( (ulFlags & (KC_KEYUP | KC_VIRTUALKEY)) == (KC_KEYUP | KC_VIRTUALKEY) )
       &&
         ( ( ulVirtualKey == VK_CAPSLOCK ) || ( ulVirtualKey == VK_NUMLOCK ) ||
           ( ulVirtualKey == VK_SCRLLOCK ) )
       )
     )
    _kbdSync();

//  debug( "Send scancode: 0x%X", ucScanCode );
  rdp_send_scancode( timeEvent,
                     (ulFlags & KC_KEYUP) == 0 ?
                       RDP_KEYPRESS : RDP_KEYRELEASE,
                     ucScanCode );
}

static VOID _wmActivate(HWND hwnd, BOOL fActivate)
{
  ULONG      ulIdx;

  if ( fActivate )
  {
    // "Release" keys pressed before window's deactivation and not released now.
    for( ulIdx = 0; ( ulIdx < ARRAY_SIZE(aucKeysPressed) ) &&
                    ( aucKeysPressed[ulIdx] != 0 ); ulIdx++ )
    {
      if ( (WinGetPhysKeyState( HWND_DESKTOP, aucKeysPressed[ulIdx] ) & 0x8000)
           == 0 )
      {
        _wmChar( hwnd,
                 (MPARAM)( KC_KEYUP | KC_SCANCODE |
                           ((ULONG)aucKeysPressed[ulIdx] << 24) ), 0 );
      }
    }

    *((PULONG)aucKeysPressed) = 0;

    if ( (ulSyncKbdFlags & _KBD_FLAG_NEED_SYNC) == 0 )
        ulSyncKbdFlags |= _KBD_FLAG_NEED_SYNC;
    else
      _kbdSync();
  }
  else
    ulSyncKbdFlags |= _KBD_FLAG_NEED_SYNC;

  swFocus( hwnd, fActivate );

  if ( hwndClipWin != NULLHANDLE )
    WinSendMsg( hwndClipWin, WM_XCLIP_RDACTIVATE, MPFROMSHORT(fActivate), 0 );
}

static VOID _wmClose(HWND hwnd)
{
}

static BOOL _wmCreate(HWND hwnd)
{
  // Register for wheel messages being sent to window.
  if ( ( pfnWinRegisterForWheelMsg != NULL ) &&
       !pfnWinRegisterForWheelMsg( hwnd, AW_OWNERFRAME ) )
    debug( "WinRegisterForWheelMsg() failed" );

  return TRUE;
}

static VOID _wmDestroy(HWND hwnd)
{
  HPS                  hpsMicro = (HPS)WinQueryWindowULong( hwnd, 0 );
  PSEAMLESSWIN         pSw = (PSEAMLESSWIN)WinQueryWindowPtr( hwnd, 4 );

                               // Not fullscreen mode and not seamless window,
  if ( ( (g_pos & 8) != 0 ) && !g_fullscreen && ( pSw == NULL ) &&
       // window not minimized.
       ( ( WinQueryWindowULong( hwndFrame, QWL_STYLE ) & WS_MINIMIZED ) == 0 ) )
  {
    RECTL    rect;
    POINTL   pt = { 0 };

    // Coordinates of bottom-left window corner relative bottom-left DT corner.
    WinMapWindowPoints( hwnd, HWND_DESKTOP, &pt, 1 );
    // Query size of the desktop.
    WinQueryWindowRect( HWND_DESKTOP, &rect );
    // Coordinates of top-left window corner relative bottom-left DT corner.
    pt.y = rect.yTop - rect.yBottom - pt.y;

/*
//    rect.xRight -= rect.xLeft; // Width of the desktop.
//    rect.yTop -= rect.yBottom; // Height of the desktop.

    // Correction coordinates on a case if xPager is used and main window not
    // at the current screen.

    rectDT.xRight += 8;
    rectDT.yTop   += 8;

    if ( pt.x < 0 )
      pt.x = rectDT.xRight - ( (-pt.x) % rectDT.xRight );
    else
      pt.x %= rectDT.xRight;

    if ( pt.y < 0 )
      pt.y = rectDT.yTop - ( (-pt.y) % rectDT.yTop );
    else
      pt.y %= rectDT.yTop;
*/

    // Coordinates of top-left window corner relative top-left DT corner.
    WinQueryWindowRect( hwnd, &rect );
    rect.yTop -= rect.yBottom;
    rect.xRight -= rect.xLeft;
    pt.y -= rect.yTop;

    // Print position and size of the window: X,Y,Width,Height.
    printf( "#winpos=%d,%d,%d,%d\n",
            (int)pt.x, (int)pt.y, (int)rect.xRight, (int)rect.yTop );
  }

  if ( ( hpsMicro != NULLHANDLE ) && !GpiDestroyPS( hpsMicro ) )
    debug( "GpiDestroyPS() failed" );
}

MRESULT EXPENTRY wndProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  switch( msg )
  {
    case WM_MINMAXFRAME:
      if ( (((PSWP)mp1)->fl & SWP_MINIMIZE) == 0 )
        swRestored( hwnd );
      break;

    case WM_CREATE:
      if ( !_wmCreate( hwnd ) )
        return (MRESULT)TRUE;
      break;

    case WM_DESTROY:
      _wmDestroy( hwnd );
      break;

    case WM_ACTIVATE:
      _wmActivate( hwnd, SHORT1FROMMP(mp1) );
      break;

    case WM_CLOSE:
      _wmClose( hwnd );
      break;

    case WM_SIZE:
      _wmSize( hwnd, SHORT1FROMMP(mp2), SHORT2FROMMP(mp2) );
      break;

    case WM_MOVE:
      _wmMove( hwnd );
      break;

    case WM_PAINT:
      _wmPaint( hwnd );
      return (MRESULT)FALSE;

    case WM_VSCROLL:
    case WM_HSCROLL:
      {
        HWND    hwndScroll = WinWindowFromID( WinQueryWindow( hwnd, QW_PARENT ),
                                              SHORT1FROMMP(mp1) );
        USHORT  usCmd      = SHORT2FROMMP(mp2);
        SHORT   sSlider;

        switch( usCmd ) // All commands.
        {
          case SB_SLIDERTRACK:
            sSlider = SHORT1FROMMP(mp2);
            break;

          case SB_ENDSCROLL:
            return (MRESULT)TRUE;

          default:
          {
            sSlider = SHORT1FROMMR(WinSendMsg( hwndScroll, SBM_QUERYPOS, 0, 0 ));

            switch( usCmd ) // Line/page offset.
            {
              case SB_LINELEFT:
                sSlider -= 10;
                break;

              case SB_LINERIGHT:
                sSlider += 10;
                break;

              default:
              {
                RECTL   rectl;
                USHORT  usPageSize;

                WinQueryWindowRect( hwnd, &rectl );
                usPageSize = ( msg == WM_HSCROLL ? rectl.xRight : rectl.yTop ) / 2;

                switch( usCmd ) // Page offset.
                {
                  case SB_PAGELEFT:
                    sSlider -= usPageSize;
                    break;

                  case SB_PAGERIGHT:
                    sSlider += usPageSize;
                    break;
                } // switch( usCmd ) - Page offset.
              }
            } // switch( usCmd ) - Line/page offset.
          }
        } // switch( usCmd ) - All commands.

        // Move slider to the new position and update window.
        WinSendMsg( hwndScroll, SBM_SETPOS, MPFROMSHORT(sSlider), 0 );

        // Store horizontal/vertical slider position for drawing in window.
        sSlider = SHORT1FROMMR(WinSendMsg( hwndScroll, SBM_QUERYPOS, 0, 0 ));
        if ( msg == WM_HSCROLL )
          sHSlider = sSlider;
        else
          sVSlider = SHORT2FROMMR(WinSendMsg( hwndScroll, SBM_QUERYRANGE, 0, 0 ))
                     - sSlider;

        // Update window.
        WinInvalidateRect( hwnd, NULL, FALSE );
        WinUpdateWindow( hwnd );
      }
      break;

    case WM_MOUSEMOVE:
      _wmMouseMove( hwnd, *((POINTS *)&mp1) );
      return (MRESULT)TRUE;

    case WM_BUTTON1DOWN:
    case WM_BUTTON1DBLCLK:
      _wmMouseButton( hwnd, 0, *((POINTS *)&mp1), TRUE );
      break;

    case WM_BUTTON1UP:
      _wmMouseButton( hwnd, 0, *((POINTS *)&mp1), FALSE );
      break;

    case WM_BUTTON2DOWN:
    case WM_BUTTON2DBLCLK:
      _wmMouseButton( hwnd, 1, *((POINTS *)&mp1), TRUE );
      break;

    case WM_BUTTON2UP:
      _wmMouseButton( hwnd, 1, *((POINTS *)&mp1), FALSE );
      break;

    case WM_BUTTON3DOWN:
    case WM_BUTTON3DBLCLK:
      _wmMouseButton( hwnd, 2, *((POINTS *)&mp1), TRUE );
      break;

    case WM_BUTTON3UP:
      _wmMouseButton( hwnd, 2, *((POINTS *)&mp1), FALSE );
      break;

    case WM_MOUSEWHEEL_VERT:
      debug( "WM_MOUSEWHEEL_VERT fwKey: %x; turns: %d, x=%d, y=%d",
             SHORT1FROMMP(mp1), SHORT2FROMMP(mp1),
             SHORT1FROMMP(mp2), SHORT2FROMMP(mp2) );
      _wmMouseButton( hwnd, (SHORT2FROMMP(mp1) & 0x8000) != 0 ? 3 : 4,
                      *((POINTS *)&mp2), TRUE );
      return (MRESULT)FALSE;

    case WM_TRANSLATEACCEL:
      if ( !g_grab_keyboard ) // FALSE - Switch -K was specified.
        break;

      // ALT and acceleration keys not allowed (must be processed in WM_CHAR)
      if ( mp1 == NULL || ((PQMSG)mp1)->msg != WM_CHAR )
        break;
      return (MRESULT)FALSE;

    case WM_CHAR:
      _wmChar( hwnd, mp1, mp2 );
      break;
  }

  return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}


/*
 * rdesktop functions implemetation.
 */

void ui_bell(void)
{
  WinAlarm( HWND_DESKTOP, WA_NOTE );
}

unsigned int read_keyboard_state(void)
{
  return 0;
}

void ui_move_pointer(int x, int y)
{
  POINTL     pointl;

  pointl.x = x;
  pointl.y = g_height - y - 1;

  if ( !fSeamlessActive )
    WinMapWindowPoints( hwnd, HWND_DESKTOP, &pointl, 1 );

  if ( !WinSetPointerPos( HWND_DESKTOP, pointl.x, pointl.y ) )
    debug( "WinSetPointerPos() failed" );
}

void ui_desktop_restore(uint32 offset, int x, int y, int cx, int cy)
{
  HPS                  hpsTemp;
  POINTL               aPoints[3];
  ULONG                ulBytesPerPel = g_server_depth == 15
                                         ? 2 : ( g_server_depth >> 3 );
  PBYTE                pbData = (PBYTE)cache_get_desktop(
                                                offset * ulBytesPerPel, cx, cy,
                                                ulBytesPerPel );
  ULONG                ulIdx;
  LONG                 lRC;
  struct {
    BITMAPINFOHEADER2  bmp2;
    RGB2               argb2Color[0x100];
  } bm;

  if ( pbData == NULL )
  {
    debug( "cache_get_desktop() failed" );
    return;
  }

  // Create temporary bitmap.
  hpsTemp = (HPS)_rdbmpCreate( g_server_depth, cx, cy, NULL );
  if ( hpsTemp == NULLHANDLE )
  {
    debug( "_rdbmpCreate() failed" );
    return;
  }

  // Copy image from the cache to the temporary bitmap line by line. Data in
  // cache not word aligned - we cannot point it for _rdbmpCreate() above.

  memset( &bm, 0, sizeof(bm) );
  bm.bmp2.cbFix = sizeof(BITMAPINFOHEADER2);//16;
  if ( !GpiQueryBitmapInfoHeader( GpiQueryBitmapHandle( hpsTemp, 1 ),
                                  &bm.bmp2 ) )
  {
    debug( "GpiQueryBitmapInfoHeader() failed" );
    _rdbmpDestroy( (RD_HBITMAP)hpsTemp );
    return;
  }

  // Query bitmap palette (it will be used by GpiSetBitmapBits()).
  if ( ( bm.bmp2.cBitCount == 8 ) &&
       ( GpiQueryBitmapBits( hpsTemp, 0, 0, NULL, (PBITMAPINFO2)&bm ) ==
           GPI_ALTERROR ) )
    debug( "GpiQueryBitmapBits() failed" );

  for( ulIdx = 0; ulIdx < bm.bmp2.cy; ulIdx++ )
  {
    lRC = GpiSetBitmapBits( hpsTemp, ulIdx, 1, pbData, (PBITMAPINFO2)&bm );
    if ( lRC == 0 || lRC == GPI_ALTERROR )
      debug( "GpiSetBitmapBits(,%u,,,) failed", ulIdx );
    pbData += cx * ulBytesPerPel;
  }

  // Copy created bitmap to the remote desktop's PS.

  _rectFromRDCoord( (PRECTL)&aPoints, x, y, cx, cy );
  aPoints[2].x = 0;  // Sx1
  aPoints[2].y = 0;  // Sy1
  if ( GpiBitBlt( hpsMem, hpsTemp, 3, aPoints, ROP_SRCCOPY, 0 ) == GPI_ERROR )
    debug( "GpiBitBlt() failed" );

  _rdbmpDestroy( (RD_HBITMAP)hpsTemp );
  _invalidateRect( (PRECTL)&aPoints );
}

void ui_desktop_save(uint32 offset, int x, int y, int cx, int cy)
{
  HPS        hpsTemp = (HPS)_rdbmpCreate( g_server_depth, cx, cy, NULL );
  POINTL     aPoints[3];

  if ( hpsTemp == NULLHANDLE )
  {
    debug( "_rdbmpCreate() failed" );
    return;
  }

  // Copy image from remote desktop's PS to the temporary bitmap.
  aPoints[0].x = 0;                 // Tx1
  aPoints[0].y = 0;                 // Ty1
  aPoints[1].x = cx;                // Tx2
  aPoints[1].y = cy;                // Ty2
  aPoints[2].x = x;                 // Sx1
  aPoints[2].y = g_height - y - cy; // Sy1
  if ( GpiBitBlt( hpsTemp, hpsMem, 3, aPoints, ROP_SRCCOPY, 0 ) == GPI_ERROR )
    debug( "GpiBitBlt() failed" );
  else
  {
    // Copy data from temporary bitmap to the buffer and put it to the cache.

    LONG               cbScanLine;
    ULONG              cbData;
    PBYTE              pbData;
    struct {
      BITMAPINFOHEADER2 bmp2;
      RGB2              argb2Color[0x100];
    } bm;
    HBITMAP            hbmTemp = GpiQueryBitmapHandle( hpsTemp, 1 );

    memset( &bm, 0, sizeof(bm) );
    bm.bmp2.cbFix = 16;

    if ( hbmTemp == GPI_ERROR )
      debug( "GpiQueryBitmapHandle() failed" );
    else if ( !GpiQueryBitmapInfoHeader( hbmTemp, &bm.bmp2 ) )
      debug( "GpiQueryBitmapInfoHeader() failed" );
    else
    {
      cbScanLine = ( (bm.bmp2.cBitCount * bm.bmp2.cx + 31) / 32 ) *
                   bm.bmp2.cPlanes * 4;
      cbData = cbScanLine * bm.bmp2.cy;
      pbData = _getTempMem( cbData );

      if ( pbData == NULL )
        debug( "Not enough memory" );
      else
      {
        ULONG          ulBytesPerPel = bm.bmp2.cBitCount >> 3;

        cy = GpiQueryBitmapBits( hpsTemp, 0, bm.bmp2.cy, pbData,
                                 (PBITMAPINFO2)&bm );
        if ( cy == GPI_ALTERROR )
          debug( "GpiQueryBitmapBits() failed" );
        else
          cache_put_desktop( offset * ulBytesPerPel, bm.bmp2.cx, bm.bmp2.cy,
                             cbScanLine, (int)ulBytesPerPel, (uint8 *)pbData );
      }
    }
  }

  _rdbmpDestroy( (RD_HBITMAP)hpsTemp );
}


/*
 *           Primitives.
 */

void ui_ellipse(uint8 opcode, uint8 fillmode,
                int x, int y, int cx, int cy,
                BRUSH *brush, int bgcolour, int fgcolour)
{
  ARCPARAMS  stParam;
  POINTL     pointl;

  // Set colors.
  GpiSetColor( hpsMem, _RDColToRGB( fgcolour ) );
  GpiSetBackColor( hpsMem, _RDColToRGB( bgcolour ) );
  // Set brush and colors.
  if ( !_rdbrushSet( hpsMem, opcode, brush ) )
    debug( "_rdbrushSet() failed" );

  stParam.lP = cx / 2;
  stParam.lQ = cy / 2;
  stParam.lR = 0;
  stParam.lS = 0;
  if ( !GpiSetArcParams( hpsMem, &stParam ) )
    debug( "GpiSetArcParams() failed" );

  // Center.
  pointl.x = x + stParam.lP;
  pointl.y = g_height - ( y + stParam.lQ ) - 1;
  GpiMove( hpsMem, &pointl );

  if ( GpiFullArc( hpsMem,
         fillmode == 1 ? DRO_OUTLINEFILL : DRO_OUTLINE, MAKEFIXED(1, 0) )
       == GPI_ERROR )
    debug( "GpiFullArc() failed" );

  _rdbrushReset( hpsMem );

  {
    RECTL    rectlUpd;

    *((PPOINTL)&rectlUpd) = pointl;
    rectlUpd.xRight += cx;
    rectlUpd.yTop += cx;
    _invalidateRect( &rectlUpd );
  }
}

void ui_line(uint8 opcode, int startx, int starty, int endx, int endy, PEN *pen)
{
  POINTL     pointl;
  RECTL      rectlUpd = { 0xFFFF, 0xFFFF, 0, 0 };

//  debug( "opcode: %d, startx: %d, starty: %d, endx: %d, endy: %d, pen: 0x%X",
//         opcode, startx, starty, endx, endy, pen );

  // Set mix mode.
  if ( opcode >= 0 && opcode < ARRAY_SIZE(aRDOpCodesToMix) )
  {
    GpiSetMix( hpsMem, aRDOpCodesToMix[opcode] );
    GpiSetBackMix( hpsMem, aRDOpCodesToMix[opcode] );
  }

  GpiSetLineWidth( hpsMem, pen->width * LINEWIDTH_NORMAL );
  GpiSetLineType( hpsMem, pen->style );
  GpiSetColor( hpsMem, _RDColToRGB( pen->colour ) );

  pointl.x = startx;
  pointl.y = g_height - starty - 1;
  GpiMove( hpsMem, &pointl );
  _rectInclPoint( &rectlUpd, &pointl );

  pointl.x = endx;
  pointl.y = g_height - endy - 1;
  GpiLine( hpsMem, &pointl );
  _rectInclPoint( &rectlUpd, &pointl );

  // Return to defaults.
  GpiSetMix( hpsMem, FM_DEFAULT );
  GpiSetBackMix( hpsMem, FM_DEFAULT );

  _invalidateRect( &rectlUpd );
}

void ui_polyline(uint8 opcode, RD_POINT *points, int npoints, PEN *pen)
{
  PWINDATA   pWinData = (PWINDATA)WinQueryWindowULong( hwnd, 0 );
  POINTL     pointl;
  ULONG      ulIdx;
  RECTL      rectlUpd = { 0xFFFF, 0xFFFF, 0, 0 };

//  debug( "opcode: %d, npoints: %d, pen: 0x%X", opcode, npoints, pen );

  if ( ( npoints < 2 ) && ( pWinData != NULL ) )
    return;

  // Set PS attributes.
  if ( opcode >= 0 && opcode < ARRAY_SIZE(aRDOpCodesToMix) )
  {
    GpiSetMix( hpsMem, aRDOpCodesToMix[opcode] );
    GpiSetBackMix( hpsMem, aRDOpCodesToMix[opcode] );
  }
  GpiSetLineWidth( hpsMem, pen->width * LINEWIDTH_NORMAL );
  GpiSetLineType( hpsMem, pen->style );
  GpiSetColor( hpsMem, _RDColToRGB( pen->colour ) );
/*  debug( "pen: width: %d, style: %d, color: 0x%X; ",
         pen->width, pen->style, _RDColToRGB( pen->colour ) );*/

  pointl.x = points->x;
  pointl.y = g_height - points->y - 1;
  GpiMove( hpsMem, &pointl );
  _rectInclPoint( &rectlUpd, &pointl );

  for( ulIdx = 1, points++; ulIdx < npoints; ulIdx++, points++ )
  {
    pointl.x += points->x;
    pointl.y -= points->y;
    GpiLine( hpsMem, &pointl );
    _rectInclPoint( &rectlUpd, &pointl );
  }

  // Return to defaults.
  GpiSetMix( hpsMem, FM_DEFAULT );
  GpiSetBackMix( hpsMem, FM_DEFAULT );

  _invalidateRect( &rectlUpd );
}

void ui_polygon(uint8 opcode, uint8 fillmode, RD_POINT *point, int npoints,
                BRUSH * brush, int bgcolour, int fgcolour)
{
  POLYGON    stPolygon;
  ULONG      ulIdx;
  RECTL      rectlUpd = { 0xFFFF, 0xFFFF, 0, 0 };

/*  debug( "opcode: %d, fillmode: %d, npoints: %d, bgcolour: %d, bgcolour: %d",
         opcode, fillmode, npoints, bgcolour, bgcolour );*/

  if ( npoints < 2 )
    return;

  stPolygon.ulPoints = npoints;
  stPolygon.aPointl = _getTempMem( sizeof(POINTL) * npoints );

  // Absolute coordinates for the first point.
  stPolygon.aPointl[0].x = point[0].x;
  stPolygon.aPointl[0].y = g_height - point[0].y - 1;
  _rectInclPoint( &rectlUpd, stPolygon.aPointl );

  // Convert relative coordinates for next point to absolute coordinates.
  for( ulIdx = 1; ulIdx < npoints; ulIdx++ )
  {
    stPolygon.aPointl[ulIdx].x = stPolygon.aPointl[ulIdx-1].x + point[ulIdx].x;
    stPolygon.aPointl[ulIdx].y = stPolygon.aPointl[ulIdx-1].y - point[ulIdx].y;
    _rectInclPoint( &rectlUpd, &stPolygon.aPointl[ulIdx] );
  }

  // Set colors.
  GpiSetColor( hpsMem, _RDColToRGB( fgcolour ) );
  GpiSetBackColor( hpsMem, _RDColToRGB( bgcolour ) );
  // Set brush and colors.
  if ( !_rdbrushSet( hpsMem, opcode, brush ) )
    debug( "_rdbrushSet() failed" );

  // Draw the plygon.
  GpiMove( hpsMem, stPolygon.aPointl );
  GpiPolygons( hpsMem, 1, &stPolygon,
               fillmode == WINDING
                 ? POLYGON_BOUNDARY | POLYGON_WINDING
                 : POLYGON_BOUNDARY | POLYGON_ALTERNATE,
               POLYGON_EXCL );

  // Return to defaults.
  _rdbrushReset( hpsMem );

  _invalidateRect( &rectlUpd );
}

void ui_rect(int x, int y, int cx, int cy, int colour)
{
  RECTL      rectl;

//  debug( "x: %d, y: %d, cx: %d, cy: %d, colour: 0x%X", x, y, cx, cy, colour );

  _rectFromRDCoord( &rectl, x, y, cx, cy );
  WinFillRect( hpsMem, &rectl, _RDColToRGB( colour ) );
  _invalidateRect( &rectl );
}

void ui_destblt(uint8 opcode, int x, int y, int cx, int cy)
{
  RECTL      rectl;

//  debug( "opcode: %d, x: %d, y: %d, cx: %d, cy: %d", opcode, x, y, cx, cy );

  GpiSetMix( hpsMem, RDOpCodesToMix(opcode) );
  _rectFromRDCoord( &rectl, x, y, cx, cy );
  _invalidateRect( &rectl );

  GpiMove( hpsMem, (PPOINTL)&rectl );
  rectl.xRight--;
  rectl.yTop--;
  GpiBox( hpsMem, DRO_FILL, (PPOINTL)&rectl.xRight, 0, 0 );

  GpiSetMix( hpsMem, FM_DEFAULT );
}

void ui_screenblt(uint8 opcode, int x, int y, int cx, int cy,
                  int srcx, int srcy)
{
  POINTL     aPoints[3];

/*  debug( "opcode: %d, x: %d, y: %d, cx: %d, cy: %d, srcx: %d, srcy: %d",
         opcode, x, y, cx, cy, srcx, srcy );*/

  _rectFromRDCoord( (PRECTL)&aPoints, x, y, cx, cy );
  aPoints[2].x = srcx;                 // Sx1
  aPoints[2].y = g_height - srcy - cy; // Sy1
  if ( GpiBitBlt( hpsMem, hpsMem, 3, aPoints,
                  RDOpCodesToROP( opcode ), 0 ) == GPI_ERROR )
    debug( "GpiBitBlt() failed" );

  _invalidateRect( (PRECTL)&aPoints );
}

void ui_triblt(uint8 opcode, int x, int y, int cx, int cy,
               RD_HBITMAP src, int srcx, int srcy,
               BRUSH *brush, int bgcolour, int fgcolour)
{
/*  debug( "opcode: %d, x: %d, y: %d, cx: %d, cy: %d, "
         "brush: 0x%X, bg: %d, fg: %d",
         opcode, x, y, cx, cy, brush, bgcolour, fgcolour );*/

  switch( opcode )
  {
    case 0x69:         // PDSxxn
      ui_memblt( ROP2_XOR, x, y, cx, cy, src, srcx, srcy );
      ui_patblt( ROP2_NXOR, x, y, cx, cy, brush, bgcolour, fgcolour );
      break;

    case 0xb8:         // PSDPxax
      ui_patblt( ROP2_XOR, x, y, cx, cy, brush, bgcolour, fgcolour );
      ui_memblt( ROP2_AND, x, y, cx, cy, src, srcx, srcy );
      ui_patblt( ROP2_XOR, x, y, cx, cy, brush, bgcolour, fgcolour );
      break;

    case 0xc0:         // PSa
      ui_memblt( ROP2_COPY, x, y, cx, cy, src, srcx, srcy );
      ui_patblt( ROP2_AND, x, y, cx, cy, brush, bgcolour, fgcolour );
      break;

    default:           // Not implemented.
      ui_memblt( ROP2_COPY, x, y, cx, cy, src, srcx, srcy );
  }
}

void ui_patblt(uint8 opcode, int x, int y, int cx, int cy,
               BRUSH *brush, int bgcolour, int fgcolour)
{
  RECTL      rectl;

  /*debug( "opcode: %d, x: %d, y: %d, cx: %d, cy: %d, "
         "brush: 0x%X, bg: %d, fg: %d",
         opcode, x, y, cx, cy, brush, bgcolour, fgcolour );*/

  if ( !_rdbrushSet( hpsMem, opcode, brush ) )
    debug( "_rdbrushSet() failed" );

  GpiSetColor( hpsMem, _RDColToRGB( fgcolour ) );
  GpiSetBackColor( hpsMem, _RDColToRGB( bgcolour ) );

  // Fill rectangle.
  _rectFromRDCoord( &rectl, x, y, cx, cy );
  _invalidateRect( &rectl );
  GpiMove( hpsMem, (PPOINTL)&rectl );
  rectl.xRight--;
  rectl.yTop--;
  GpiBox( hpsMem, DRO_FILL, (PPOINTL)&rectl.xRight, 0, 0 );

  _rdbrushReset( hpsMem );
}


/*
 *           Bitmap.
 */

void ui_paint_bitmap(int x, int y, int cx, int cy,
                     int width, int height, uint8* data)
{
#if 0
  RD_HBITMAP rdBmp;

  rdBmp = ui_create_bitmap( width, height, data );
  if ( rdBmp != NULL )
  {
    ui_memblt( ROP2_COPY, x, y, cx, cy, rdBmp, 0, 0 );
    ui_destroy_bitmap( rdBmp );
  }
#else
  HBITMAP    hbmMem;
  RECTL      rectl;
  POINTL     pointl;

  // Create a bitmap object.
  hbmMem = _createSysBitmap( hpsMem, g_server_depth, width, height,
                             (PBYTE)data, TRUE );
  if ( hbmMem == NULLHANDLE )
    return;

  // Draw created bitmap on window's memory presentation space.
  rectl.xLeft   = 0;
  rectl.yBottom = 0;
  rectl.xRight  = cx;
  rectl.yTop    = cy;
  pointl.x      = x;
  pointl.y      = g_height - y - cy;
  if ( !WinDrawBitmap( hpsMem, hbmMem, &rectl, &pointl, 0, 0,
                       DBM_NORMAL ) )
    debug( "WinDrawBitmap() failed" );

  GpiDeleteBitmap( hbmMem );

  WinOffsetRect( hab, &rectl, pointl.x, pointl.y );
  _invalidateRect( &rectl );
#endif
}

void ui_memblt(uint8 opcode, int x, int y, int cx, int cy,
               RD_HBITMAP src, int srcx, int srcy)
{
  HPS                  hpsSrc = (HPS)src;
  HBITMAP              hbmMem;
  BITMAPINFOHEADER2    bmih;
  POINTL               aPoints[3];

  if ( hpsMem == NULLHANDLE )
  {
    debug( "Presentation space was not obtained." );
    return;
  }

  // Query bitmap header, we need to know height of the bitmap.
  hbmMem = GpiQueryBitmapHandle( hpsSrc, 1 );
  if ( hbmMem == GPI_ERROR )
  {
    debug( "GpiQueryBitmapHandle() failed" );
    return;
  }
  bmih.cbFix = sizeof(bmih);
  if ( !GpiQueryBitmapInfoHeader( hbmMem, &bmih ) )
  {
    debug( "GpiQueryBitmapInfoHeader(%u,) failed", (HBITMAP)src );
    return;
  }

  // Blit bitmap to the window's memory presentation space.
  _rectFromRDCoord( (PRECTL)&aPoints, x, y, cx, cy );
  aPoints[2].x = srcx;                // Sx1
  aPoints[2].y = bmih.cy - srcy - cy; // Sy1
  if ( GpiBitBlt( hpsMem, hpsSrc, 3, aPoints,
                  RDOpCodesToROP( opcode ), 0 ) == GPI_ERROR )
    debug( "GpiBitBlt() failed" );

  _invalidateRect( (PRECTL)&aPoints );
}

RD_HBITMAP ui_create_bitmap(int width, int height, uint8* data)
{
  return _rdbmpCreate( g_server_depth, width, height, (PBYTE)data );
}

void ui_destroy_bitmap(RD_HBITMAP bmp)
{
  _rdbmpDestroy( bmp );
}


/*
 *           Glyph.
 */

RD_HGLYPH ui_create_glyph(int width, int height, uint8* data)
{
  return (RD_HGLYPH)_rdbmpCreate( 1, width, height, (PBYTE)data );
}

void ui_destroy_glyph(RD_HGLYPH glyph)
{
  _rdbmpDestroy( (RD_HGLYPH)glyph );
}

#define DO_GLYPH(ttext,idx) \
{\
  FONTGLYPH *pGlyph = cache_get_font( font, ttext[idx] );\
  if ( (flags & TEXT2_IMPLICIT_X) == 0 )\
  {\
    int xyoffset = ttext[++idx];\
    if ( (xyoffset & 0x80) != 0 )\
    {\
      if ( (flags & TEXT2_VERTICAL) != 0 )\
        y += ttext[idx+1] | (ttext[idx+2] << 8);\
      else\
        x += ttext[idx+1] | (ttext[idx+2] << 8);\
      idx += 2;\
    }\
    else\
    {\
      if ( (flags & TEXT2_VERTICAL) != 0 )\
        y += xyoffset;\
      else\
        x += xyoffset;\
    }\
  }\
  if ( pGlyph != NULL )\
  {\
    ui_memblt( 255, x + pGlyph->offset, y + pGlyph->baseline,\
      pGlyph->width, pGlyph->height, (RD_HBITMAP)pGlyph->pixmap, 0, 0 );\
    if ( (flags & TEXT2_IMPLICIT_X) != 0 )\
      x += pGlyph->width;\
  }\
}

void ui_draw_text(uint8 font, uint8 flags, uint8 opcode, int mixmode,
                  int x, int y, int clipx, int clipy, int clipcx, int clipcy,
                  int boxx, int boxy, int boxcx, int boxcy, BRUSH * brush,
                  int bgcolour, int fgcolour, uint8* text, uint8 length)
{
  IMAGEBUNDLE          stAttr;
  RECTL                rectl = { 0 };
  ULONG                ulIdx, j;
  DATABLOB             *pEntry;

  if (boxx + boxcx > g_width)
    boxcx = g_width - boxx;

  if ( boxcx > 1 )
    _rectFromRDCoord( &rectl, boxx, boxy, boxcx, boxcy );
  else if ( mixmode == MIX_OPAQUE )
    _rectFromRDCoord( &rectl, clipx, clipy, clipcx, clipcy );

  if ( rectl.xLeft != rectl.xRight )
  {
    // Fill rectangle.
    GpiSetColor( hpsMem, _RDColToRGB( bgcolour ) );
    GpiMove( hpsMem, (PPOINTL)&rectl );
    GpiBox( hpsMem, DRO_FILL, (PPOINTL)&rectl.xRight, 0, 0 );
    _invalidateRect( &rectl );
  }

  // Set PS attributes.
  stAttr.lColor     = _RDColToRGB( fgcolour );
  stAttr.lBackColor = _RDColToRGB( bgcolour );
  stAttr.usMixMode  = BM_OVERPAINT;
  stAttr.usBackMixMode = BM_SRCTRANSPARENT;        // Transparent background.
  if ( !GpiSetAttrs( hpsMem, PRIM_IMAGE, IBB_COLOR | IBB_BACK_COLOR |
                     IBB_MIX_MODE | IBB_BACK_MIX_MODE, 0, &stAttr ) )
    debug( "GpiSetAttrs() failed" );

  // Paint text, character by character.
  for( ulIdx = 0; ulIdx < length; )
  {
    switch( text[ulIdx] )
    {
      case 0xFF:
        /* At least two bytes needs to follow */
        if ( (ulIdx + 3) > length )
        {
          // Skipping short 0xFF command.
          ulIdx = length = 0;
          break;
        }
        cache_put_text( text[ulIdx + 1], text, text[ulIdx + 2] );
        ulIdx += 3;
        length -= ulIdx;
        // This will move pointer from start to first character after 0xFF
        // command.
        text = &text[ulIdx];
        ulIdx = 0;
        break;

      case 0xFE:
        // At least one byte needs to follow.
        if ( (ulIdx + 2) > length )
        {
          // Skipping short 0xFE command.
          ulIdx = length = 0;
          break;
        }

        pEntry = cache_get_text( text[ulIdx + 1] );

        if ( pEntry->data != NULL )
        {
          if ( ( ((PBYTE)(pEntry->data))[1] == 0 ) &&
               ( (flags & TEXT2_IMPLICIT_X) == 0 ) && ( (ulIdx + 2) < length ) )
          {
            if (flags & TEXT2_VERTICAL)
              y += text[ulIdx + 2];
            else
              x += text[ulIdx + 2];
          }

          for( j = 0; j < pEntry->size; j++ )
            DO_GLYPH( ((uint8 *)pEntry->data), j );
        }

        ulIdx += (ulIdx + 2) < length ? 3 : 2;
        length -= ulIdx;

        // Move pointer from start to first character after 0xFE command.
        text = &text[ulIdx];
        ulIdx = 0;
        break;

      default:
        DO_GLYPH( text, ulIdx );
        ulIdx++;
        break;
    }
  }

  // Return to default PS attributes.
  if ( !GpiSetAttrs( hpsMem, PRIM_IMAGE, IBB_COLOR | IBB_BACK_COLOR |
                     IBB_MIX_MODE | IBB_BACK_MIX_MODE, 0xFFFFFFFF, NULL ) )
    debug( "GpiSetAttrs() failed" );
}


/*
 *           Colormap.
 */

void ui_set_colourmap(void* map)
{
  HPAL         hpalOld;

  if ( g_owncolmap )
    return;

  debug( "Use colormap %u", map );

  if ( map == NULL )
  {
    debug( "Given colormap is NULL" );
    return;
  }

  hpalCurrent = (HPAL)map;

  if ( hwnd == NULLHANDLE )
  {
    debug( "Window was not created." );
    return;
  }

  GpiCreateLogColorTable( hpsMem, LCOL_RESET, LCOLF_CONSECRGB,
                          0, 0, NULL );
  hpalOld = GpiSelectPalette( hpsMem, (HPAL)map );
  if ( ( hpalOld != NULLHANDLE ) && ( hpalOld != PAL_ERROR ) )
    GpiDeletePalette( hpalOld );
}

void* ui_create_colourmap(COLOURMAP *colours)
{
  COLOURENTRY  *pSrcEntry;
  PULONG       pulIndexRGB;
  PULONG       pulDstEntry;
  ULONG        ulIdx;
  HPAL         hpal;

  if ( ( g_server_depth > 8 ) || ( colours == NULL ) )
  {
    debugCP( "WTF?!" );
    return NULL;
  }

  pulIndexRGB = _getTempMem( colours->ncolours * sizeof(LONG) );
  if ( pulIndexRGB == NULL )
  {
    debug( "Not enough memory" );
    return NULL;
  }

  for( pulDstEntry = pulIndexRGB, pSrcEntry = colours->colours, ulIdx = 0;
       ulIdx < colours->ncolours; ulIdx++, pulDstEntry++, pSrcEntry++ )
  {
    *pulDstEntry = (pSrcEntry->red << 16) | (pSrcEntry->green << 8) |
                  pSrcEntry->blue;
  }

  hpal = GpiCreatePalette( hab, LCOL_OVERRIDE_DEFAULT_COLORS, LCOLF_CONSECRGB,
                           colours->ncolours, pulIndexRGB );

  if ( hpal == GPI_ERROR )
    debug( "GpiCreatePalette() failed" );
  else
    debug( "New colormap %u (items: %u, max.: %u)", hpal, colours->ncolours,
           CAPS_COLOR_INDEX );

  return (void *)hpal;
}


/*
 *           Cursor.
 */

void ui_set_cursor(RD_HCURSOR cursor)
{
  POINTL     pointl;
  RECTL      rectl;

  hptrCursor = (HPOINTER)cursor;

  // Change cursor on screen right now if our window under mouse pointer.
  WinQueryPointerPos( HWND_DESKTOP, &pointl );
  WinMapWindowPoints( HWND_DESKTOP, hwnd, &pointl, 1 );
  WinQueryWindowRect( hwnd, &rectl );
  if ( WinPtInRect( hab, &rectl, &pointl ) )
    WinSetPointer( HWND_DESKTOP, hptrCursor );
}

void ui_set_null_cursor(void)
{
  hptrCursor = NULLHANDLE;
}

RD_HCURSOR ui_create_cursor(unsigned int x, unsigned int y,
                            int width, int height, uint8 *andmask,
                            uint8 *xormask, int bpp)
{
  HPOINTER             hptr;
  POINTERINFO          ptri = { 0 };
  HPS                  hps = WinGetPS( HWND_DESKTOP );
  ULONG                cbMaskLine = (width + 7) / 8;
  ULONG                cbMask = cbMaskLine * height;
  PBYTE                pbMask = alloca( cbMask * 2 );

  if ( pbMask == NULL )
  {
    debug( "Not enough stack" );
    return NULL;
  }

  // Create colour system bitmap (we don't use this for 1-bit images).
  ptri.hbmColor = _createSysBitmap( hps, bpp, width, height, (PBYTE)xormask,
                                    FALSE );

  // Prepare masks bitmaps.
  if ( bpp == 1 )
  {
    memcpy( pbMask, andmask, cbMask );
    memcpy( &pbMask[cbMask], xormask, cbMask );
  }
  else
  {
    memset( pbMask, 0, cbMask );
    memcpy( &pbMask[cbMask], andmask, cbMask );
  }

  // Create masks system bitmap.
  ptri.hbmPointer = _createSysBitmap( hps, 1, width, height * 2, pbMask,
                                      bpp == 1 );
  WinReleasePS( hps );

  // Create a system pointer.
  ptri.fPointer   = TRUE;
  ptri.xHotspot   = x;
  ptri.yHotspot   = height - y - 1;
  hptr = WinCreatePointerIndirect( HWND_DESKTOP, &ptri );

  // Destroy bitmaps.
  GpiDeleteBitmap( ptri.hbmPointer );
  GpiDeleteBitmap( ptri.hbmColor );

  return (RD_HCURSOR)hptr;
}

void ui_destroy_cursor(void* cursor)
{
  HPOINTER             hptr = (HPOINTER)cursor;

  if ( hptr == hptrCursor )
    hptrCursor = WinQuerySysPointer( HWND_DESKTOP, SPTR_ARROW, FALSE );

  WinDestroyPointer( hptr );
}


/*
 *           Clip.
 */

void ui_set_clip(int x, int y, int cx, int cy)
{
  HRGN       hrgn, hrgnOld;
  RECTL      rectl;

  rectl.xLeft   = x;
  rectl.yTop    = g_height - y;
  rectl.xRight  = rectl.xLeft + cx;
  rectl.yBottom = rectl.yTop - cy;
  hrgn = GpiCreateRegion( hpsMem, 1, &rectl );
  if ( hrgn == RGN_ERROR )
    debug( "GpiCreateRegion() failed" );
  else if ( GpiSetClipRegion( hpsMem, hrgn, &hrgnOld ) == RGN_ERROR )
    debug( "GpiSetClipRegion() failed" );
  else if ( ( hrgnOld != HRGN_ERROR ) && ( hrgnOld != NULLHANDLE ) &&
            !GpiDestroyRegion( hpsMem, hrgnOld ) )
    debug( "GpiDestroyRegion(,%u) failed", hrgnOld );
}

void ui_reset_clip(void)
{
  HRGN       hrgnOld;

  if ( hpsMem == NULLHANDLE )
    debug( "Presentation space was not obtained." );
  else if ( GpiSetClipRegion( hpsMem, NULLHANDLE, &hrgnOld ) ==
            RGN_ERROR )
    debug( "GpiSetClipRegion() failed" );
  else if ( ( hrgnOld != HRGN_ERROR ) && ( hrgnOld != NULLHANDLE ) &&
            !GpiDestroyRegion( hpsMem, hrgnOld ) )
    debug( "GpiDestroyRegion() failed" );
}


/*
 *           Update.
 */

void ui_begin_update(void)
{
//  RECTL      rectl;

  if ( hwnd == NULLHANDLE )
    return;

//  WinQueryWindowRect( hwnd, &rectl );
//  WinValidateRect( hwnd, &rectl, FALSE );
}

void ui_end_update(void)
{
//  WinUpdateWindow( hwnd );
  swUpdate();
}


/*
 *           Window.
 */

void ui_resize_window(void)
{
  ULONG      ulScreenCX = WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN );
  ULONG      ulScreenCY = WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN );
  RECTL      rectl = { 0 };
  ULONG      ulWinCX, ulWinCY;

  debugCP();

  if ( hpsMem != NULLHANDLE )
  {
    SIZEL    sizel;
    HPS      hpsNew;

    if ( !GpiQueryBitmapDimension( GpiQueryBitmapHandle( hpsMem, 1 ), &sizel ) )
      debug( "GpiQueryBitmapDimension() failed" );
    else if ( ( sizel.cx < g_width ) || ( sizel.cy < g_height ) )
    {
      hpsNew = (HPS)_rdbmpCreate( g_server_depth, g_width, g_height, NULL );
      if ( hpsNew == NULLHANDLE )
        debug( "_rdbmpCreate() failed" );
      else
      {
        GpiCreateLogColorTable( hpsNew, 0, LCOLF_RGB, 0, 0, NULL );
        _rdbmpDestroy( (RD_HBITMAP)hpsMem );
        hpsMem = hpsNew;
      }
    }
  }

  WinMapWindowPoints( hwnd, HWND_DESKTOP, (PPOINTL)&rectl, 1 );

  rectl.xRight = rectl.xLeft + g_width;
  rectl.yTop = rectl.yBottom + g_height;
  WinCalcFrameRect( hwndFrame, &rectl, FALSE );
  ulWinCX = rectl.xRight - rectl.xLeft;
  ulWinCY = rectl.yTop - rectl.yBottom;

  if ( rectl.yBottom < 0 )
    rectl.yBottom = 0;
  if ( ( rectl.yBottom + ulWinCY ) > ulScreenCY )
    rectl.yBottom = ulScreenCY - ulWinCY;

  if ( ( rectl.xLeft + ulWinCX ) > ulScreenCX )
    rectl.xLeft = ulScreenCX - ulWinCX;
  if ( rectl.xLeft < 0 )
    rectl.xLeft = 0;

  WinSetWindowPos( hwndFrame, HWND_TOP, rectl.xLeft, rectl.yBottom,
                   ulWinCX, ulWinCY, SWP_SIZE );
}

RD_BOOL ui_have_window()
{
  return hwndFrame != NULLHANDLE;
}

void ui_destroy_window(void)
{
  if ( hwndFrame != NULLHANDLE )
  {
    HPOINTER   hptrIcon = (HPOINTER)WinSendMsg( hwndFrame, WM_QUERYICON, 0, 0 );

    if ( hptrIcon != NULLHANDLE )
      WinDestroyPointer( hptrIcon );

    if ( !WinDestroyWindow( hwndFrame ) )
      debug( "WinDestroyWindow() failed" );

    hwndFrame = NULLHANDLE;
    hwnd      = NULLHANDLE;
  }
}

RD_BOOL ui_create_window(void)
{
                            // Frame window flags.
  ULONG      ulWinFlags = FCF_TASKLIST | FCF_NOBYTEALIGN;
  RECTL      rectl, rectlParent;
  ULONG      ulSWPFlags = SWP_ACTIVATE | SWP_SHOW | SWP_ZORDER | SWP_SIZE |
                          SWP_MOVE;
  HPOINTER   hptrIcon;

  //debugCP();

  if ( !WinQueryWindowRect( g_embed_wnd != 0 ? g_embed_wnd : HWND_DESKTOP,
                            &rectlParent ) )
  {
    error( "Window 0x%X does not exist (check the switch -X).\n", g_embed_wnd );
    return False;
  }

  if ( g_fullscreen )
  {
    g_xpos = 0;
    g_ypos = 0;
    g_width = rectlParent.xRight;
    g_height = rectlParent.yTop;
  }
  else
  {
    if ( (g_pos & 7) != 0 )
    {
      // Handle -x-y portion of geometry string.

      if ( g_xpos < 0 || ( g_xpos == 0 && (g_pos & 2) ) )
        g_xpos = rectlParent.xRight + g_xpos - g_width;

      if ( g_ypos < 0 || (g_ypos == 0 && (g_pos & 4)) )
        g_ypos = rectlParent.yTop + g_ypos - g_height;
    }
    else
    {
      // Default position at center of the desktop / parent window.
      g_xpos = ( rectlParent.xRight - g_width ) / 2;
      g_ypos = ( rectlParent.yTop - g_height ) / 2;
    }

    if ( !g_hide_decorations )
      ulWinFlags |= FCF_SIZEBORDER | FCF_TITLEBAR | FCF_SYSMENU | FCF_MINBUTTON;
  }

  // Create a PM window.
  hwndFrame = WinCreateStdWindow( HWND_DESKTOP, 0, &ulWinFlags,
                                  WIN_CLIENT_CLASS, g_title, 0, 0, 1, &hwnd );
  if ( hwndFrame == NULLHANDLE )
  {
    debug( "WinCreateStdWindow() failed" );
    return False;
  }

  nonblockingWin( hwndFrame, TRUE );

  oldWndFrameProc = WinSubclassWindow( hwndFrame, wndFrameProc );

  if ( g_embed_wnd != 0 )
  {
    if ( !WinSetParent( hwndFrame, g_embed_wnd, FALSE ) )
    {
      error( "Cannot set specified parent window 0x%X", g_embed_wnd );
      return False;
    }
  }

  // Set window icon. We have VIO application, so we need to do it "manualy".

  hptrIcon = WinLoadPointer( HWND_DESKTOP, NULLHANDLE, 1 );
  if ( hptrIcon != NULLHANDLE )
    WinSendMsg( hwndFrame, WM_SETICON, MPFROMLONG(hptrIcon), 0 );

  // Set window position, visibility, e.t.c.

  WinSetWindowULong( hwnd, 0, 0 );
  WinSetWindowPtr( hwnd, 4, NULL );

  // Calc frame window position relative top-left corner of screen.
  rectl.xLeft = g_xpos;
  rectl.yTop  = rectlParent.yTop - g_ypos;
  rectl.xRight = rectl.xLeft + g_width;
  rectl.yBottom = rectl.yTop - g_height;
  WinCalcFrameRect( hwndFrame, &rectl, FALSE );

  WinSetWindowPos( hwndFrame, HWND_TOP, rectl.xLeft, rectl.yBottom,
                   rectl.xRight - rectl.xLeft, rectl.yTop - rectl.yBottom,
                   ulSWPFlags );

  if ( ( (g_pos & 7) == 0 ) && !g_fullscreen )
  {
    SWP      swp;

    WinQueryWindowPos( hwndFrame, &swp );
    if ( (swp.x + swp.cx) > rectlParent.xRight )
      swp.x = rectlParent.xRight > swp.cx ? rectlParent.xRight - swp.cx : 0;

    if ( (swp.y + swp.cy) > rectlParent.yTop )
      swp.y = rectlParent.yTop - swp.cy;

    WinSetWindowPos( hwndFrame, HWND_TOP, swp.x, swp.y, 0, 0, SWP_MOVE );
  }

  sHSlider = 0;
  sVSlider = 0;

  if ( g_seamless_rdp )
    seamless_reset_state();

  return True;
}


/*
 *           Event queue.
 */

int ui_select(int rdp_socket)
{
  int                  iSockNum;
  struct timeval       tv;
  fd_set               rfds, wfds;
  QMSG                 qmsg;
  RD_BOOL              s_timeout = False;
  int                  iRC;

  if ( swNoWindowsLeft() )
  {
    debug( "No seamless windows left" );
    return 0;
  }

  do
  {
    // Process window events.

    if ( WinPeekMsg( hab, &qmsg, NULLHANDLE, 0, 0, PM_REMOVE ) )
    {
      if ( ( qmsg.msg == WM_CLOSE ) || ( qmsg.msg == WM_QUIT ) )
        return 0;
      WinDispatchMsg( hab, &qmsg );
    }

    xclipProcess();

    FD_ZERO( &rfds );
    FD_ZERO( &wfds );

    iSockNum = rdp_socket;
    FD_SET( rdp_socket, &rfds );

    /* default timeout */
    tv.tv_sec  = 0;
    tv.tv_usec = 10000;

#ifdef WITH_RDPSND
    rdpsnd_add_fds( &iSockNum, &rfds, &wfds, &tv );
#endif

    /* add redirection handles */
    rdpdr_add_fds( &iSockNum, &rfds, &wfds, &tv, &s_timeout );
    seamless_select_timeout( &tv );

    /* add ctrl slaves handles */
    ctrl_add_fds( &iSockNum, &rfds );

    iSockNum++;

    iRC = select( iSockNum, &rfds, &wfds, NULL, &tv );
#ifdef WITH_RDPSND
    rdpsnd_check_fds( &rfds, &wfds );
#endif

    switch( iRC )
    {
      case -1:
        error( "select: %s\n", strerror( errno ) );

      case 0:
        /* Abort serial read calls */
        if ( s_timeout )
          rdpdr_check_fds( &rfds, &wfds, (RD_BOOL)True );
        continue;
    }

    rdpdr_check_fds( &rfds, &wfds, (RD_BOOL)False );

    ctrl_check_fds( &rfds, &wfds );
  }
  while( !FD_ISSET( rdp_socket, &rfds ) );

  return 1;
}


/*
 *           Initialization / finalization
 */

// Initialize connection specific data, such as session size. 

void ui_init_connection(void)
{
  RECTL      rectlParent;

  if ( !WinQueryWindowRect( g_embed_wnd != 0 ? g_embed_wnd : HWND_DESKTOP,
                            &rectlParent ) )
  {
    debug( "Window %X does not exist (check the switch -X)", g_embed_wnd );
    rectlParent.xRight = WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN );
    rectlParent.yTop   = WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN );
  }

  if ( !g_fullscreen && ( g_sizeopt < 0 ) )
  {
    /* Percent of screen */
    if ( -g_sizeopt >= 100 )
      g_fullscreen = True;
    else
    {
      g_width  = rectlParent.xRight * (-g_sizeopt) / 100;
      g_height = rectlParent.yTop * (-g_sizeopt) / 100;
    }
  }

  if ( g_fullscreen )
  {
    g_width  = rectlParent.xRight;
    g_height = rectlParent.yTop;
  }
  else
    /* make sure width is a multiple of 4 */
    g_width = (g_width + 3) & ~3;

  hpsMem = (HPS)_rdbmpCreate( g_server_depth, g_width, g_height, NULL );
  if ( hpsMem == NULLHANDLE )
    debug( "_rdbmpCreate() failed" );
  else
    GpiCreateLogColorTable( hpsMem, 0, LCOLF_RGB, 0, 0, NULL );
}


// Initialize the UI. This is done once per process.

RD_BOOL ui_init(void)
{
  PTIB       tib;
  PPIB       pib;
  HPS        hps;
  HDC        hdc;
  LONG       lDesktopPlanes, lDesktopDepth;
  ULONG      ulRC;
  CHAR       acError[256];

  debugInit();
  sock_init();
  setbuf( stdout, NULL );

  // Change process type code for use Win* API from VIO session.
  DosGetInfoBlocks( &tib, &pib );
  if ( pib->pib_ultype == 2 || pib->pib_ultype == 0 )
  {
    // VIO windowable or fullscreen protect-mode session.
    pib->pib_ultype = 3; // Presentation Manager protect-mode session.
    // ...and switch to the desktop (if we are in fullscreen now)?
  }

  hab = WinInitialize( 0 );
  hmq = WinCreateMsgQueue( hab, 0 );
  if ( hmq == NULLHANDLE )
  {
    debug( "WinCreateMsgQueue() failed" );
    return False;
  }

  WinRegisterClass( hab, WIN_CLIENT_CLASS, wndProc,
                    CS_SIZEREDRAW | CS_MOVENOTIFY,
                    sizeof(HPS) + sizeof(PSEAMLESSWIN) );

  hptrCursor = WinQuerySysPointer( HWND_DESKTOP, SPTR_ARROW, FALSE );

  // Detect screen color depth.
  hps = WinGetScreenPS( HWND_DESKTOP );
  hdc = GpiQueryDevice( hps );
  DevQueryCaps( hdc, CAPS_COLOR_PLANES, 1, &lDesktopPlanes );
  DevQueryCaps( hdc, CAPS_COLOR_BITCOUNT, 1, &lDesktopDepth );
  WinReleasePS( hps );
  lDesktopDepth *= lDesktopPlanes;

  if ( g_server_depth == -1 )
    g_server_depth = lDesktopDepth;
  else if ( g_server_depth > lDesktopDepth )
  {
    warning( "Remote desktop colour depth %d higher than display colour "
             "depth %d.\n", g_server_depth, lDesktopDepth );
  }

  // Load AMouse API (AMouDll.dll)

  pfnWinRegisterForWheelMsg = NULL;
  ulRC = DosLoadModule( acError, sizeof(acError), _AMOUSE_DLL, &hmAMouDll );
  if ( ulRC != NO_ERROR )
    debug( _AMOUSE_DLL" not loaded: %s", acError );
  else
  {
    ulRC = DosQueryProcAddr( hmAMouDll, 0, _AMOUSE_WINREGISTERFORWHEELMSG,
                             (PFN *)&pfnWinRegisterForWheelMsg );
    if ( ulRC != NO_ERROR )
    {
      debug( "Cannot find entry "_AMOUSE_WINREGISTERFORWHEELMSG" in "_AMOUSE_DLL );
      DosFreeModule( hmAMouDll );
      hmAMouDll = NULLHANDLE;
    }
    else
      debug( "AMouse API loaded" );
  }

  xclipInit();

  if ( g_seamless_rdp )
    seamless_init();

  disk_init();

  signal( SIGINT, _sigBreak );
  signal( SIGTERM, _sigBreak );

  return True;
}

void ui_deinit(void)
{
  xclipDone();

  WinDestroyMsgQueue( hmq );
  WinTerminate( hab );
  hab = NULLHANDLE;

  if ( hpalCurrent != NULLHANDLE )
    GpiDeletePalette( hpalCurrent );

  if ( hpsMem != NULLHANDLE )
    _rdbmpDestroy( (RD_HBITMAP)hpsMem );

  if ( hmAMouDll != NULLHANDLE )
    DosFreeModule( hmAMouDll );

  if ( pTempMem != NULL )
    debugFree( pTempMem );

  disk_done();
  debugStat();
  debugDone();
}
