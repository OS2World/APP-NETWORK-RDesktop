/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Sound Channel Process Functions - alsa-driver
   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 2003-2008
   Copyright (C) GuoJunBo <guojunbo@ict.ac.cn> 2003
   Copyright (C) Michael Gernoth <mike@zerfleddert.de> 2006-2008
   Copyright 2006-2008 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include "rdesktop.h"
#include "rdpsnd.h"
#include "rdpsnd_dsp.h"
#include "debug.h"

#define  INCL_OS2MM
#define  INCL_DOS
#define  INCL_DOSERRORS
#include <os2.h>
#include <os2me.h>
#include "os2rd.h"

// We use only two audio buffers. Real buffering will be released in
// pcMidBufOut - buffer for data transmission between rdesktop's main thread
// and system audio threads.
#define NUM_BUFFERS     2

static BOOL                      fShareAudio = FALSE;

static USHORT                    usDeviceOutId = ~0;
static MCI_MIXSETUP_PARMS        stMCIMixOutSetup = { 0 };
static MCI_MIX_BUFFER            aOutBuffers[NUM_BUFFERS] = {{ 0 }};
static PCHAR                     pcMidBufOut = NULL;
static ULONG                     cbMidBufOut = 0;
static ULONG                     ulMidBufOutWritePos;
static ULONG                     ulMidBufOutReadPos;
static HMTX                      hmtxBufOut = NULLHANDLE;
static BOOL                      fReopened = FALSE;
static struct audio_driver       stDartDriver = { 0 };


static VOID _MCIError(PCHAR pszFunc, ULONG ulResult)
{
  CHAR			acBuf[128];

  mciGetErrorString( ulResult, (PSZ)&acBuf, sizeof(acBuf) );
  warning( "[%s] %s\n", pszFunc, &acBuf );
}

LONG APIENTRY cbAudioOutEvent(ULONG ulStatus, PMCI_MIX_BUFFER pMixBuffer,
                              ULONG ulFlags)
{
  ULONG      cbRead;

  if ( ulFlags != MIX_WRITE_COMPLETE )
    debug( "flags = 0x%X\n", ulFlags );

  DosRequestMutexSem( hmtxBufOut, SEM_INDEFINITE_WAIT );
  pMixBuffer = &aOutBuffers[ pMixBuffer->ulUserParm ];

  // Copy data from buffer to the system audio buffer. Data amount may be
  // insufficient (low network speed, high system load, e.t.c).

  if ( ulMidBufOutWritePos > pMixBuffer->ulBufferLength )
    cbRead = pMixBuffer->ulBufferLength;
  else
  {
    // Copy the whole samples (do not brake trailing sample).
    ULONG cbSample = (stMCIMixOutSetup.ulBitsPerSample >> 3) *
                     stMCIMixOutSetup.ulChannels;
    cbRead = ( ulMidBufOutWritePos % cbSample ) * cbSample;

    memset( &((PCHAR)pMixBuffer->pBuffer)[cbRead], 0,
            pMixBuffer->ulBufferLength - cbRead );
  }

  memcpy( pMixBuffer->pBuffer, pcMidBufOut, cbRead );
  ulMidBufOutWritePos -= cbRead;
  memcpy( pcMidBufOut, &pcMidBufOut[cbRead], ulMidBufOutWritePos );

  // Send filled audio buffer to the audio system.
  stMCIMixOutSetup.pmixWrite( stMCIMixOutSetup.ulMixHandle,
                              pMixBuffer, 1 );
  DosReleaseMutexSem( hmtxBufOut );

  return 1; // It seems, return value is not matter.
}

static VOID _audioClose()
{
  MCI_GENERIC_PARMS     sMCIGenericParms;
  MCI_BUFFER_PARMS      stMCIBuffer;
  ULONG                 ulRC;

  /* Ack all remaining packets */
	while( !rdpsnd_queue_empty() )
    rdpsnd_queue_next( 0 );

  /* Close up audio */

  if ( usDeviceOutId != (USHORT)~0 )
  {
    // Device is open.

    if ( stMCIMixOutSetup.ulBitsPerSample != 0 )
    {
      // Mixer was initialized.
      ulRC = mciSendCommand( usDeviceOutId, MCI_MIXSETUP,
                             MCI_WAIT | MCI_MIXSETUP_DEINIT,
                             &stMCIMixOutSetup, 0 );
      if ( ulRC != MCIERR_SUCCESS )
        debug( "MCI_MIXSETUP, MCI_MIXSETUP_DEINIT - failed" );
    }

    if ( aOutBuffers[0].ulBufferLength != 0 )
    {
      // Buffers was allocated.

      stMCIBuffer.ulNumBuffers = NUM_BUFFERS;
      stMCIBuffer.pBufList = &aOutBuffers;

      ulRC = mciSendCommand( usDeviceOutId, MCI_BUFFER,
                             MCI_WAIT | MCI_DEALLOCATE_MEMORY, &stMCIBuffer, 0 );
      if ( ulRC != MCIERR_SUCCESS )
        debug( "MCI_BUFFER, MCI_DEALLOCATE_MEMORY - failed" );

      aOutBuffers[0].ulBufferLength = 0;
    }

    ulRC = mciSendCommand( usDeviceOutId, MCI_CLOSE, MCI_WAIT,
                           &sMCIGenericParms, 0 );
    if ( ulRC != MCIERR_SUCCESS )
      debug( "MCI_CLOSE - failed" );

    usDeviceOutId = (USHORT)~0;
  }

  if ( hmtxBufOut != NULLHANDLE )
  {
    DosCloseMutexSem( hmtxBufOut );
    hmtxBufOut = NULLHANDLE;
  }

  if ( pcMidBufOut != NULL )
  {
    debugFree( pcMidBufOut );
    pcMidBufOut = NULL;
  }
}

