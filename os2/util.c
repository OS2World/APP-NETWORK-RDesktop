#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys\socket.h>
#include <netinet\in.h>
#include <sys\un.h>
#include <unistd.h>
#include <iconv.h>
#include "os2rd.h"
#include "debug.h"

// Returns length of result string without trailing ZERO.
ULONG utilStrIConv(PSZ pszSrcCP, PSZ pszResCP, PCHAR pcSrc, ULONG cbSrc,
                   PCHAR pcRes, ULONG cbRes)
{
  ULONG          cbResRem = cbRes;
  PCHAR          pcResRem = pcRes;
  iconv_t        cd;

  debug( "%s -> %s", pszSrcCP, pszResCP );

  cd = iconv_open( pszResCP, pszSrcCP );
  if ( cd == ((iconv_t)(-1)) )
    debug( "iconv_open(\"%s\",\"%s\") failed", pszResCP, pszSrcCP );
  else
  {
    size_t   ret;

    while( cbSrc > 0 )
    {
      ret = iconv( cd, (ICONV_CONST char **)&pcSrc, (size_t *)&cbSrc,
                   &pcResRem, (size_t *)&cbResRem );
      if ( ( ret == -1 ) && ( errno == EILSEQ ) )
      {
        // Try skipping some input data. We'll lost some characters.

        if ( ( cbSrc > 1 ) && strcmp( pszSrcCP, "UTF-16LE" ) == 0 )
        {
          CHAR         chNew;

          // Dirty workaround for quotas, dash and other symbols from UTF-16LE.

debug( "Unknown character: %lu", *(PUSHORT)pcSrc );
          switch( *(PUSHORT)pcSrc )
          {
            case 0x00AB: // quota <<
            case 0x00BB: // quota >>
            case 0x201C: // quota "
            case 0x201D: // quota "
            case 0x201E: // quota ,,
              chNew = '"';
              break;

            case 0x2013: // short dash
            case 0x2014: // long dash
            case 0xF0BE:
              chNew = '-';
              break;

            case 0x0301: // accent (skip)
              chNew = '\0';
              break;

            default:
              chNew = '?';
              break;
          }

          if ( chNew != '\0' )
          {
            debug( "Convert 0x%X -> %c", *(PUSHORT)pcSrc, chNew );

            *pcResRem = chNew;
            pcResRem += 1;
            cbResRem -= 1;
          }

          pcSrc++;
          cbSrc--;
        } // if ( ( cbSrc > 1 ) && strcmp( pszSrcCP, "UTF-16LE" ) == 0 )
        else
        {
          debug( "Cannot convert 0x%X (%c) from %s",
                 *(PUSHORT)pcSrc, *pcSrc, pszSrcCP );
        }

        pcSrc++;
        cbSrc--;
        continue;
      } // if ( ( ret == -1 ) && ( errno == EILSEQ ) )

      break;
    } // while( cbSrc > 0 )

    iconv_close( cd );
  }

  if ( cbResRem > 1 )
    *((PUSHORT)pcResRem) = 0;
  else if ( cbResRem == 1 )
    *pcResRem = '\0';

  return pcResRem - pcRes;
}

BOOL utilPipeSock(int *piSock1, int *piSock2)
{
  int                  iServSock = -1, iSock1 = -1, iSock2 = -1;
  struct sockaddr_un   stUn;
  struct sockaddr_in   sClientSockAddr;
  int                  cbSockAddr = sizeof(struct sockaddr_in);

  iSock1 = socket( PF_UNIX, SOCK_STREAM, 0 );
  iServSock = socket( PF_UNIX, SOCK_STREAM, 0 );
  if ( ( iSock1 == -1 ) || ( iServSock == -1 ) )
  {
    debug( "socket() failed" );
    goto pipeSock_fail;
  }

  stUn.sun_len = sizeof(stUn);
  stUn.sun_family = AF_UNIX;
  _snprintf( stUn.sun_path, sizeof(stUn.sun_path), "\\socket\\%u-%u",
             iServSock, iSock1 );
  stUn.sun_path[sizeof(stUn.sun_path) - 1] = '\0';

  if ( bind( iServSock, (struct sockaddr *)&stUn, sizeof(stUn) ) == -1 )
  {
    debug( "bind() failed" );
    goto pipeSock_fail;
  }

  if ( listen( iServSock, 1 ) == -1 )
  {
    debug( "listen() failed" );
    goto pipeSock_fail;
  }

  if ( connect( iSock1, (struct sockaddr *)&stUn, SUN_LEN( &stUn ) ) == -1 )
  {
    debug( "connect() failed" );
    goto pipeSock_fail;
  }

  iSock2 = accept( iServSock, (struct sockaddr *)&sClientSockAddr,
                        &cbSockAddr );
  if ( iSock2 == -1 )
  {    
    debug( "accept() failed" );
    goto pipeSock_fail;
  }

  soclose( iServSock );

  *piSock1 = iSock1;
  *piSock2 = iSock2;
  return TRUE;

pipeSock_fail:

  if ( iSock1 != -1 )
    soclose( iSock1 );

  if ( iSock2 != -1 )
    soclose( iSock2 );

  if ( iServSock != -1 )
    soclose( iServSock );

  return FALSE;
}


