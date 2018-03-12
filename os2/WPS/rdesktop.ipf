:userdoc.

.*
.* Properties notebook.
.* ====================

:h1 res=1010 name=IDHELP_NB.Rdesktop properties
:p.
Related Information&colon.
:ul compact.
:li.:link refid=IDHELP_NB_SERVER reftype=hd.Server:elink.
:li.:link refid=IDHELP_NB_PROTOCOL1 reftype=hd.Protocol, page 1/2:elink.
:li.:link refid=IDHELP_NB_PROTOCOL2 reftype=hd.Protocol, page 2/2:elink.
:li.:link refid=IDHELP_NB_REDIRECTION1 reftype=hd.Redirection, page 1/2:elink.
:li.:link refid=IDHELP_NB_REDIRECTION2 reftype=hd.Redirection, page 2/2:elink.
:eul.

.*
.* Properties notebook. Page "Server".
.* -----------------------------------

:h2 res=1011 id=IDHELP_NB_SERVER.Server
:p.Use this page to specify the RDP server, user name, user domain, password
and the remote desktop properties to be used when connecting to the remote host. 
:dl break=all.
:lp.Logon information.
:dt.:hp2.Host:ehp2.
:dd.Type the name of the remote host to which you want to connect. You can
specify the host name or IP address.
:dt.:hp2.User name:ehp2.
:dd.Enter the name that the remote host you want to connect to recognizes as
your user name.
:p.If you leave this field blank, you are prompted to type the user name when
you start this Rdesktop instance.
:dt.:hp2.Domain:ehp2.
:dd.This field is optional.
:dt.:hp2.Password:ehp2.
:dd.Enter the password for your user name on the remote host or domain.
:p.This field is optional if
:ul compact.
:li.A password is not required by the remote host.
:li.:hp1.Prompt:ehp1. checkbox is checked.
:eul.
:dt.:hp2.Prompt:ehp2.
:dd.When this option is checked, it will prompt for the user name, domain and
password when you start this Rdesktop instance.
:lp.Remote desktop properties.
:dt.:hp2.Absolute:ehp2.
:dd.Desktop geometry in pixels.
:dt.:hp2.Proportional:ehp2.
:dd.Relative desktop size. Percentage of local desktop.
:dt.:hp2.Full-screen mode:ehp2.
:dd.Remote desktop will expand to full screen, hiding the border and title
bar.
:note.:font facename=Courier size=12x8.Ctrl-Alt-Enter:font facename=default.
can be used to switch over from full mode to windowed mode and vice versa.
:dt.:hp2.Depth:ehp2.
:dd.Remote desktop color depth in bits per pixel (BPP).
:edl.

.*
.* Properties notebook. Page "Protocol 1/2".
.* -----------------------------------------

:h2 res=1012 id=IDHELP_NB_PROTOCOL1.Protocol, page 1/2
:dl break=all.
:lp.Transport.
:dt.:hp2.Client hostname:ehp2.
:dd.This field is optional. Normally, you do not need to specify you host name.
:dt.:hp2.Local codepage:ehp2.
:dd.This field is optional. Normally, you do not need to specify you host name.
In this case codepage will be automatically detected.
:dt.:hp2.Encryption:ehp2.
:dd.
:parml compact tsize=10 break=none.
:pt.Full
:pd.Encrypt all of your RDP traffic (recomended).
:pt.None
:pd.Disable encryption.
.*
.* Deprecated encryption mode "Logon only" has been removed from the properties
.* notebook.
.*
.* :pt.Logon only
.* :pd.Encrypt login packet but unencrypted transfer of other packets.
.*
:eparml.
:lp.RDP protocol version.
:dt.:hp2.Use RDP prtocol version 4:ehp2.
:dd.Version 4 of RDP protocol will be used.
:dt.:hp2.Use RDP prtocol version 5:ehp2.
:dd.Version 5 of RDP protocol will be used. RDP5 allows you to optimize
performance by disabling unnecessary features:
:ul compact.
:li.wallpaper,
:li.window contents while dragging,
:li.menu animations,
:li.themes.
:li.font anti-aliasing.
:eul.
:edl.

.*
.* Properties notebook. Page "Protocol 2/2".
.* -----------------------------------------

:h2 res=1013 id=IDHELP_NB_PROTOCOL2.Protocol, page 2/2
:p.Use this page to specify the protocol settings which will help improve
performance.

.*
.* Properties notebook. Page "Redirection 1/2".
.* --------------------------------------------

:h2 res=1014 id=IDHELP_NB_REDIRECTION1.Redirection, page 1/2
:p.Use this page to gain access to your directories on the local computer
during a Remote Desktop session. The drives are displayed as ":hp1.Name:ehp1.
on :hp1.Displayed client name:ehp1." in both Windows Explorer and My Computer.
:dl break=all.
:dt.:hp2.Displayed client name:ehp2.
:dd.This field is optional. If left blank the local hostname will be used.
:edl.

.*
.* Properties notebook. Page "Redirection 2/2".
.* --------------------------------------------

:h2 res=1015 id=IDHELP_NB_REDIRECTION2.Redirection, page 2/2
:dl break=all.
:dt.:hp2.Clipboard:ehp2.
:dd.
:parml compact tsize=10 break=none.
:pt.Primary
:pd.Server will be informed about new data type in local clipboard. It allows
to send/receive plain text and images.
:pt.Passive
:pd.It allows to receive plain text only.
:pt.Off
:pd.Do not share clipboard.
:eparml.
:dt.:hp2.Sound:ehp2.
:dd.
:parml compact tsize=10 break=none.
:pt.Local
:pd.Redirect sound from server on local host.
:pt.Remote
:pd.Would leave sound on server.
:pt.Off
:pd.All sound Off.
:eparml.
:edl.

