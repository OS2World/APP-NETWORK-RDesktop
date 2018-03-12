// [Digi]

#include "os2cp.h"
#define INCL_DOSNLS
#define INCL_DOSERRORS
#include <os2.h>
#include <string.h>
#include <ctype.h>

static NAME2CP		aName2CP[] =
{
  {"850", 850, 0},
  {"862", 862, 0},
  {"866", 866, 0},
  {"ANSI_X3.4-1968", 367, 0},
  {"ANSI_X3.4-1986", 367, 0},
  {"ARABIC", 1089, 0},
  {"ASCII", 367, 0},
  {"ASMO-708", 1089, 0},
  {"CN-GB", 1383, 0},
  {"CP1250", 1250, 0},
  {"CP1251", 1251, 0},
  {"CP1252", 1252, 0},
  {"CP1253", 1253, 0},
  {"CP1254", 1254, 0},
  {"CP1255", 1255, 0},
  {"CP1256", 1256, 0},
  {"CP1257", 1257, 0},
  {"CP367", 367, 0},
  {"CP819", 819, 0},
  {"CP850", 850, 0},
  {"CP862", 862, 0},
  {"CP866", 866, 0},
  {"CP936", 1386, 0},
  {"CSASCII", 367, 0},
  {"CSEUCKR", 970, 0},
  {"CSEUCPKDFMTJAPANESE", 954, 0},
  {"CSEUCTW", 964, 0},
  {"CSGB2312", 1383, 0},
  {"CSHALFWIDTHKATAKANA", 896, 0},
  {"CSHPROMAN8", 1051, 0},
  {"CSIBM866", 866, 0},
  {"CSISOLATIN1", 819, 0},
  {"CSISOLATIN2", 912, 0},
  {"CSISOLATIN3", 913, 0},
  {"CSISOLATIN4", 914, 0},
  {"CSISOLATIN5", 920, 0},
  {"CSISOLATINARABIC", 1089, 0},
  {"CSISOLATINCYRILLIC", 915, 0},
  {"CSISOLATINGREEK", 813, 0},
  {"CSISOLATINHEBREW", 62210, 0},
  {"CSKOI8R", 878, 0},
  {"CSKSC56011987", 970, 0},
  {"CSMACINTOSH", 1275, 0},
  {"CSPC850MULTILINGUAL", 850, 0},
  {"CSPC862LATINHEBREW", 862, 0},
  {"CSSHIFTJIS", 943, 0},
  {"CSUCS4", 1236, 0},
  {"CSUNICODE", 1200, 0},
  {"CSUNICODE11", 1200, 0},
  {"CSVISCII", 1129, 0},
  {"CYRILLIC", 915, 0},
  {"ECMA-114", 1089, 0},
  {"ECMA-118", 813, 0},
  {"ELOT_928", 813, 0},
  {"EUC-CN", 1383, 0},
  {"EUC-JP", 954, 0},
  {"EUC-KR", 970, 0},
  {"EUC-TW", 964, 0},
  {"EUCCN", 1383, 0},
  {"EUCJP", 954, 0},
  {"EUCKR", 970, 0},
  {"EUCTW", 964, 0},
  {"EXTENDED_UNIX_CODE_PACKED_FORMAT_FOR_JAPANESE", 954, 0},
  {"GB18030", 1392, 0},
  {"GB2312", 1383, 0},
  {"GBK", 1386, 0},
  {"GREEK", 813, 0},
  {"GREEK8", 813, 0},
  {"HEBREW", 62210, 0},
  {"HP-ROMAN8", 1051, 0},
  {"IBM367", 367, 0},
  {"IBM819", 819, 0},
  {"IBM850", 850, 0},
  {"IBM862", 862, 0},
  {"IBM866", 866, 0},
  {"ISO-10646-UCS-2", 1200, 0},
  {"ISO-10646-UCS-4", 1236, 0},
  {"ISO-8859-1", 819, 0},
  {"ISO-8859-13", 901, 0},
  {"ISO-8859-15", 923, 0},
  {"ISO-8859-2", 912, 0},
  {"ISO-8859-3", 913, 0},
  {"ISO-8859-4", 914, 0},
  {"ISO-8859-5", 915, 0},
  {"ISO-8859-6", 1089, 0},
  {"ISO-8859-7", 813, 0},
  {"ISO-8859-8", 62210, 0},
  {"ISO-8859-9", 920, 0},
  {"ISO-IR-100", 819, 0},
  {"ISO-IR-101", 912, 0},
  {"ISO-IR-109", 913, 0},
  {"ISO-IR-110", 914, 0},
  {"ISO-IR-126", 813, 0},
  {"ISO-IR-127", 1089, 0},
  {"ISO-IR-138", 62210, 0},
  {"ISO-IR-144", 915, 0},
  {"ISO-IR-148", 920, 0},
  {"ISO-IR-149", 970, 0},
  {"ISO-IR-166", 874, 0},
  {"ISO-IR-179", 901, 0},
  {"ISO-IR-203", 923, 0},
  {"ISO-IR-6", 367, 0},
  {"ISO646-US", 367, 0},
  {"ISO8859-1", 819, 0},
  {"ISO8859-13", 901, 0},
  {"ISO8859-15", 923, 0},
  {"ISO8859-2", 912, 0},
  {"ISO8859-3", 913, 0},
  {"ISO8859-4", 914, 0},
  {"ISO8859-5", 915, 0},
  {"ISO8859-6", 1089, 0},
  {"ISO8859-7", 813, 0},
  {"ISO8859-8", 62210, 0},
  {"ISO8859-9", 920, 0},
  {"ISO_646.IRV:1991", 367, 0},
  {"ISO_8859-1", 819, 0},
  {"ISO_8859-13", 901, 0},
  {"ISO_8859-15", 923, 0},
  {"ISO_8859-15:1998", 923, 0},
  {"ISO_8859-1:1987", 819, 0},
  {"ISO_8859-2", 912, 0},
  {"ISO_8859-2:1987", 912, 0},
  {"ISO_8859-3", 913, 0},
  {"ISO_8859-3:1988", 913, 0},
  {"ISO_8859-4", 914, 0},
  {"ISO_8859-4:1988", 914, 0},
  {"ISO_8859-5", 915, 0},
  {"ISO_8859-5:1988", 915, 0},
  {"ISO_8859-6", 1089, 0},
  {"ISO_8859-6:1987", 1089, 0},
  {"ISO_8859-7", 813, 0},
  {"ISO_8859-7:1987", 813, 0},
  {"ISO_8859-7:2003", 813, 0},
  {"ISO_8859-8", 62210, 0},
  {"ISO_8859-8:1988", 62210, 0},
  {"ISO_8859-9", 920, 0},
  {"ISO_8859-9:1989", 920, 0},
  {"JISX0201-1976", 896, 0},
  {"JIS_X0201", 896, 0},
  {"KOI8-R", 878, 0},
  {"KOI8-U", 1168, 0},
  {"KOREAN", 970, 0},
  {"KSC_5601", 970, 0},
  {"KS_C_5601-1987", 970, 0},
  {"KS_C_5601-1989", 970, 0},
  {"L1", 819, 0},
  {"L2", 912, 0},
  {"L3", 913, 0},
  {"L4", 914, 0},
  {"L5", 920, 0},
  {"L7", 901, 0},
  {"LATIN-9", 923, 0},
  {"LATIN1", 819, 0},
  {"LATIN2", 912, 0},
  {"LATIN3", 913, 0},
  {"LATIN4", 914, 0},
  {"LATIN5", 920, 0},
  {"LATIN7", 901, 0},
  {"MAC", 1275, 0},
  {"MACINTOSH", 1275, 0},
  {"MACROMAN", 1275, 0},
  {"MS-ANSI", 1252, 0},
  {"MS-ARAB", 1256, 0},
  {"MS-CYRL", 1251, 0},
  {"MS-EE", 1250, 0},
  {"MS-GREEK", 1253, 0},
  {"MS-HEBR", 1255, 0},
  {"MS-TURK", 1254, 0},
  {"MS936", 1386, 0},
  {"MS_KANJI", 943, 0},
  {"R8", 1051, 0},
  {"ROMAN8", 1051, 0},
  {"SHIFT-JIS", 943, 0},
  {"SHIFT_JIS", 943, 0},
  {"SJIS", 943, 0},
  {"TIS-620", 874, 0},
  {"TIS620", 874, 0},
  {"TIS620-0", 874, 0},
  {"TIS620.2529-1", 874, 0},
  {"TIS620.2533-0", 874, 0},
  {"TIS620.2533-1", 874, 0},
  {"UCS-2", 1200, TRUE},
  {"UCS-2BE", 1200, TRUE},
  {"UCS-4", 1236, 0},
  {"UNICODE-1-1", 1200, 0},
  {"UNICODEBIG", 1200, TRUE},
  {"US", 367, 0},
  {"US-ASCII", 367, 0},
  {"UTF-16", 1200, 0},
  {"UTF-16BE", 1200, TRUE},
  {"UTF-16LE", 1200, 0},
  {"UTF-32", 1236, 0},
  {"UTF-32BE", 1232, TRUE},
  {"UTF-32LE", 1234, 0},
  {"UTF-8", 1208, 0},
  {"VISCII", 1129, 0},
  {"VISCII1.1-1", 1129, 0},
  {"WINBALTRIM", 1257, 0},
  {"WINDOWS-1250", 1250, 0},
  {"WINDOWS-1251", 1251, 0},
  {"WINDOWS-1252", 1252, 0},
  {"WINDOWS-1253", 1253, 0},
  {"WINDOWS-1254", 1254, 0},
  {"WINDOWS-1255", 1255, 0},
  {"WINDOWS-1256", 1256, 0},
  {"WINDOWS-1257", 1257, 0},
  {"WINDOWS-936", 1386, 0},
  {"X0201", 896}
};

