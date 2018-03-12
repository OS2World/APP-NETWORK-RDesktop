#ifndef LINKSEQ_H
#define LINKSEQ_H

typedef struct _SEQOBJ {
  struct _SEQOBJ	*pNext;
  struct _SEQOBJ	**ppSelf;
} SEQOBJ, *PSEQOBJ;

typedef struct _LINKSEQ {
  PSEQOBJ		pList;
  PSEQOBJ		*ppLast;
  ULONG			ulCount;
} LINKSEQ, *PLINKSEQ;

#define lnkseqInit(ls) do { \
  (ls)->pList = NULL; \
  (ls)->ppLast = &(ls)->pList; \
  (ls)->ulCount = 0; \
} while(0)

#define lnkseqDone(ls)

#define lnkseqFree(ls, type, freeFn) do {\
  PSEQOBJ	_ls_pObj; \
  while( TRUE ) { \
    _ls_pObj = lnkseqGetFirst( ls ); \
    if ( _ls_pObj == NULL ) break; \
    lnkseqRemove( ls, _ls_pObj ); \
    freeFn( (type)_ls_pObj ); \
  } \
} while(0)

#define lnkseqIsEmpty(ls) ( (ls)->pList == NULL )
#define lnkseqGetCount(ls) ( (ls)->ulCount )
#define lnkseqGetFirst(ls) ( (ls)->pList )
#define lnkseqGetNext(so) ( ((PSEQOBJ)(so))->pNext )
#define lnkseqRemove(ls,so) do { \
  if ( ((PSEQOBJ)(so))->ppSelf != NULL ) { \
    *((PSEQOBJ)(so))->ppSelf = ((PSEQOBJ)(so))->pNext; \
    if ( ((PSEQOBJ)(so))->pNext != NULL ) \
      ((PSEQOBJ)(so))->pNext->ppSelf = ((PSEQOBJ)(so))->ppSelf; \
    else (ls)->ppLast = ((PSEQOBJ)(so))->ppSelf; \
    ((PSEQOBJ)(so))->ppSelf = NULL; \
    (ls)->ulCount--; \
} } while(0)
#define lnkseqReplace(ls,so,sonew) do { \
  ((PSEQOBJ)(sonew))->ppSelf = ((PSEQOBJ)(so))->ppSelf; \
  ((PSEQOBJ)(sonew))->pNext = ((PSEQOBJ)(so))->pNext; \
  *((PSEQOBJ)(sonew))->ppSelf = (PSEQOBJ)(sonew); \
  if ( ((PSEQOBJ)(sonew))->pNext != NULL ) \
    ((PSEQOBJ)(sonew))->pNext->ppSelf = &((PSEQOBJ)(sonew))->pNext; \
  else \
    (ls)->ppLast = &((PSEQOBJ)(sonew))->pNext; \
  ((PSEQOBJ)(so))->ppSelf = NULL; \
} while(0)

#define lnkseqFirstToEnd(ls) do { \
  if ( (ls)->ppLast != &(ls)->pList->pNext ) { \
    PSEQOBJ	_ls_pObj = (ls)->pList; \
    _ls_pObj->pNext->ppSelf = &(ls)->pList; \
    (ls)->pList = _ls_pObj->pNext; \
    *(ls)->ppLast = _ls_pObj; \
    (ls)->ppLast = &_ls_pObj->pNext; \
    _ls_pObj->pNext = NULL; \
  } \
} while(0)

#define lnkseqAdd(ls,so) do { \
  ((PSEQOBJ)(so))->pNext = NULL; \
  ((PSEQOBJ)(so))->ppSelf = (ls)->ppLast; \
  *(ls)->ppLast = ((PSEQOBJ)(so)); \
  (ls)->ppLast = &((PSEQOBJ)(so))->pNext; \
  (ls)->ulCount++; \
} while(0)

#define lnkseqAddAfter(ls,sobase,so) do { \
  if ( ((PSEQOBJ)(sobase))->pNext != NULL ) { \
    ((PSEQOBJ)(so))->pNext = ((PSEQOBJ)(sobase))->pNext; \
    ((PSEQOBJ)(so))->ppSelf = &((PSEQOBJ)(sobase))->pNext; \
    ((PSEQOBJ)(sobase))->pNext->ppSelf = &((PSEQOBJ)(so))->pNext; \
    ((PSEQOBJ)(sobase))->pNext = ((PSEQOBJ)(so)); \
    (ls)->ulCount++; \
  } \
  else lnkseqAdd( ls, so ); \
} while(0)

#define lnkseqAddFirst(ls,so) do { \
  if ( (ls)->pList != NULL ) (ls)->pList->ppSelf = &((PSEQOBJ)(so))->pNext; \
  else (ls)->ppLast = &((PSEQOBJ)(so))->pNext; \
  ((PSEQOBJ)(so))->pNext = (ls)->pList; \
  ((PSEQOBJ)(so))->ppSelf = &(ls)->pList; \
  (ls)->pList = ((PSEQOBJ)(so)); \
  (ls)->ulCount++; \
} while(0)

#define lnkseqAddBefore(ls,sobase,so) do { \
  if ( (ls)->pList != ((PSEQOBJ)(sobase)) ) { \
    ((PSEQOBJ)(so))->ppSelf = ((PSEQOBJ)(sobase))->ppSelf; \
    ((PSEQOBJ)(so))->pNext = ((PSEQOBJ)(sobase)); \
    *((PSEQOBJ)(sobase))->ppSelf = ((PSEQOBJ)(so)); \
    ((PSEQOBJ)(sobase))->ppSelf = &((PSEQOBJ)(so))->pNext; \
    (ls)->ulCount++; \
  } \
  else lnkseqAddFirst( ls, so ); \
} while(0)

#define lnkseqIsLinked(so) ( ((PSEQOBJ)(so))->ppSelf != NULL )

// lnkseqForEach(), fn: BOOL (PSEQOBJ,data)
#define lnkseqForEach(ls,fn,data) do { \
  PSEQOBJ	_ls_pObj, _ls_pNext; \
  _ls_pObj = (ls)->pList; \
  while( _ls_pObj != NULL ) { \
    _ls_pNext = _ls_pObj->pNext; \
    if ( !fn( _ls_pObj, data ) ) break; \
    _ls_pObj = _ls_pNext; \
  } \
} while(0)

#define lnkseqMove(lsto, lsfrom) do { \
  if ( (lsfrom)->pList != NULL ) { \
    (lsfrom)->pList->ppSelf = (lsto)->ppLast; \
    *(lsto)->ppLast = (lsfrom)->pList; \
    (lsto)->ppLast = (lsto)->pList == NULL ? &(lsto)->pList : (lsfrom)->ppLast; \
    (lsfrom)->pList = NULL; \
    (lsfrom)->ppLast = &(lsfrom)->pList; \
    (lsto)->ulCount += (lsfrom)->ulCount; (lsfrom)->ulCount = 0; \
  } \
} while(0)

#endif // LINKSEQ_H