static BOOL _audioOutOpen()
{
  MCI_AMP_OPEN_PARMS   stMCIAmpOpen = { 0 };
  MCI_BUFFER_PARMS     stMCIBuffer = { 0 };
  ULONG                ulRC;
  ULONG                ulIdx;

  // Open audio device
  stMCIAmpOpen.usDeviceID = 0;
  stMCIAmpOpen.pszDeviceType = (PSZ)MCI_DEVTYPE_AUDIO_AMPMIX;
  ulRC = mciSendCommand( 0, MCI_OPEN,
                         fShareAudio
                           ? MCI_WAIT | MCI_OPEN_TYPE_ID | MCI_OPEN_SHAREABLE
                           : MCI_WAIT | MCI_OPEN_TYPE_ID,
                         &stMCIAmpOpen,  0 );
  if ( ulRC != MCIERR_SUCCESS )
  {
    _MCIError( "MCI_OPEN", ulRC );
    usDeviceOutId = (USHORT)~0;
    _audioClose();
    return FALSE;
  }
  usDeviceOutId = stMCIAmpOpen.usDeviceID;

  // Setup mixer.
  ulRC = mciSendCommand( usDeviceOutId, MCI_MIXSETUP,
                         MCI_WAIT | MCI_MIXSETUP_INIT, &stMCIMixOutSetup, 0 );
  if ( ulRC != MCIERR_SUCCESS )
  {
    _MCIError( "MCI_MIXSETUP", ulRC );
    stMCIMixOutSetup.ulBitsPerSample = 0;
    _audioClose();
    return FALSE;
  }

  // Allocate memory buffers
  stMCIBuffer.ulBufferSize = 
             ( ( (stMCIMixOutSetup.ulBitsPerSample >> 3) *
                  stMCIMixOutSetup.ulSamplesPerSec *
                  stMCIMixOutSetup.ulChannels              ) / 1000 ) * 200;
  stMCIBuffer.ulNumBuffers = NUM_BUFFERS;
  stMCIBuffer.pBufList     = &aOutBuffers;
  ulRC = mciSendCommand( usDeviceOutId, MCI_BUFFER,
                         MCI_WAIT | MCI_ALLOCATE_MEMORY, &stMCIBuffer, 0 );
  if ( ulRC != MCIERR_SUCCESS )
  {
    _MCIError( "MCI_BUFFER", ulRC );
    stMCIBuffer.ulBufferSize = 0;
    _audioClose();
    return FALSE;
  }

  // Fill all device buffers with data.
  for( ulIdx = 0; ulIdx < stMCIBuffer.ulNumBuffers; ulIdx++ )
  {
    aOutBuffers[ulIdx].ulFlags        = 0;
    aOutBuffers[ulIdx].ulUserParm     = ulIdx + 1;
    // Set silence value for initial buffer data.
    memset( ((PMCI_MIX_BUFFER)stMCIBuffer.pBufList)[ulIdx].pBuffer,
            stMCIMixOutSetup.ulBitsPerSample == 8 ? 0x80 : 0,
            stMCIBuffer.ulBufferSize );
  }
  aOutBuffers[stMCIBuffer.ulNumBuffers - 1].ulUserParm = 0;

  if ( hmtxBufOut == NULLHANDLE )
  {
    ulRC = DosCreateMutexSem( NULL, &hmtxBufOut, 0, FALSE );
    if ( ulRC != NO_ERROR )
    {
      debug( "DosCreateMutexSem(), rc = %u", ulRC );
      _audioClose();
      return FALSE;
    }
  }

  if ( pcMidBufOut != NULL )
    debugFree( pcMidBufOut );

  cbMidBufOut = stMCIBuffer.ulBufferSize * stMCIBuffer.ulNumBuffers;
  pcMidBufOut = debugMAlloc( cbMidBufOut );
  if ( pcMidBufOut == NULL )
  {
    debug( "Not enough memory" );
    _audioClose();
    return FALSE;
  }
  ulMidBufOutReadPos = 0;
  ulMidBufOutWritePos = 0;

  fReopened = TRUE;
  stMCIMixOutSetup.pmixWrite( stMCIMixOutSetup.ulMixHandle,
                              aOutBuffers, NUM_BUFFERS );

  return TRUE;
}

