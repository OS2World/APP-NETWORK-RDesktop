#define INCL_DOSPROCESS
#define INCL_ERRORS
#define INCL_DOSQUEUES
#define INCL_DOSMISC
#define INCL_DOSEXCEPTIONS
#include <os2.h>
#include <io.h>
#include <string.h>
#include <process.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <stddef.h>
#include <fcntl.h>
#include <debug.h>
#include <rwpipe.h>

#define REDIRECT_STDERR 1

#define STDIN		0
#define STDOUT		1
#define STDERR		2

// RWP_WAIT_PROCESS - how long time rwpClose() must wait child process ends.
// If process still life after RWP_WAIT_PROCESS msec. it will be killed by
// calling DosKillProcess()
#define RWP_WAIT_PROCESS_INTR	300
#define RWP_WAIT_PROCESS_BREAK  600
#define RWP_WAIT_PROCESS_KILL	1800

BOOL rwpOpen(PRWPIPE pRWPipe, PSZ pszCmd)
{
  HFILE			hProgOutR, hProgOutW;
  HFILE			hProgInR, hProgInW;
  HFILE			hSaveStdIn = -1, hSaveStdOut = -1;
#ifdef REDIRECT_STDERR
  HFILE			hSaveStdErr = -1;
#endif
  HFILE			hFile;
  ULONG			ulRC;
  RESULTCODES	sResCodes;
  CHAR      acError[CCHMAXPATH];


  ulRC = DosCreatePipe( &hProgOutR, &hProgOutW, 1024 );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosCreatePipe(), rc = %u", ulRC );
    return FALSE;
  }

  ulRC = DosCreatePipe( &hProgInR, &hProgInW, 1024 );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosCreatePipe(), rc = %u", ulRC );
    DosClose( hProgOutR );
    DosClose( hProgOutW );
    return FALSE;
  }

  ulRC = DosDupHandle( STDIN, &hSaveStdIn );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosDupHandle( STDIN, &hSaveStdIn ), rc = %u", ulRC );
    DosClose( hProgOutR );
    DosClose( hProgOutW );
    DosClose( hProgInR );
    DosClose( hProgInW );
    return FALSE;
  }
  DosSetFHState( hSaveStdIn, OPEN_FLAGS_NOINHERIT );
  ulRC = DosDupHandle( STDOUT, &hSaveStdOut );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosDupHandle( STDOUT, &hSaveStdOut ), rc = %u", ulRC );
    DosClose( hProgOutR );
    DosClose( hProgOutW );
    DosClose( hProgInR );
    DosClose( hProgInW );
    DosClose( hSaveStdIn );
    return FALSE;
  }
  DosSetFHState( hSaveStdOut, OPEN_FLAGS_NOINHERIT );
#ifdef REDIRECT_STDERR
  ulRC = DosDupHandle( STDERR, &hSaveStdErr );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosDupHandle( STDERR, &hSaveStdErr ), rc = %u", ulRC );
    DosClose( hProgOutR );
    DosClose( hProgOutW );
    DosClose( hProgInR );
    DosClose( hProgInW );
    DosClose( hSaveStdIn );
    DosClose( hSaveStdOut );
    return FALSE;
  }
  DosSetFHState( hSaveStdErr, OPEN_FLAGS_NOINHERIT );
#endif

  hFile = STDIN;
  ulRC = DosDupHandle( hProgInR, &hFile );
  if ( ulRC != NO_ERROR )
    { debug( "DosDupHandle( hProgInR, &hFile ), rc = %u", ulRC ); }

  hFile = STDOUT;
  ulRC = DosDupHandle( hProgOutW, &hFile );
  if ( ulRC != NO_ERROR )
    { debug( "DosDupHandle( hProgOutW, &hFile ), rc = %u", ulRC ); }
#ifdef REDIRECT_STDERR
  hFile = STDERR;
  ulRC = DosDupHandle( hProgOutW, &hFile );
  if ( ulRC != NO_ERROR )
    { debug( "DosDupHandle( hProgOutW, &hFile ), rc = %u", ulRC ); }
#endif
 
  DosClose( hProgInR );
  DosClose( hProgOutW );
  ulRC = DosSetFHState( hProgInW, //OPEN_FLAGS_WRITE_THROUGH |
                        OPEN_FLAGS_NO_CACHE | OPEN_FLAGS_FAIL_ON_ERROR );
  if ( ulRC != NO_ERROR )
    { debug( "DosSetFHState( hProgInW, ), rc = %u", ulRC ); }
  ulRC = DosSetFHState( hProgOutR, //OPEN_FLAGS_WRITE_THROUGH |
                        OPEN_FLAGS_NO_CACHE | OPEN_FLAGS_FAIL_ON_ERROR );
  if ( ulRC != NO_ERROR )
    { debug( "DosSetFHState( hProgOutR, ), rc = %u", ulRC ); }
/*
  DosSetFHState( hProgInW, OPEN_FLAGS_NOINHERIT );
  DosSetFHState( hProgOutR, OPEN_FLAGS_NOINHERIT );
*/

  ulRC = DosExecPgm( acError, sizeof(acError), EXEC_ASYNC/*EXEC_ASYNCRESULT*/,
                     pszCmd, NULL, &sResCodes, pszCmd );

  if ( ulRC != NO_ERROR )
  {
    debug( "DosExecPgm(,,,,,,%s), rc = %u", pszCmd, ulRC );
    pRWPipe->pid = -1;
  }
  else
    pRWPipe->pid = sResCodes.codeTerminate;

  hFile = STDIN;
  ulRC = DosDupHandle( hSaveStdIn, &hFile );
  if ( ulRC != NO_ERROR )
    { debug( "DosDupHandle( hSaveStdIn, STDIN ), rc = %u", ulRC ); }
  ulRC = DosClose( hSaveStdIn );
  if ( ulRC != NO_ERROR )
    { debug( "DosClose( hSaveStdIn ), rc = %u", ulRC ); }
