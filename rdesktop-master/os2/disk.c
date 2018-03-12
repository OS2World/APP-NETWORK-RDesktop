/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Disk Redirection
   Copyright (C) Jeroen Meijer <jeroen@oldambt7.com> 2003-2008
   Copyright 2003-2011 Peter Astrand <astrand@cendio.se> for Cendio AB

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "..\disk.h"
#include "rdesktop.h"
#include "os2rd.h"
#include <ctype.h>
#include "linkseq.h"
#include "debug.h"

typedef struct _OSFILE {
  SEQOBJ     lsObj;
  PSZ        pszName;
  HFILE      hFile;              // NULLHANDLE for the directory.
  ULONG      ulAttribute;
  ULONG      ulOpenCount;
} OSFILE, *POSFILE;

typedef struct _RDFILE {
  uint32     ui32DeviceId;
  POSFILE    pOSFile;
  BOOL       fDeleteOnClose;
  uint32     ui32AccessMask;
  uint32     ui32ShareMode;
  HDIR       hDir;
  NOTIFY     stNotify;
} RDFILE, *PRDFILE;

/* rdpdr.c */
extern RDPDR_DEVICE g_rdpdr_device[];

RD_BOOL                g_notify_stamp = False;

static LINKSEQ         lsOSFiles;
static PRDFILE         *papRDFiles = NULL;
static ULONG           cRDFiles = 0;

// Convert handle to/from index of paRDFiles[]. We should not have handle equal
// to 0. We should avoid the intersection of handles sets for disk and serial
// devices (serial device handle is a socket number).
// XOR with 0x7FFFFFFF to keep significant bit cleared to avoid interpreting
// the handle as a negative value.
#define _handleToIndex(hdl) ((ULONG)hdl ^ 0x7FFFFFFF)
#define _handleFromIndex(idx) (RD_NTHANDLE)(idx ^ 0x7FFFFFFF)

#define _osfIsDir(osf) ( osf->hFile == NULLHANDLE )

static ULONG _queryPathInfo(PSZ pszPathName, PFILESTATUS3L pInfo)
{
  CHAR       acBuf[4];

  if ( ( pszPathName[1] == ':' ) && ( pszPathName[2] == '\0' ) ) // "?:"
  {
    *((PULONG)acBuf) = 0x005C3A00 + pszPathName[0]; // Add slash.
//    acBuf[0] = pszPathName[0];
    pszPathName = acBuf;
  }

  memset( pInfo, 0, sizeof(FILESTATUS3L) );

  return DosQueryPathInfo( pszPathName, FIL_STANDARDL, pInfo,
                           sizeof(FILESTATUS3L) );
}

static VOID _osfFree(POSFILE pOSFile)
{
  if ( pOSFile->hFile != NULLHANDLE )
  {
//    debug( "Close file: %s", pOSFile->pszName );

    if ( pOSFile->hFile != NULLHANDLE )
    {
      ULONG  ulRC = DosClose( pOSFile->hFile );

      if ( ulRC != NO_ERROR )
        debug( "DosClose(), rc = %u", ulRC );
    }
  }
/*  else
    debug( "Close directory: %s", pOSFile->pszName );*/

  if ( pOSFile->pszName != NULL )
    debugFree( pOSFile->pszName );
  debugFree( pOSFile );
}

static ULONG _makePath(PCHAR pcBuf, ULONG cbBuf, PSZ pszBase, PSZ pszName)
{
  ULONG      cbBase = strlen( pszBase );
  ULONG      cbName = pszName == NULL ? 0 : strlen( pszName );

  while( ( cbBase > 0 ) && ( pszBase[cbBase-1] == '\\' ) )
    cbBase--;

  while( ( cbName > 0 ) && ( *pszName == '\\' ) )
  {
    pszName++;
    cbName--;
  }

  if ( ( cbBase + cbName + 2 ) >= cbBuf ) // +2 - slash and ZERO.
    return 0;

  memcpy( pcBuf, pszBase, cbBase );
  if ( cbName != 0 )
  {
    pcBuf[cbBase] = '\\';
    cbBase++;
    memcpy( &pcBuf[cbBase], pszName, cbName );
    cbBase += cbName;
  }
  pcBuf[cbBase] = '\0';

  return cbBase;
}

static PRDFILE _getRDFile(RD_NTHANDLE handle)
{
  ULONG      ulIdx = _handleToIndex( handle );

  return ulIdx >= cRDFiles ? NULL : papRDFiles[ ulIdx ];
}

/* Convert unix timestamp to a windows file date/time */
static void _convert_unixtime_to_filetime(time_t seconds, uint32 *pui32High,
                                          uint32 *pui32Low)
{
  unsigned long long ullTicks;

  ullTicks = (seconds + 11644473600LL) * 10000000;
  *pui32Low = (uint32)ullTicks;
  *pui32High = (uint32)(ullTicks >> 32);
}

/* Convert windows file date/time to the unix timestamp. */
static time_t _convert_filetime_to_unixtime(uint32 ui32High, uint32 ui32Low)
{
  // high:low - number of 100-nanosecond intervals since January 1, 1601.
  // The result represents the time in seconds since January 1, 1970.

	unsigned long long ullTicks;
	time_t val;

  if ( ( ui32High == 0 ) && ( ui32Low == 0 ) )
    return 0;

  ullTicks = (((unsigned long long)ui32High) << 32) + ui32Low;
  ullTicks /= 10000000;             // 100-nanoseconds -> seconds
  ullTicks -= 11644473600LL;

  val = (time_t)ullTicks;
  return val;
}

static VOID _convert_unixtime_to_os2fdatetime(time_t timeFile,
                                              PFDATE pfdate, PFTIME pftime)
{
  struct tm  *pTM;

  // Get local time from the unix time stamp.
  pTM = localtime( &timeFile );
  if ( pTM->tm_isdst == 1 )
  {
    // We got time with Daylight Savings Time flag. 
    pTM->tm_isdst = 0;
    timeFile -= ( mktime( pTM ) - timeFile );
    pTM = localtime( &timeFile );
  }

  pfdate->year    = pTM->tm_year - 80;
  pfdate->month   = pTM->tm_mon + 1;
  pfdate->day     = pTM->tm_mday;
  pftime->hours   = pTM->tm_hour;
  pftime->minutes = pTM->tm_min;
  pftime->twosecs = pTM->tm_sec / 2;
}

static VOID _out_fdatetime(STREAM out, FDATE fdate, FTIME ftime)
{
  struct tm  stTM;
  uint32     ui32High, ui32Low;

  stTM.tm_year  = fdate.year + 80; 
  stTM.tm_mon   = fdate.month - 1;
  stTM.tm_mday  = fdate.day; 
  stTM.tm_hour  = ftime.hours; 
  stTM.tm_min   = ftime.minutes; 
  stTM.tm_sec   = ftime.twosecs * 2;
  stTM.tm_isdst = 0; 

  _convert_unixtime_to_filetime( mktime( &stTM ), &ui32High, &ui32Low );
  out_uint32_le( out, ui32Low );
  out_uint32_le( out, ui32High );
}