.*
.* Object popup menu items.
.* ========================

:h1 res=1020 id=IDHELP_MI.Object popup menu items
Related Information&colon.
:ul compact.
:li.:link refid=IDHELP_MI_COPYRUNCMD reftype=hd.Copy run command:elink.
:li.:link refid=IDHELP_MI_CONSOLE reftype=hd.Console&per.&per.&per.:elink.
:eul.

.*
.* Menu item: Copy run command.
.* ----------------------------

:h2 res=1021 id=IDHELP_MI_COPYRUNCMD.Copy run command
:p.Select this menu item to copy the command to run Rdesktop with current
properties into the clipboard.

.*
.* Menu item: Console...
.* ---------------------

:h2 res=1022 id=IDHELP_MI_CONSOLE.Console&per.&per.&per.
:p.Select this menu item to show console window.
Console contains a list of messages from rdesktop.exe. It may be useful
in problems solving.

.*
.* Setup strings.
.* ==============

:h1 res=1100 id=IDHELP_SETUP_STRINGS.Rdesktop WPS object setup strings
:p.A WPS setup string is a sequence of
:font facename=Courier size=12x8.keyword=value:font facename=default. pairs,
each separated with a semicolon.
:p.:hp2.List of all keywords for Rdesktop WPS object.:ehp2.
:parml tsize=5 break=all.
:pt.:hp2.host:ehp2.=hostname/IP-address
:pd.Remote host name or IP address.
:pt.:hp2.user:ehp2.=string
:pd.User name.
:pt.:hp2.domain:ehp2.=string
:pd.Domain name.
:pt.:hp2.password:ehp2.=string
:pd.Password.
:pt.:hp2.prompt:ehp2.=YES/NO
:pd.Prompt for the user name, domain and password on Rdesktop instance start.
:pt.:hp2.sizemode:ehp2.=ABSOLUTE/PROPORTIONAL/FULLSCREEN
:pd.Remote desktop size mode.
:pt.:hp2.width:ehp2.=NNN
:pd.Remote desktop width (for sizemode=ABSOLUTE).
:pt.:hp2.height:ehp2.=NNN
:pd.Remote desktop height (for sizemode=ABSOLUTE).
:pt.:hp2.proportionalsize:ehp2.=NNN
:pd.Remote desktop size in percentages (for sizemode=PROPORTIONAL).
:pt.:hp2.depth:ehp2.=8/15/16/24/32
:pd.Remote desktop color depth.
:pt.:hp2.switches:ehp2.=item1&comma.item2&comma.item3,...
:pd.List of protocol features separated by comma.
:dl compact tsize=25.
:dthd.:hp2.Feature:ehp2.
:ddhd.:hp2.Description:ehp2.
:dt.RDP5
:dd.Use protocol version 5 instead 4.
:dt.COMPRESSION
:dd.Enable RDP compression.
:dt.PRES_BMP_CACHING
:dd.Use persistent bitmap caching.
:dt.FORCE_BMP_UPD
:dd.Force bitmap updates.
:dt.NUMLOCK_SYNC
:dd.Enable numlock syncronization.
:dt.NO_MOTION_EVENTS
:dd.Do not send mouse motion events.
:dt.KEEP_WIN_KEYS
:dd.Handle pressing ALT and ALT-F4 normally.
:edl.
:pt.:hp2.drp5perf:ehp2.=item1&comma.item2&comma.item3,...
:pd.Optimize performance by disabling unnecessary features. Uses only when item
:hp1.RDP5:ehp1. specified in :hp2.switches:ehp2.
:dl compact tsize=25.
:dthd.:hp2.Feature:ehp2.
:ddhd.:hp2.Description:ehp2.
:dt.NO_WALLPAPER
:dd.Remove remote wallpaper.
:dt.NO_FULLWINDOWDRAG
:dd.Disable window contents while dragging.
:dt.NO_MENUANIMATIONS
:dd.Disable menu animations.
:dt.NO_THEMING
:dd.Disable themes.
:dt.ANTIALIASING
:dd.Enable font anti-aliasing.
:edl.
:pt.:hp2.encryption:ehp2.=FULL/NONE/LOGON
:pd.Data encryption.
:parml compact tsize=10 break=none.
:pt.FULL
:pd.Encrypt all of RDP traffic.
:pt.NONE
:pd.Disable encryption.
:pt.LOGON
:pd.Encrypt login packet but unencrypted transfer of other packets.
:caution.
This encryprion mode is deprecated and not allowed in the properties notebook.
It can cause problems in rdesktop.
:ecaution.
:eparml.
:pt.:hp2.clipboard:ehp2.=PRIMARY/PASSIVE/OFF
:pd.Clipboard redirection.
:parml compact tsize=10 break=none.
:pt.PRIMARY
:pd.Server will be informed about new data type in local clipboard. It allows
to send/receive plain text and images.
:pt.PASSIVE
:pd.Allows to receive plain text only.
:pt.OFF
:pd.Do not share clipboard.
:eparml.
:pt.:hp2.sound:ehp2.=LOCAL/REMOTE/OFF
:pd.Sound redirection.
:parml compact tsize=10 break=none.
:pt.LOCAL
:pd.Redirect sound from server on local host.
:pt.REMOTE
:pd.Would leave sound on server.
:pt.OFF
:pd.All sound Off.
:eparml.
:pt.:hp2.disk:ehp2.=name1=D&colon.\local_directory&comma.name2="D&colon.\My local\directory",...
:pd.Disks redirection.
:pt.:hp2.diskclient:ehp2.=string
:pd.Client name to display in Windows Explorer and My Computer for redirected
drives.
:eparml.

:euserdoc.