//           Bitmaps.
//           --------

typedef ULONG          DWORD;
typedef USHORT         WORD;

// FXPT2DOT30 interpreted as fixed-point values with a 2-bit integer part and a
// 30-bit fractional part.
typedef LONG           FXPT2DOT30;

#pragma pack(1)

typedef struct _WCIEXYZ {
  FXPT2DOT30   ciexyzX;
  FXPT2DOT30   ciexyzY;
  FXPT2DOT30   ciexyzZ;
} CIEXYZ;

typedef struct _CIEXYZTRIPLE {
  CIEXYZ       ciexyzRed;
  CIEXYZ       ciexyzGreen;
  CIEXYZ       ciexyzBlue;
} CIEXYZTRIPLE;

// LCS_xxxxx - WBITMAPV5HEADER.bV5CSType values.
#define LCS_sRGB                 0x73524742

// LCS_GM_xxxxx - WBITMAPV5HEADER.bV5Intent values.
#define LCS_GM_BUSINESS          1  /* Saturation */
#define LCS_GM_GRAPHICS          2  /* Relative */
#define LCS_GM_IMAGES            4  /* Perceptual */
#define LCS_GM_ABS_COLORIMETRIC  8  /* Absolute */

typedef struct _WBITMAPV5HEADER {
  DWORD        bV5Size;
  LONG         bV5Width;
  LONG         bV5Height;
  WORD         bV5Planes;
  WORD         bV5BitCount;
  DWORD        bV5Compression;
  DWORD        bV5SizeImage;
  LONG         bV5XPelsPerMeter;
  LONG         bV5YPelsPerMeter;
  DWORD        bV5ClrUsed;
  DWORD        bV5ClrImportant;
  DWORD        bV5RedMask;
  DWORD        bV5GreenMask;
  DWORD        bV5BlueMask;
  DWORD        bV5AlphaMask;
  DWORD        bV5CSType;
  CIEXYZTRIPLE bV5Endpoints;
  DWORD        bV5GammaRed;
  DWORD        bV5GammaGreen;
  DWORD        bV5GammaBlue;
  DWORD        bV5Intent;
  DWORD        bV5ProfileData;
  DWORD        bV5ProfileSize;
  DWORD        bV5Reserved;
} WBITMAPV5HEADER, *PWBITMAPV5HEADER;

// BI_xxxxx - WBITMAPINFOHEADER.biCompression values
#define BI_RGB 0
#define BI_RLE8 1
#define BI_RLE4 2
#define BI_BITFIELDS 3

typedef struct _WBITMAPINFOHEADER {
  DWORD        biSize;
  LONG         biWidth;
  LONG         biHeight;
  WORD         biPlanes;
  WORD         biBitCount;
  DWORD        biCompression;
  DWORD        biSizeImage;
  LONG         biXPelsPerMeter;
  LONG         biYPelsPerMeter;
  DWORD        biClrUsed;
  DWORD        biClrImportant;
} WBITMAPINFOHEADER, *PWBITMAPINFOHEADER;

typedef struct _WRGBQUAD {
  BYTE         rgbBlue; 
  BYTE         rgbGreen; 
  BYTE         rgbRed; 
  BYTE         rgbReserved; 
} WRGBQUAD, *PWRGBQUAD;

typedef struct _WBITMAPINFO {
  WBITMAPINFOHEADER bmiHeader;
  WRGBQUAD          bmiColors[1];
} WBITMAPINFO, *PWBITMAPINFO;

#pragma pack()

