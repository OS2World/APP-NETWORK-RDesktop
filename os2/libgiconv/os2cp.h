// [Digi]

#ifndef OS2CP_H
#define OS2CP_H 1

typedef struct _NAME2CP {
  char    *pszName;
  int     ulCode;
  int     fBigEndian;
} NAME2CP, *PNAME2CP;

PNAME2CP os2cpFromName(char *cp);

#endif // OS2CP_H
