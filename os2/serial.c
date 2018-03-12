/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.

   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008

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
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/route.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <unistd.h>
#include "rdesktop.h"
#define INCL_DOSDEVICES
#define INCL_DOSDEVIOCTL
#define INCL_DOSERRORS
#include "os2rd.h"
#include "debug.h"

/*
   To support fast unblocking read/write operations and select()-oriented
   architecture of rdesktop we use local sockets and thread.
   Too dirty work with windows-style timeouts for RS232 released, it requires
   additional calculation in thread or/and in read/write functions to be good
   enough. Timing for RS232 in Windows differs from serial port timing in OS/2.
*/

#ifdef WITH_DEBUG_SERIAL
#define DEBUG_SERIAL(args) printf args;
#else
#define DEBUG_SERIAL(args)
#endif

#define FILE_DEVICE_SERIAL_PORT		0x1b

#define SERIAL_SET_BAUD_RATE        1
#define SERIAL_SET_QUEUE_SIZE       2
#define SERIAL_SET_LINE_CONTROL     3
#define SERIAL_SET_BREAK_ON         4
#define SERIAL_SET_BREAK_OFF        5
#define SERIAL_IMMEDIATE_CHAR       6
#define SERIAL_SET_TIMEOUTS         7
#define SERIAL_GET_TIMEOUTS         8
#define SERIAL_SET_DTR              9
#define SERIAL_CLR_DTR             10
#define SERIAL_RESET_DEVICE        11
#define SERIAL_SET_RTS             12
#define SERIAL_CLR_RTS             13
#define SERIAL_SET_XOFF            14
#define SERIAL_SET_XON             15
#define SERIAL_GET_WAIT_MASK       16
#define SERIAL_SET_WAIT_MASK       17
#define SERIAL_WAIT_ON_MASK        18
#define SERIAL_PURGE               19
#define SERIAL_GET_BAUD_RATE       20
#define SERIAL_GET_LINE_CONTROL    21
#define SERIAL_GET_CHARS           22
#define SERIAL_SET_CHARS           23
#define SERIAL_GET_HANDFLOW        24
#define SERIAL_SET_HANDFLOW        25
#define SERIAL_GET_MODEMSTATUS     26
#define SERIAL_GET_COMMSTATUS      27
#define SERIAL_XOFF_COUNTER        28
#define SERIAL_GET_PROPERTIES      29
#define SERIAL_GET_DTRRTS          30
#define SERIAL_LSRMST_INSERT       31
#define SERIAL_CONFIG_SIZE         32
#define SERIAL_GET_COMMCONFIG      33
#define SERIAL_SET_COMMCONFIG      34
#define SERIAL_GET_STATS           35
#define SERIAL_CLEAR_STATS         36
#define SERIAL_GET_MODEM_CONTROL   37
#define SERIAL_SET_MODEM_CONTROL   38
#define SERIAL_SET_FIFO_CONTROL    39

#define SERIAL_CHAR_EOF             0
#define SERIAL_CHAR_ERROR           1
#define SERIAL_CHAR_BREAK           2
#define SERIAL_CHAR_EVENT           3
#define SERIAL_CHAR_XON             4
#define SERIAL_CHAR_XOFF            5

#define SERIAL_PURGE_TXABORT 0x00000001
#define SERIAL_PURGE_RXABORT 0x00000002
#define SERIAL_PURGE_TXCLEAR 0x00000004
#define SERIAL_PURGE_RXCLEAR 0x00000008

/* Modem Status */
#define SERIAL_MS_DTR 0x01
#define SERIAL_MS_RTS 0x02

// Serial functions: https://msdn.microsoft.com/en-us/library/windows/hardware/aa363194%28v=vs.85%29.aspx

typedef struct _PORTDATA {
  HFILE      hFile;
  TID        tid;
  int        iSockThread;
  int        iSockRD;            // RD_NTHANDLE
  uint32     uiWaitEventMask;
  uint32     uiOnLimit;
  uint32     uiOffLimit;

  // Timeouts: https://msdn.microsoft.com/en-us/library/windows/hardware/hh439614%28v=vs.85%29.aspx
  uint32     uiReadIntervalTimeout;
  uint32     uiReadTotalTimeoutMultiplier;
  uint32     uiReadTotalTimeoutConstant;
  uint32     uiWriteTotalTimeoutMultiplier;
  uint32     uiWriteTotalTimeoutConstant;
} PORTDATA, *PPORTDATA;


extern RDPDR_DEVICE    g_rdpdr_device[];

