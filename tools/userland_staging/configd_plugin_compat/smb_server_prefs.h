/* Real, narrow compat stand-in for <smb_server_prefs.h> (the SMB
 * FILE-SHARING SERVER's own preference-key header -- distinct from the
 * SMB *client* filesystem, apple-oss-distributions/SMBClient/smb, which
 * IS real and pinned per docs/apple-oss-catalog.md but only ships the
 * client side's lib/smb/smb_preferences.h, not a server-side
 * smb_server_prefs.h). No apple-oss-distributions repo for an SMB
 * *server* daemon exists at all (macOS's SMB File Sharing server has
 * never been open-sourced) -- confirmed by cloning both `SMBClient` and
 * `smb` at their real pinned/HEAD tags and grepping for these exact
 * symbol names.
 *
 * DAR-241 (2026-09-04): Plugins/IPMonitor/smb-configuration.c's whole
 * purpose is syncing SMB-file-sharing-SERVER preferences (NetBIOS name,
 * workgroup, WINS servers, etc, i.e. `com.apple.smb.server.plist`) into
 * SCDynamicStore -- called exactly once, at IPMonitor load time
 * (`load_smb_configuration()`, ip_plugin.c line ~9279), for a File
 * Sharing service this project has never ported and has no daemon for
 * at all (no smbd equivalent anywhere in this repo). Real, narrow,
 * honestly-documented "feature genuinely doesn't apply to this
 * embedded target" exclusion, same category as the VPN-status/PPP
 * exclusions in this same session -- not a stand-in for DHCP/network-
 * config-relevant behavior (smb-configuration.c never touches DNS/DHCP/
 * IP configuration at all).
 *
 * All string VALUES below except kSMBPrefNetBIOSScope are the real,
 * confirmed values from apple-oss-distributions/SMBClient's own
 * lib/smb/smb_preferences.h (client side reuses the identical server
 * preference-key names/values). kSMBPrefNetBIOSScope has no client-side
 * equivalent to confirm against; its value is inferred from every other
 * entry's own "key name equals its own suffix" convention (NetBIOSName
 * -> "NetBIOSName", Workgroup -> "Workgroup", etc) -- flagged here as
 * the one unconfirmed value, in case a real macOS box is ever available
 * to verify `defaults read /Library/Preferences/SystemConfiguration/
 * com.apple.smb.server NetBIOSScope`.
 */
#ifndef _SMB_SERVER_PREFS_COMPAT_H
#define _SMB_SERVER_PREFS_COMPAT_H

#define kSMBPreferencesAppID			"com.apple.smb.server.plist"
#define kSMBPrefNetBIOSName			"NetBIOSName"
#define kSMBPrefNetBIOSNodeType			"NetBIOSNodeType"
#define kSMBPrefNetBIOSNodeBroadcast		"Broadcast"
#define kSMBPrefNetBIOSNodePeer			"Peer"
#define kSMBPrefNetBIOSNodeMixed		"Mixed"
#define kSMBPrefNetBIOSNodeHybrid		"Hybrid"
#define kSMBPrefNetBIOSScope			"NetBIOSScope"	/* unconfirmed, see header comment */
#define kSMBPrefWorkgroup			"Workgroup"
#define kSMBPrefServerDescription		"ServerDescription"
#define kSMBPrefDOSCodePage			"DOSCodePage"
#define kSMBPrefWINSServerAddressList		"WINSServerAddressList"

#endif /* _SMB_SERVER_PREFS_COMPAT_H */
