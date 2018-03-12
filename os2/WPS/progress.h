#ifndef PROGRESS_H
#define PROGRESS_H

VOID prInit(Rdesktop *somSelf);
VOID prDone(Rdesktop *somSelf);
BOOL prStart(Rdesktop *somSelf, ULONG ulPID);
VOID prStop(Rdesktop *somSelf, ULONG ulPID);
BOOL prSwitchTo(Rdesktop *somSelf);

#endif // PROGRESS_H

