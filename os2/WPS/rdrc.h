#ifndef RDRC_H
#define RDRC_H

// Class icon.
#define IDRES_ICON                   1

// Progress bar image.
#define IDRES_PROGRESS_BAR_BMP       2

// Dialogs.
#define IDD_PB_UNDO                101
#define IDD_PB_DEFAULT             102

#define IDDLG_PAGE_SERVER         1000
#define IDD_EF_HOST               1001
#define IDD_EF_USER               1002
#define IDD_EF_DOMAIN             1003
#define IDD_EF_PASSWORD           1004
#define IDD_CB_PROMPT             1005
#define IDD_RB_ABSOLUTE           1006
#define IDD_RB_PROPORTIONAL       1007
#define IDD_RB_FULLSCREEN         1008
#define IDD_SB_WIDTH              1009
#define IDD_SB_HEIGHT             1010
#define IDD_SB_PROPSIZE           1011
#define IDD_SB_DEPTH              1012

#define IDDLG_PAGE_PROTOCOL1      2000
#define IDD_EF_CLIENT_HOST        2001
#define IDD_EF_LOCAL_CP           2002
#define IDD_CB_ENCRYPTION         2003
#define IDD_RB_RDP4               2004
#define IDD_RB_RDP5               2005
#define IDD_CB_NO_WALLPAPER       2006
#define IDD_CB_NO_FULLWINDOWDRAG  2007
#define IDD_CB_NO_MENUANIMATIONS  2008
#define IDD_CB_NO_THEMING         2009
#define IDD_CB_NO_CURSOR_SHADOW   2010
#define IDD_CB_NO_CURSORSETTINGS  2011
#define IDD_CB_ANTIALIASING       2012

#define IDDLG_PAGE_PROTOCOL2      3000
#define IDD_CB_COMPRESSION        3001
#define IDD_CB_PRES_BMP_CACHING   3002
#define IDD_CB_FORCE_BMP_UPD      3003
#define IDD_CB_NUMLOCK_SYNC       3004
#define IDD_CB_NO_MOTION_EVENTS   3005
#define IDD_CB_KEEP_WIN_KEYS      3006

#define IDDLG_PAGE_REDIRECTION1   4000
#define IDD_CNT_DISKS             4001
#define IDD_PB_DISK_ADD           4002
#define IDD_PB_DISK_DELETE        4003
#define IDD_EF_DISKCLIENT         4004

#define IDDLG_PAGE_REDIRECTION2   5000
#define IDD_RB_CBPRIMARY          5001
#define IDD_RB_CBPASSIVE          5002
#define IDD_RB_CBOFF              5003
#define IDD_RB_SNDLOCAL           5004
#define IDD_RB_SNDREMOTE          5005
#define IDD_RB_SNDOFF             5006
#define IDD_CNT_SERIALDEVICES     5007
#define IDD_CB_SDREMOTE           5008
#define IDD_CB_SDLOCAL            5009
#define IDD_PB_SDSET              5010

#define IDDLG_DISK                7000
#define IDD_EF_NAME               7001
#define IDD_EF_LOCAL_PATH         7002
#define IDD_CB_DRIVE              7003
#define IDD_LB_DIR                7004
#define IDD_PB_OK                 7005
#define IDD_PB_CANCEL             7006

#define IDDLG_LOGON               8000

#define IDDLG_CONSOLE             9000
#define IDD_CNT_RECORDS           9001

#define IDDLG_PROGRESS           10000
#define IDD_ST_BAR               10001

// String table.
#define IDS_PAGE_NO                 16
#define IDS_SERVER                  17
#define IDS_PROTOCOL                18
#define IDS_REDIRECTION             19
#define IDS_BPP_8_PRIVATE_COLORS    20
#define IDS_BPP_8                   21
#define IDS_BPP_15                  22
#define IDS_BPP_16                  23
#define IDS_BPP_32                  24
#define IDS_ENC_FULL                25
#define IDS_ENC_LOGON               26
#define IDS_ENC_NONE                27
#define IDS_NAME                    28
#define IDS_LOCAL_PATH              29
#define IDS_YES                     30
#define IDS_NO                      31
#define IDS_REMOTE                  32
#define IDS_LOCAL                   33
#define IDS_PORT_NONE               34
#define IDS_CONWIN_TITLE            35
#define IDS_PID                     36
#define IDS_TIME                    37
#define IDS_MESSAGE                 38
#define IDS_PROGRESS_TITLE          39

// Message table.
#define IDM_PATH_NOT_EXIST           1
#define IDM_DUPLICATE_NAME           2
#define IDM_RDESKTOP_RUN             3
#define IDM_RDESKTOP_EXIT            4
#define IDM_INVALID_HOSTNAME         5
#define IDM_START_FAILED             6

#include <wpobject.h> // WPMENUID_USER

// Class popup menu.
#define IDMNU_RDESKTOP               (WPMENUID_USER+1)
#define IDMI_COPYRUNCMD              (WPMENUID_USER+2)
#define IDMI_CONSOLE                 (WPMENUID_USER+3)

// Console popup menu.
#define IDMENU_CONSOLE               100
#define IDMI_CON_CLEAR               101
#define IDMI_CON_COPY                102

// Help.

#define HELP_FILE                    "rdesktop.hlp"

#define IDHELP_NB_SERVER             1011
#define IDHELP_NB_PROTOCOL1          1012
#define IDHELP_NB_PROTOCOL2          1013
#define IDHELP_NB_REDIRECTION1       1014
#define IDHELP_NB_REDIRECTION2       1015
#define IDHELP_MI_COPYRUNCMD         1021
#define IDHELP_MI_CONSOLE            1022

#endif // RDRC_H