static RD_NTSTATUS _makeNotify(POSFILE pOSFile, NOTIFY *pNotify)
{
  ULONG                ulRC;
  FILESTATUS3L  	     stInfo;
  FILEFINDBUF3L        stFind;
  ULONG                cFind;
  HDIR                 hDir = HDIR_CREATE;
  CHAR                 acPattern[CCHMAXPATH];

  ulRC = _queryPathInfo( pOSFile->pszName, &stInfo );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosQueryPathInfo('%s',,,), rc = %u", pOSFile->pszName, ulRC );
    return RD_STATUS_INVALID_PARAMETER;
  }

  pNotify->modify_time = (time_t)( (*((PUSHORT)&stInfo.fdateCreation) << 16) +
                             *((PUSHORT)&stInfo.ftimeCreation) );
  pNotify->status_time = (time_t)( (*((PUSHORT)&stInfo.fdateLastWrite) << 16) +
                             *((PUSHORT)&stInfo.ftimeLastWrite) );
  pNotify->num_entries = 0;
  pNotify->total_time = 0;

  if ( _makePath( acPattern, sizeof(acPattern), pOSFile->pszName, "*.*" ) == 0 )
  {
    debug( "Path too long" );
    return RD_STATUS_INVALID_PARAMETER;
  }

  cFind = 1;
  ulRC = DosFindFirst( acPattern, &hDir, FILE_ARCHIVED | FILE_DIRECTORY |
                       FILE_SYSTEM | FILE_HIDDEN | FILE_READONLY,
                       &stFind, sizeof(stFind), &cFind, FIL_STANDARDL );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosFindFirst(%s,,,,,,), rc = %u", acPattern, ulRC );
    return RD_STATUS_PENDING;
  }

  pNotify->num_entries = 1;
  pNotify->total_time = (time_t)( *((PUSHORT)&stFind.ftimeLastWrite) );

  while( ulRC != ERROR_NO_MORE_FILES )
  {
    cFind = 1;
    ulRC = DosFindNext( hDir, &stFind, sizeof(stFind), &cFind );

    if ( ( ulRC != NO_ERROR ) && ( ulRC != ERROR_NO_MORE_FILES ) )
    {
      debug("DosFindNext(), rc = %u", ulRC );
      break;
    }

    pNotify->num_entries++;
    pNotify->total_time += (time_t)( *((PUSHORT)&stFind.ftimeLastWrite) );
  }

  DosFindClose( hDir );

  return RD_STATUS_PENDING;
}

// Returns TRUE if file was realy closed and FALSE if file still opened for
// other handle(s).
static BOOL _osfClose(POSFILE pOSFile)
{
  pOSFile->ulOpenCount--;

  if ( pOSFile->ulOpenCount == 0 )
  {
    lnkseqRemove( &lsOSFiles, pOSFile );
    _osfFree( pOSFile );
    return TRUE;
  }

  return FALSE;
}

/* Returns system error code. */
static ULONG _osfQueryInfo(POSFILE pOSFile, PFILESTATUS3L pFileStat)
{
  ULONG      ulRC = _osfIsDir( pOSFile )
                  ? _queryPathInfo( pOSFile->pszName, pFileStat )
                  : DosQueryFileInfo( pOSFile->hFile, FIL_STANDARDL, pFileStat,
                                      sizeof(FILESTATUS3L) );

  if ( ulRC != NO_ERROR )
    debug( "DosQueryPathInfo() / DosQueryFileInfo(), rc = %u", ulRC );

  return ulRC;
}

/* Returns system error code. */
static ULONG _osfSetInfo(POSFILE pOSFile, PFILESTATUS3L pFileStat)
{
  ULONG      ulRC = _osfIsDir( pOSFile )
                  ? DosSetPathInfo( pOSFile->pszName, FIL_STANDARDL, pFileStat,
                                          sizeof(FILESTATUS3L), 0 )
                  : DosSetFileInfo( pOSFile->hFile, FIL_STANDARDL, pFileStat,
                                         sizeof(FILESTATUS3L) );

  if ( ulRC != NO_ERROR )
    debug( "DosQueryPathInfo() / DosQueryFileInfo(), rc = %u", ulRC );

  return ulRC;
}


//  Public routines for rdesktop.
//  -----------------------------

/* OS/2 specific function. Called from rdpdr.c/rdpdr_handle_ok(). */
RD_BOOL disk_is_device_id(RD_NTHANDLE handle, uint32 device_id)
{
  PRDFILE              pRDFile = _getRDFile( handle );

  return ( pRDFile != NULL ) && ( pRDFile->ui32DeviceId == device_id );
}

/* OS/2 specific function. Called from os2win.c/ui_init(). */
VOID disk_init()
{
  lnkseqInit( &lsOSFiles );
}

/* OS/2 specific function. Called from os2win.c/ui_deinit(). */
VOID disk_done()
{
  if ( papRDFiles != NULL )
  {
    PRDFILE  *ppRDFile;

    for( ppRDFile = papRDFiles; cRDFiles > 0; cRDFiles--, ppRDFile++ )
      if ( *ppRDFile != NULL )
        debugFree( *ppRDFile );

    debugFree( papRDFiles );
  }

  lnkseqFree( &lsOSFiles, POSFILE, _osfFree );
}

/* Enumeration of devices from rdesktop.c        */
/* returns numer of units found and initialized. */
/* optarg looks like ':h=/mnt/floppy,b=/mnt/usbdevice1' */
/* when it arrives to this function.             */
int disk_enum_devices(uint32 * id, char *optarg)
{
  char       *pos = optarg;
  char       *pos2;
  int        count = 0;
  ULONG      ulLen;

  /* skip the first colon */
  optarg++;
  while( (pos = next_arg(optarg, ',')) && *id < RDPDR_MAX_DEVICES )
  {
    pos2 = next_arg(optarg, '=');

    strncpy(g_rdpdr_device[*id].name, optarg, sizeof(g_rdpdr_device[*id].name) - 1);
    if ( strlen(optarg) > (sizeof(g_rdpdr_device[*id].name) - 1) )
      fprintf(stderr, "share name %s truncated to %s\n", optarg,
              g_rdpdr_device[*id].name);

    ulLen = strlen( pos2 );
    // Remove trailing slash.
    while( ( ulLen > 1 ) && pos2[ulLen - 1] == '\\' )
      ulLen--;

    g_rdpdr_device[*id].local_path = (char *)xmalloc( ulLen + 1 );
//		strcpy(g_rdpdr_device[*id].local_path, pos2);
    memcpy( g_rdpdr_device[*id].local_path, pos2, ulLen );
    g_rdpdr_device[*id].local_path[ulLen] = '\0';

    g_rdpdr_device[*id].device_type = DEVICE_TYPE_DISK;
    count++;
    (*id)++;

    optarg = pos;
  }

  return count;
}

RD_NTSTATUS disk_query_information(RD_NTHANDLE handle, uint32 ui32InfoClass,
                                   STREAM out)
{
  ULONG                ulRC;
  FILESTATUS3L         stFileStat;
  PRDFILE              pRDFile = _getRDFile( handle );

  if ( pRDFile == NULL )
  {
    debug( "Invalid handle %u", handle );
    return RD_STATUS_INVALID_HANDLE;
  }

  ulRC = _osfQueryInfo( pRDFile->pOSFile, &stFileStat );
  if ( ulRC != NO_ERROR )
  {
    out_uint8( out, 0 );
    return RD_STATUS_ACCESS_DENIED;
  }

/*  debug( "handle: %u, info_class: %u, dir: %c : %s",
         handle, ui32InfoClass,
         pRDFile->pOSFile->ulAttribute & FILE_DIRECTORY ? 'Y' : 'N',
         pRDFile->pOSFile->pszName );*/

	/* Return requested data */
  switch( ui32InfoClass )
  {
    case FileBasicInformation: // 4
      /* create_access_time */
      _out_fdatetime( out, stFileStat.fdateCreation, stFileStat.ftimeCreation );
      /* last_access_time */
      _out_fdatetime( out, stFileStat.fdateLastAccess, stFileStat.ftimeLastAccess );
      /* last_write_time */
      _out_fdatetime( out, stFileStat.fdateLastWrite, stFileStat.ftimeLastWrite );
      /* last_change_time */
      _out_fdatetime( out, stFileStat.fdateLastWrite, stFileStat.ftimeLastWrite );

      out_uint32_le( out, pRDFile->pOSFile->ulAttribute );
      break;

    case FileStandardInformation: // 5
      // File allocated size.
      out_uint32_le( out, (uint32)stFileStat.cbFileAlloc.ulLo );
      out_uint32_le( out, (uint32)stFileStat.cbFileAlloc.ulHi );
      // End of file.
      out_uint32_le( out, (uint32)stFileStat.cbFile.ulLo );
      out_uint32_le( out, (uint32)stFileStat.cbFile.ulHi );

      out_uint32_le( out, 0 );             // Number of links.
      out_uint8( out, 0 );                 // Delete pending.
      out_uint8( out, _osfIsDir( pRDFile->pOSFile ) ? 1 : 0 ); // Directory.
      break;

    case FileObjectIdInformation:
      out_uint32_le( out, pRDFile->pOSFile->ulAttribute );
      out_uint32_le( out, 0 );                 // Reparse tag.
      break;

    default:
      unimpl( "IRP Query (File) Information class: 0x%x\n", ui32InfoClass );
      return RD_STATUS_INVALID_PARAMETER;
  }

  return RD_STATUS_SUCCESS;
}

