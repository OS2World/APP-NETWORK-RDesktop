#ifndef UTIL_H
#define UTIL_H

#define INCL_DOSMODULEMGR
#include <os2.h>
#include <ctype.h>
#include <string.h>

// String resources IDs.
#define UTIL_IDS_OK            16001
#define UTIL_IDS_CANCEL        16002
#define UTIL_IDS_ABORT         16003
#define UTIL_IDS_RETRY         16004
#define UTIL_IDS_IGNORE        16005
#define UTIL_IDS_YES           16006
#define UTIL_IDS_NO            16007

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[1]))

// Moves pointer __psz up to first not space character, zet ZERO after last not
// space character.
#define STR_TRIM(__psz) do { \
  PCHAR      __pcEnd = strchr( __psz, '\0' ); \
  while( (__psz < __pcEnd) && isspace( *__psz ) ) { __psz++; } \
  while( (__psz < __pcEnd) && isspace( *(__pcEnd-1) ) ) { __pcEnd--; } \
  *__pcEnd = '\0'; \
} while( FALSE )

#define STR_LEN(__psz) ( __psz == NULL ? 0 : strlen(__psz) )

#define BUF_SKIP_SPACES(cb, pc) \
  while( (cb > 0) && isspace( *pc ) ) { cb--; pc++; }

#define utilWPFreeMem(__somSelf, __p) \
          if ( __p != NULL ) _wpFreeMem( __somSelf, (PBYTE)__p )

HMODULE utilGetModuleHandle();
VOID utilStrTrim(PSZ pszStr);
ULONG utilLoadInsertStr(HMODULE hMod, BOOL fStrMsg, ULONG ulId,
                        ULONG cVal, PSZ *ppszVal, ULONG cbBuf, PCHAR pcBuf);
ULONG utilMessageBox(HWND hwnd, PSZ pszTitle, ULONG ulMsgResId,
                            ULONG ulStyle);
BOOL utilVerifyDomainName(ULONG cbName, PCHAR pcName);
VOID util3DFrame(HPS hps, PRECTL pRect, LONG lLTColor, LONG lRBColor);

BOOL utilSplitList(PULONG pcbList, PCHAR *ppcList, CHAR chSep,
                   PULONG pcbItem, PCHAR *ppcItem);
LONG utilFindItem(ULONG cbList, PCHAR pcList, CHAR chSep, PSZ pszItem);
BOOL utilFindItemStr(PSZ pszList, PSZ pszItem);
LONG utilUnQuote(ULONG cbBuf, PCHAR pcBuf, ULONG cbStr, PCHAR pcStr);

#endif // UTIL_H