/* Returns index in g_rdpdr_device[] for given handle. */
static LONG _getSerialDeviceIndex(RD_NTHANDLE handle)
{
  ULONG      ulIdx;

  for( ulIdx = 0; ulIdx < RDPDR_MAX_DEVICES; ulIdx++ )
  {
		if ( ( g_rdpdr_device[ulIdx].device_type == DEVICE_TYPE_SERIAL ) &&
         ( g_rdpdr_device[ulIdx].handle == handle ) )
    {
      if ( ((PPORTDATA)g_rdpdr_device[ulIdx].pdevice_data)->iSockRD !=
           (int)handle )
        debug( "handle and socket numbers are different!" );

      return ulIdx;
    }
  }

  debug( "Port handle %u not found", (unsigned int)handle );
  return -1;
}

static PPORTDATA _getPortData(RD_NTHANDLE handle)
{
  LONG       lIdx = _getSerialDeviceIndex( handle );

  return lIdx != -1 ? (PPORTDATA)g_rdpdr_device[lIdx].pdevice_data : NULL;
}

static BOOL _setDTR(HFILE hFile, BOOL fOn)
{
  MODEMSTATUS          stModemStatus;
  ULONG                ulRC;
  USHORT               usRet;

  stModemStatus.fbModemOn = fOn ? DTR_ON : 0;
  stModemStatus.fbModemOff = fOn ? 0xFF : DTR_OFF;
  ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_SETMODEMCTRL,
                      &stModemStatus, sizeof(stModemStatus), NULL,
                      &usRet, sizeof(usRet), NULL );
  if ( ulRC != NO_ERROR )
    debug( "DosDevIOCtl(%u,,,,,,,,), rc = %u", hFile, ulRC );

  return ulRC == NO_ERROR;
}

static BOOL _setRTS(HFILE hFile, BOOL fOn)
{
  MODEMSTATUS          stModemStatus;
  ULONG                ulRC;
  USHORT               usRet;

  stModemStatus.fbModemOn = fOn ? RTS_ON : 0;
  stModemStatus.fbModemOff = fOn ? 0xFF : RTS_OFF;
  ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_SETMODEMCTRL,
                      &stModemStatus, sizeof(stModemStatus), NULL,
                      &usRet, sizeof(usRet), NULL );
  if ( ulRC != NO_ERROR )
    debug( "DosDevIOCtl(%u,,,,,,,,), rc = %u", hFile, ulRC );

  return ulRC == NO_ERROR;
}

/* Returns value with set/clear flags DTR_ON and RTS_ON. */
static ULONG _getDTRRTS(HFILE hFile)
{
  ULONG      ulRC;
  ULONG      ulVal = 0;

  ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETMODEMOUTPUT,
                      NULL, 0, NULL, &ulVal, sizeof(BYTE), NULL );
  if ( ulRC != NO_ERROR )
    debug( "#2 DosDevIOCtl(), rc = %u", ulRC );

  return ulVal;
}


VOID threadPort(PVOID pThreadData)
{
  PPORTDATA  pData = (PPORTDATA)pThreadData;
  ULONG      ulRC;
  CHAR       acBuf[4096];
  ULONG      ulActual;
  int        iSock, iCount;
  RXQUEUE    stQueue;

  while( ( pData->iSockRD != -1 ) && ( pData->hFile != NULLHANDLE ) )
  {
    // Socket -> port.

    iSock = pData->iSockThread;
    iCount = os2_select( &iSock, 1, 0, 0, 50 );
    if ( iCount == -1 )
    {
      if ( pData->iSockRD == -1 )
        break;

      debug( "os2_select() failed" );
    }
    else if ( iCount != 0 )
    {
      iCount = recv( pData->iSockThread, acBuf, sizeof(acBuf), 0 );
      if ( iCount < 0 )
        debug( "recv() failed" );
      else
      {
        ulRC = DosWrite( pData->hFile, acBuf, iCount, &ulActual );
        if ( ulRC != NO_ERROR )
          debug( "DosWrite(), rc = %u", ulRC );
        else if ( ulActual < iCount )
          debug( "%u bytes obtained from socket but only %u sent to port",
                 iCount, ulActual );
      }
    }

    // Port -> socket.

    ulRC = DosDevIOCtl( pData->hFile, IOCTL_ASYNC, ASYNC_GETINQUECOUNT,
                        NULL, 0, NULL, &stQueue, sizeof(stQueue), NULL );
    if ( ulRC != NO_ERROR )
    {
      if ( pData->hFile == NULLHANDLE )
        break;

      debug( "DosDevIOCtl(%u,,ASYNC_GETINQUECOUNT,,,,,,), rc = %u",
             pData->hFile, ulRC );
    }
    else if ( stQueue.cch != 0 )
    {
      ulRC = DosRead( pData->hFile, acBuf, MIN( sizeof(acBuf), stQueue.cch ),
                      &ulActual );
      if ( ulRC != NO_ERROR )
        debug( "DosRead(), rc = %u", ulRC );
      else
      {
        iCount = send( pData->iSockThread, acBuf, ulActual, 0 );
        if ( iCount < 0 )
          debug( "send() failed" );
        else if ( iCount < ulActual )
          debug( "%u bytes obtained from port but only %u sent to socket",
                 ulActual, iCount );
      }
    }
  }

  _endthread();
}