RD_NTSTATUS disk_set_information(RD_NTHANDLE handle, uint32 ui32InfoClass,
                                 STREAM in, STREAM out)
{
  FILESTATUS3L         stFileStat;
  ULONG                ulRC, ulRC2, ulAction;
  PRDFILE              pRDFile = _getRDFile( handle );
  POSFILE              pOSFile;
  CHAR                 acFullPath[PATH_MAX];

  if ( pRDFile == NULL )
  {
    debug( "Invalid handle %u", handle );
    return RD_STATUS_INVALID_HANDLE;
  }

  g_notify_stamp = True;
  pOSFile = pRDFile->pOSFile;

/*  debug( "handle: %u, info_class: %u : %s",
         handle, ui32InfoClass, pOSFile->pszName );*/

  switch( ui32InfoClass )
  {
    case FileBasicInformation: // 4
    {
      time_t timeCreation, timeAccess, timeWrite, timeChange;
      uint32 ui32Low, ui32High, ui32Attribute;

      ulRC = _osfQueryInfo( pOSFile, &stFileStat );

      in_uint8s( in, 4 );   /* Handle of root dir? */
      in_uint8s( in, 24 );  /* unknown (zeros?) */

      /* CreationTime */
      in_uint32_le( in, ui32Low );
      in_uint32_le( in, ui32High );
      timeCreation = _convert_filetime_to_unixtime( ui32High, ui32Low );
      if ( timeCreation != 0 )
        _convert_unixtime_to_os2fdatetime( timeCreation,
                                           &stFileStat.fdateCreation,
                                           &stFileStat.ftimeCreation );

      /* AccessTime */
      in_uint32_le( in, ui32Low );
      in_uint32_le( in, ui32High );
      timeAccess = _convert_filetime_to_unixtime( ui32High, ui32Low );
      if ( timeAccess != 0 )
        _convert_unixtime_to_os2fdatetime( timeAccess,
                                         &stFileStat.fdateLastAccess,
                                         &stFileStat.ftimeLastAccess );

      /* WriteTime */
      in_uint32_le( in, ui32Low );
      in_uint32_le( in, ui32High );
      timeWrite = _convert_filetime_to_unixtime( ui32High, ui32Low );

      /* ChangeTime */
      in_uint32_le( in, ui32Low );
      in_uint32_le( in, ui32High );
      timeChange = _convert_filetime_to_unixtime( ui32High, ui32Low );

			if ( ( timeWrite != 0 ) && ( timeChange != 0 ) )
				timeWrite = MIN(timeWrite, timeChange);
			else
				timeWrite = timeWrite ? timeWrite : timeChange;

      if ( timeWrite != 0 )
        _convert_unixtime_to_os2fdatetime( timeWrite,
                                           &stFileStat.fdateLastWrite,
                                           &stFileStat.ftimeLastWrite );

      in_uint32_le( in, ui32Attribute );
      if ( ui32Attribute != 0 )
        // Basic flags for Windows and OS/2 are identical. Do not set RO here.
        stFileStat.attrFile = ui32Attribute &
                              (FILE_ARCHIVED | FILE_SYSTEM | FILE_HIDDEN);

      ulRC = _osfSetInfo( pOSFile, &stFileStat );

      if ( ( ulRC == NO_ERROR ) && ( ui32Attribute != 0 ) )
        pOSFile->ulAttribute = ui32Attribute &
                   (FILE_ARCHIVED | FILE_SYSTEM | FILE_HIDDEN | FILE_READONLY);

      break;
    }

    case FileRenameInformation:
    {
      LONG   lLength;
      PSZ    pszNewName;
      ULONG  ulRenRC;

      in_uint8s( in, 4 );     /* Handle of root dir? */
      in_uint8s( in, 0x1A );  /* unknown */
      in_uint32_le( in, lLength );

      if ( ( lLength == 0 ) || ( lLength >= (256 * 2) ) )
        return RD_STATUS_INVALID_PARAMETER;

      rdp_in_unistr( in, lLength, &pszNewName, (uint32 *)&lLength );
      if ( pszNewName == NULL )
        return RD_STATUS_INVALID_PARAMETER;

      lLength = _makePath( acFullPath, sizeof(acFullPath),
                           g_rdpdr_device[pRDFile->ui32DeviceId].local_path,
                           pszNewName );

      xfree( pszNewName );

      if ( lLength == -1 )
        return RD_STATUS_INVALID_PARAMETER;

      // Close file before rename.
      if ( !_osfIsDir( pOSFile ) )
        DosClose( pOSFile->hFile );

      // Rename and change full file name in file's record.
      ulRenRC = DosMove( pOSFile->pszName, acFullPath );
      if ( ulRenRC != NO_ERROR )
        debug( "DosMove(), rc = %u", ulRenRC );
      else
      {
        pszNewName = debugStrDup( acFullPath );

        if ( pszNewName == NULL )
          debug( "Not enough memory" );
        else
        {
          debugFree( pOSFile->pszName );
          pOSFile->pszName = pszNewName;
        }
      }

      // Reopen file if it was opened before rename.
      if ( !_osfIsDir( pOSFile ) )
      {
        ulRC = DosOpen( acFullPath, &pOSFile->hFile, (PULONG)&lLength, 0,
                        FILE_NORMAL,
                        OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                        OPEN_FLAGS_FAIL_ON_ERROR | OPEN_SHARE_DENYREADWRITE |
                        OPEN_ACCESS_READWRITE, NULL );
        if ( ulRC != NO_ERROR )
          debug( "DosOpen(), rc = %u", ulRC );
      }

      if ( ulRenRC != NO_ERROR )
        return RD_STATUS_ACCESS_DENIED;

      break;
    }

    case FileDispositionInformation:
      /* [Digi] Original disk.c comments:
         As far as I understand it, the correct thing to do here is to
         *schedule* a delete, so it will be deleted when the file is closed.
         Subsequent FileDispositionInformation requests with DeleteFile set to
         FALSE should unschedule the delete. See
         http://www.osronline.com/article.cfm?article=245.

         FileDispositionInformation always sets delete_on_close to true.
         "STREAM in" includes Length(4bytes) , Padding(24bytes) and
         SetBuffer(zero byte). Length is always set to zero.
         [MS-RDPEFS] http://msdn.microsoft.com/en-us/library/cc241305%28PROT.10%29.aspx
         - 2.2.3.3.9 Server Drive Set Information Request
       */
      in_uint8s( in, 4 );   /* length of SetBuffer */
      in_uint8s( in, 24 );  /* padding */

      if ( (pRDFile->ui32AccessMask &
            (FILE_DELETE_ON_CLOSE | FILE_COMPLETE_IF_OPLOCKED)) != 0 )
      {
        /* if file exists in directory , necessary to return RD_STATUS_DIRECTORY_NOT_EMPTY with win2008
           [MS-RDPEFS] http://msdn.microsoft.com/en-us/library/cc241305%28PROT.10%29.aspx
           - 2.2.3.3.9 Server Drive Set Information Request
           - 2.2.3.4.9 Client Drive Set Information Response
           [MS-FSCC] http://msdn.microsoft.com/en-us/library/cc231987%28PROT.10%29.aspx
           - 2.4.11 FileDispositionInformation
           [FSBO] http://msdn.microsoft.com/en-us/library/cc246487%28PROT.13%29.aspx
           - 4.3.2 Set Delete-on-close using FileDispositionInformation Information Class (IRP_MJ_SET_INFORMATION)
         */
        if ( _osfIsDir( pOSFile ) )
        {
          HDIR                   hDir = HDIR_CREATE;
          FILEFINDBUF3L          stFind;
          ULONG                  cFind;

          _makePath( acFullPath, sizeof(acFullPath), pOSFile->pszName, "*.*" );
          cFind = 1;
          ulRC = DosFindFirst( acFullPath, &hDir, FILE_ARCHIVED |
                               FILE_DIRECTORY | FILE_SYSTEM | FILE_HIDDEN |
                               FILE_READONLY,
                               &stFind, sizeof(stFind), &cFind, FIL_STANDARDL );
          if ( ( ulRC != ERROR_NO_MORE_FILES ) && ( ulRC != NO_ERROR ) )
          {
            debug( "DosFindFirst(%s,,,,), rc = %u", acFullPath, ulRC );
            return RD_STATUS_DIRECTORY_NOT_EMPTY;
          }
          // I think it's no need to check first entry (is it "." ?)

          while( ulRC != ERROR_NO_MORE_FILES )
          {
            cFind = 1;
            ulRC = DosFindNext( hDir, &stFind, sizeof(stFind), &cFind );

            if ( ( ulRC != NO_ERROR ) && ( ulRC != ERROR_NO_MORE_FILES ) )
            {
              debug("DosFindNext(), rc = %u", ulRC );
              break;
            }

            if ( ( ( stFind.cchName == 1 ) && ( stFind.achName[0] == '.' ) ) ||
                 ( ( stFind.cchName == 2 ) &&
                    ( *((PUSHORT)&stFind.achName[0]) == 0x2E2E /* ".." */ ) ) )
              continue;

            DosFindClose( hDir );
            return RD_STATUS_DIRECTORY_NOT_EMPTY;
          }

          DosFindClose( hDir );
        }

        pRDFile->fDeleteOnClose = TRUE;
      }
      break;

    case FileAllocationInformation:
      /* Fall through to FileEndOfFileInformation,
         which uses ftrunc. This is like Samba with
         "strict allocation = false", and means that
         we won't detect out-of-quota errors, for
         example. */

    case FileEndOfFileInformation:
    {
      ULONG            ulNewFSize;

      if ( ( pRDFile->ui32AccessMask & (GENERIC_ALL | GENERIC_WRITE) ) == 0 )
      {
        debug( "RD_STATUS_ACCESS_DENIED: Has no write access: %s",
               pOSFile->pszName );
        return RD_STATUS_ACCESS_DENIED;
      }

      in_uint8s( in, 28 );             /* unknown */
      in_uint32_le( in, ulNewFSize );  /* file size */

      ulRC = DosSetFileSize( pOSFile->hFile, ulNewFSize );
      if ( ulRC != NO_ERROR )
      {
        return ulRC == ERROR_DISK_FULL ? RD_STATUS_DISK_FULL :
                 ulRC == ERROR_ACCESS_DENIED ? RD_STATUS_ACCESS_DENIED
                                             : RD_STATUS_INVALID_PARAMETER;
      }

      break;
    }

    default:
      unimpl( "IRP Set File Information class: 0x%x\n", ui32InfoClass );
      return RD_STATUS_INVALID_PARAMETER;
  }

  return RD_STATUS_SUCCESS;
}