/*
static ULONG _audioOutGetPos()
{
  ULONG                    ulRC;
  MCI_STATUS_PARMS         stMCIStatus = { 0 };

  stMCIStatus.ulItem = MCI_STATUS_POSITION;
  ulRC = mciSendCommand( usDeviceOutId, MCI_STATUS,
                         MCI_STATUS_ITEM | MCI_WAIT, &stMCIStatus, 0 );
  if ( ULONG_LOWD(ulRC) != MCIERR_SUCCESS )
  {
    _MCIError( "MCI_MIXSETUP", ulRC );
    return 0;
  }

  return stMCIStatus.ulReturn;
}
*/

static int _audioOutFillBuffer(PBYTE pbData, ULONG cbData)
{
  ULONG                cbWrite;
  int                  cbActual = 0;

  DosRequestMutexSem( hmtxBufOut, SEM_INDEFINITE_WAIT );

  cbWrite = (cbMidBufOut - ulMidBufOutWritePos);
  if ( cbWrite > cbData )
    cbWrite = cbData;
  memcpy( &pcMidBufOut[ulMidBufOutWritePos], pbData, cbWrite );
  cbActual += cbWrite;
  ulMidBufOutWritePos += cbWrite;

  DosReleaseMutexSem( hmtxBufOut );

  return cbActual;
}


void dart_add_fds(int *n, fd_set * rfds, fd_set * wfds, struct timeval *tv)
{
}

/* Ported from rdpsnd_libao.c... */
void dart_check_fds(fd_set *rfds, fd_set *wfds)
{
  struct audio_packet  *pPacket;
  STREAM               out;
  int                  iNextTick, iLen;
  struct timeval       stTV;
  ULONG                ulDuration;
  static long          lPrevS, lPrevUs;

  if ( usDeviceOutId == (USHORT)~0 )
    return;

  if ( fReopened )
  {
		gettimeofday( &stTV, NULL );
		lPrevS = stTV.tv_sec;
		lPrevUs = stTV.tv_usec;
    fReopened = FALSE;
  }

  if ( !rdpsnd_queue_empty() )
  {
    pPacket = rdpsnd_queue_current_packet();
    out = &pPacket->s;

    iNextTick = rdpsnd_queue_next_tick();

    iLen = _audioOutFillBuffer( (PBYTE)out->p, out->end - out->p );
    out->p += iLen;

#if 0
    if ( out->p == out->end )
      rdpsnd_queue_next( iNextTick - pPacket->tick );
#else
    gettimeofday( &stTV, NULL );

    ulDuration = ( (stTV.tv_sec - lPrevS) * 1000000 + (stTV.tv_usec - lPrevUs) )
                 / 1000;

    if ( pPacket->tick > iNextTick )
      iNextTick += 65536;

    if ( (out->p == out->end) || ulDuration > (iNextTick - pPacket->tick + 500) )
    {
      unsigned int       uiDelayUs;

      lPrevS = stTV.tv_sec;
      lPrevUs = stTV.tv_usec;

      if ( abs( (iNextTick - pPacket->tick ) - ulDuration ) > 20 )
      {
        DEBUG(("duration: %d, calc: %d, ", ulDuration, iNextTick - pPacket->tick) );
        DEBUG(("last: %d, is: %d, should: %d\n", pPacket->tick,
               (pPacket->tick + ulDuration) % 65536, iNextTick % 65536));
      }

      uiDelayUs = ( out->size / ( (stMCIMixOutSetup.ulBitsPerSample >> 3) *
                                  stMCIMixOutSetup.ulChannels )
                  ) * ( 1000000 / stMCIMixOutSetup.ulSamplesPerSec );

      rdpsnd_queue_next( uiDelayUs );
    }
#endif
  }
}


void dart_close_out(void)
{
  _audioClose();
}

RD_BOOL dart_open_out(void)
{
  // We will open audio output in dart_set_format_out().
  return True;
}