/* Enumeration of devices from rdesktop.c        */
/* returns numer of units found and initialized. */
/* optarg looks like ':com1=com1'                */
/* (remote port=local port).                     */
/* when it arrives to this function.             */
/* :com1=com1,com2=com2                          */
int serial_enum_devices(uint32 *id, char *optarg)
{
  PPORTDATA  pData;
  PCHAR      pcPos = optarg;
  PCHAR      pcPos2;
  ULONG      ulCount = 0;

  /* skip the first colon */
  optarg++;

  while( ( pcPos = next_arg( optarg, ',' ) ) && ( *id < RDPDR_MAX_DEVICES ) )
  {
    pcPos2 = next_arg( optarg, '=' );
    strcpy( g_rdpdr_device[*id].name, optarg );
    toupper_str( g_rdpdr_device[*id].name );

    g_rdpdr_device[*id].local_path = xmalloc( strlen( pcPos2 ) + 1 );
    strcpy( g_rdpdr_device[*id].local_path, pcPos2 );
    printf( "SERIAL %s to local %s\n", g_rdpdr_device[*id].name,
            g_rdpdr_device[*id].local_path );

    /* Init data structures for device */
    pData = debugCAlloc( 1, sizeof(PORTDATA) );
    pData->iSockRD = -1;

    /* set device type */
    g_rdpdr_device[*id].device_type = DEVICE_TYPE_SERIAL;
    g_rdpdr_device[*id].pdevice_data = (void *)pData;

    ulCount++;
    (*id)++;
    optarg = pcPos;
  }

  return ulCount;
}

RD_BOOL serial_get_timeout(RD_NTHANDLE handle, uint32 length, uint32 *timeout,
                           uint32 *itv_timeout)
{
  PPORTDATA  pData = _getPortData( handle );

  if ( pData == NULL )
  {
    // rdesktop calls this function in rdpdr.c/rdpdr_process_irp() for any
    // device type 8-( ) , we returns zero timeouts when handle is not a serial
    // device. Rdesktop uses not-zero timeouts to tune select() timeout.
    *timeout = 0;
    *itv_timeout = 0;
    return False; // rdesktop don't check result code.
  }

	*timeout = pData->uiReadTotalTimeoutMultiplier * length +
             pData->uiReadTotalTimeoutConstant;
	*itv_timeout = pData->uiReadIntervalTimeout;

  return True;
}

RD_BOOL serial_get_event(RD_NTHANDLE handle, uint32 *result)
{
  PPORTDATA  pData = _getPortData( handle );
  ULONG      ulRC;
  ULONG      ulEventMask = 0;

  ulRC = DosDevIOCtl( pData->hFile, IOCTL_ASYNC, ASYNC_GETCOMMEVENT,
                      NULL, 0, NULL, &ulEventMask, sizeof(USHORT), NULL );
  if ( ulRC != NO_ERROR )
    debug( "DosDevIOCtl(), rc = %u", ulRC );
  else
  {
    ulEventMask &= ( 0x01FF & pData->uiWaitEventMask );
    if ( ulEventMask != 0 )
    {
      *result = (uint32)ulEventMask;
      return True;
    }
  }

  return False;
}