HBITMAP utilWinToSysBitmap(HPS hps, PCHAR pcData, ULONG cbData)
{
  PWBITMAPINFOHEADER   pWBmHdr = (PWBITMAPINFOHEADER)pcData;
  PBITMAPINFOHEADER2   pbmih;
  PBITMAPINFO2         pbmi;
  HBITMAP              hbm;
  PWRGBQUAD            pbmiColors;
  ULONG                cColors;

  // Bitmap format checks.

  if ( pWBmHdr->biSize < sizeof(WBITMAPINFOHEADER) )
  {
    debug( "Bitmap header size value to small: %u, minimum: %u",
           pWBmHdr->biSize, sizeof(WBITMAPINFOHEADER) );
    return NULLHANDLE;
  }

  switch( pWBmHdr->biBitCount )
  {
    case 1:
    case 4:
    case 8:
    case 16:
    case 24:
    case 32:
      break;
    default:
      debug( "Invalid BPP value: %u", pWBmHdr->biBitCount );
      return NULLHANDLE;
  }

  // Check compression method.
  switch( pWBmHdr->biCompression )
  {
    case BI_RGB:
    case BI_RLE8:
    case BI_RLE4:
    case BI_BITFIELDS:
      break;
    default:
      debug( "Unsupported compression method: %u", pWBmHdr->biCompression );
      return NULLHANDLE;
  }

  // Check width and height.
  if ( ( pWBmHdr->biWidth <= 0 ) || ( pWBmHdr->biHeight == 0 ) )
  {
    debug( "Invalid size: %d x %d", pWBmHdr->biWidth, pWBmHdr->biHeight );
    return NULLHANDLE;
  }

  // Check number of colors in palette.
  if ( ( pWBmHdr->biBitCount < 16 ) &&
       ( pWBmHdr->biClrUsed > (1 << pWBmHdr->biBitCount) ) )
  {
    debug( "Invalid the palette size: %u (for BPP %u)",
           pWBmHdr->biClrUsed, pWBmHdr->biBitCount );
    return NULLHANDLE;
  }

  // Detect a palette size.

  if ( pWBmHdr->biBitCount < 16 )
  {
    cColors = pWBmHdr->biClrUsed != 0
                ? pWBmHdr->biClrUsed : ( 1 << pWBmHdr->biBitCount );
  }
  else if ( pWBmHdr->biBitCount == 16 )
    cColors = pWBmHdr->biClrUsed;          // May be a zero.
  else
    cColors = 0;                           // No palette for BPP > 16.

  // Create a system bitmap header.

  pbmih = alloca( sizeof(BITMAPINFO2) + (sizeof(RGB2) * cColors) );
  if ( pbmih == NULL )
  {
    debug( "Not enough stack size" );
    return NULLHANDLE;
  }
  memset( pbmih, 0, sizeof(BITMAPINFOHEADER2) );
  pbmih->cbFix           = sizeof(BITMAPINFOHEADER2);
  pbmih->cx              = pWBmHdr->biWidth;
  pbmih->cy              = abs( pWBmHdr->biHeight );
  pbmih->cPlanes         = pWBmHdr->biPlanes == 0 ? 1 : pWBmHdr->biPlanes;
  pbmih->cBitCount       = pWBmHdr->biBitCount;
                           // Fast dirty workaround for BI_BITFIELDS...
  pbmih->ulCompression   = pWBmHdr->biCompression == BI_BITFIELDS ?
                             BCA_UNCOMP : pWBmHdr->biCompression;
  pbmih->cbImage         = cbData - pWBmHdr->biSize - (cColors * sizeof(RGB2));
  pbmih->cxResolution    = pWBmHdr->biXPelsPerMeter;
  pbmih->cyResolution    = pWBmHdr->biYPelsPerMeter;
  pbmih->cclrUsed        = cColors;
  pbmih->cclrImportant   = pWBmHdr->biClrImportant;
  // Copy paltte.
  pbmi = (PBITMAPINFO2)pbmih;
  pbmiColors = (PWRGBQUAD)&pcData[pWBmHdr->biSize];
  memcpy( pbmi->argbColor, pbmiColors, cColors * sizeof(RGB2) );

  // Create a system bitmap object
  hbm = GpiCreateBitmap( hps, pbmih, CBM_INIT, (PCHAR)&pbmiColors[cColors],
                         pbmi );
  if ( hbm == GPI_ERROR )
  {
    debug( "GpiCreateBitmap() failed" );
    return NULLHANDLE;
  }

  return hbm;
}