RD_NTSTATUS disk_check_notify(RD_NTHANDLE handle)
{
  RD_NTSTATUS          status;
  PRDFILE              pRDFile = _getRDFile( handle );
  NOTIFY               stNotify;

  if ( pRDFile == NULL )
  {
    debug( "Invalid handle %u", handle );
    return RD_STATUS_INVALID_HANDLE;
  }

  status = _makeNotify( pRDFile->pOSFile, &stNotify );
  if ( status != RD_STATUS_PENDING )
    return status;

  if ( memcmp( &pRDFile->stNotify, &stNotify, sizeof(NOTIFY) ) != 0 )
  {
    memcpy( &pRDFile->stNotify, &stNotify, sizeof(NOTIFY) );
    status = RD_STATUS_NOTIFY_ENUM_DIR;
  }

  return status;
}

RD_NTSTATUS disk_create_notify(RD_NTHANDLE handle, uint32 ui32InfoClass)
{
  PRDFILE              pRDFile = _getRDFile( handle );
  RD_NTSTATUS          status;

  if ( pRDFile == NULL )
  {
    debug( "Invalid handle %u", handle );
    return RD_STATUS_INVALID_HANDLE;
  }

  status = _makeNotify( pRDFile->pOSFile, &pRDFile->stNotify );

  if ( (ui32InfoClass & 0x1000) != 0 )
  {			/* ???? */
    if ( status == RD_STATUS_PENDING )
      return RD_STATUS_SUCCESS;
  }

  return status;
}