static RD_NTSTATUS serial_create(uint32 device_id, uint32 access,
                                 uint32 share_mode, uint32 disposition,
                                 uint32 flags_and_attributes, char *filename,
                                 RD_NTHANDLE *handle)
{
  ULONG      ulAction;
  ULONG      ulRC;
  RXQUEUE    stQueue;
  PPORTDATA  pData = (PPORTDATA)g_rdpdr_device[device_id].pdevice_data;

  ulRC = DosOpen( g_rdpdr_device[device_id].local_path, &pData->hFile,
                  &ulAction, 0L, 0, 1, 0x12, 0L );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosOpen('%s',,,,,,,), rc = %u",
           g_rdpdr_device[device_id].local_path, ulRC );
    return RD_STATUS_ACCESS_DENIED;
  }

  // Create sockets to send/receive data with thread.
  if ( !utilPipeSock( &pData->iSockThread, &pData->iSockRD ) )
  {
    DosClose( pData->hFile );
    pData->hFile = NULLHANDLE;
    return RD_STATUS_ACCESS_DENIED;
  }

  ulRC = DosDevIOCtl( pData->hFile, IOCTL_ASYNC, ASYNC_GETINQUECOUNT,
                      NULL, 0, NULL, &stQueue, sizeof(stQueue), NULL );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosDevIOCtl(%u,,ASYNC_GETINQUECOUNT,,,,,,), rc = %u",
           pData->hFile, ulRC );
  }
  else
  {
    int      iVal = stQueue.cb;

    if ( setsockopt( pData->iSockThread, SOL_SOCKET, SO_SNDBUF, (char *)&iVal,
                     sizeof(int) ) == -1 ||
         setsockopt( pData->iSockRD, SOL_SOCKET, SO_RCVBUF, (char *)&iVal,
                     sizeof(int) ) == -1 )
      error( "setsockopt: %s\n", strerror(errno) );
  }

  ulRC = DosDevIOCtl( pData->hFile, IOCTL_ASYNC, ASYNC_GETOUTQUECOUNT,
                      NULL, 0, NULL, &stQueue, sizeof(stQueue), NULL );
  if ( ulRC != NO_ERROR )
  {
    debug( "DosDevIOCtl(%u,,ASYNC_GETINQUECOUNT,,,,,,), rc = %u",
           pData->hFile, ulRC );
  }
  else
  {
    int      iVal = stQueue.cb;

    if ( setsockopt( pData->iSockThread, SOL_SOCKET, SO_RCVBUF, (char *)&iVal,
                     sizeof(int) ) == -1 ||
         setsockopt( pData->iSockRD, SOL_SOCKET, SO_SNDBUF, (char *)&iVal,
                     sizeof(int) ) == -1 )
      error( "setsockopt: %s\n", strerror(errno) );
  }

  /* Store handle for later use. Handle should be socket handle to be used in
     select() by rdesktop. */
  g_rdpdr_device[device_id].handle = (RD_NTHANDLE)pData->iSockRD;
  *handle = (RD_NTHANDLE)pData->iSockRD;

  pData->tid = _beginthread( threadPort, NULL, 65535, pData );
  if ( pData->tid == ((TID)(-1)) )
  {
    error( "Cannot start thread.\n" );
    soclose( pData->iSockThread );
    soclose( pData->iSockRD );
    pData->iSockRD = -1;
    DosClose( pData->hFile );
    pData->hFile = NULLHANDLE;
    pData->tid = 0;
    return RD_STATUS_ACCESS_DENIED;
  }

  ulRC = 1;
  ioctl( pData->iSockRD, FIONBIO, (char *)&ulRC );

  return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS serial_close(RD_NTHANDLE handle)
{
  ULONG      ulRC;
  LONG       lIdx = _getSerialDeviceIndex( handle );
  PPORTDATA  pData;
  HFILE      hFile;
  int        iSockRD;

  if ( lIdx >= 0 )
    g_rdpdr_device[lIdx].handle = NULLHANDLE;

  rdpdr_abort_io( handle, 0, RD_STATUS_TIMEOUT );

  pData = (PPORTDATA)g_rdpdr_device[lIdx].pdevice_data;
  hFile = pData->hFile;
  iSockRD = pData->iSockRD;

  // Signals for the thread to exit.
  pData->iSockRD = -1;
  pData->hFile = NULLHANDLE;

  soclose( pData->iSockThread );
  ulRC = 0;
	ioctl( iSockRD, FIONBIO, (char *)&ulRC );
  soclose( iSockRD );

  ulRC = DosClose( hFile );
  if ( ulRC != NO_ERROR )
    debug( "DosClose(), rc = %u", ulRC );

  ulRC = DosWaitThread( &pData->tid, DCWW_WAIT );
  if ( ( ulRC != NO_ERROR ) && ( ulRC != ERROR_INVALID_THREADID ) )
    debug( "DosWaitThread(), rc = %u", ulRC );
  pData->tid = 0;

  return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS serial_read(RD_NTHANDLE handle, uint8 *data, uint32 length,
                               uint32 offset, uint32 *result)
{
  PPORTDATA  pData = _getPortData( handle );
  int        iCount = pData == NULL ?
                        -1 : recv( pData->iSockRD, data, length, 0 );

  if ( iCount == -1 )
    return RD_STATUS_INVALID_PARAMETER;

  *result = (uint32)iCount;
  return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS serial_write(RD_NTHANDLE handle, uint8 *data, uint32 length,
                                uint32 offset, uint32 *result)
{
  PPORTDATA  pData = _getPortData( handle );
  int        iCount = pData == NULL ?
                        -1 : send( pData->iSockRD, data, length, 0 );

  if ( iCount == -1 )
    return RD_STATUS_INVALID_PARAMETER;

  *result = (uint32)iCount;
  return RD_STATUS_SUCCESS;
}

static RD_NTSTATUS serial_device_control(RD_NTHANDLE handle, uint32 request,
                                         STREAM in, STREAM out)
{
  PPORTDATA  pData;
  HFILE      hFile;
  ULONG      ulRC;

  if ( (request >> 16) != FILE_DEVICE_SERIAL_PORT )
          return RD_STATUS_INVALID_PARAMETER;

  /* extract operation */
  request >>= 2;
  request &= 0xfff;

  pData = _getPortData( handle );
  if ( pData == NULL )
		return RD_STATUS_INVALID_HANDLE;

  hFile = pData->hFile;

  switch( request )
  {
     case SERIAL_SET_BAUD_RATE:
     {
       uint32         uiBaudRate;

       in_uint32_le( in, uiBaudRate );

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_SETBAUDRATE,
                           &uiBaudRate, sizeof(USHORT), NULL, NULL, 0L, NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_SETBAUDRATE,,,,,,), rc = %u", ulRC );

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_BAUD_RATE %d\n", uiBaudRate));
     }
     break;

     case SERIAL_GET_BAUD_RATE:
     {
       uint32         uiBaudRate;

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETBAUDRATE,
                           NULL, 0, NULL, &uiBaudRate, sizeof(USHORT), NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_GETBAUDRATE,,,,,,), rc = %u", ulRC );
       else
         out_uint32_le( out, uiBaudRate );
  
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_GET_BAUD_RATE %d\n", uiBaudRate));
       break;
     }

     case SERIAL_SET_QUEUE_SIZE:
     {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
       uint32         uiInSize, uiOutSize;

       in_uint32_le( in, uiInSize );
       in_uint32_le( in, uiOutSize );
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_QUEUE_SIZE in %d out %d\n",
                         uiInSize, uiOutSize));
#pragma GCC diagnostic pop
       break;
     }

     case SERIAL_SET_LINE_CONTROL:
     {
       LINECONTROL    stLineCtl;

       in_uint8( in, stLineCtl.bStopBits );
       in_uint8( in, stLineCtl.bParity );
       in_uint8( in, stLineCtl.bDataBits );
       stLineCtl.fTransBreak = 0;

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_SETLINECTRL, &stLineCtl,
                           sizeof(stLineCtl), NULL, NULL, 0, NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_SETLINECTRL,,,,,,), rc = %u", ulRC );

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_LINE_CONTROL stop %d parity "
                     "%d word %d\n", stLineCtl.bStopBits, stLineCtl.bParity,
                     stLineCtl.bDataBits ));
       break;
     }

     case SERIAL_GET_LINE_CONTROL:
     {
       LINECONTROL    stLineCtl;

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETLINECTRL, NULL,
                           0, NULL, &stLineCtl, sizeof(stLineCtl), NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_GETLINECTRL,,,,,,), rc = %u", ulRC );
       else
       {
         out_uint8( out, stLineCtl.bStopBits );
         out_uint8( out, stLineCtl.bParity );
         out_uint8( out, stLineCtl.bDataBits );
       }
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_GET_LINE_CONTROL\n"));
       break;
     }

     case SERIAL_IMMEDIATE_CHAR:
     {
       uint8          uiChar;

       in_uint8( in, uiChar );

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_TRANSMITIMM, &uiChar,
                           1, NULL, NULL, 0, NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_GETLINECTRL,,,,,,), rc = %u", ulRC );

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_IMMEDIATE_CHAR\n"));
       break;
     }

     case SERIAL_CONFIG_SIZE:
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_CONFIG_SIZE\n"));
       out_uint32_le( out, 0 );
       break;

     case SERIAL_GET_CHARS:
     {
       DCBINFO        stDCB;

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETDCBINFO,
                           NULL, 0, NULL, &stDCB, sizeof(stDCB), NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_GETDCBINFO,,,,,,), rc = %u", ulRC );
       else
       {
         out_uint8( out, 0 );               // SERIAL_CHAR_EOF   0
         out_uint8( out, stDCB.bErrorReplacementChar ); // SERIAL_CHAR_ERROR	1
         out_uint8( out, stDCB.bBreakReplacementChar ); // SERIAL_CHAR_BREAK	2
         out_uint8( out, 0 );               // SERIAL_CHAR_EVENT 3
         out_uint8( out, stDCB.bXONChar );  // SERIAL_CHAR_XON   4
         out_uint8( out, stDCB.bXOFFChar ); // SERIAL_CHAR_XOFF  5
       }

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_GET_CHARS\n"));
       break;
     }

     case SERIAL_SET_CHARS:
     {
       DCBINFO        stDCB;
       uint8          auiChars[6];

       in_uint8a( in, auiChars, 6 );

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETDCBINFO,
                           NULL, 0, NULL, &stDCB, sizeof(stDCB), NULL );
       if ( ulRC != NO_ERROR )
       {
         debug( "DosDevIOCtl(,,ASYNC_GETDCBINFO,,,,,,), rc = %u", ulRC );
         break;
       }

       stDCB.bErrorReplacementChar = auiChars[SERIAL_CHAR_ERROR];
       stDCB.bBreakReplacementChar = auiChars[SERIAL_CHAR_BREAK];
       stDCB.bXONChar = auiChars[SERIAL_CHAR_XON];
       stDCB.bXOFFChar = auiChars[SERIAL_CHAR_XOFF];

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_SETDCBINFO,
                           &stDCB, sizeof(stDCB), NULL, NULL, 0, NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_SETDCBINFO,,,,,,), rc = %u", ulRC );

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_CHARS\n"));
#ifdef WITH_DEBUG_SERIAL
       hexdump( auiChars, 6 );
