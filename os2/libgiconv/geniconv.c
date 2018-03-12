/*
  Universal iconv implementation for OS/2.

  Andrey Vasilkin, 2016.
*/

#define INCL_DOSMODULEMGR     /* Module Manager values */
#define INCL_DOSERRORS        /* Error values */
#define INCL_DOSNLS
#include <os2.h>
#include <iconv.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef DEBUG_FILE

#include <os2/debug.h>

#else

#ifdef DEBUG
# define debug(s,...) printf("%s(): "s"\n", __func__, ##__VA_ARGS__)
#else
# define debug(s,...)
#endif

#endif

// Exports from os2iconv.c.
extern iconv_t _System os2_iconv_open(const char* tocode, const char* fromcode);
extern size_t _System os2_iconv(iconv_t cd, const char* * inbuf,
                                size_t *inbytesleft, char* * outbuf,
                                size_t *outbytesleft);
extern int _System os2_iconv_close(iconv_t cd);

// Functions pointers types.
typedef iconv_t _System (*FNICONV_OPEN)(const char* tocode, const char* fromcode);
typedef size_t _System (*FNICONV)(iconv_t cd, const char* * inbuf,
                                  size_t *inbytesleft, char* * outbuf,
                                  size_t *outbytesleft);
typedef int _System (*FNICONV_CLOSE)(iconv_t cd);

// Used DLL module handle.
static HMODULE         hmIconv = NULLHANDLE;
// Functions pointers.
static FNICONV_OPEN    fn_iconv_open = NULL;
static FNICONV         fn_iconv = NULL;
static FNICONV_CLOSE   fn_iconv_close = NULL;
// Flag: one of iconv DLL is used and local codepage is 437.
static BOOL            fLocalCP437 = FALSE;



static BOOL _loadDLL(PSZ pszName, PSZ pszIconvOpen, PSZ pszIconv,
                     PSZ pszIconvClose)
{
  ULONG      ulRC;
  CHAR       acError[256];

  ulRC = DosLoadModule( acError, sizeof(acError), pszName, &hmIconv );
  if ( ulRC != NO_ERROR )
  {
    debug( "DLL not loaded: %s", acError );
    return FALSE;
  }

  do
  {
    ulRC = DosQueryProcAddr( hmIconv, 0, pszIconvOpen, (PFN *)&fn_iconv_open );
    if ( ulRC != NO_ERROR )
    {
      debug( "Error: cannot find entry %s in %s", pszIconvOpen, pszName );
      break;
    }

    ulRC = DosQueryProcAddr( hmIconv, 0, pszIconv, (PFN *)&fn_iconv );
    if ( ulRC != NO_ERROR )
    {
      debug( "Error: cannot find entry %s in %s", pszIconv, pszName );
      break;
    }

    ulRC = DosQueryProcAddr( hmIconv, 0, pszIconvClose, (PFN *)&fn_iconv_close );
    if ( ulRC != NO_ERROR )
    {
      debug( "Error: cannot find entry %s in %s", pszIconvClose, pszName );
      break;
    }

    debug( "DLL %s used", pszName );
    return TRUE;
  }
  while( FALSE );

  DosFreeModule( hmIconv );
  hmIconv = NULLHANDLE;
  return FALSE;
}

static void _init()
{
  PSZ        pszEnvSet;

  if ( fn_iconv_open != NULL )
    // Already was initialized.
    return;

  pszEnvSet = getenv( "GENICONV" );

  // Try to load kiconv.dll, iconv2.dll or iconv.dll.
  if (
       (
         ( pszEnvSet != NULL ) && ( stricmp( pszEnvSet, "UCONV" ) == 0 )
       )
     ||
       (
         !_loadDLL( "KICONV", "_libiconv_open", "_libiconv", "_libiconv_close" ) &&
         !_loadDLL( "ICONV2", "_libiconv_open", "_libiconv", "_libiconv_close" ) &&
         !_loadDLL( "ICONV", "_iconv_open", "_iconv", "_iconv_close" )
       )
     )
  {
    // No one DLL was loaded - use OS/2 conversion objects API.

    debug( "Uni*() API used" );
    fn_iconv_open  = os2_iconv_open;
    fn_iconv       = os2_iconv;
    fn_iconv_close = os2_iconv_close;
  }
  else
  {
    ULONG    aulCP[3];
    ULONG    cbCP, ulRC;

    ulRC = DosQueryCp( sizeof(aulCP), aulCP, &cbCP );

    if ( ulRC != NO_ERROR ) 
      debug( "DosQueryCp(), rc = %u", ulRC );
    else if ( aulCP[0] == 437 )
    {
      debug( "Local codepage 437 detected" );
      fLocalCP437 = TRUE;
    }
    else
      debug( "Local codepage: %u", aulCP[0] );
  }
}

