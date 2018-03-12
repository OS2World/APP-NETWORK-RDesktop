#ifndef _OS2RD_H_
#define _OS2RD_H_

#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_DOSSEMAPHORES
#define INCL_DOSFILEMGR
#define INCL_DOSMODULEMGR
#define INCL_DOSMISC
#define INCL_WIN
#define INCL_GPI
#define INCL_WINATOM
#include <os2.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define WIN_CLIENT_CLASS         "rdesktop"

typedef struct _SEAMLESSWIN {
  HWND                 hwndFrame;
  HWND                 hwnd;
  ULONG                ulId;
  ULONG                ulParentId;
  ULONG                ulGroup;
  LONG                 lState;  // normal/minimized/maximized.

  ULONG                ulIconSize;
  ULONG                ulIconOffset;
  PCHAR                pcIconBuffer;

  struct _SEAMLESSWIN  *pNext;
} SEAMLESSWIN, *PSEAMLESSWIN;


// xclip.c

#define WM_XCLIP_RDACTIVATE      (WM_USER + 1)
extern HWND  hwndClipWin;

VOID xclipInit();
VOID xclipDone();
VOID xclipProcess();

// os2seamless.c
VOID swInvalidate(PRECTL prectl);
VOID swUpdate();
VOID swFocus(HWND hwnd, BOOL fSet);
VOID swMoved(HWND hwnd);
VOID swRestored(HWND hwnd);
BOOL swSeamlessToggle();
BOOL swNoWindowsLeft();

// nonblkwin.c
VOID nonblockingWin(HWND hwndFrame, BOOL fResizable);

// util.c
ULONG utilStrIConv(PSZ pszSrcCP, PSZ pszResCP, PCHAR pcSrc, ULONG cbSrc,
                   PCHAR pcRes, ULONG cbRes);
BOOL utilPipeSock(int *piSock1, int *piSock2);
HBITMAP utilWinToSysBitmap(HPS hps, PCHAR pcData, ULONG cbData);
ULONG utilSysBitmapToWinDIBV5(HAB hab, HBITMAP hbm, PCHAR pcBuf, ULONG cbBuf);

#endif // _OS2RD_H_