#endif
       break;
     }

     case SERIAL_GET_HANDFLOW:
     {
       DCBINFO        stDCB;
       uint32         uiVal;

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETDCBINFO, NULL, 0,
                           NULL, &stDCB, sizeof(stDCB), NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_GETDCBINFO,,,,,,), rc = %u", ulRC );
       else
       {
         uiVal = stDCB.fbCtlHndShake;
         out_uint32_le( out, uiVal );
         uiVal = stDCB.fbFlowReplace;
         out_uint32_le( out, uiVal );
         out_uint32_le( out, pData->uiOnLimit );
         out_uint32_le( out, pData->uiOffLimit );
       }
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_GET_HANDFLOW\n"));
       break;
     }

     case SERIAL_SET_HANDFLOW:
     {
       DCBINFO        stDCB;
       uint32         uiVal1, uiVal2;

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETDCBINFO, NULL, 0,
                           NULL, &stDCB, sizeof(stDCB), NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_GETDCBINFO,,,,,,), rc = %u", ulRC );

       in_uint32_le( in, uiVal1 );
       stDCB.fbCtlHndShake = uiVal1;
       in_uint32_le( in, uiVal2 );
       stDCB.fbFlowReplace = uiVal2;
       in_uint32_le( in, pData->uiOnLimit );
       in_uint32_le( in, pData->uiOffLimit );

       if ( ulRC == NO_ERROR )
       {
         ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_SETDCBINFO, &stDCB,
                             sizeof(stDCB), NULL, NULL, 0, NULL );
         if ( ulRC != NO_ERROR )
           debug( "DosDevIOCtl(,,ASYNC_SETDCBINFO,,,,,,), rc = %u", ulRC );
       }
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_HANDFLOW %x %x %x %x\n",
                     uiVal1, uiVal2, pData->uiOnLimit, pData->uiOffLimit));
       break;
     }

     case SERIAL_SET_TIMEOUTS:
     {
       DCBINFO        stDCB;

       in_uint32( in, pData->uiReadIntervalTimeout );
       in_uint32( in, pData->uiReadTotalTimeoutMultiplier );
       in_uint32( in, pData->uiReadTotalTimeoutConstant );
       in_uint32( in, pData->uiWriteTotalTimeoutMultiplier );
       in_uint32( in, pData->uiWriteTotalTimeoutConstant );

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETDCBINFO, NULL, 0,
                           NULL, &stDCB, sizeof(stDCB), NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_GETDCBINFO,,,,,,), rc = %u", ulRC );

       if ( ( pData->uiWriteTotalTimeoutMultiplier == 0 ) &&
            ( pData->uiWriteTotalTimeoutConstant == 0 ) )
       {
         stDCB.fbTimeout |= MODE_NO_WRITE_TIMEOUT;
       }
       else
       {
         stDCB.usWriteTimeout = ( ( pData->uiWriteTotalTimeoutMultiplier * 5 )
                                + pData->uiWriteTotalTimeoutConstant ) / 10;
         stDCB.fbTimeout &= ~MODE_NO_WRITE_TIMEOUT;
       }

       if ( ( pData->uiReadIntervalTimeout == 0 ) &&
            ( pData->uiReadTotalTimeoutMultiplier == 0 ) &&
            ( pData->uiReadTotalTimeoutConstant == 0 ) )
       {
         stDCB.fbTimeout |= MODE_NOWAIT_READ_TIMEOUT;
       }
       else
       {
         stDCB.fbTimeout &= ~MODE_READ_TIMEOUT;
         stDCB.fbTimeout |= MODE_WAIT_READ_TIMEOUT;
         stDCB.usReadTimeout =
           ( pData->uiReadIntervalTimeout != 0 ? pData->uiReadIntervalTimeout :
                                ( pData->uiReadTotalTimeoutMultiplier * 5 )
                                + pData->uiReadTotalTimeoutConstant ) / 10;
       }

       if ( ulRC == NO_ERROR )
       {
         ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_SETDCBINFO, &stDCB,
                             sizeof(stDCB), NULL, NULL, 0, NULL );
         if ( ulRC != NO_ERROR )
           debug( "DosDevIOCtl(,,ASYNC_SETDCBINFO,,,,,,), rc = %u", ulRC );
       }

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_TIMEOUTS read timeout %d %d %d\n",
            pData->uiReadIntervalTimeout, pData->uiReadTotalTimeoutMultiplier,
            pData->uiReadTotalTimeoutConstant));
       break;
     }

     case SERIAL_GET_TIMEOUTS:
     {
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_GET_TIMEOUTS read timeout %d %d %d\n",
            pData->uiReadIntervalTimeout, pData->uiReadTotalTimeoutMultiplier,
            pData->uiReadTotalTimeoutConstant));

       out_uint32( out, pData->uiReadIntervalTimeout );
       out_uint32( out, pData->uiReadTotalTimeoutMultiplier );
       out_uint32( out, pData->uiReadTotalTimeoutConstant );
       out_uint32( out, pData->uiWriteTotalTimeoutMultiplier );
       out_uint32( out, pData->uiWriteTotalTimeoutConstant );
       break;
     }

     case SERIAL_GET_WAIT_MASK:
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_GET_WAIT_MASK %X\n",
                   pData->uiWaitEventMask));
       out_uint32( out, pData->uiWaitEventMask );
       break;

     case SERIAL_SET_WAIT_MASK:
       in_uint32( in, pData->uiWaitEventMask );
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_WAIT_MASK %X\n",
                  pData->uiWaitEventMask));
       break;

     case SERIAL_SET_DTR:
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_DTR\n"));
       _setDTR( hFile, TRUE );
       break;

     case SERIAL_CLR_DTR:
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_CLR_DTR\n"));
       _setDTR( hFile, FALSE );
       break;

     case SERIAL_SET_RTS:
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_RTS\n"));
       _setRTS( hFile, TRUE );
       break;

     case SERIAL_CLR_RTS:
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_CLR_RTS\n"));
       _setRTS( hFile, FALSE );
       break;

     case SERIAL_GET_MODEMSTATUS:
     {
       uint32         uiVal = 0;
       ULONG          ulDTRRTS;

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETMODEMINPUT,
                           NULL, 0, NULL, &uiVal, sizeof(USHORT), NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_GETMODEMINPUT,,,,,,), rc = %u", ulRC );
       else
       {
         ulDTRRTS = _getDTRRTS( hFile );
         if ( (ulDTRRTS & DTR_ON) != 0 )
           uiVal |= SERIAL_MS_DTR;
         if ( (ulDTRRTS & RTS_ON) != 0 )
           uiVal |= SERIAL_MS_RTS;

         out_uint32_le( out, uiVal );
       }

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_GET_MODEMSTATUS %X\n", uiVal));
       break;
     }

     case SERIAL_GET_COMMSTATUS:
     {
       RXQUEUE        stQueue;
       int            iVal1, iVal2;
       uint8          ui8Val3 = 0;
       CHAR           acBuf[4096];

       // For RD, number of bytes in input queue is number of bytes in socket,
       // i.e. we reports how many bytes can be readed by RD right not.
       iVal1 = recv( pData->iSockRD, acBuf, sizeof(acBuf),
                     MSG_PEEK | MSG_DONTWAIT );
       if ( iVal1 == -1 )
         iVal1 = 0;

       // For RD, number of bytes in output queue is number of bytes in socket
       // plus number of bytes in serial driver's queue.

       iVal2 = recv( pData->iSockThread, acBuf, sizeof(acBuf),
                     MSG_PEEK | MSG_DONTWAIT );
       if ( (int)iVal2 == -1 )
         iVal2 = 0;

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETOUTQUECOUNT,
                           NULL, 0, NULL, &stQueue, sizeof(stQueue), NULL );
       if ( ulRC != NO_ERROR )
       {
         debug( "DosDevIOCtl(,,ASYNC_GETINQUECOUNT,,,,,,), rc = %u", ulRC );
         break;
       }
       iVal2 += stQueue.cch;

       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_GETCOMMSTATUS,
                           NULL, 0, NULL, &ui8Val3, 1, NULL );
       if ( ulRC != NO_ERROR )
       {
         debug( "DosDevIOCtl(,,ASYNC_GETCOMMSTATUS,,,,,,), rc = %u", ulRC );
         break;
       }
       ui8Val3 &= TX_WAITING_TO_SEND_IMM;
       ui8Val3 >>= 6;

       out_uint32_le( out, 0 );       /* Errors */
       out_uint32_le( out, 0 );       /* Hold reasons */
       out_uint32_le( out, iVal1 );   /* Amount in in queue */
       out_uint32_le( out, iVal2 );   /* Amount in out queue */
       out_uint8( out, 0 );           /* EofReceived */
       out_uint8( out, ui8Val3 );     /* WaitForImmediate */
           DEBUG_SERIAL(("serial_ioctl -> SERIAL_GET_COMMSTATUS queue in: %d, "
                     "out: %d, wait imm: %d\n", iVal1, iVal2, ui8Val3));
       break;
     }

     case SERIAL_PURGE:
     {
       uint32         uiPurgeMask;
       UCHAR          ucParam = 0;
       ULONG          ulParamLen = 0, ulDataLen = 0;
       USHORT         usData = 0;

       in_uint32( in, uiPurgeMask );
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_PURGE purge_mask %X\n", uiPurgeMask));

       if ( (uiPurgeMask & (SERIAL_PURGE_TXCLEAR | SERIAL_PURGE_TXABORT)) != 0 )
       {
         ulRC = DosDevIOCtl( hFile, IOCTL_GENERAL, DEV_FLUSHOUTPUT,
                             &ucParam, sizeof(ucParam), &ulParamLen,
                             &usData, sizeof(usData), &ulDataLen );
         if ( ulRC != NO_ERROR )
           debug( "DosDevIOCtl(,IOCTL_GENERAL,DEV_FLUSHOUTPUT,,,,,,), rc = %u", ulRC );
       }

       if ( (uiPurgeMask & (SERIAL_PURGE_RXCLEAR | SERIAL_PURGE_RXABORT)) != 0 )
       {
         ulRC = DosDevIOCtl( hFile, IOCTL_GENERAL, DEV_FLUSHINPUT,
                             &ucParam, sizeof(ucParam), &ulParamLen,
                             &usData, sizeof(usData), &ulDataLen );
         if ( ulRC != NO_ERROR )
           debug( "DosDevIOCtl(,IOCTL_GENERAL,DEV_FLUSHINPUT,,,,,,), rc = %u", ulRC );
       }

       if ( (uiPurgeMask & SERIAL_PURGE_TXABORT) != 0 )
         rdpdr_abort_io( hFile, 4, RD_STATUS_CANCELLED );
       if ( (uiPurgeMask & SERIAL_PURGE_RXABORT) != 0 )
         rdpdr_abort_io( hFile, 3, RD_STATUS_CANCELLED );
       break;
     }

     case SERIAL_WAIT_ON_MASK:
     {
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_WAIT_ON_MASK\n"));

       uint32         uiResult;

       if ( serial_get_event( handle, &uiResult ) )
       {
         DEBUG_SERIAL(("WAIT end  event = %x\n", uiResult));
         out_uint32_le( out, uiResult );
         break;
       }
       return RD_STATUS_PENDING;
     }

     case SERIAL_SET_BREAK_ON:
     {
       USHORT         usErr;
        
       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_SETBREAKON,
                           NULL, 0, NULL, &usErr, sizeof(usErr), NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_SETBREAKON,,,,,,), rc = %u", ulRC );

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_BREAK_ON\n"));
       break;
     }

     case SERIAL_SET_BREAK_OFF:
     {
       USHORT         usErr;
        
       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_SETBREAKOFF,
                           NULL, 0, NULL, &usErr, sizeof(usErr), NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_SETBREAKON,,,,,,), rc = %u", ulRC );

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_BREAK_OFF\n"));
       break;
     }

     case SERIAL_RESET_DEVICE:
       DEBUG_SERIAL(("serial_ioctl -> SERIAL_RESET_DEVICE\n"));
       break;

     case SERIAL_SET_XOFF:
       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_STOPTRANSMIT,
                           NULL, 0, NULL, NULL, 0, NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_STOPTRANSMIT,,,,,,), rc = %u", ulRC );

       DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_XOFF\n"));
       break;

     case SERIAL_SET_XON:
       ulRC = DosDevIOCtl( hFile, IOCTL_ASYNC, ASYNC_STARTTRANSMIT,
                           NULL, 0, NULL, NULL, 0, NULL );
       if ( ulRC != NO_ERROR )
         debug( "DosDevIOCtl(,,ASYNC_STARTTRANSMIT,,,,,,), rc = %u", ulRC );

          DEBUG_SERIAL(("serial_ioctl -> SERIAL_SET_XON\n"));
       break;

     default:
       unimpl("SERIAL IOCTL %d\n", request);
       return RD_STATUS_INVALID_PARAMETER;
  }

  return RD_STATUS_SUCCESS;
}


DEVICE_FNS serial_fns = {
  serial_create,
  serial_close,
  serial_read,
  serial_write,
  serial_device_control
};
