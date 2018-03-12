#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <som.h>
#define INCL_DOS
#define INCL_WIN
#define INCL_ERRORS
#define INCL_DOSMISC
#define INCL_WINWORKPLACE
#include <util.h>
#include <debug.h>
//#include <wpabs.h>

#ifndef DosQueryModFromEIP
APIRET APIENTRY  DosQueryModFromEIP(HMODULE *phMod, ULONG *pObjNum,
                                    ULONG BuffLen, PCHAR pBuff, ULONG *pOffset,
                                    ULONG Address);
#endif

HMODULE utilGetModuleHandle()
{
  HMODULE    hmod = NULLHANDLE;
  ULONG      ulObjectNumber  = 0;
  ULONG      ulOffset        = 0;
  CHAR       szModName[_MAX_PATH];

  DosQueryModFromEIP( &hmod, &ulObjectNumber,
                    sizeof( szModName), szModName,
                    &ulOffset, (ULONG)utilGetModuleHandle);
  return hmod;
}

VOID utilStrTrim(PSZ pszStr)
{
  PSZ        pszNew = pszStr;

  STR_TRIM( pszNew );
  strcpy( pszStr, pszNew );
}

// ULONG utilLoadInsertStr(HMODULE hMod, BOOL fStrMsg, ULONG ulId,
//                         ULONG cVal, PSZ *ppszVal, ULONG cbBuf, PCHAR pcBuf)
//
// Loads string ulId from resource ( string table or message table ) and
// inserts variable text-string information from the array ppszVal.
// pcBuf - The address where function returns the requested string.
// cbBuf - The length, in bytes, of the buffer.
// Returns actual length, in bytes, of the string in pcBuf or 0 on error.

ULONG utilLoadInsertStr(HMODULE hMod,      // module handle
                        BOOL fStrMsg,      // string (1) / message (0)
                        ULONG ulId,        // string/message id
                        ULONG cVal, PSZ *ppszVal,// count and pointers to values
                        ULONG cbBuf, PCHAR pcBuf)// result buffer
{
  ULONG		ulLen;
  PCHAR		pcTemp;
  ULONG		ulRC;
  HAB     hab = WinQueryAnchorBlock( HWND_DESKTOP );

  if ( ( cVal == 0 ) || ( ppszVal == NULL ) )
  {
    return fStrMsg ? WinLoadString( hab, hMod, ulId, cbBuf, pcBuf )
                   : WinLoadMessage( hab, hMod, ulId, cbBuf, pcBuf );
  }

  pcTemp = alloca( cbBuf );
  if ( pcTemp == NULL )
    return 0;

  ulLen = fStrMsg ? WinLoadString( hab, hMod, ulId, cbBuf, pcTemp )
                  : WinLoadMessage( hab, hMod, ulId, cbBuf, pcTemp );

  ulRC = DosInsertMessage( ppszVal, cVal, pcTemp, ulLen, pcBuf, cbBuf - 1,
                           &ulLen );
  if ( ulRC != NO_ERROR )
    ulLen = 0;

  pcBuf[ulLen] = '\0';

  return ulLen;
}

/*static VOID _mbAddBtn(MB2D *pMb2d, ULONG ulResStrId, ULONG ulBtnId)
{
  HAB      hab = WinQueryAnchorBlock( HWND_DESKTOP );
  HMODULE  hModule = utilGetModuleHandle();

  WinLoadString( hab, hModule, ulResStrId, MAX_MBDTEXT, pMb2d->achText );
  pMb2d->idButton = ulBtnId;
  pMb2d->flStyle = 0;
}*/

