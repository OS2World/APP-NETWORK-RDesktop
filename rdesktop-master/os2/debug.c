// [Digi] Debug stuff.

#include <time.h>
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define INCL_DOSSEMAPHORES
#include <os2.h>
#include "debug.h"

static HMTX		hMtx;

typedef struct _DBGCOUNTER {
  struct _DBGCOUNTER		*pNext;
  char				*pcName;
  int				iValue;
  unsigned int			cInc;
  unsigned int			cDec;
} DBGCOUNTER, *PDBGCOUNTER;

typedef struct _BUFPSZ {
  struct _BUFPSZ		*pNext;
  struct _BUFPSZ		**ppSelf;
  char				acString[1];
} BUFPSZ, *PBUFPSZ;

typedef struct _BUFPSZSEQ {
  PBUFPSZ		pList;
  PBUFPSZ		*ppLast;
  int			cBufPSZ;
} BUFPSZSEQ, *PBUFPSZSEQ;

static FILE		*fdDebug = NULL;
static PDBGCOUNTER	pCounters = NULL;
static unsigned int	cInit = 0;
static BUFPSZSEQ	lsBufPSZ;

#define DEBUG_BEGIN\
  if ( !fdDebug ) return;\
  DosRequestMutexSem( hMtx, SEM_INDEFINITE_WAIT );
#define DEBUG_BEGIN_0\
  if ( !fdDebug ) return 0;\
  DosRequestMutexSem( hMtx, SEM_INDEFINITE_WAIT );

#define DEBUG_END DosReleaseMutexSem( hMtx );


void debug_init(char *pcDebugFile)
{
  if ( cInit == 0 )
  {
    fdDebug = fopen( pcDebugFile, "a" );
    if ( fdDebug == NULL )
      printf( "Cannot open debug file %s\n", pcDebugFile );

    lsBufPSZ.ppLast = &lsBufPSZ.pList;
    DosCreateMutexSem( NULL, &hMtx, 0, FALSE );
  }

  cInit++;
}

void debug_done()
{
  if ( cInit == 0 )
    return;
  if ( cInit > 0 )
    cInit--;

  if ( cInit == 0 )
  {
    PBUFPSZ		pNextBufPSZ;
    PDBGCOUNTER		pNext, pScan = pCounters;

    while( lsBufPSZ.pList != NULL )
    {
      pNextBufPSZ = lsBufPSZ.pList->pNext;
      free( lsBufPSZ.pList );
      lsBufPSZ.pList = pNextBufPSZ;
    }

    debug_state();
    while( pScan != NULL )
    {
      pNext = pScan->pNext;
      free( pScan->pcName );
      free( pScan );
      pScan = pNext;
    }

    fclose( fdDebug );
    fdDebug = NULL;
    DosCloseMutexSem( hMtx );
  }
}

void debug_write(char *pcFormat, ...)
{
  va_list	arglist;
  time_t	t;
  char		acBuf[32];

  t = time( NULL );
  strftime( acBuf, sizeof(acBuf)-1, "%T", localtime( &t ) );
  DEBUG_BEGIN
  fprintf( fdDebug, "[%s] ", acBuf );
  va_start( arglist, pcFormat ); 
  vfprintf( fdDebug, pcFormat, arglist );
  va_end( arglist );
  DEBUG_END
  fflush(NULL);
}

void debug_text(char *pcFormat, ...)
{
  va_list	arglist;

  DEBUG_BEGIN
  va_start( arglist, pcFormat ); 
  vfprintf( fdDebug, pcFormat, arglist );
  va_end( arglist );
  DEBUG_END
  fflush(NULL);
}

char *debug_buf2psz(char *pcBuf, unsigned int cbBuf)
{
  PBUFPSZ	pBufPSZ = malloc( sizeof(BUFPSZ) + cbBuf );

  DEBUG_BEGIN_0;
  if ( lsBufPSZ.cBufPSZ > 32 )
  {
    // Remove first record
    PBUFPSZ	pFirst = lsBufPSZ.pList;

    lsBufPSZ.pList = pFirst->pNext;
    if ( pFirst->pNext != NULL )
      pFirst->pNext->ppSelf = &lsBufPSZ.pList;
    else
      lsBufPSZ.ppLast = &lsBufPSZ.pList;
    lsBufPSZ.cBufPSZ--;

    free( pFirst );
  }

  if ( pBufPSZ == NULL )
  {
    DEBUG_END;
    return "<debug_buf2psz() : not enough memory>";
  }

  memcpy( &pBufPSZ->acString, pcBuf, cbBuf );
  pBufPSZ->acString[cbBuf] = '\0';

  // Add new record to the end of list
  pBufPSZ->pNext = NULL;
  pBufPSZ->ppSelf = lsBufPSZ.ppLast;
  *lsBufPSZ.ppLast = pBufPSZ;
  lsBufPSZ.ppLast = &pBufPSZ->pNext;
  lsBufPSZ.cBufPSZ++;

  DEBUG_END;
  return pBufPSZ->acString;
}

