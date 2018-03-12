#ifndef DISKDLG_H
#define DISKDLG_H

#define DISKDLG_MAX_NAME         8
#define DISKDLG_MAX_PATH         261

#define WM_DISKDLG_CHECK         (WM_USER + 1655)

typedef struct _DISKDLGINFO {
  BOOL       fEditableName;
  CHAR       szName[DISKDLG_MAX_NAME];
  CHAR       szPath[DISKDLG_MAX_PATH];
} DISKDLGINFO, *PDISKDLGINFO;

BOOL diskDlg(HWND hwndOwner, PDISKDLGINFO pDlgInfo);

#endif