ULONG utilMessageBox(HWND hwnd, PSZ pszTitle, ULONG ulMsgResId, ULONG ulStyle)
{
  HAB      hab = WinQueryAnchorBlock( HWND_DESKTOP );
  HMODULE  hModule = utilGetModuleHandle();
  CHAR     acText[512];
  CHAR     acTitle[256];
/*  CHAR     acMBInfo[sizeof(MB2INFO) + 2*sizeof(MB2D)]; // Enough for 3 buttons.
  PMB2INFO pMBInfo = (PMB2INFO)acMBInfo;
  ULONG    ulBtnStyle = ulStyle & 0x0F;*/

  if ( pszTitle == NULL )
  {
    WinQueryWindowText( hwnd, sizeof(acTitle) - 1, acTitle );
    pszTitle = acTitle;
  }
  WinLoadMessage( hab, hModule, ulMsgResId, sizeof(acText), acText );

  return WinMessageBox( HWND_DESKTOP, hwnd, acText, pszTitle, 0, ulStyle );

/*
  pMBInfo->cb = sizeof(acMBInfo);
  pMBInfo->hIcon = NULLHANDLE;
  pMBInfo->flStyle = ulStyle & 0xF0F0; // No buttons informaion here.
  pMBInfo->hwndNotify = NULLHANDLE;
  pMBInfo->cButtons = 0;

  if ( ulBtnStyle == MB_ABORTRETRYIGNORE )
  {
    _mbAddBtn( &pMBInfo->mb2d[pMBInfo->cButtons++], UTIL_IDS_ABORT, MBID_ABORT );
    _mbAddBtn( &pMBInfo->mb2d[pMBInfo->cButtons++], UTIL_IDS_RETRY, MBID_RETRY );
    _mbAddBtn( &pMBInfo->mb2d[pMBInfo->cButtons++], UTIL_IDS_IGNORE, MBID_IGNORE );
  }
  else
  {
    switch( ulBtnStyle )
    {
      case MB_OK:
      case MB_OKCANCEL:
        _mbAddBtn( &pMBInfo->mb2d[pMBInfo->cButtons++], UTIL_IDS_OK, MBID_OK );
        break;

      case MB_YESNOCANCEL:
      case MB_YESNO:
        _mbAddBtn( &pMBInfo->mb2d[pMBInfo->cButtons++], UTIL_IDS_YES, MBID_YES );
        _mbAddBtn( &pMBInfo->mb2d[pMBInfo->cButtons++], UTIL_IDS_NO, MBID_NO );
        break;
    }

    switch( ulBtnStyle )
    {
      case MB_RETRYCANCEL:
        _mbAddBtn( &pMBInfo->mb2d[pMBInfo->cButtons++], UTIL_IDS_RETRY, MBID_RETRY );

      case MB_OKCANCEL:
      case MB_YESNOCANCEL:
        _mbAddBtn( &pMBInfo->mb2d[pMBInfo->cButtons++], UTIL_IDS_CANCEL, MBID_CANCEL );
        break;
    }
  }

  pMBInfo->mb2d[(ulStyle & 0x0F00) >> 8].flStyle = BS_DEFAULT;

  return WinMessageBox2( HWND_DESKTOP, hwnd, acText, pszTitle, 0, pMBInfo );
*/
}

BOOL utilVerifyDomainName(ULONG cbName, PCHAR pcName)
{
  ULONG      cbPart;

  if ( ( pcName == NULL ) || ( cbName == 0 ) ||
       ( pcName[cbName - 1] == '.' ) )
    return FALSE;

  cbPart = 0;
  do
  {
    if ( ( *pcName == '-' ) && ( cbPart == 0 ) )
      return FALSE;

    if ( ( !isalnum( *pcName ) && ( *pcName != '-' ) && ( *pcName != '_' ) ) ||
         ( (++cbPart) > 63 ) )
      return FALSE;

    cbName--;
    pcName++;
    if ( *pcName == '.' ) 
    {
      if ( cbPart == 0 )
        return FALSE;

      cbName--;
      pcName++;
      cbPart = 0;
    }
  }
  while( cbName != 0 );

  return cbPart != 0;
}