RD_NTSTATUS disk_query_volume_information(RD_NTHANDLE handle,
                                          uint32 ui32InfoClass, STREAM out)
{
  PRDFILE              pRDFile = _getRDFile( handle );
  POSFILE              pOSFile;
  ULONG                ulRC;
  ULONG                ulDiskNum;
  ULONG                ulLen;

  if ( pRDFile == NULL )
  {
    debug( "Invalid handle %u", handle );
    return RD_STATUS_INVALID_HANDLE;
  }

  pOSFile = pRDFile->pOSFile;
  ulDiskNum = pOSFile->pszName[1] == ':' ?
                ( toupper( pOSFile->pszName[0] ) - 'A' + 1 ) : 0;
/*  debug( "handle: %u, info_class: %u, disk num: %u : %s",
         handle, ui32InfoClass, ulDiskNum, pOSFile->pszName );*/

  switch( ui32InfoClass )
  {
    case FileFsVolumeInformation:
    {
      FSINFO           stFSInfo;

      ulRC = DosQueryFSInfo( ulDiskNum, FSIL_VOLSER, &stFSInfo, sizeof(FSINFO) );
      if ( ulRC != NO_ERROR )
      {
        debug( "#1 DosQueryFSInfo(), rc = %u", ulRC );
        return RD_STATUS_INVALID_PARAMETER;
      }

      // Volume creation time.
      _out_fdatetime( out, stFSInfo.fdateCreation, stFSInfo.ftimeCreation );
      // Serial.
      out_uint32_le( out, ulDiskNum ); // hm...
      // Length of volume label.
      ulLen = 2 * strlen( stFSInfo.vol.szVolLabel );
      out_uint32_le( out, ulLen );
      out_uint8( out, 0 );	/* support objects? */
      // Volume label.
      rdp_out_unistr( out, stFSInfo.vol.szVolLabel, ulLen - 2 );
      break;
    }

    case FileFsSizeInformation:
    case FileFsFullSizeInformation:
    {
      FSALLOCATE       stFSAllocate;

      ulRC = DosQueryFSInfo( ulDiskNum, FSIL_ALLOC, &stFSAllocate,
                             sizeof(FSALLOCATE) );
      if ( ulRC != NO_ERROR )
      {
        debug( "#2 DosQueryFSInfo(), rc = %u", ulRC );
        return RD_STATUS_INVALID_PARAMETER;
      }

      out_uint32_le( out, stFSAllocate.cUnit );	// Total allocation units low.
      out_uint32_le( out, 0 );  // Total allocation units high.
      if ( ui32InfoClass == FileFsFullSizeInformation )
      {
        out_uint32_le( out, stFSAllocate.cUnitAvail );	// Caller allocation units low.
        out_uint32_le( out, 0 );	// Caller allocation units high.
      }
      out_uint32_le( out, stFSAllocate.cUnitAvail );  // Available allocation units.
      out_uint32_le( out, 0 );  // Available allowcation units.
      out_uint32_le( out, stFSAllocate.cSectorUnit ); // Sectors per allocation unit.
      out_uint32_le( out, stFSAllocate.cbSector ); // Bytes per sector.
      break;
    }

    case FileFsAttributeInformation:
    {
      CHAR         szDrive[8] = "D:\0";
      ULONG        ulVal;
      BYTE         abBuf[sizeof(FSQBUFFER2) + (3 * CCHMAXPATH)] = { 0 };
      PFSQBUFFER2  pBuf = (PFSQBUFFER2)&abBuf;

      if ( pOSFile->pszName[1] == ':' )
        szDrive[0] = pOSFile->pszName[0];
      else
      {
        DosQueryCurrentDisk( &ulDiskNum, &ulVal );
        szDrive[0] = ulVal - 1 + 'A';
      }

      ulVal = sizeof(abBuf);
      ulRC = DosQueryFSAttach( (PSZ)szDrive, 0, FSAIL_QUERYNAME, pBuf, &ulVal );

      if ( ulRC != NO_ERROR )
      {
        debug( "DosQueryFSAttach(%s,,,,), rc = %u\n", szDrive, ulRC );
        return RD_STATUS_INVALID_PARAMETER;
      }

      DosQuerySysInfo( QSV_MAX_PATH_LENGTH, QSV_MAX_PATH_LENGTH, &ulVal,
                       sizeof(ULONG) );

      out_uint32_le( out, FS_CASE_IS_PRESERVED ); /* fs attributes */
      out_uint32_le( out, ulVal ); /* max length of filename */

      out_uint32_le( out, 2 * pBuf->cbFSDName ); /* length of fs_type */
      rdp_out_unistr( out, (char *)&pBuf->szName[pBuf->cbName + 1],
                      2 * pBuf->cbFSDName - 2 );
      break;
    }

    default:
      unimpl( "IRP Query Volume Information class: 0x%x\n", ui32InfoClass );
      return RD_STATUS_INVALID_PARAMETER;
  }

  return RD_STATUS_SUCCESS;
}

RD_NTSTATUS disk_query_directory(RD_NTHANDLE handle, uint32 ui32InfoClass,
                                 char *pszPattern, STREAM out)
{
  PRDFILE              pRDFile = _getRDFile( handle );
  POSFILE              pOSFile;
  ULONG                ulRC;
  FILEFINDBUF3L        stFind;
  ULONG                cFind;

  if ( pRDFile == NULL )
  {
    debug( "Invalid handle %u", handle );
    return RD_STATUS_INVALID_HANDLE;
  }
  pOSFile = pRDFile->pOSFile;

/*  debug( "handle: %u, info_class: %u, pattern: %s : %s",
         handle, ui32InfoClass, pszPattern, pOSFile->pszName );*/

  switch( ui32InfoClass )
  {
    case FileBothDirectoryInformation:
    case FileDirectoryInformation:
    case FileFullDirectoryInformation:
    case FileNamesInformation:
      break;
    default:
      unimpl( "IRP Query Directory sub: 0x%x\n", ui32InfoClass );
      return RD_STATUS_INVALID_PARAMETER;
  }

  cFind = 1;

  if ( ( pszPattern != NULL ) && ( pszPattern[0] != 0 ) )
  {
    // If a search pattern is received, restart search with new pattern.
    CHAR     acPattern[PATH_MAX];
    LONG     cbPattern;

    if ( pRDFile->hDir != HDIR_CREATE )
    {
      DosFindClose( pRDFile->hDir );
      pRDFile->hDir = HDIR_CREATE;
    }

    cbPattern = _makePath( acPattern, sizeof(acPattern) - 3,
                           g_rdpdr_device[pRDFile->ui32DeviceId].local_path,
                           pszPattern );
    if ( cbPattern == -1 )
    {
      debug( "Pattern too long" );
      return RD_STATUS_NO_MORE_FILES;
    }

    ulRC = DosFindFirst( acPattern, &pRDFile->hDir,
                         FILE_ARCHIVED | FILE_DIRECTORY | FILE_SYSTEM |
                         FILE_HIDDEN | FILE_READONLY,
                         &stFind, sizeof(stFind), &cFind, FIL_STANDARDL );
    if ( ulRC != NO_ERROR )
    {
      pRDFile->hDir = HDIR_CREATE;
      return RD_STATUS_NO_MORE_FILES;
    }
  }
  else if ( pRDFile->hDir == HDIR_CREATE )
    return RD_STATUS_NO_MORE_FILES;
  else
    ulRC = DosFindNext( pRDFile->hDir, &stFind, sizeof(stFind), &cFind );

  // Send data to the server.

  out_uint32_le( out, 0 );  /* NextEntryOffset */
  out_uint32_le( out, 0 );  /* FileIndex zero */

  if ( ui32InfoClass != FileNamesInformation )
  {
    _out_fdatetime( out, stFind.fdateCreation, stFind.ftimeCreation );
    _out_fdatetime( out, stFind.fdateLastAccess, stFind.ftimeLastAccess );
    _out_fdatetime( out, stFind.fdateLastWrite, stFind.ftimeLastWrite );
    // Change write time... last status change... :-/
    _out_fdatetime( out, stFind.fdateCreation, stFind.ftimeCreation );
    // File size.
    out_uint32_le( out, (uint32)stFind.cbFile.ulLo );
    out_uint32_le( out, (uint32)stFind.cbFile.ulHi );
    // File allocated size.
    out_uint32_le( out, (uint32)stFind.cbFileAlloc.ulLo );
    out_uint32_le( out, (uint32)stFind.cbFileAlloc.ulHi );
    // File attributes.
    out_uint32_le( out, (uint32)stFind.attrFile );
  }

  out_uint32_le( out, 2 * stFind.cchName + 2 );	/* unicode name length */

  switch( ui32InfoClass )
  {
    case FileBothDirectoryInformation:
      out_uint32_le( out, 0 );	/* EaSize */
      out_uint8( out, 0 );	/* ShortNameLength */
      /* this should be correct according to MS-FSCC specification
         but it only works when commented out... */
      /* out_uint8(out, 0); *//* Reserved/Padding */
      out_uint8s( out, 2 * 12 );	/* ShortName (8.3 name) */
      break;

    case FileFullDirectoryInformation:
      out_uint32_le( out, 0 );                      /* EaSize */
      break;

//    default: // FileDirectoryInformation, FileNamesInformation:
  }

  rdp_out_unistr( out, stFind.achName, 2 * stFind.cchName );

  if ( ulRC == ERROR_NO_MORE_FILES )
  {
    DosFindClose( pRDFile->hDir );
    pRDFile->hDir = HDIR_CREATE;
    return RD_STATUS_NO_MORE_FILES;
  }

  return RD_STATUS_SUCCESS;
}

/* Opens or creates a file or directory */
static RD_NTSTATUS disk_create(uint32 ui32DeviceId, uint32 ui32AccessMask,
                             uint32 ui32ShareMode, uint32 ui32CreateDisposition,
                             uint32 ui32FlagsAndAttr, PSZ pszName,
                             RD_NTHANDLE *handle)
