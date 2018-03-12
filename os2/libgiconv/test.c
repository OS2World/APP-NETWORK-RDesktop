#include <stdio.h>
#include <string.h>
#include "iconv.h"

void main()
{
  char       acBuf[128];
//  char       *inbuf = "Тест - проверка"; // KOI8-R string.
  char       *inbuf = "Test"; // KOI8-R string.
  size_t     inbytesleft = strlen( inbuf );
  char       *outbuf = acBuf;
  size_t     outbytesleft = sizeof( acBuf );
  iconv_t    ic;

  // KOI8 -> system cp.

//  ic = iconv_open( "IBM-866", "KOI8-R" );
//  ic = iconv_open( "ISO-8859-1", "KOI8-R" );
  ic = iconv_open( "", "KOI8-R" );
  if ( ic == (iconv_t)(-1) )
  {
    puts( "iconv_open() fail" );
    return;
  }

  if ( iconv( ic, (const char **)&inbuf, &inbytesleft,
              &outbuf, &outbytesleft ) == -1 )
    puts( "iconv() failed" );
  else
  {
    *outbuf = '\0';
    printf( "Result: %s\n", &acBuf );
  }

  iconv_close( ic );
/*
  // System cp. -> UTF-8 -> system cp.

  // System cp. -> UTF-8 by StrUTF8New().
  inbuf = StrUTF8New( 1, &acBuf, strlen( &acBuf ) );

  // UTF-8 -> system cp. by StrUTF8().
  if ( StrUTF8( 0, &acBuf, sizeof(acBuf), inbuf, strlen( inbuf ) ) == -1 )
    puts( "StrUTF8() failed" );
  else
    printf( "system cp. -> UTF-8 -> system cp.: %s\n", &acBuf );

  free( inbuf );

  // Unload used DLL.
  iconv_clean();
*/
  puts( "Done." );
}