RD_BOOL dart_format_supported(RD_WAVEFORMATEX *pwfx)
{
  if ( pwfx->wFormatTag != WAVE_FORMAT_PCM )
    return False;

  if ( ( pwfx->nChannels != 1 ) && ( pwfx->nChannels != 2 ) )
    return False;

  debug( "channels: %u, bps: %u, samples: %u",
         pwfx->nChannels, pwfx->wBitsPerSample, pwfx->nSamplesPerSec );

  if ( ( pwfx->wBitsPerSample != 8 ) && ( pwfx->wBitsPerSample != 16 ) )
    return False;

/*	if ((pwfx->nSamplesPerSec != 44100) && (pwfx->nSamplesPerSec != 22050))
    return False;*/

  return True;
}

RD_BOOL dart_set_format_out(RD_WAVEFORMATEX *pwfx)
{
  debug( "BPS: %u, samples per sec.: %u, channels: %u",
         pwfx->wBitsPerSample, pwfx->nSamplesPerSec, pwfx->nChannels );

  _audioClose();

  // Fill data for the mixer.
  stMCIMixOutSetup.ulBitsPerSample = pwfx->wBitsPerSample;
  stMCIMixOutSetup.ulSamplesPerSec = pwfx->nSamplesPerSec;
  stMCIMixOutSetup.ulChannels      = pwfx->nChannels;
  _audioOutOpen();

  return True;
}

void dart_volume(uint16 left, uint16 right)
{
  MCI_SET_PARMS        stMCISet = { 0 };
  ULONG                ulRC;
  ULONG                ulLeft  = left / (65536 / 100);
  ULONG                ulRight = right / (65536 / 100);

  if ( usDeviceOutId == ~0 )
    return;

  stMCISet.ulAudio = MCI_SET_AUDIO_ALL;
  stMCISet.ulLevel = MAX( ulLeft, ulRight );

  ulRC = mciSendCommand( usDeviceOutId,
                  MCI_SET,
                  MCI_SET_AUDIO | MCI_SET_VOLUME | MCI_WAIT,
                  &stMCISet, 0 );
  if ( ulRC != MCIERR_SUCCESS )
    _MCIError( "MCI_SET_AUDIO | MCI_SET_VOLUME", ulRC );
  else
  {
    memset( &stMCISet, 0, sizeof(stMCISet) );
    stMCISet.ulAudio = MCI_SET_AUDIO_ALL;
    stMCISet.ulLevel = 50 - (ulLeft / 2) + (ulRight / 2);
    ulRC = mciSendCommand( usDeviceOutId,
                    MCI_SET,
                    MCI_SET_AUDIO | MCI_AMP_SET_BALANCE | MCI_WAIT,
                    &stMCISet, 0 );
    if ( ulRC == MCIERR_SUCCESS )
      return; // Success.

    _MCIError( "MCI_SET_AUDIO | MCI_AMP_SET_BALANCE", ulRC );
  }

  // Falling back to software volume control.
  stDartDriver.wave_out_volume = rdpsnd_dsp_softvol_set;
  rdpsnd_dsp_softvol_set( left, right );
}


RD_BOOL dart_open_in(void)
{
  debugCP();
  return True;
}

void dart_close_in(void)
{
}

RD_BOOL dart_set_format_in(RD_WAVEFORMATEX * pwfx)
{
  return False;
}

struct audio_driver *dart_register(char *options)
{
  PSZ        pszEnvAudioShared = getenv( "RDP_AUDIO_SHARED" );

  fShareAudio = ( pszEnvAudioShared != NULL ) && ( *pszEnvAudioShared == '1' );

  stMCIMixOutSetup.ulFormatTag     = MCI_WAVE_FORMAT_PCM;
  stMCIMixOutSetup.ulBitsPerSample = 16;
  stMCIMixOutSetup.ulSamplesPerSec = 44100;
  stMCIMixOutSetup.ulChannels      = 2;
  stMCIMixOutSetup.ulDeviceType    = MCI_DEVTYPE_WAVEFORM_AUDIO;
  stMCIMixOutSetup.ulFormatMode    = MCI_PLAY;
  stMCIMixOutSetup.pmixEvent       = cbAudioOutEvent;

  stDartDriver.name                      = "DART";
  stDartDriver.description               = "OS/2 DART output driver";
  stDartDriver.add_fds                   = dart_add_fds;
  stDartDriver.check_fds                 = dart_check_fds;
  stDartDriver.wave_out_open             = dart_open_out;
  stDartDriver.wave_out_close            = dart_close_out;
  stDartDriver.wave_out_format_supported = dart_format_supported;
  stDartDriver.wave_out_set_format       = dart_set_format_out;
  stDartDriver.wave_out_volume           = dart_volume;

  stDartDriver.need_byteswap_on_be = 0;
  stDartDriver.need_resampling = 0;

  return &stDartDriver;
}
