/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SIWX91X_IEEEE80211_H
#define SIWX91X_IEEEE80211_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/net/wifi.h>

/* Masks and flags for Frame Control field */
#define IEEE80211_FCTL_VERS                0x0003
#define IEEE80211_FCTL_FTYPE               0x000c
#define IEEE80211_FCTL_STYPE               0x00f0
#define IEEE80211_FCTL_TODS                0x0100
#define IEEE80211_FCTL_FROMDS              0x0200
#define IEEE80211_FCTL_MOREFRAGS           0x0400
#define IEEE80211_FCTL_RETRY               0x0800
#define IEEE80211_FCTL_PM                  0x1000
#define IEEE80211_FCTL_MOREDATA            0x2000
#define IEEE80211_FCTL_PROTECTED           0x4000
#define IEEE80211_FCTL_ORDER               0x8000
#define IEEE80211_FCTL_CTL_EXT             0x0f00

/* Masks for Sequence Control field */
#define IEEE80211_SCTL_FRAG                0x000F
#define IEEE80211_SCTL_SEQ                 0xFFF0

/* Frame types (see IEEE80211_FCTL_FTYPE) */
#define IEEE80211_FTYPE_MGMT               0x0000
#define IEEE80211_FTYPE_CTL                0x0004
#define IEEE80211_FTYPE_DATA               0x0008
#define IEEE80211_FTYPE_EXT                0x000c

/* Subtype for management frame (see IEEE80211_FCTL_STYPE) */
#define IEEE80211_STYPE_ASSOC_REQ          0x0000
#define IEEE80211_STYPE_ASSOC_RESP         0x0010
#define IEEE80211_STYPE_REASSOC_REQ        0x0020
#define IEEE80211_STYPE_REASSOC_RESP       0x0030
#define IEEE80211_STYPE_PROBE_REQ          0x0040
#define IEEE80211_STYPE_PROBE_RESP         0x0050
#define IEEE80211_STYPE_BEACON             0x0080
#define IEEE80211_STYPE_ATIM               0x0090
#define IEEE80211_STYPE_DISASSOC           0x00A0
#define IEEE80211_STYPE_AUTH               0x00B0
#define IEEE80211_STYPE_DEAUTH             0x00C0
#define IEEE80211_STYPE_ACTION             0x00D0

/* Subtype for control frame (see IEEE80211_FCTL_STYPE) */
#define IEEE80211_STYPE_TRIGGER            0x0020
#define IEEE80211_STYPE_CTL_EXT            0x0060
#define IEEE80211_STYPE_BACK_REQ           0x0080
#define IEEE80211_STYPE_BACK               0x0090
#define IEEE80211_STYPE_PSPOLL             0x00A0
#define IEEE80211_STYPE_RTS                0x00B0
#define IEEE80211_STYPE_CTS                0x00C0
#define IEEE80211_STYPE_ACK                0x00D0
#define IEEE80211_STYPE_CFEND              0x00E0
#define IEEE80211_STYPE_CFENDACK           0x00F0

/* Subtype for data frame (see IEEE80211_FCTL_STYPE) */
#define IEEE80211_STYPE_DATA               0x0000
#define IEEE80211_STYPE_DATA_CFACK         0x0010
#define IEEE80211_STYPE_DATA_CFPOLL        0x0020
#define IEEE80211_STYPE_DATA_CFACKPOLL     0x0030
#define IEEE80211_STYPE_NULLFUNC           0x0040
#define IEEE80211_STYPE_CFACK              0x0050
#define IEEE80211_STYPE_CFPOLL             0x0060
#define IEEE80211_STYPE_CFACKPOLL          0x0070
#define IEEE80211_STYPE_QOS_DATA           0x0080
#define IEEE80211_STYPE_QOS_DATA_CFACK     0x0090
#define IEEE80211_STYPE_QOS_DATA_CFPOLL    0x00A0
#define IEEE80211_STYPE_QOS_DATA_CFACKPOLL 0x00B0
#define IEEE80211_STYPE_QOS_NULLFUNC       0x00C0
#define IEEE80211_STYPE_QOS_CFACK          0x00D0
#define IEEE80211_STYPE_QOS_CFPOLL         0x00E0
#define IEEE80211_STYPE_QOS_CFACKPOLL      0x00F0

/* Capability Information field */
#define WLAN_CAPABILITY_ESS                BIT(0)
#define WLAN_CAPABILITY_IBSS               BIT(1)
#define WLAN_CAPABILITY_CF_POLLABLE        BIT(2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST    BIT(3)
#define WLAN_CAPABILITY_PRIVACY            BIT(4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE     BIT(5)
#define WLAN_CAPABILITY_PBCC               BIT(6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY    BIT(7)
#define WLAN_CAPABILITY_SPECTRUM_MGMT      BIT(8)
#define WLAN_CAPABILITY_QOS                BIT(9)
#define WLAN_CAPABILITY_SHORT_SLOT_TIME    BIT(10)
#define WLAN_CAPABILITY_APSD               BIT(11)
#define WLAN_CAPABILITY_RADIO_MEASURE      BIT(12)
#define WLAN_CAPABILITY_DSSS_OFDM          BIT(13)
#define WLAN_CAPABILITY_DEL_BACK           BIT(14)
#define WLAN_CAPABILITY_IMM_BACK           BIT(15)

/* Only a subset of interresting IEs */
enum ieee80211_eid {
	WLAN_EID_SSID = 0,
	WLAN_EID_RSN = 48,
};

/* Can also used for probe response */
struct ieee80211_beacon {
	uint16_t frame_control;
	uint16_t duration_id;
	uint8_t  da[WIFI_MAC_ADDR_LEN];
	uint8_t  sa[WIFI_MAC_ADDR_LEN];
	uint8_t  bssid[WIFI_MAC_ADDR_LEN];
	uint16_t seq_ctrl;
	uint64_t timestamp;
	uint16_t beacon_int;
	uint16_t capab_info;
	uint8_t  ies[];
} __packed __aligned(2);

/* Information Element */
struct ieee80211_element {
	uint8_t id;
	uint8_t datalen;
	uint8_t data[];
} __packed;

static inline bool ieee80211_is_beacon(uint16_t fc)
{
	return (fc & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
		(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
}

static inline bool ieee80211_is_probe_resp(uint16_t fc)
{
	return (fc & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
		(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_RESP);
}

#endif
