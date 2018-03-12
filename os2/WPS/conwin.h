#ifndef CONWIN_H
#define CONWIN_H

VOID cwInit(Rdesktop *somSelf);
VOID cwDone(Rdesktop *somSelf);
BOOL cwShow(Rdesktop *somSelf);
VOID cwAddMessage(Rdesktop *somSelf, ULONG pid, ULONG ulLevel, PSZ pszMsg);
VOID cwAddMessage2(Rdesktop *somSelf, ULONG pid, PSZ pszMsg);
VOID cwAddMessageFmt(Rdesktop *somSelf, ULONG pid, ULONG ulLevel,
                     ULONG ulMsgId, ULONG cArg, PSZ *apszArg);

#endif // CONWIN_H
