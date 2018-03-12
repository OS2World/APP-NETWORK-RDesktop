/***************************************************************************\
 *
 * PROGRAMNAME: AMouse
 * ------------
 *
 * VERSION: 2.80
 * --------
 *
 * MODULE NAME: pmwinmou.H
 * ------------
 *
 * DESCRIPTION: Additional entries for the AMouse system dll.
 * ------------ 1. Definitions for additional window message to be sent to focus window,
 *                 when the mouse wheel is rotated. The WM_MOUSEWHEEL_* messages are
 *                 sent to the focus window or to the owner frame window depending on
 *                 the registration flags being set during registering the window using
 *                 WinRegisterWindowForWheelMsg. This flag is necessary, because
 *                 WinDefWindowProc does not propagate WM_USER-message to the owner, if
 *                 they are not processed.
 *              2. A call to set the Master Volume
 *
 *  Ver.    Date      Comment
 *  ----    --------  -------
 *  1.00    20-02-00  First release
 *  2.00    06-16-01  WheelThread in separate process
 *  2.10    05-12-02  Handling of shift-keys
 *  2.20    10-03-02  USB support added
 *  2.40    02-01-03  support for 2 wheels; wildcards for process name
 *  2.50    04-20-03  application behaviour support added
 *  2.60    06-13-04  remove unused settings pages from mouse object
 *  2.70    10-23-04  support for 7 buttons added
 *  2.80    10-02-06  support of arbitary key combinations
 *
 *  Copyright (C) noller & breining software 2001...2006
 *
\******************************************************************************/
#ifndef _PMWINMOU_H_
#define _PMWINMOU_H_

#ifdef __cplusplus
      extern "C" {
#endif

// Set master volume. Increment is in percent. Call returns FALSE, if it was called
// from a non-PM-session (session type must be 3)
BOOL WinSetMasterVolume (SHORT sIncrement);

// Register application to receive wheel messages instead of standard messages
BOOL WinRegisterForWheelMsg (HWND hwnd, ULONG flWindow);

// flags for flWindow
#define AW_NONE                     0x0000
#define AW_OWNERFRAME               0x0001

// WM_MOUSEWHEEL_* messages are sent to the window, if registered with the
// WinRegisterWindowForWheelMsg API call. The message paramters are:
// mp1: SHORT1: MK_* - key state flags
//      SHORT2: <wheel-turns> * <scroll-number> * WHEEL_DELTA
// mp2: SHORT1: x-pointer position
//      SHORT2: y-pointer position
#define WM_MOUSEWHEEL_HORZ          WM_USER + 3110
#define WM_MOUSEWHEEL_VERT          WM_USER + 3111
#define WHEEL_DELTA                 120         /* Value for rolling one detent */
#define WHEEL_PAGESCROLL            (UINT_MAX)  /* Scroll one page */

// key state flags for WM_MOUSEWHEEL_* message; SHIFT/CTRL/ALT must be
// identical to rsp. KC_* values in pmwin.h
#define MK_SHIFT                    0x0008
#define MK_CTRL                     0x0010
#define MK_ALT                      0x0020
#define MK_BUTTON1                  0x0100
#define MK_BUTTON2                  0x0200
#define MK_BUTTON3                  0x0400
#define MK_BUTTON4                  0x0800
#define MK_BUTTON5                  0x1000
#define MK_BUTTON6                  0x2000
#define MK_BUTTON7                  0x4000

#ifdef __cplusplus
        }
#endif

#endif /* _PMWINMOU_H_ */
