/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   User interface services - X keyboard mapping

   Copyright (C) Matthew Chapman <matthewc.unsw.edu.au> 1999-2008
   Copyright 2003-2008 Peter Astrand <astrand@cendio.se> for Cendio AB
   Copyright 2014 Henrik Andersson <hean01@cendio.se> for Cendio AB

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

#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "rdesktop.h"
#include "scancodes.h"
#include "os2rd.h"
#include "debug.h"

#define KEYMAP_SIZE 0x7f+1
#define KEYMAP_MASK 0x7f
#define KEYMAP_MAX_LINE_LENGTH 80

// rdesktop.c
extern char g_keymapname[16];
extern RD_BOOL g_numlock_sync;

/* Joins two path components. The result should be freed with
   xfree(). */
static char *
pathjoin(const char *a, const char *b)
{
	char *result;
	result = xmalloc(PATH_MAX * 2 + 1);

	if (b[0] == '/')
	{
		strncpy(result, b, PATH_MAX);
	}
	else
	{
		strncpy(result, a, PATH_MAX);
		strcat(result, "/");
		strncat(result, b, PATH_MAX);
	}
	return result;
}


/* Try to open a keymap with fopen() */
FILE* xkeymap_open(const char *filename)
{
	char *path1, *path2;
	char *home;
	FILE *fp;

	/* Try ~/.rdesktop/keymaps */
	home = getenv("HOME");
	if (home)
	{
		path1 = pathjoin(home, ".rdesktop/keymaps");
		path2 = pathjoin(path1, filename);
		xfree(path1);
		fp = fopen(path2, "r");
		xfree(path2);
		if (fp)
			return fp;
	}

	/* Try KEYMAP_PATH */
	path1 = pathjoin( ".", filename);
	fp = fopen(path1, "r");
	xfree(path1);
	if (fp)
		return fp;

	/* Try current directory, in case we are running from the source
	   tree */
	path1 = pathjoin("keymaps", filename);
	fp = fopen(path1, "r");
	xfree(path1);
	if (fp)
		return fp;

	return NULL;
}


RD_BOOL
xkeymap_from_locale(const char *locale)
{
  return False;
}



/* Before connecting and creating UI */
void
xkeymap_init(void)
{
}

uint16 ui_get_numlock_state(unsigned int state)
{
  return ( WinGetKeyState( HWND_DESKTOP, VK_NUMLOCK ) & 0x0001 ) != 0
           ? KBD_FLAG_NUMLOCK : 0;
}

/* Handle special key combinations */
RD_BOOL handle_special_keys(uint32 keysym, unsigned int state, uint32 ev_time,
                            RD_BOOL pressed)
{
  return False;
}


key_translation
xkeymap_translate_key(uint32 keysym, unsigned int keycode, unsigned int state)
{
	key_translation tr = { 0, 0, 0, 0 };

  return tr;
}

void
ensure_remote_modifiers(uint32 ev_time, key_translation tr)
{
}

static void
update_modifier_state(uint8 scancode, RD_BOOL pressed)
{
}

/* Send keyboard input */
void rdp_send_scancode(uint32 time, uint16 flags, uint8 scancode)
{
  update_modifier_state( scancode, (flags & RDP_KEYRELEASE) == 0 );

  if ( (scancode & SCANCODE_EXTENDED) != 0 )
  {
    rdp_send_input( time, RDP_INPUT_SCANCODE, flags | KBD_FLAG_EXT,
                    scancode & ~SCANCODE_EXTENDED, 0 );
  }
  else
    rdp_send_input( time, RDP_INPUT_SCANCODE, flags, scancode, 0 );
}