/*  ulRC = DosClose( hFile );
  if ( ulRC != NO_ERROR )
    { debug( "#1 DosClose( hFile ), rc = %u", ulRC ); }*/

  hFile = STDOUT;
  ulRC = DosDupHandle( hSaveStdOut, &hFile );
  if ( ulRC != NO_ERROR )
    { debug( "DosDupHandle( hSaveStdOut, STDOUT ), rc = %u", ulRC ); }
  ulRC = DosClose( hSaveStdOut );
  if ( ulRC != NO_ERROR )
    { debug( "DosClose( hSaveStdIn ), rc = %u", ulRC ); }
/*  ulRC = DosClose( hFile );
  if ( ulRC != NO_ERROR )
    { debug( "#1 DosClose( hFile ), rc = %u", ulRC ); }*/

#ifdef REDIRECT_STDERR
  hFile = STDERR;
  ulRC = DosDupHandle( hSaveStdErr, &hFile );
  if ( ulRC != NO_ERROR )
    { debug( "DosDupHandle( hSaveStdErr, STDERR ), rc = %u", ulRC ); }
  DosClose( hSaveStdErr );
#endif

  if ( pRWPipe->pid == -1 )
  {
    debug( "Cannot start: %s", pszCmd );
    DosClose( hProgInW );
    DosClose( hProgOutR );
    return FALSE;
  }

  pRWPipe->hRead = hProgOutR;
  pRWPipe->hWrite = hProgInW;

  debugInc( "RWPIPE" );
  return TRUE;
}

LONG rwpClose(PRWPIPE pRWPipe)
{
  RESULTCODES	sResCodes;
  PID		pid = 0;
  ULONG		ulRC;
  ULONG		ulStart, ulElapsed;
  BOOL		fIntr = FALSE;
  BOOL		fBreak = FALSE;

  if ( !pRWPipe->fEnd )
  {
    sResCodes.codeTerminate = pRWPipe->pid;
    DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &ulStart, sizeof(ULONG) );
    while( TRUE )
    {
      ulRC = DosWaitChild( DCWA_PROCESS/*DCWA_PROCESSTREE*/, DCWW_NOWAIT, &sResCodes,
                           &pid, pRWPipe->pid );
      if ( ulRC == NO_ERROR )
      {
        DosSleep( 80 );
        break;
      }

      DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &ulElapsed, sizeof(ULONG) );
      ulElapsed -= ulStart;

      if ( !fIntr && ulElapsed > RWP_WAIT_PROCESS_INTR )
      {
        ulRC = DosSendSignalException( pRWPipe->pid, XCPT_SIGNAL_INTR );
        fIntr = TRUE;//ulRC == NO_ERROR;
      }
      else if ( !fBreak && ulElapsed > RWP_WAIT_PROCESS_BREAK )
      {
        ulRC = DosSendSignalException( pRWPipe->pid, XCPT_SIGNAL_BREAK );
        fBreak = TRUE;//ulRC == NO_ERROR;
        fIntr = TRUE;
      }
      else if ( ulElapsed > RWP_WAIT_PROCESS_KILL )
      {
        debug( "DosWaitChild(), rc = %u; call DosKillProcess(,%u)...", ulRC,
               pRWPipe->pid );
        sResCodes.codeTerminate = TC_HARDERROR; // some differen with TC_EXIT

        ulRC = DosKillProcess( DCWA_PROCESSTREE, pRWPipe->pid );
        if ( ulRC != NO_ERROR )
          { debug( "DosKillProcess(), rc = %u", ulRC ); }
        break;
      }

      DosSleep( 1 );
    }
  }

  ulRC = DosClose( pRWPipe->hRead );
  if ( ulRC != NO_ERROR )
    { debug( "DosClose( pRWPipe->hRead ), rc = %u", ulRC ); }

  ulRC = DosClose( pRWPipe->hWrite );
  if ( ulRC != NO_ERROR )
    { debug( "DosClose( pRWPipe->hWrite ), rc = %u", ulRC ); }

  debugDec( "RWPIPE" );
  return sResCodes.codeTerminate == TC_EXIT ? sResCodes.codeResult : -1;
}

LONG rwpRead(PRWPIPE pRWPipe, PVOID pBuf, ULONG cbBuf)
{
  ULONG		cbActual;
  ULONG		ulRC;

  ulRC = DosRead( pRWPipe->hRead, pBuf, cbBuf, &cbActual );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosRead(%u,,,), rc = %u, PID: %u",
           pRWPipe->hRead, ulRC, pRWPipe->pid );
    return -1;
  }

  if ( cbActual == 0 )
    pRWPipe->fEnd = TRUE;

  return cbActual;
}

LONG rwpWrite(PRWPIPE pRWPipe, PVOID pBuf, ULONG cbBuf)
{
  ULONG		cbActual;
  ULONG		ulRC;

  ulRC = DosWrite( pRWPipe->hWrite, pBuf, cbBuf, &cbActual );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosWrite(%u,,,), rc = %u, PID: %u",
           pRWPipe->hWrite, ulRC, pRWPipe->pid );
    return -1;
  }

  return cbActual;
}