VOID util3DFrame(HPS hps, PRECTL pRect, LONG lLTColor, LONG lRBColor)
{
  POINTL	pt;
  // Store current color to lColor,
  LONG		lSaveColor = GpiQueryColor( hps );

  GpiSetColor( hps, lLTColor );
  GpiMove( hps, (PPOINTL)pRect );
  pt.x = pRect->xLeft;
  pt.y = pRect->yTop - 1;
  GpiLine( hps, &pt );
  pt.x = pRect->xRight - 1;
  pt.y = pRect->yTop - 1;
  GpiLine( hps, &pt );
  GpiSetColor( hps, lRBColor );
  pt.x = pRect->xRight - 1;
  pt.y = pRect->yBottom;
  GpiLine( hps, &pt );
  GpiLine( hps, (PPOINTL)pRect );

  // Restore color.
  GpiSetColor( hps, lSaveColor );
}

// Splits (get first item) list of items pcbList/ppcList separated by
// character chSep. Returns first item in pcbItem/ppcItem and moves
// pcbList/ppcList to the next item. Returns result FALSE if no more items
// left in list.
BOOL utilSplitList(PULONG pcbList, PCHAR *ppcList, CHAR chSep,
                   PULONG pcbItem, PCHAR *ppcItem)
{
  ULONG      cbList = *pcbList;
  PCHAR      pcList = *ppcList;
  BOOL       fInQuotas = FALSE;
  PCHAR      pcItem, pcEndItem;

  BUF_SKIP_SPACES( cbList, pcList );
  if ( cbList == 0 )
    return FALSE;

  pcItem = pcList;

  while( cbList > 0 )
  {
    if ( *pcList == '"' )
      fInQuotas = !fInQuotas;
    else if ( !fInQuotas && ( *pcList == chSep ) )
      break;

    cbList--;
    pcList++;
  }

  pcEndItem = pcList;
  while( ( pcEndItem > pcItem ) && isspace( *(pcEndItem - 1) ) )
    pcEndItem--;

  *pcbItem = pcEndItem - pcItem;
  *ppcItem = pcItem;

  if ( cbList != 0 )
  {
    cbList--;
    pcList++;
  }

  *pcbList = cbList;
  *ppcList = pcList;

  return TRUE;
}

LONG utilFindItem(ULONG cbList, PCHAR pcList, CHAR chSep, PSZ pszItem)
{
  PCHAR   pcScan;
  ULONG   cbScan;
  ULONG   cbItem = strlen( pszItem );
  LONG    lIdx = 0;

  while( utilSplitList( &cbList, &pcList, chSep, &cbScan, &pcScan ) )
  {
    if ( ( cbScan == cbItem ) && ( memicmp( pcScan, pszItem, cbScan ) == 0 ) )
      return lIdx;

    lIdx++;
  }

  return -1;
}

BOOL utilFindItemStr(PSZ pszList, PSZ pszItem)
{
  return utilFindItem( strlen( pszList ), pszList, ',', pszItem ) != -1;
}

LONG utilUnQuote(ULONG cbBuf, PCHAR pcBuf, ULONG cbStr, PCHAR pcStr)
{
  BUF_SKIP_SPACES( cbStr, pcStr )
  if ( cbBuf == 0 )
    return cbStr == 0 ? 0 : -1;

  if ( ( cbStr != 0 ) && ( *pcStr == '"' ) )
  {
    PCHAR    pcBufStart = pcBuf;

    cbStr--;
    pcStr++;
    while( ( *pcStr != '"' ) && ( cbStr != 0 ) )
    {
      if ( cbBuf == 0 )
        return -1;

      *pcBuf = *pcStr;
      cbStr--;
      pcStr++;
      cbBuf--;
      pcBuf++;
    }

    if ( cbBuf == 0 )
      return -1;
    *pcBuf = '\0';

    return pcBuf - pcBufStart;
  }

  if ( cbBuf <= cbStr )
    return -1;

  memcpy( pcBuf, pcStr, cbStr );
  pcBuf[cbStr] = '\0';
  return cbStr;
}