void debug_textbuf(char *pcBuf, unsigned int cbBuf, int fCRLF)
{
  DEBUG_BEGIN
  fwrite( pcBuf, cbBuf, 1, fdDebug );
  if ( fCRLF )
    fputs( "\n", fdDebug );
  DEBUG_END
  fflush(NULL);
}

int debug_counter(char *pcName, int iDelta)
{
  PDBGCOUNTER		pScan;
  int			iRes = 0;

  DEBUG_BEGIN_0

  for( pScan = pCounters;
       ( pScan != NULL ) && ( strcmp( pcName, pScan->pcName ) != 0 );
       pScan = pScan->pNext )
  { }

  if ( pScan == NULL )
  {
    pScan = calloc( 1, sizeof(DBGCOUNTER) );
    if ( pScan == NULL )
      fprintf( fdDebug, "Not enough memory for new counter: %s\n", pcName );
    else
    {
      pScan->pcName = strdup( pcName );
      if ( pScan->pcName == NULL )
      {
        free( pScan );
        pScan = NULL;
        fprintf( fdDebug, "Not enough memory for new counter name: %s\n", pcName );
      }
      else
      {
        pScan->pNext = pCounters;
        pCounters = pScan;
      }
    }
  }

  if ( pScan != NULL )
  {
    if ( iDelta > 0 )
      pScan->cInc++;
    else if ( iDelta < 0 )
    {
      pScan->cDec++;
      iRes = pScan->iValue == 0;
    }
    pScan->iValue += iDelta;
  }

  DEBUG_END
  return iRes;
}


void *debug_malloc(size_t size, char *pcFile, int iLine)
{
  void		*pBlock = malloc( size + sizeof(size_t) );

  if ( pBlock == NULL )
  {
    debug_write( "%s#%u : Not enough memory\n", pcFile, iLine );
    return NULL;
  }

  *((size_t *)pBlock) = size;
  debug_counter( "mem_alloc", size );

  return ((char *)pBlock) + sizeof(size_t);
}

void *debug_calloc(size_t n, size_t size, char *pcFile, int iLine)
{
  void		*pBlock;

  size *= n;
  pBlock = debug_malloc( size, pcFile, iLine );
  if ( pBlock != NULL )
    bzero( pBlock, size );
  return pBlock;
}

void debug_free(void *ptr)
{
  if ( ptr == NULL )
  {
    debug_write( "debug_free() : Pointer is NULL\n" );
    return;
  }
  ptr = ((char *)ptr) - sizeof(size_t);
  debug_counter( "mem_alloc", -(*((size_t *)ptr)) );
  free( ptr );
}

void *debug_realloc(void *old_blk, size_t size, char *pcFile, int iLine)
{
  size_t	old_size;
  void		*pBlock = debug_malloc( size, pcFile, iLine );

  old_size = old_blk == NULL ? 0 : *(((size_t *)old_blk) - 1);

  if ( pBlock != NULL && old_blk != NULL )
    memcpy( pBlock, old_blk, min( old_size, size ) );

  if ( ( size == 0 || pBlock != NULL ) && ( old_blk != NULL ) )
    debug_free( old_blk );

  return pBlock;
}

char *debug_strdup(const char *src, char *pcFile, int iLine)
{
  char		*dst;

  if ( src == NULL )
    return NULL;

  dst = debug_malloc( strlen( src ) + 1, pcFile, iLine );
  if ( dst != NULL )
    strcpy( dst, src );

  return dst;
}


void DBGLIBENTRY debug_state()
{
  PDBGCOUNTER		pScan;

  debug_write( "Debug counters:\n"
               "Counter name    Incr. times     Decr. times     Value\n" );

  for( pScan = pCounters; pScan != NULL; pScan = pScan->pNext )
    debug_text( "%-17s   %5u           %5u(%d)      %5d\n",
             pScan->pcName, pScan->cInc, pScan->cDec,
             pScan->cInc - pScan->cDec, pScan->iValue );
}

int debug_memused()
{
  PDBGCOUNTER		pScan;

  for( pScan = pCounters;
       ( pScan != NULL ) && ( strcmp( "mem_alloc", pScan->pcName ) != 0 );
       pScan = pScan->pNext )
  { }

  return pScan == NULL ? -1 : pScan->iValue;
}