/*
  ui32AccessMask - flags
    GENERIC_ALL
    GENERIC_READ
    GENERIC_WRITE
    GENERIC_EXECUTE

  ui32ShareMode - flags
    FILE_SHARE_READ
    FILE_SHARE_WRITE

  ui32CreateDisposition - value
    CREATE_ALWAYS     - Delete existing file/link
    CREATE_NEW        - If the file already exists, then fail.
    OPEN_ALWAYS       - Create if not already exists.
    TRUNCATE_EXISTING - If the file does not exist, then fail.
    OPEN_EXISTING

  ui32FlagsAndAttr - flags
    FILE_NON_DIRECTORY_FILE
    FILE_DIRECTORY_FILE

  result:
    RD_STATUS_SUCCESS
    RD_STATUS_NO_SUCH_FILE
    RD_STATUS_ACCESS_DENIED - Sharing violation
    RD_STATUS_OBJECT_NAME_COLLISION - File/directory already exists.
    RD_STATUS_FILE_IS_A_DIRECTORY
*/
{
  CHAR       acPathName[PATH_MAX];
  PRDFILE    pRDFile;
  POSFILE    pOSFile = (POSFILE)lnkseqGetFirst( &lsOSFiles );
  ULONG      ulIdx;
  ULONG      ulRC;
  BOOL       fAccOpenR = ( ui32AccessMask & (GENERIC_ALL | GENERIC_READ |
                                             GENERIC_EXECUTE) ) != 0;
  BOOL       fAccOpenW = ( ui32AccessMask & (GENERIC_ALL | GENERIC_WRITE) ) != 0;

  // Prepare file/directory name.

  ulRC = _makePath( acPathName, sizeof(acPathName),
                     g_rdpdr_device[ui32DeviceId].local_path, pszName );
  if ( ulRC == 0 )
  {
    debug( "Too long name: '%s' + '%s'",
           g_rdpdr_device[ui32DeviceId].local_path, pszName );
    return RD_STATUS_ACCESS_DENIED;
  }

  // Make system style slashes.
  for( pszName = acPathName; *pszName != '\0'; pszName++ )
    if ( *pszName == '/' )
      *pszName = '\\';

  /* Protect against mailicous servers:
     somelongpath\..     not allowed
     somelongpath\..\b   not allowed
     somelongpath\..b    in principle ok, but currently not allowed
     somelongpath\b..    ok
     somelongpath\b..b   ok
     somelongpath\b..\c  ok */
  if ( strstr( acPathName, "\\.." ) != NULL )
  {
    debug( "Not allowed name: %s", acPathName );
    return RD_STATUS_ACCESS_DENIED;
  }
  pszName = acPathName;


  // Look for already opened file.
  for( ; pOSFile != NULL; pOSFile = (POSFILE)lnkseqGetNext( pOSFile ) )
  {
    if ( strcoll( pOSFile->pszName, pszName ) == 0 )
      break;
  }

  if ( pOSFile != NULL )
  {
    // File was opened in RD.

    BOOL     fAccRdfR, fAccRdfW;

    if ( ui32CreateDisposition == CREATE_ALWAYS ||
         ui32CreateDisposition == CREATE_NEW )
    {
      debug( "RD_STATUS_OBJECT_NAME_COLLISION: File already opened in RD but whant disposition = %u: %s",
             ui32CreateDisposition, pszName );
      return RD_STATUS_OBJECT_NAME_COLLISION;
    }

    if ( _osfIsDir( pOSFile ) )
    {
      if ( (ui32FlagsAndAttr & FILE_NON_DIRECTORY_FILE) != 0 )
      {
        debug( "RD_STATUS_FILE_IS_A_DIRECTORY: File already open in RD and this is a directory: %s",
               pszName );
        return RD_STATUS_FILE_IS_A_DIRECTORY;
      }
    }
    else
    {
      if ( (ui32FlagsAndAttr & FILE_DIRECTORY_FILE) != 0 )
      {
        debug( "RD_STATUS_ACCESS_DENIED: File already open in RD and this is not a directory: %s",
               pszName );
        return RD_STATUS_ACCESS_DENIED;
      }

      if ( ( (pOSFile->ulAttribute & FILE_READONLY) != 0 ) && fAccOpenW )
      {
        debug( "RD_STATUS_ACCESS_DENIED: pOSFile has \"read only\" attribute: %s",
               pszName );
        return RD_STATUS_ACCESS_DENIED;
      }
    }

    for( ulIdx = 0; ulIdx < cRDFiles; ulIdx++ )
    {
      pRDFile = papRDFiles[ulIdx];
      if ( ( pRDFile == NULL ) || ( pRDFile->pOSFile != pOSFile ) )
        continue;

      if (
           ( ((pRDFile->ui32ShareMode & FILE_SHARE_READ) == 0) && fAccOpenR )
         ||
           ( ((pRDFile->ui32ShareMode & FILE_SHARE_WRITE) == 0) && fAccOpenW )
         )
      {
        debug( "RD_STATUS_ACCESS_DENIED: #1 Sharing violation: %s", pszName );
        return RD_STATUS_ACCESS_DENIED;
      }

      fAccRdfR = ( pRDFile->ui32AccessMask & (GENERIC_ALL | GENERIC_READ |
                                              GENERIC_EXECUTE) ) != 0;
      fAccRdfW = ( pRDFile->ui32AccessMask & (GENERIC_ALL | GENERIC_WRITE) )
                   != 0;

      if ( ( ((ui32ShareMode & FILE_SHARE_READ) == 0) && fAccRdfR ) ||
           ( ((ui32ShareMode & FILE_SHARE_WRITE) == 0) && fAccRdfW ) )
      {
        debug( "RD_STATUS_ACCESS_DENIED: #2 Sharing violation: %s", pszName );
        return RD_STATUS_ACCESS_DENIED;
      }
    }

  }          // if ( pOSFile != NULL )
  else
  {
    // File was not opened in RD - realy open file on "OS layer".

    FILESTATUS3L       stInfo;
    HFILE              hFile = NULLHANDLE;

    // Check existing file/directory on the disk.

    ulRC = _queryPathInfo( pszName, &stInfo );

    switch( ulRC )
    {
      case NO_ERROR:
        if ( ui32CreateDisposition == CREATE_NEW )
        {
          debug( "RD_STATUS_OBJECT_NAME_COLLISION: File/directory already exists: %s",
                 pszName );
          return RD_STATUS_OBJECT_NAME_COLLISION;
        }

        if ( (stInfo.attrFile & FILE_DIRECTORY) != 0 )
        {
          if ( (ui32FlagsAndAttr & FILE_NON_DIRECTORY_FILE) != 0 )
          {
            debug( "RD_STATUS_FILE_IS_A_DIRECTORY: Is a directory: %s",
                   pszName );
            return RD_STATUS_FILE_IS_A_DIRECTORY;
          }

          ui32FlagsAndAttr |= FILE_DIRECTORY_FILE;
        }
        break;

      case ERROR_SHARING_VIOLATION:
        debug( "RD_STATUS_ACCESS_DENIED: Sharing violation: %s", pszName );
        return RD_STATUS_ACCESS_DENIED;

      case ERROR_FILE_NOT_FOUND:
      case ERROR_PATH_NOT_FOUND:
        if ( (ui32FlagsAndAttr & FILE_DIRECTORY_FILE) != 0 )
        {
          if ( ui32CreateDisposition == OPEN_EXISTING )
          {
            debug( "RD_STATUS_NO_SUCH_FILE: Directory does not exists: %s",
                   pszName );
            return RD_STATUS_NO_SUCH_FILE;
          }

          debug( "Create a directory: %s", pszName );
          ulRC = DosCreateDir( pszName, NULL );
          if ( ulRC != NO_ERROR )
          {
            debug( "DosCreateDir(), rc = %u", ulRC );
            return ulRC = ERROR_PATH_NOT_FOUND
                     ? RD_STATUS_NO_SUCH_FILE : RD_STATUS_ACCESS_DENIED;
          }
#if 0
          ulRC = DosQueryPathInfo( pszName, FIL_STANDARDL, &stInfo,
                                   sizeof(FILESTATUS3L) );
          if ( ulRC != NO_ERROR )
            debug( "DosQueryPathInfo(), rc = %u", ulRC );
#else
          stInfo.attrFile = FILE_DIRECTORY;
#endif
          break;
        }

//      case ERROR_FILE_NOT_FOUND:
        if ( ( ui32CreateDisposition != OPEN_EXISTING ) &&
             ( ui32CreateDisposition != TRUNCATE_EXISTING ) )
          break;
        debug( "RD_STATUS_NO_SUCH_FILE: Not found: %s (disposition: %d)",
               pszName, ui32CreateDisposition );
        return RD_STATUS_NO_SUCH_FILE;

      default:
        debug( "RD_STATUS_NO_SUCH_FILE: File %s already exist (rc: %u, disposition: %d)",
               pszName, ulRC, ui32CreateDisposition );
        return RD_STATUS_NO_SUCH_FILE;
    }

    if ( (ui32FlagsAndAttr & FILE_DIRECTORY_FILE) == 0 )
    {
      ULONG    ulAction, ulOpenFlags, ulOpenMode;

      if ( (stInfo.attrFile & FILE_READONLY) != 0 )
      {
        // File have read-only attribute.

        if ( fAccOpenW )
        {
          debug( "RD_STATUS_ACCESS_DENIED: file has \"read only\" attribute: %s",
                 pszName );
          return RD_STATUS_ACCESS_DENIED;
        }

        // Remove read-only atribute for file before DosOpen(), we will restore
        // this attribute on close.
        stInfo.attrFile &= ~FILE_READONLY;
        ulRC = DosSetPathInfo( pszName, FIL_STANDARDL, &stInfo,
                               sizeof(stInfo), 0 );
        if ( ulRC != NO_ERROR )
          debug( "DosSetPathInfo(), rc = %u: %s", ulRC, pszName );
        // Restore RO-flag, we will store flags in pOSFile->ulAttribute. It will
        // be used to restore RO-flag on close.
        stInfo.attrFile |= FILE_READONLY;
      }

      // Detect open flags.
      switch( ui32CreateDisposition )
      {
        case CREATE_ALWAYS:      /* Delete existing file/link. */
          ulOpenFlags = OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS;
          break;

        case CREATE_NEW:         /* If the file already exists, then fail. */
          ulOpenFlags = OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_FAIL_IF_EXISTS;
          break;

        case OPEN_ALWAYS:        /* Create if not already exists. */
          ulOpenFlags = OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS;
          break;

        case TRUNCATE_EXISTING:  /* If the file does not exist, then fail. */
          ulOpenFlags = OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS;
          break;

        default:                 /* case OPEN_EXISTING: Default behaviour */
          ulOpenFlags = OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS;
          break;
      }

      ulRC = DosOpen( pszName, &hFile, &ulAction, 0, FILE_NORMAL, ulOpenFlags,
                      OPEN_FLAGS_FAIL_ON_ERROR | OPEN_SHARE_DENYREADWRITE |
                      OPEN_ACCESS_READWRITE, NULL );

      if ( ulRC != NO_ERROR )
      {
        if ( (stInfo.attrFile & FILE_READONLY) != 0 )
          // Restore (hm... try to restore) read-only attribute.
          DosSetPathInfo( pszName, FIL_STANDARDL, &stInfo,
                          sizeof(FILESTATUS3L), 0 );

        switch( ulRC )
        {
          case ERROR_FILE_NOT_FOUND:
          case ERROR_PATH_NOT_FOUND:
            debug( "RD_STATUS_NO_SUCH_FILE: DosOpen(), rc = %u: %s",
                   ulRC, pszName );
            return RD_STATUS_NO_SUCH_FILE;

          default:
            debug( "RD_STATUS_ACCESS_DENIED: DosOpen(), rc = %u: %s",
                   ulRC, pszName );
            return RD_STATUS_ACCESS_DENIED;
        }
      }      // if ( ulRC != NO_ERROR )
    }        // if ( (ui32FlagsAndAttr & FILE_DIRECTORY_FILE) == 0 )

    // Record for "real" file/directory.

    pOSFile = (POSFILE)debugMAlloc( sizeof(OSFILE) );
    if ( pOSFile == NULL )
    {
      debug( "Not enough memory" );
      if ( hFile != NULLHANDLE )
        DosClose( hFile );
      return RD_STATUS_ACCESS_DENIED;
    }

    pOSFile->pszName = debugStrDup( pszName );
    if ( pOSFile->pszName == NULL )
    {
      debug( "Not enough memory" );
      debugFree( pOSFile );
      if ( hFile != NULLHANDLE )
        DosClose( hFile );
      return RD_STATUS_ACCESS_DENIED;
    }
    pOSFile->hFile = hFile;
    pOSFile->ulOpenCount = 0;
    pOSFile->ulAttribute = stInfo.attrFile;

    lnkseqAdd( &lsOSFiles, pOSFile );
  }          // if ( pOSFile != NULL ) else


  // Record for new instance of opened in RD file/directory.

  pRDFile = (PRDFILE)debugMAlloc( sizeof(RDFILE) );
  if ( pRDFile == NULL )
  {
    debug( "Not enough memory" );
    _osfClose( pOSFile );
    return RD_STATUS_ACCESS_DENIED;
  }

  pRDFile->ui32DeviceId   = ui32DeviceId;
  pRDFile->pOSFile        = pOSFile;
  pRDFile->fDeleteOnClose = FALSE;
  pRDFile->ui32AccessMask = ui32AccessMask;
  pRDFile->ui32ShareMode  = ui32ShareMode;
  pRDFile->hDir           = HDIR_CREATE;

  pOSFile->ulOpenCount++;

  if ( ( ui32AccessMask & (GENERIC_ALL | GENERIC_WRITE) ) != 0 )
    g_notify_stamp = True;

  // Store pointer pRDFile in the list.
  
  // Search free index.
  for( ulIdx = 0; ulIdx < cRDFiles; ulIdx++ )
  {
    if ( papRDFiles[ulIdx] == NULL )
    {
      papRDFiles[ulIdx] = pRDFile;
      *handle = _handleFromIndex( ulIdx );
/*      debug( "handle: %u, sys.handle: %u - %s",
             *handle, pOSFile->hFile, pszName );*/
      return RD_STATUS_SUCCESS;
    }
  }

  // No free indexes, expand list and add pointer.

  // Expand list for every next 256 records.
  if ( (cRDFiles & 0xFF) == 0 )
  {
    PRDFILE  *papNew = debugReAlloc( papRDFiles,
                                     (cRDFiles + 0x100) * sizeof(PRDFILE) );
    if ( papNew == NULL )
    {
      debug( "Not enough memory" );
      debugFree( pRDFile );
      pOSFile->ulOpenCount--;
      _osfClose( pOSFile );
      return RD_STATUS_ACCESS_DENIED;
    }
    papRDFiles = papNew;
  }

  // Add pointer to the list.
  papRDFiles[cRDFiles] = pRDFile;
  *handle = _handleFromIndex( cRDFiles );
  cRDFiles++;
/*  debug( "handle: %u, sys.handle: %u, list size: %u - %s",
         *handle, pOSFile->hFile, cRDFiles, pszName );*/

  return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS disk_close(RD_NTHANDLE handle)
{
  PRDFILE    pRDFile = _getRDFile( handle );
  POSFILE    pOSFile;
  ULONG      ulRC;

  if ( pRDFile == NULL )
  {
    debug( "Invalid handle %u", handle );
    return RD_STATUS_INVALID_HANDLE;
  }

  pOSFile = pRDFile->pOSFile;

  if ( ( pRDFile->ui32AccessMask & (GENERIC_ALL | GENERIC_WRITE) ) != 0 )
    g_notify_stamp = True;

  rdpdr_abort_io( handle, 0, RD_STATUS_CANCELLED );

/*  debug( "handle: %u, delete: %s, %s",
         handle, pRDFile->fDeleteOnClose ? "Yes" : "No", pOSFile->pszName );*/

  if ( pRDFile->hDir != HDIR_CREATE )
  {
    ulRC = DosFindClose( pRDFile->hDir );
    if ( ulRC != NO_ERROR )
      debug( "DosFindClose(), rc = %u", ulRC );
  }

  if ( pOSFile->ulOpenCount == 1 )
  {
    // Last instance of the file handle (no more RDFILE records for OSFILE).

    if ( pRDFile->fDeleteOnClose )
    {
      PSZ      pszName = pOSFile->pszName;

      if ( _osfIsDir( pOSFile ) )
      {
        ulRC = DosDeleteDir( pszName );
        if ( ulRC != NO_ERROR )
        {
          debug( "RD_STATUS_ACCESS_DENIED: DosDeleteDir('%s'), rc = %u",
                 pszName, ulRC );
          pRDFile->fDeleteOnClose = FALSE;
          return RD_STATUS_ACCESS_DENIED; // It seems code will be ignored. 8( )
        }
      }
      else
      {
        // We must close the file before deleting.
        pOSFile->pszName = NULL; // Prevent free memory for name in _osfClose().
        _osfClose( pOSFile );
        pOSFile = NULL;

        ulRC = DosDelete( pszName );
        if ( ulRC != NO_ERROR )
          debug( "DosDelete(\"%s\"), rc = %u", pszName, ulRC );

        debugFree( pszName );
      }
    }        // if ( pRDFile->fDeleteOnClose )
    else if ( (pOSFile->ulAttribute & FILE_READONLY) != 0 )
    {
      FILESTATUS3L  	     stFileStat;

      debug( "Set \"read only\" attribute: %s", pOSFile->pszName );

      if ( _osfQueryInfo( pOSFile, &stFileStat ) == NO_ERROR )
      {
        stFileStat.attrFile = pOSFile->ulAttribute;
        _osfSetInfo( pOSFile, &stFileStat );
      }
    }
  }          // if ( pOSFile->ulOpenCount == 1 )
  else if ( pRDFile->fDeleteOnClose )
  {
    debug( "RD_STATUS_ACCESS_DENIED: Not last instance of opened file/directory: %s",
           pOSFile->pszName );
    pRDFile->fDeleteOnClose = FALSE;
    return RD_STATUS_ACCESS_DENIED;
  }

  if ( pOSFile != NULL )
    _osfClose( pOSFile );

  debugFree( pRDFile );
  papRDFiles[ _handleToIndex( handle ) ] = NULL;

  return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS disk_read(RD_NTHANDLE handle, uint8 *pui8Data,
                             uint32 cbData, uint32 ui32Offset,
                             uint32 *pui32Actual)
{
  PRDFILE    pRDFile = _getRDFile( handle );
  POSFILE    pOSFile;
  ULONG      ulRC;
  ULONG      ulActual;

  if ( pRDFile == NULL )
  {
    debug( "Invalid handle %u", handle );
    return RD_STATUS_INVALID_HANDLE;
  }

  pOSFile = pRDFile->pOSFile;
  if ( _osfIsDir( pOSFile ) )
  {
    debug( "RD_STATUS_NOT_IMPLEMENTED: Attempt to read the directory: %s",
           pOSFile->pszName );
    *pui32Actual = 0;
    return RD_STATUS_NOT_IMPLEMENTED;
  }

  if ( ( pRDFile->ui32AccessMask &
         (GENERIC_ALL | GENERIC_READ | GENERIC_EXECUTE) ) == 0 )
  {
    debug( "RD_STATUS_ACCESS_DENIED: Has no read access: %s", pOSFile->pszName );
    *pui32Actual = 0;
    return RD_STATUS_ACCESS_DENIED;
  }

  ulRC = DosSetFilePtr( pOSFile->hFile, ui32Offset, FILE_BEGIN, &ulActual );
  if ( ulRC != NO_ERROR )
    debug( "handle: %u, DosSetFilePtr(), rc = %u", handle, ulRC );

  ulRC = DosRead( pOSFile->hFile, pui8Data, cbData, (PULONG)pui32Actual );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosRead(), rc = %u", ulRC );
    *pui32Actual = 0;

    switch( ulRC )
    {
      case ERROR_ACCESS_DENIED:
        return RD_STATUS_ACCESS_DENIED;

      case ERROR_INVALID_HANDLE:
        return RD_STATUS_INVALID_HANDLE;

      default:
        return RD_STATUS_INVALID_PARAMETER;
    }
  }

  return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS disk_write(RD_NTHANDLE handle, uint8 *pui8Data,
                             uint32 cbData, uint32 ui32Offset,
                             uint32 *pui32Actual)
{
  PRDFILE    pRDFile = _getRDFile( handle );
  POSFILE    pOSFile;
  ULONG      ulRC;
  ULONG      ulActual;

  if ( pRDFile == NULL )
  {
    debug( "Invalid handle %u", handle );
    return RD_STATUS_INVALID_HANDLE;
  }

  pOSFile = pRDFile->pOSFile;
  if ( _osfIsDir( pOSFile ) )
  {
    debug( "RD_STATUS_NOT_IMPLEMENTED: Attempt to write the directory: %s",
           pOSFile->pszName );
    *pui32Actual = 0;
    return RD_STATUS_NOT_IMPLEMENTED;
  }

  if ( ( pRDFile->ui32AccessMask & (GENERIC_ALL | GENERIC_WRITE) ) == 0 ||
       (pOSFile->ulAttribute & FILE_READONLY) != 0 )
  {
    debug( "RD_STATUS_ACCESS_DENIED: Has no write access: %s", pOSFile->pszName );
    *pui32Actual = 0;
    return RD_STATUS_ACCESS_DENIED;
  }

  ulRC = DosSetFilePtr( pOSFile->hFile, ui32Offset, FILE_BEGIN, &ulActual );
  if ( ulRC != NO_ERROR )
    debug( "handle: %u, DosSetFilePtr(), rc = %u", handle, ulRC );

  ulRC = DosWrite( pOSFile->hFile, pui8Data, cbData, (PULONG)pui32Actual );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosWrite(), rc = %u", ulRC );
    *pui32Actual = 0;

    switch( ulRC )
    {
      case ERROR_DISK_FULL:
        return RD_STATUS_DISK_FULL;

      default:
        return RD_STATUS_INVALID_PARAMETER;
    }
  }

  return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS disk_device_control(RD_NTHANDLE handle, uint32 ui32Request,
                                       STREAM in, STREAM out)
{
  if ( ( (ui32Request >> 16) != 20 ) || ( (ui32Request >> 16) != 9 ) )
    return RD_STATUS_INVALID_PARAMETER;

  /* extract operation */
  ui32Request >>= 2;
  ui32Request &= 0xfff;

  debug( "DISK IOCTL %u", ui32Request );

  switch( ui32Request )
  {
    case 25: /* ? */
    case 42: /* ? */
    default:
      unimpl( "DISK IOCTL %d\n", ui32Request );
      return RD_STATUS_INVALID_PARAMETER;
  }

  return RD_STATUS_SUCCESS;
}



DEVICE_FNS disk_fns = {
  disk_create,
  disk_close,
  disk_read,
  disk_write,
  disk_device_control	/* device_control */
};