// Makes libiconv-style codepage name from OS/2-style IBM-xxxx name.
// Convert IBM-437 to ISO-8859-1, IBM-xxxx to CPxxxx
// ppszName - in/out.
// pcBuf/cbBuf - buffer for the new name.
// Returns FALSE if buffer pcBuf/cbBuf too small.

static BOOL _correctName(PSZ *ppszName, PCHAR pcBuf, ULONG cbBuf)
{
  if ( *ppszName == NULL )
    return FALSE;

  if ( ( stricmp( *ppszName, "IBM-437" ) == 0 ) ||
       ( stricmp( *ppszName, "CP437"   ) == 0 ) ||
       ( stricmp( *ppszName, "CP-437"  ) == 0 ) )
  {
    if ( cbBuf < 11 )
      return FALSE;

    strcpy( pcBuf, "ISO-8859-1" );
  }
  else
  {
    if ( ( memicmp( *ppszName, "IBM-", 4 ) != 0 ) ||
         ( _snprintf( pcBuf, cbBuf, "CP%s", &((*ppszName)[4]) ) == -1 ) )
      return FALSE;

    pcBuf[cbBuf - 1] = '\0';
  }

  debug( "CP name %s used instead %s", pcBuf, *ppszName );
  *ppszName = pcBuf;

  return TRUE;
}

static iconv_t _iconv_open(const char* tocode, const char* fromcode)
{
  iconv_t    ic;

  ic = fn_iconv_open( tocode, fromcode );

  if ( ic == (iconv_t)-1 )
  {
    CHAR  acToCode[128];
    CHAR  acFromCode[128];
    BOOL  fToCode = _correctName( (PSZ *)&tocode, acToCode, sizeof(acToCode) );
    BOOL  fFromCode = _correctName( (PSZ *)&fromcode, acFromCode,
                                    sizeof(acFromCode) );

    if ( fToCode || fFromCode )
      ic = fn_iconv_open( tocode, fromcode );
  }

  return ic;
}


//           Public routines.
//           ----------------

// Non-standard function for iconv to unload the used dynamic library.
void iconv_clean()
{
  if ( hmIconv != NULLHANDLE )
  {
    DosFreeModule( hmIconv );
    hmIconv = NULLHANDLE;

    fn_iconv_open  = NULL;
    fn_iconv       = NULL;
    fn_iconv_close = NULL;
  }
}

iconv_t iconv_open(const char* tocode, const char* fromcode)
{
  iconv_t    ic;
  BOOL       fCP437;

  _init();

  ic = _iconv_open( tocode, fromcode );

  // For iconv DLL (not for Uni*() API) and local codepage 437 we use name
  // ISO-8859-1 when system default codepage specified (name of cp is empty
  // string).
  // We need to use name ISO-8859-1 as system-default cp because iconv DLL
  // does not understand codepage 437.

  if ( ( ic == (iconv_t)-1 ) && fLocalCP437 )
  {
    BOOL     fCP437 = FALSE;

    if ( ( tocode != NULL ) && ( *tocode == '\0' ) )
    {
      tocode = "ISO-8859-1";
      fCP437 = TRUE;
    }

    if ( ( fromcode != NULL ) && ( *fromcode == '\0' ) )
    {
      fromcode = "ISO-8859-1";
      fCP437 = TRUE;
    }

    if ( fCP437 )
    {
      debug( "Local cp is 437, try to open iconv for ISO-8859-1 as system cp" );
      ic = _iconv_open( tocode, fromcode );
    }
  }

  return ic;
}

size_t iconv(iconv_t cd, const char* * inbuf, size_t *inbytesleft,
                char* * outbuf, size_t *outbytesleft)
{
  return fn_iconv( cd, (const char **)inbuf, inbytesleft, outbuf, outbytesleft );
}

int iconv_close(iconv_t cd)
{
  return fn_iconv_close( cd );
}
