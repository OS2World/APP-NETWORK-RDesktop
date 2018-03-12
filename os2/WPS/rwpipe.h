#ifndef POPENRW_H
#define POPENRW_H

#include <os2.h>

typedef struct _RWPIPE {
  LONG		pid;
  ULONG		hRead;
  ULONG		hWrite;
  BOOL    fEnd;
} RWPIPE, *PRWPIPE;

BOOL rwpOpen(PRWPIPE pRWPipe, PSZ pszCmd);
LONG rwpClose(PRWPIPE pRWPipe);
LONG rwpRead(PRWPIPE pRWPipe, PVOID pBuf, ULONG cbBuf);
LONG rwpWrite(PRWPIPE pRWPipe, PVOID pBuf, ULONG cbBuf);

#endif // POPENRW_H