ULONG utilSysBitmapToWinDIBV5(HAB hab, HBITMAP hbm, PCHAR pcBuf, ULONG cbBuf)
{
  HPS                  hpsMem;
  HDC                  hdcMem;
  SIZEL                sizel;
  CHAR                 acbmih[sizeof(BITMAPINFOHEADER2) + sizeof(RGB2) * 256]
                         = { 0 };
  PBITMAPINFOHEADER2   pbmih = (PBITMAPINFOHEADER2)acbmih;
  ULONG                cbImage, cColors, cbColors, cbDIB;

  pbmih->cbFix = sizeof(BITMAPINFOHEADER2);
  if ( !GpiQueryBitmapInfoHeader( hbm, pbmih ) )
  {
    debug( "GpiQueryBitmapInfoHeader() failed" );
    return 0;
  }
  sizel.cx = pbmih->cx;
  sizel.cy = pbmih->cy;

  // Detect patette size.
  if ( pbmih->cBitCount < 16 )
    cColors = pbmih->cclrUsed != 0 ? pbmih->cclrUsed : ( 1 << pbmih->cBitCount );
  else
    cColors = 0;                           // No palette for BPP >= 16.
  cbColors = cColors * sizeof(RGB2);

  // Bitmap data size.
  cbImage = ( ( ( pbmih->cBitCount * pbmih->cx + 31 ) / 32 ) * 4 ) * pbmih->cy;

  // Result size.
  cbDIB = sizeof(WBITMAPV5HEADER) + cbColors + cbImage;
  if ( cbBuf < cbDIB )
    return ( cbBuf == 0 ) || ( pcBuf == NULL ) ? cbDIB : 0;

  hdcMem = DevOpenDC( hab, OD_MEMORY, "*", 0, NULL, NULLHANDLE );
  if ( hdcMem == NULLHANDLE )
  {
    debug( "Cannot open memory device context" );
    return 0;
  }

  // Create a new memory presentation space.
  hpsMem = GpiCreatePS( hab, hdcMem, &sizel,
                        PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC );
  if ( hpsMem == NULLHANDLE )
  {
    debug( "GpiCreatePS() failed. Memory PS was not created." );
    DevCloseDC( hdcMem );
    return 0;
  }

  if ( GpiSetBitmap( hpsMem, hbm ) == HBM_ERROR )
    debug( "GpiSetBitmap() failed" );

  // Create header for windows bitmap (DIBv5).
  memset( pcBuf, 0, sizeof(WBITMAPV5HEADER) );
  ((PWBITMAPV5HEADER)pcBuf)->bV5Size          = sizeof(WBITMAPV5HEADER);
  ((PWBITMAPV5HEADER)pcBuf)->bV5Width         = pbmih->cx;
  ((PWBITMAPV5HEADER)pcBuf)->bV5Height        = pbmih->cy;
  ((PWBITMAPV5HEADER)pcBuf)->bV5Planes        = 1;
  ((PWBITMAPV5HEADER)pcBuf)->bV5BitCount      = pbmih->cBitCount;
  ((PWBITMAPV5HEADER)pcBuf)->bV5Compression   = BI_RGB;
  ((PWBITMAPV5HEADER)pcBuf)->bV5SizeImage     = cbImage;
  ((PWBITMAPV5HEADER)pcBuf)->bV5XPelsPerMeter = pbmih->cxResolution;
  ((PWBITMAPV5HEADER)pcBuf)->bV5YPelsPerMeter = pbmih->cyResolution;
  ((PWBITMAPV5HEADER)pcBuf)->bV5ClrUsed       = cColors;
  ((PWBITMAPV5HEADER)pcBuf)->bV5ClrImportant  = pbmih->cclrImportant;
  ((PWBITMAPV5HEADER)pcBuf)->bV5RedMask       = 0x00FF0000;
  ((PWBITMAPV5HEADER)pcBuf)->bV5GreenMask     = 0x0000FF00;
  ((PWBITMAPV5HEADER)pcBuf)->bV5BlueMask      = 0x000000FF;
  ((PWBITMAPV5HEADER)pcBuf)->bV5AlphaMask     = 0xFF000000;
  ((PWBITMAPV5HEADER)pcBuf)->bV5CSType        = LCS_sRGB;
  ((PWBITMAPV5HEADER)pcBuf)->bV5Intent        = LCS_GM_ABS_COLORIMETRIC;

  // Copy image.
  if ( GpiQueryBitmapBits( hpsMem, 0, pbmih->cy,
                           &pcBuf[sizeof(WBITMAPV5HEADER) + cbColors],
                           (PBITMAPINFO2)pbmih ) == GPI_ALTERROR )
  {
    debug( "GpiQueryBitmapBits() failed" );
    cbDIB = 0;
  }
  else
    // Now (after GpiQueryBitmapBits()) we have filled color table.
    memcpy( &pcBuf[sizeof(WBITMAPV5HEADER)],
            &((PCHAR)pbmih)[sizeof(BITMAPINFOHEADER2)], cbColors );

  GpiDestroyPS( hpsMem );
  DevCloseDC( hdcMem );

  // Return DIB size in bytes.
  return cbDIB;
}
