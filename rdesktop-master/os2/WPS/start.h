#ifndef START_H
#define START_H

BOOL startRD(Rdesktop *somSelf, PSZ pszMClass);
LONG startQueryCmd(Rdesktop *somSelf, PSZ pszMClass, PSZ pszUser,
                   PSZ pszDomain, PSZ pszPassword, BOOL fSepZero,
                   ULONG cbBuf, PCHAR pcBuf);
BOOL startSwithTo(Rdesktop *somSelf);
VOID startKillAllSessions(Rdesktop *somSelf);

#endif // START_H