PNAME2CP os2cpFromName(char *cp)
{
  ULONG		ulLo = 0;
  ULONG		ulHi = ( sizeof(aName2CP) / sizeof(struct _NAME2CP) ) - 1;
  ULONG		ulNext;
  LONG		lFound = -1;
  LONG		lCmp;
  PCHAR		pcEnd;
  CHAR		acBuf[64];

  while( isspace( *cp ) ) cp++;
  pcEnd = strchr( cp, ' ' );
  if ( pcEnd == NULL )
    pcEnd = strchr( cp, '\0' );

  ulNext = pcEnd - cp;
  if ( ulNext >= sizeof(acBuf) )
    return 0;
  
  memcpy( acBuf, cp, ulNext );
  acBuf[ulNext] = '\0';
  strupr( acBuf ); 

  lCmp = strcmp( aName2CP[0].pszName, acBuf );
  if ( lCmp > 0 )
    return NULL;
  else if ( lCmp == 0 )
    return &aName2CP[0];

  lCmp = strcmp( aName2CP[ulHi].pszName, acBuf );
  if ( lCmp < 0 )
    return NULL;
  else if ( lCmp == 0 )
    return &aName2CP[ulHi];

  while( ( ulHi - ulLo ) > 1 )
  {
    ulNext = ( ulLo + ulHi ) / 2;

    lCmp = strcmp( aName2CP[ulNext].pszName, acBuf );
    if ( lCmp < 0 )
      ulLo = ulNext;
    else if ( lCmp > 0 )
      ulHi = ulNext;
    else
    {
      lFound = ulNext;
      break;
    }
  }

  return lFound == -1 ? NULL : &aName2CP[lFound];
}
