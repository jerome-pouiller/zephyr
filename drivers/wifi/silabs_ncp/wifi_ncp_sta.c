/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Station mode: commands sent to the network processor on the management
 * endpoint and handling of its confirmations and indications.
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_utils.h>
#include <zephyr/sys/byteorder.h>

#include "wifi_ncp.h"
#include "wifi_ncp_sta.h"

/* After wifi_ncp.h, which brings the types of the supplicant */
#include <common/eapol_common.h>

LOG_MODULE_DECLARE(wifi_ncp, CONFIG_WIFI_LOG_LEVEL);

/* Time given to the co-processor to report it is ready, once the endpoints are
 * connected
 */
#define WIFI_NCP_CARD_READY_TIMEOUT K_SECONDS(5)

/* Echoed by the TX status of the last EAPOL frame */
#define WIFI_NCP_EAPOL_TOKEN 1

/* Fixed fields of a beacon or probe response after the MAC header */
#define WIFI_NCP_BEACON_FIXED_LEN 12

BUILD_ASSERT(offsetof(sli_wifi_sta_join_ap_info_t, scan_result) == 0);

static void wifi_ncp_scan_report(const struct device *dev, scan_result_cb_t cb);

int wifi_ncp_nwp_cmd(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr, const void *head,
		     size_t head_len, const void *body, size_t body_len, void *resp,
		     size_t *resp_size)
{
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_cmd *cmd = &data->cmd;
	int ret;

	k_mutex_lock(&cmd->lock, K_FOREVER);

	cmd->id = hdr->command_id;
	cmd->status = 0;
	cmd->resp = resp;
	cmd->resp_size = resp ? *resp_size : 0;
	k_sem_reset(&cmd->done);
	cmd->pending = true;

	ret = wifi_ncp_nwp_send(dev, hdr, head, head_len, body, body_len);
	if (!ret && k_sem_take(&cmd->done, K_MSEC(CONFIG_WIFI_SILABS_NCP_CMD_TIMEOUT_MS))) {
		LOG_ERR("%s: command 0x%02x: no confirmation", dev->name, hdr->command_id);
		ret = -ETIMEDOUT;
	}

	/* A confirmation arriving after the timeout would be matched against
	 * a command that is over; the co-processor is not expected to be that
	 * late.
	 */
	cmd->pending = false;

	if (!ret) {
		ret = cmd->status;
		if (resp_size) {
			*resp_size = cmd->resp_size;
		}
		LOG_DBG("%s: command 0x%02x: status 0x%04x", dev->name, hdr->command_id, ret);
	}

	k_mutex_unlock(&cmd->lock);

	return ret;
}

/* Deliver a confirmation to the command waiting for it, if any */
static bool wifi_ncp_cmd_confirm(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
				 const uint8_t *payload, size_t len)
{
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_cmd *cmd = &data->cmd;

	if (!cmd->pending || cmd->id != hdr->command_id) {
		return false;
	}

	cmd->status = wifi_ncp_nwp_status(hdr);
	cmd->resp_size = MIN(len, cmd->resp_size);
	memcpy(cmd->resp, payload, cmd->resp_size);
	cmd->pending = false;
	k_sem_give(&cmd->done);

	return true;
}

/* Keep a beacon or probe response for the supplicant. The information
 * elements are truncated to whole elements when they do not fit.
 */
static void wifi_ncp_scan_add(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
			      const uint8_t *frame, size_t len)
{
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *)frame;
	struct wifi_ncp_scan_entry *entry = NULL;
	const uint8_t *ie;
	size_t ie_len, kept = 0;
	uint16_t fc, stype;
	uint8_t channel = hdr->channel;

	if (len < IEEE80211_HDRLEN + WIFI_NCP_BEACON_FIXED_LEN) {
		return;
	}
	fc = sys_le16_to_cpu(mgmt->frame_control);
	stype = WLAN_FC_GET_STYPE(fc);
	if (WLAN_FC_GET_TYPE(fc) != WLAN_FC_TYPE_MGMT ||
	    (stype != WLAN_FC_STYPE_BEACON && stype != WLAN_FC_STYPE_PROBE_RESP)) {
		return;
	}

	ie = mgmt->u.beacon.variable;
	ie_len = len - IEEE80211_HDRLEN - WIFI_NCP_BEACON_FIXED_LEN;
	while (kept + 2 <= ie_len && kept + 2 + ie[kept + 1] <= ie_len &&
	       kept + 2 + ie[kept + 1] <= CONFIG_WIFI_SILABS_NCP_SCAN_IE_MAX) {
		kept += 2 + ie[kept + 1];
	}

	k_mutex_lock(&sta->lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(sta->scan); i++) {
		if (sta->scan[i].used &&
		    !memcmp(sta->scan[i].bssid, mgmt->bssid, NET_ETH_ADDR_LEN)) {
			entry = &sta->scan[i];
			break;
		}
		if (!entry && !sta->scan[i].used) {
			entry = &sta->scan[i];
		}
	}
	if (!entry) {
		sta->scan_dropped++;
	} else {
		entry->used = true;
		memcpy(entry->bssid, mgmt->bssid, NET_ETH_ADDR_LEN);
		entry->channel = channel;
		entry->beacon_int = sys_le16_to_cpu(mgmt->u.beacon.beacon_int);
		entry->caps = sys_le16_to_cpu(mgmt->u.beacon.capab_info);
		entry->ie_len = kept;
		memcpy(entry->ie, ie, kept);
	}
	k_mutex_unlock(&sta->lock);
}

static void wifi_ncp_scan_done(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
			       const uint8_t *frame, size_t len)
{
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;
	union wpa_event_data event;
	scan_result_cb_t cb;
	uint16_t status = wifi_ncp_nwp_status(hdr);
	unsigned int dropped;

	ARG_UNUSED(frame);
	ARG_UNUSED(len);

	k_mutex_lock(&sta->lock, K_FOREVER);
	if (!sta->scanning) {
		k_mutex_unlock(&sta->lock);
		LOG_WRN("%s: unexpected scan confirmation (0x%04x)", dev->name, status);
		return;
	}
	sta->scanning = false;
	cb = sta->scan_cb;
	sta->scan_cb = NULL;
	dropped = sta->scan_dropped;
	k_mutex_unlock(&sta->lock);

	LOG_DBG("%s: scan done: 0x%04x", dev->name, status);
	if (dropped) {
		LOG_WRN("%s: %u BSS dropped, raise CONFIG_WIFI_SILABS_NCP_SCAN_RESULTS", dev->name,
			dropped);
	}
	if (cb) {
		wifi_ncp_scan_report(dev, cb);
	} else if (data->supp.if_ctx && data->supp.cb.scan_done) {
		memset(&event, 0, sizeof(event));
		data->supp.cb.scan_done(data->supp.if_ctx, &event);
	}
}

/* The co-processor lost the connection: a join frame with an error status
 * comes without request.
 */
static void wifi_ncp_disconnected(const struct device *dev, uint16_t status, const uint8_t *payload,
				  size_t len)
{
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;
	union wpa_event_data event;
	struct ieee80211_mgmt mgmt;

	LOG_INF("%s: disconnected: 0x%04x", dev->name, status);
	net_if_dormant_on(data->iface);

	if (!data->supp.if_ctx || !data->supp.cb.deauth) {
		return;
	}

	memset(&event, 0, sizeof(event));
	event.deauth_info.addr = sta->bssid;
	if (status == SLI_STATUS_DEAUTHENTICATION_RECEIVED_FROM_AP && len >= 1) {
		event.deauth_info.reason_code = payload[0];
	} else {
		event.deauth_info.reason_code = WLAN_REASON_UNSPECIFIED;
	}
	event.deauth_info.locally_generated = (status == SLI_STATUS_DEAUTH_REQUEST_FROM_SUPPLICANT);
	/* driver_zephyr.c reads the BSSID from the frame */
	memset(&mgmt, 0, sizeof(mgmt));
	memcpy(mgmt.bssid, sta->bssid, NET_ETH_ADDR_LEN);
	data->supp.cb.deauth(data->supp.if_ctx, &event, &mgmt);
}

/* Deferred confirmation of the join. The result is reported to the
 * supplicant, which then runs the key exchange. Once associated, the same
 * frame reports the loss of the connection.
 */
static void wifi_ncp_join_status(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
				 const uint8_t *payload, size_t len)
{
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;
	union wpa_event_data event;
	enum wifi_ncp_sta_state state;
	enum wifi_frequency_bands band = wifi_utils_chan_to_band(sta->channel);
	uint16_t status = wifi_ncp_nwp_status(hdr);

	k_mutex_lock(&sta->lock, K_FOREVER);
	state = sta->state;
	if (state == WIFI_NCP_STA_JOINING && status != SLI_STATUS_JOIN_SAE_TRIGGER) {
		sta->state = !status ? WIFI_NCP_STA_ASSOCIATED : WIFI_NCP_STA_IDLE;
	} else if (state == WIFI_NCP_STA_ASSOCIATED && status) {
		sta->state = WIFI_NCP_STA_IDLE;
	}
	k_mutex_unlock(&sta->lock);

	if (state == WIFI_NCP_STA_ASSOCIATED && status) {
		wifi_ncp_disconnected(dev, status, payload, len);
		return;
	}

	if (!data->supp.if_ctx || !data->supp.cb.assoc_resp) {
		return;
	}

	memset(&event, 0, sizeof(event));
	if (state != WIFI_NCP_STA_JOINING) {
		LOG_WRN("%s: unexpected join status 0x%04x", dev->name, status);
	} else if (status == SLI_STATUS_JOIN_SAE_TRIGGER) {
		/* The co-processor waits for the supplicant to authenticate;
		 * the join goes on once SLI_MGMTIF_CMD_SAE_CONFIRM_SUCCESS is
		 * sent and is confirmed later.
		 */
		LOG_INF("%s: SAE authentication requested", dev->name);
		if (!data->supp.cb.external_auth) {
			LOG_ERR("%s: the supplicant cannot authenticate", dev->name);
			return;
		}
		event.external_auth.action = EXT_AUTH_START;
		event.external_auth.bssid = sta->bssid;
		event.external_auth.ssid = sta->ssid;
		event.external_auth.ssid_len = sta->ssid_len;
		event.external_auth.key_mgmt_suite = RSN_AUTH_KEY_MGMT_SAE;
		data->supp.cb.external_auth(data->supp.if_ctx, &event);
	} else if (!status) {
		LOG_INF("%s: associated", dev->name);
		event.assoc_info.addr = sta->bssid;
		event.assoc_info.req_ies = sta->wpa_ie;
		event.assoc_info.req_ies_len = sta->wpa_ie_len;
		event.assoc_info.freq = wifi_utils_chan_to_freq(band, sta->channel);
		/* No key exchange on an open network */
		event.assoc_info.authorized = (sta->key_mgmt == WPA_KEY_MGMT_NONE) ? 1 : 0;
		data->supp.cb.assoc_resp(data->supp.if_ctx, &event, WLAN_STATUS_SUCCESS);
	} else {
		LOG_WRN("%s: join failed: 0x%04x", dev->name, status);
		event.assoc_reject.bssid = sta->bssid;
		event.assoc_reject.status_code = WLAN_STATUS_UNSPECIFIED_FAILURE;
		data->supp.cb.assoc_resp(data->supp.if_ctx, &event,
					 WLAN_STATUS_UNSPECIFIED_FAILURE);
	}
}

/* Only the last EAPOL frame of the handshake is reported, not used yet */
static void wifi_ncp_tx_status(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
			       const uint8_t *frame, size_t len)
{
	ARG_UNUSED(frame);
	ARG_UNUSED(len);

	LOG_INF("%s: TX status: subtype 0x%02x, token 0x%04x", dev->name, hdr->sta_id,
		wifi_ncp_nwp_status(hdr));
}

/* The key installation is confirmed only when it fails */
static void wifi_ncp_set_keys_failed(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
				     const uint8_t *frame, size_t len)
{
	ARG_UNUSED(frame);
	ARG_UNUSED(len);

	LOG_ERR("%s: set key failed: 0x%04x", dev->name,
		wifi_ncp_nwp_status(hdr));
}

static void wifi_ncp_card_ready(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
				const uint8_t *frame, size_t len)
{
	struct wifi_ncp_data *data = dev->data;

	ARG_UNUSED(hdr);
	ARG_UNUSED(frame);
	ARG_UNUSED(len);

	LOG_DBG("%s: network processor ready", dev->name);
	k_sem_give(&data->card_ready);
}

/* Authentication frame of the SAE exchange, for the supplicant */
static void wifi_ncp_auth_rx(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
			     const uint8_t *frame, size_t len)
{
	struct wifi_ncp_data *data = dev->data;
	enum wifi_frequency_bands band = wifi_utils_chan_to_band(data->sta.channel);

	ARG_UNUSED(hdr);

	if (data->supp.if_ctx && data->supp.cb.mgmt_rx) {
		data->supp.cb.mgmt_rx(data->supp.if_ctx, (char *)frame, len,
				      wifi_utils_chan_to_freq(band, data->sta.channel), 0);
	}
}

/* Confirmations of the deferred commands and indications */
static const struct {
	uint8_t id;
	void (*fn)(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
		   const uint8_t *frame, size_t len);
} wifi_ncp_rx_handlers[] = {
	{ SLI_WIFI_IND_CARD_READY, wifi_ncp_card_ready },
	{ SLI_WIFI_IND_ON_AIR_MGMT, wifi_ncp_scan_add },
	{ SLI_WIFI_CMD_SCAN, wifi_ncp_scan_done },
	{ SLI_WIFI_CMD_JOIN, wifi_ncp_join_status },
	{ SLI_NWP_IND_ON_AIR_MGMT, wifi_ncp_auth_rx },
	{ SLI_NWP_IND_TX_STATUS, wifi_ncp_tx_status },
	{ SLI_MGMTIF_CMD_SET_KEYS, wifi_ncp_set_keys_failed },
};

void wifi_ncp_sta_rx(const struct device *dev, const uint8_t *frame, size_t len)
{
	const struct wifi_ncp_nwp_hdr *hdr = (const struct wifi_ncp_nwp_hdr *)frame;
	const uint8_t *payload;
	size_t payload_len;

	if (len < sizeof(*hdr) || len < sizeof(*hdr) + hdr->ext_desc_size) {
		LOG_WRN("%s: short management frame (%u bytes)", dev->name, len);
		return;
	}
	/* The extended descriptor is not used */
	payload = frame + sizeof(*hdr) + hdr->ext_desc_size;
	payload_len = len - sizeof(*hdr) - hdr->ext_desc_size;

	/* EAPOL frames arrive on the management endpoint, as data frames */
	if (wifi_ncp_nwp_queue(hdr) == WIFI_NCP_NWP_QUEUE_DATA) {
		wifi_ncp_eth_recv(dev, payload, payload_len);
		return;
	}

	if (wifi_ncp_cmd_confirm(dev, hdr, payload, payload_len)) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(wifi_ncp_rx_handlers); i++) {
		if (wifi_ncp_rx_handlers[i].id == hdr->command_id) {
			wifi_ncp_rx_handlers[i].fn(dev, hdr, payload, payload_len);
			return;
		}
	}
	LOG_WRN("%s: unhandled frame 0x%02x, status 0x%04x, %u bytes", dev->name, hdr->command_id,
		wifi_ncp_nwp_status(hdr), payload_len);
}

const uint8_t wifi_ncp_5ghz_channels[WIFI_NCP_5GHZ_CHANNELS] = {
	36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116,
	120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165,
};

/* Ranges of the 5 GHz band, as bits of the scan bitmap */
#define WIFI_NCP_5GHZ_36_64   GENMASK(7, 0)
#define WIFI_NCP_5GHZ_100_140 GENMASK(18, 8)
#define WIFI_NCP_5GHZ_144     BIT(19)
#define WIFI_NCP_5GHZ_149_165 GENMASK(24, 20)

/* Channels the network processor may scan, on the band it was started on: it
 * rejects a scan naming a channel outside its regulatory region. The 5 GHz
 * allocations are the usual ones, they are not reported by the processor.
 */
static struct wifi_ncp_channels wifi_ncp_scan_region_channels(const struct wifi_ncp_sta *sta)
{
	struct wifi_ncp_channels allowed = { };

	switch (sta->region) {
	case SL_WIFI_REGION_JP:
		allowed.band_2_4 = BIT_MASK(14);
		allowed.band_5 = WIFI_NCP_5GHZ_36_64 | WIFI_NCP_5GHZ_100_140;
		break;
	case SL_WIFI_REGION_EU:
	case SL_WIFI_REGION_WORLD_DOMAIN:
	case SL_WIFI_REGION_WORLD_SAFE:
		allowed.band_2_4 = BIT_MASK(13);
		allowed.band_5 = WIFI_NCP_5GHZ_36_64 | WIFI_NCP_5GHZ_100_140;
		break;
	case SL_WIFI_REGION_KR:
		allowed.band_2_4 = BIT_MASK(13);
		allowed.band_5 = WIFI_NCP_5GHZ_36_64 | WIFI_NCP_5GHZ_100_140 |
				 WIFI_NCP_5GHZ_144 | WIFI_NCP_5GHZ_149_165;
		break;
	case SL_WIFI_REGION_CN:
		allowed.band_2_4 = BIT_MASK(13);
		allowed.band_5 = WIFI_NCP_5GHZ_36_64 | WIFI_NCP_5GHZ_149_165;
		break;
	default:
		allowed.band_2_4 = BIT_MASK(11);
		allowed.band_5 = WIFI_NCP_5GHZ_36_64 | WIFI_NCP_5GHZ_100_140 |
				 WIFI_NCP_5GHZ_144 | WIFI_NCP_5GHZ_149_165;
		break;
	}
	if (IS_ENABLED(CONFIG_WIFI_SILABS_NCP_BAND_5GHZ)) {
		allowed.band_2_4 = 0;
	} else {
		allowed.band_5 = 0;
	}

	return allowed;
}

/* Add a channel to a set, -EINVAL when the driver does not scan it */
static int wifi_ncp_channels_add(struct wifi_ncp_channels *set, enum wifi_frequency_bands band,
				 uint16_t channel)
{
	if (band == WIFI_FREQ_BAND_2_4_GHZ && channel >= 1 && channel <= 14) {
		set->band_2_4 |= BIT(channel - 1);
		return 0;
	}
	if (band == WIFI_FREQ_BAND_5_GHZ) {
		for (size_t i = 0; i < ARRAY_SIZE(wifi_ncp_5ghz_channels); i++) {
			if (wifi_ncp_5ghz_channels[i] == channel) {
				set->band_5 |= BIT(i);
				return 0;
			}
		}
	}

	return -EINVAL;
}

static bool wifi_ncp_channels_empty(const struct wifi_ncp_channels *set)
{
	return !set->band_2_4 && !set->band_5;
}

/* The channel of a set holding only one, else 0 */
static uint8_t wifi_ncp_channels_single(const struct wifi_ncp_channels *set)
{
	if (!set->band_5 && POPCOUNT(set->band_2_4) == 1) {
		return find_lsb_set(set->band_2_4);
	}
	if (!set->band_2_4 && POPCOUNT(set->band_5) == 1) {
		return wifi_ncp_5ghz_channels[find_lsb_set(set->band_5) - 1];
	}

	return 0;
}

/* Start a scan on the given channels, none for all the channels of the region,
 * for one SSID or any. The results are reported to cb when given, else to the
 * supplicant.
 */
static int wifi_ncp_scan_start(const struct device *dev, const struct wifi_ncp_channels *channels,
			       const uint8_t *ssid, size_t ssid_len, bool passive,
			       scan_result_cb_t cb)
{
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;
	struct wifi_ncp_nwp_hdr hdr = {.command_id = SLI_WIFI_CMD_SCAN};
	sli_mgmtif_request_scan_t req = { };
	struct wifi_ncp_channels set = wifi_ncp_scan_region_channels(sta);
	uint8_t single;
	int ret;

	if (!wifi_ncp_channels_empty(channels)) {
		set.band_2_4 &= channels->band_2_4;
		set.band_5 &= channels->band_5;
		if (wifi_ncp_channels_empty(&set)) {
			return -EINVAL;
		}
	}
	/* The channels must be named: with channel 0 and no bitmap, the
	 * processor only listens on its current channel. In the host SDK a
	 * single channel goes in the channel field, several in the bitmap.
	 */
	single = wifi_ncp_channels_single(&set);
	if (single) {
		req.scan_req.channel[0] = single;
	} else {
		sys_put_le16(set.band_2_4, req.scan_req.channel_bit_map_2_4);
		sys_put_le32(set.band_5, req.scan_req.channel_bit_map_5);
	}
	if (ssid && ssid_len > 0 && ssid_len < SLI_WIFI_SSID_LEN) {
		memcpy(req.scan_req.ssid, ssid, ssid_len);
	}
	if (passive) {
		req.scan_req.pscan_bitmap = SLI_WIFI_ENABLE_PASSIVE_SCAN;
	}
	req.scan_req.power_level = SLI_WIFI_TX_POWER_DECIDBM_MAX;

	k_mutex_lock(&sta->lock, K_FOREVER);
	if (sta->scanning) {
		k_mutex_unlock(&sta->lock);
		return -EBUSY;
	}
	for (size_t i = 0; i < ARRAY_SIZE(sta->scan); i++) {
		sta->scan[i].used = false;
	}
	sta->scan_dropped = 0;
	sta->scanning = true;
	sta->scan_cb = cb;
	k_mutex_unlock(&sta->lock);

	/* The confirmation comes once the scan is over */
	ret = wifi_ncp_nwp_send(dev, &hdr, &req, sizeof(req), NULL, 0);
	if (ret < 0) {
		k_mutex_lock(&sta->lock, K_FOREVER);
		sta->scanning = false;
		sta->scan_cb = NULL;
		k_mutex_unlock(&sta->lock);
	}

	return ret;
}

int wifi_ncp_sta_scan(void *if_priv, struct wpa_driver_scan_params *params)
{
	const struct device *dev = if_priv;
	const uint8_t *ssid = NULL;
	size_t ssid_len = 0;
	struct wifi_ncp_channels channels = { };

	/* Frequencies outside the bands scanned are ignored */
	for (size_t i = 0; params->freqs && params->freqs[i]; i++) {
		uint16_t chan = wifi_utils_freq_to_chan(params->freqs[i]);

		wifi_ncp_channels_add(&channels, wifi_utils_chan_to_band(chan), chan);
	}
	if (params->freqs && params->freqs[0] && wifi_ncp_channels_empty(&channels)) {
		return -EINVAL;
	}
	if (params->num_ssids > 0) {
		ssid = params->ssids[0].ssid;
		ssid_len = params->ssids[0].ssid_len;
	}

	return wifi_ncp_scan_start(dev, &channels, ssid, ssid_len, false, NULL);
}

int wifi_ncp_sta_mgmt_scan(const struct device *dev, struct net_if *iface,
			   struct wifi_scan_params *params, scan_result_cb_t cb)
{
	const uint8_t *ssid = NULL;
	size_t ssid_len = 0;
	struct wifi_ncp_channels channels = { };
	bool passive = false;

	ARG_UNUSED(iface);

	if (params) {
		/* Channels outside the bands scanned are ignored */
		for (size_t i = 0;
		     i < ARRAY_SIZE(params->band_chan) && params->band_chan[i].channel; i++) {
			wifi_ncp_channels_add(&channels, params->band_chan[i].band,
					      params->band_chan[i].channel);
		}
		if (params->band_chan[0].channel && wifi_ncp_channels_empty(&channels)) {
			return -EINVAL;
		}
		if (params->ssids[0]) {
			ssid = params->ssids[0];
			ssid_len = strlen(params->ssids[0]);
		}
		passive = (params->scan_type == WIFI_SCAN_TYPE_PASSIVE);
	}

	return wifi_ncp_scan_start(dev, &channels, ssid, ssid_len, passive, cb);
}

int wifi_ncp_sta_scan_abort(void *if_priv)
{
	const struct device *dev = if_priv;

	/* The co-processor completes the scan on its own */
	LOG_DBG("%s: scan abort ignored", dev->name);

	return 0;
}

/* Replay the kept results to the supplicant. The results have to be reported
 * here and not during the scan: driver_zephyr.c drops them until this call.
 */
int wifi_ncp_sta_get_scan_results(void *if_priv)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;
	struct wpa_scan_res *res = (struct wpa_scan_res *)sta->scratch;
	size_t count = 0, sent = 0;

	if (!data->supp.if_ctx || !data->supp.cb.scan_res) {
		return -EINVAL;
	}

	k_mutex_lock(&sta->lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(sta->scan); i++) {
		if (sta->scan[i].used) {
			count++;
		}
	}
	if (!count) {
		k_mutex_unlock(&sta->lock);
		data->supp.if_ctx->scan_res2_get_in_prog = false;
		k_sem_give(&data->supp.if_ctx->drv_resp_sem);
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(sta->scan); i++) {
		const struct wifi_ncp_scan_entry *entry = &sta->scan[i];

		if (!entry->used) {
			continue;
		}
		memset(res, 0, sizeof(*res));
		memcpy(res->bssid, entry->bssid, NET_ETH_ADDR_LEN);
		res->freq = wifi_utils_chan_to_freq(wifi_utils_chan_to_band(entry->channel),
						    entry->channel);
		res->beacon_int = entry->beacon_int;
		res->caps = entry->caps;
		/* The signal level is not reported by the co-processor */
		res->level = 0;
		res->noise = -95;
		res->ie_len = entry->ie_len;
		memcpy(res + 1, entry->ie, entry->ie_len);
		sent++;
		data->supp.cb.scan_res(data->supp.if_ctx, res, sent < count);
	}
	k_mutex_unlock(&sta->lock);

	return 0;
}

static const uint8_t *wifi_ncp_ie_find(const uint8_t *ies, size_t len, uint8_t eid)
{
	size_t off = 0;

	while (off + 2 <= len && off + 2 + ies[off + 1] <= len) {
		if (ies[off] == eid) {
			return &ies[off];
		}
		off += 2 + ies[off + 1];
	}

	return NULL;
}

static const uint8_t *wifi_ncp_ie_find_ext(const uint8_t *ies, size_t len, uint8_t ext_eid)
{
	size_t off = 0;

	while (off + 2 <= len && off + 2 + ies[off + 1] <= len) {
		if (ies[off] == WLAN_EID_EXTENSION && ies[off + 1] >= 1 &&
		    ies[off + 2] == ext_eid) {
			return &ies[off];
		}
		off += 2 + ies[off + 1];
	}

	return NULL;
}

static const uint8_t *wifi_ncp_ie_find_vendor(const uint8_t *ies, size_t len, const uint8_t *oui,
					      uint8_t type)
{
	size_t off = 0;

	while (off + 2 <= len && off + 2 + ies[off + 1] <= len) {
		if (ies[off] == WLAN_EID_VENDOR_SPECIFIC && ies[off + 1] >= 4 &&
		    !memcmp(&ies[off + 2], oui, 3) && ies[off + 5] == type) {
			return &ies[off];
		}
		off += 2 + ies[off + 1];
	}

	return NULL;
}

/* Security of a BSS, from its RSN or WPA element */
static void wifi_ncp_scan_security(const struct wifi_ncp_scan_entry *entry,
				   struct wifi_scan_result *res)
{
	static const uint8_t wpa_oui[] = {0x00, 0x50, 0xf2};
	const uint8_t *ie;
	struct wpa_ie_data rsn;

	ie = wifi_ncp_ie_find(entry->ie, entry->ie_len, WLAN_EID_RSN);
	if (ie && !wpa_parse_wpa_ie_rsn(ie, 2 + ie[1], &rsn)) {
		if (rsn.key_mgmt & WPA_KEY_MGMT_SAE) {
			res->security = WIFI_SECURITY_TYPE_SAE;
		} else if (rsn.key_mgmt & WPA_KEY_MGMT_PSK_SHA256) {
			res->security = WIFI_SECURITY_TYPE_PSK_SHA256;
		} else if (rsn.key_mgmt & WPA_KEY_MGMT_PSK) {
			res->security = WIFI_SECURITY_TYPE_PSK;
		} else {
			res->security = WIFI_SECURITY_TYPE_UNKNOWN;
		}
		if (rsn.capabilities & WPA_CAPABILITY_MFPR) {
			res->mfp = WIFI_MFP_REQUIRED;
		} else if (rsn.capabilities & WPA_CAPABILITY_MFPC) {
			res->mfp = WIFI_MFP_OPTIONAL;
		}
		return;
	}
	if (wifi_ncp_ie_find_vendor(entry->ie, entry->ie_len, wpa_oui, 1)) {
		res->security = WIFI_SECURITY_TYPE_WPA_PSK;
	} else if (entry->caps & WLAN_CAPABILITY_PRIVACY) {
		res->security = WIFI_SECURITY_TYPE_WEP;
	} else {
		res->security = WIFI_SECURITY_TYPE_NONE;
	}
}

/* Report the kept results through the Wi-Fi management API. The signal level
 * is not known.
 */
static void wifi_ncp_scan_report(const struct device *dev, scan_result_cb_t cb)
{
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;
	struct wifi_scan_result res;
	const uint8_t *ie;

	k_mutex_lock(&sta->lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(sta->scan); i++) {
		const struct wifi_ncp_scan_entry *entry = &sta->scan[i];

		if (!entry->used) {
			continue;
		}
		memset(&res, 0, sizeof(res));
		ie = wifi_ncp_ie_find(entry->ie, entry->ie_len, WLAN_EID_SSID);
		if (ie && ie[1] <= WIFI_SSID_MAX_LEN) {
			res.ssid_length = ie[1];
			memcpy(res.ssid, &ie[2], ie[1]);
		}
		memcpy(res.mac, entry->bssid, WIFI_MAC_ADDR_LEN);
		res.mac_length = WIFI_MAC_ADDR_LEN;
		res.channel = entry->channel;
		res.band = wifi_utils_chan_to_band(entry->channel);
		wifi_ncp_scan_security(entry, &res);
		cb(data->iface, 0, &res);
	}
	k_mutex_unlock(&sta->lock);

	cb(data->iface, 0, NULL);
}

/* Describe the selected BSS to the co-processor, from the beacon kept during
 * the scan. This is what the WiseConnect management interface appends to the
 * join request.
 */
static void wifi_ncp_join_ap_info(sli_wifi_sta_join_ap_info_t *info,
				  const struct wifi_ncp_scan_entry *entry, uint8_t sec_mode)
{
	static const uint8_t wmm_oui[] = {0x00, 0x50, 0xf2};
	/* First member of the packed structure, hence aligned */
	sli_wifi_scan_result_t *res = (sli_wifi_scan_result_t *)info;
	const uint8_t *ie;
	struct wpa_ie_data rsn;

	memset(info, 0, sizeof(*info));
	res->channel_num = entry->channel;
	res->sec_mode = sec_mode;
	memcpy(res->bssid, entry->bssid, NET_ETH_ADDR_LEN);
	res->beacon_interval = entry->beacon_int;
	res->capability_info = entry->caps;
	info->ap_rrm_capable = (entry->caps & WLAN_CAPABILITY_RADIO_MEASUREMENT) ? 1 : 0;

	ie = wifi_ncp_ie_find(entry->ie, entry->ie_len, WLAN_EID_SSID);
	if (ie && ie[1] < sizeof(res->ssid)) {
		res->ssid_len = ie[1];
		memcpy(res->ssid, &ie[2], ie[1]);
	}
	ie = wifi_ncp_ie_find(entry->ie, entry->ie_len, WLAN_EID_SUPP_RATES);
	if (ie) {
		res->supported_rates.rates_len = MIN(ie[1], SLI_WIFI_RATES_LEN);
		memcpy(res->supported_rates.rates, &ie[2], res->supported_rates.rates_len);
	}
	ie = wifi_ncp_ie_find(entry->ie, entry->ie_len, WLAN_EID_EXT_SUPP_RATES);
	if (ie) {
		res->supported_rates.ext_rates_len = MIN(ie[1], SLI_WIFI_RATES_LEN);
		memcpy(res->supported_rates.ext_rates, &ie[2], res->supported_rates.ext_rates_len);
	}
	ie = wifi_ncp_ie_find(entry->ie, entry->ie_len, WLAN_EID_HT_CAP);
	if (ie && 2 + ie[1] <= sizeof(res->ht_cap_ie)) {
		memcpy(res->ht_cap_ie, ie, 2 + ie[1]);
		res->ht_cap_present = 1;
	}
	ie = wifi_ncp_ie_find_vendor(entry->ie, entry->ie_len, wmm_oui, WMM_OUI_TYPE);
	if (ie) {
		res->qos_enable = 1;
	}
	ie = wifi_ncp_ie_find(entry->ie, entry->ie_len, WLAN_EID_RSN);
	if (ie && !wpa_parse_wpa_ie_rsn(ie, 2 + ie[1], &rsn)) {
		/* Cipher suite selector of the group cipher */
		if (rsn.group_cipher & WPA_CIPHER_CCMP) {
			res->group_cipher = RSN_CIPHER_SUITE_CCMP & 0xff;
		} else if (rsn.group_cipher & WPA_CIPHER_TKIP) {
			res->group_cipher = RSN_CIPHER_SUITE_TKIP & 0xff;
		}
		res->mfpc = (rsn.capabilities & WPA_CAPABILITY_MFPC) ? 1 : 0;
	}
	res->vht_capable = wifi_ncp_ie_find(entry->ie, entry->ie_len, WLAN_EID_VHT_CAP) ? 1 : 0;
	res->he_capable =
		wifi_ncp_ie_find_ext(entry->ie, entry->ie_len, WLAN_EID_EXT_HE_CAPABILITIES) ? 1
											     : 0;
}

static uint8_t wifi_ncp_security_type(unsigned int key_mgmt)
{
	if (key_mgmt & WPA_KEY_MGMT_SAE) {
		return SL_WIFI_WPA3;
	}
	if ((key_mgmt & (WPA_KEY_MGMT_PSK | WPA_KEY_MGMT_PSK_SHA256)) != 0) {
		return SL_WIFI_WPA2;
	}
	return SL_WIFI_OPEN;
}

/* Load the RSN element of the association request, then join. The join is
 * confirmed later, once the co-processor has authenticated and associated.
 */
int wifi_ncp_sta_associate(void *if_priv, struct wpa_driver_associate_params *params)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;
	struct wifi_ncp_nwp_hdr load_hdr = {.command_id = SLI_MGMTIF_CMD_LOAD_IE_INFO};
	struct wifi_ncp_nwp_hdr join_hdr = {.command_id = SLI_WIFI_CMD_JOIN};
	sli_wifi_sta_join_ap_info_t *ap_info = (sli_wifi_sta_join_ap_info_t *)sta->scratch;
	const struct wifi_ncp_scan_entry *entry = NULL;
	sli_mgmtif_load_ie_t load = { };
	sli_mgmtif_join_request_t join = { };
	int ret;

	if (!params->bssid || !params->ssid || params->ssid_len > WIFI_SSID_MAX_LEN ||
	    params->wpa_ie_len > WIFI_NCP_WPA_IE_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&sta->lock, K_FOREVER);
	if (sta->state != WIFI_NCP_STA_IDLE) {
		k_mutex_unlock(&sta->lock);
		return -EBUSY;
	}
	for (size_t i = 0; i < ARRAY_SIZE(sta->scan); i++) {
		if (sta->scan[i].used &&
		    !memcmp(sta->scan[i].bssid, params->bssid, NET_ETH_ADDR_LEN)) {
			entry = &sta->scan[i];
			break;
		}
	}
	if (!entry) {
		k_mutex_unlock(&sta->lock);
		LOG_ERR("%s: BSS not found in the scan results", dev->name);
		return -ENOENT;
	}

	memcpy(sta->bssid, params->bssid, NET_ETH_ADDR_LEN);
	memcpy(sta->ssid, params->ssid, params->ssid_len);
	sta->ssid_len = params->ssid_len;
	sta->channel = entry->channel;
	sta->beacon_int = entry->beacon_int;
	sta->key_mgmt = params->key_mgmt_suite;
	sta->wpa_ie_len = params->wpa_ie_len;
	if (params->wpa_ie_len > 0) {
		memcpy(sta->wpa_ie, params->wpa_ie, params->wpa_ie_len);
	}

	load.frame_type = SLI_WIFI_IE_ASSOC_REQ;
	load.ie_length = MIN(params->wpa_ie_len, SLI_WIFI_MAX_IE_SIZE);
	memcpy(load.ie_data, params->wpa_ie, load.ie_length);

	join.join_req.security_type = wifi_ncp_security_type(params->key_mgmt_suite);
	join.join_req.power_level = SLI_WIFI_TX_POWER_DECIDBM_MAX;
	memcpy(join.join_req.ssid, params->ssid, params->ssid_len);
	join.join_req.ssid_len = params->ssid_len;
	join.join_req.listen_interval_multiplier = SLI_WIFI_LISTEN_INTERVAL_MULTIPLIER_DEFAULT;
	join.join_req.vap_id = SLI_WIFI_STA_MODE;
	memcpy(join.join_req.join_bssid, params->bssid, NET_ETH_ADDR_LEN);
	join.join_ext_info.vif_type = SLI_VIF_TYPE_STA;
	memcpy(join.join_ext_info.bssid, params->bssid, NET_ETH_ADDR_LEN);
	join.join_ext_info.wps_session = SLI_WIFI_WPS_SESSION_DISABLE;
	join.join_ext_info.sae_pmksa_caching = SLI_WIFI_SAE_PMKSA_CACHING_DISABLE;
	wifi_ncp_join_ap_info(ap_info, entry, join.join_req.security_type);
	sta->state = WIFI_NCP_STA_JOINING;
	k_mutex_unlock(&sta->lock);

	ret = wifi_ncp_nwp_send(dev, &load_hdr, &load, sizeof(load), NULL, 0);
	if (!ret) {
		ret = wifi_ncp_nwp_send(dev, &join_hdr, &join, sizeof(join), ap_info,
					sizeof(*ap_info));
	}
	if (ret) {
		k_mutex_lock(&sta->lock, K_FOREVER);
		sta->state = WIFI_NCP_STA_IDLE;
		k_mutex_unlock(&sta->lock);
	}

	return ret;
}

int wifi_ncp_sta_deauthenticate(void *if_priv, const char *addr, unsigned short reason_code)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;
	struct wifi_ncp_nwp_hdr hdr = {.command_id = SLI_WIFI_CMD_DISCONNECT};
	sli_mgmtif_disconnect_req_t req;
	int ret;

	ARG_UNUSED(addr);
	ARG_UNUSED(reason_code);

	k_mutex_lock(&sta->lock, K_FOREVER);
	sta->state = WIFI_NCP_STA_IDLE;
	k_mutex_unlock(&sta->lock);
	net_if_dormant_on(data->iface);

	memset(&req, 0, sizeof(req));
	req.disconn_req.mode_flag = SLI_WIFI_STA_MODE;
	req.host_initiated = SLI_WIFI_DISCONN_FROM_HOST;
	ret = wifi_ncp_nwp_cmd(dev, &hdr, &req, sizeof(req), NULL, 0, NULL, NULL);
	if (ret > 0) {
		/* Not connected, for instance */
		LOG_DBG("%s: disconnect: 0x%04x", dev->name, ret);
		ret = 0;
	}

	return ret;
}

int wifi_ncp_sta_set_key(void *if_priv, const unsigned char *ifname, enum wpa_alg alg,
			 const unsigned char *addr, int key_idx, int set_tx,
			 const unsigned char *seq, size_t seq_len, const unsigned char *key,
			 size_t key_len, enum key_flag key_flag)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_nwp_hdr hdr = {.command_id = SLI_MGMTIF_CMD_SET_KEYS};
	sli_mgmtif_set_keys_t keys;

	ARG_UNUSED(ifname);
	ARG_UNUSED(addr);
	ARG_UNUSED(set_tx);
	ARG_UNUSED(seq);
	ARG_UNUSED(seq_len);

	/* Key removal is implied by the disconnection */
	if (alg == WPA_ALG_NONE) {
		return 0;
	}
	if (!key || key_len > SLI_WIFI_MAX_KEY_LEN || alg > 0xf) {
		return -EINVAL;
	}

	memset(&keys, 0, sizeof(keys));
	/* The high nibble is the algorithm, aligned with enum wpa_alg */
	keys.type = (alg << 4) | ((key_flag & KEY_FLAG_PAIRWISE) ? SLI_WIFI_KEY_TYPE_PTK
								 : SLI_WIFI_KEY_TYPE_GTK);
	/* The key is installed, not only stored, whatever set_tx says: the
	 * group key of a station is installed too.
	 */
	keys.key_id = SLI_WIFI_INSTALL_KEY | (key_idx & 0x3);
	memcpy(keys.key, key, key_len);
	keys.key_len = key_len;
	hdr.iface = SLI_WIFI_STA_MODE;

	/* The network processor answers only to report a failure */
	return wifi_ncp_nwp_send(dev, &hdr, &keys, sizeof(keys), NULL, 0);
}

/* The supplicant opens the port once the key exchange is over */
int wifi_ncp_sta_set_supp_port(void *if_priv, int authorized, char *bssid)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_data *data = dev->data;

	ARG_UNUSED(bssid);

	if (authorized) {
		LOG_INF("%s: connected", dev->name);
		net_if_dormant_off(data->iface);
	} else {
		net_if_dormant_on(data->iface);
	}

	return 0;
}

/* The last EAPOL-Key frame of a handshake carries a MIC and the Secure bit
 * without requesting an acknowledgment: message 4 of the 4-way handshake,
 * message 2 of the group key handshake.
 */
static bool wifi_ncp_eapol_is_last(unsigned short proto, const uint8_t *buf, size_t len)
{
	const struct ieee802_1x_hdr *hdr = (const struct ieee802_1x_hdr *)buf;
	const struct wpa_eapol_key *key = (const struct wpa_eapol_key *)(hdr + 1);
	uint16_t key_info;

	if (proto != ETH_P_EAPOL || len < sizeof(*hdr) + sizeof(*key) ||
	    hdr->type != IEEE802_1X_TYPE_EAPOL_KEY) {
		return false;
	}
	key_info = WPA_GET_BE16(key->key_info);

	return (key_info & (WPA_KEY_INFO_MIC | WPA_KEY_INFO_SECURE | WPA_KEY_INFO_ACK)) ==
	       (WPA_KEY_INFO_MIC | WPA_KEY_INFO_SECURE);
}

/* EAPOL frames go to the co-processor as Ethernet frames, on the management
 * endpoint. Nothing confirms them, except the last one of a handshake: its
 * transmission is reported by a TX status, asked for with a flag and a token
 * in the extended descriptor.
 */
int wifi_ncp_sta_tx_control_port(void *if_priv, const unsigned char *dest, unsigned short proto,
				 const unsigned char *buf, size_t len, int no_encrypt)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_nwp_hdr hdr = { .command_id = SLI_WIFI_TX_DOT11_MGMT_FRAME };
	struct net_linkaddr *own = net_if_get_link_addr(data->iface);
	struct {
		uint32_t token;
		struct net_eth_hdr eth;
	} __packed head;
	const void *head_ptr = &head.eth;
	size_t head_len = sizeof(head.eth);

	ARG_UNUSED(no_encrypt);

	if (!own || own->len != NET_ETH_ADDR_LEN) {
		return -EINVAL;
	}

	memcpy(head.eth.dst.addr, dest, NET_ETH_ADDR_LEN);
	memcpy(head.eth.src.addr, own->addr, NET_ETH_ADDR_LEN);
	head.eth.type = sys_cpu_to_be16(proto);
	if (wifi_ncp_eapol_is_last(proto, buf, len)) {
		hdr.flags |= WIFI_NCP_NWP_FLAG_CONFIRM;
		hdr.ext_desc_size = sizeof(head.token);
		head.token = sys_cpu_to_le32(WIFI_NCP_EAPOL_TOKEN);
		head_ptr = &head;
		head_len = sizeof(head);
	}

	return wifi_ncp_nwp_send(dev, &hdr, head_ptr, head_len, buf, len);
}

/* The signal level is not reported by the co-processor */
int wifi_ncp_sta_signal_poll(void *if_priv, struct wpa_signal_info *si, unsigned char *bssid)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_sta *sta = &data->sta;

	memset(si, 0, sizeof(*si));
	si->frequency = wifi_utils_chan_to_freq(wifi_utils_chan_to_band(sta->channel),
						sta->channel);
	si->current_noise = -95;
	si->data.signal = 0;
	if (bssid) {
		memcpy(bssid, sta->bssid, NET_ETH_ADDR_LEN);
	}

	return 0;
}

int wifi_ncp_sta_get_conn_info(void *if_priv, struct wpa_conn_info *info)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_data *data = dev->data;

	memset(info, 0, sizeof(*info));
	info->beacon_interval = data->sta.beacon_int;

	return 0;
}

/* Authentication frames built by the supplicant for the SAE exchange. The
 * co-processor is told before the commit is sent, and the confirmation of the
 * transmission is reported as an acknowledgment. The descriptors are filled
 * as in the captures of the host SDK.
 */
int wifi_ncp_sta_send_mlme(void *if_priv, const u8 *data_buf, size_t data_len, int noack,
			   unsigned int freq, int no_cck, int offchanok, unsigned int wait_time,
			   int cookie)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_nwp_hdr tx_hdr = {
		.command_id = SLI_WIFI_TX_DOT11_MGMT_FRAME,
		.mgmt_hdr_len = IEEE80211_HDRLEN,
		.frame_info = 1,
	};
	struct wifi_ncp_nwp_hdr commit_hdr = {
		.command_id = SLI_MGMTIF_CMD_SAE_COMMIT_STATUS,
		.frame_info = 1,
	};
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *)data_buf;
	sli_mgmtif_sae_commit_t commit = SLI_WIFI_SAE_COMMIT_STATE_TRUE;
	uint16_t fc;
	int ret;

	ARG_UNUSED(noack);
	ARG_UNUSED(freq);
	ARG_UNUSED(no_cck);
	ARG_UNUSED(offchanok);
	ARG_UNUSED(wait_time);
	ARG_UNUSED(cookie);

	if (data_len < IEEE80211_HDRLEN + sizeof(mgmt->u.auth)) {
		return -EINVAL;
	}
	fc = sys_le16_to_cpu(mgmt->frame_control);
	if (WLAN_FC_GET_TYPE(fc) != WLAN_FC_TYPE_MGMT ||
	    WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_AUTH) {
		LOG_WRN("%s: only authentication frames can be sent", dev->name);
		return -ENOTSUP;
	}

	if (sys_le16_to_cpu(mgmt->u.auth.auth_transaction) == 1) {
		ret = wifi_ncp_nwp_send(dev, &commit_hdr, &commit, sizeof(commit), NULL, 0);
		if (ret) {
			LOG_ERR("%s: SAE commit status failed: %d", dev->name, ret);
			return -EIO;
		}
	}

	ret = wifi_ncp_nwp_send(dev, &tx_hdr, data_buf, data_len, NULL, 0);
	if (ret) {
		LOG_ERR("%s: authentication frame transmission failed: %d", dev->name, ret);
		return -EIO;
	}

	if (data->supp.if_ctx && data->supp.cb.mgmt_tx_status) {
		data->supp.cb.mgmt_tx_status(data->supp.if_ctx, data_buf, data_len, true);
	}

	return 0;
}

/* The SAE exchange is over: on success the co-processor associates, which
 * confirms the join.
 */
int wifi_ncp_sta_send_external_auth_status(void *if_priv, struct external_auth *params)
{
	const struct device *dev = if_priv;
	struct wifi_ncp_nwp_hdr hdr = {
		.command_id = SLI_MGMTIF_CMD_SAE_CONFIRM_SUCCESS,
		.frame_info = 1,
	};
	int ret;

	if (params->status != WLAN_STATUS_SUCCESS) {
		LOG_WRN("%s: SAE authentication failed: %u", dev->name, params->status);
		return 0;
	}

	ret = wifi_ncp_nwp_send(dev, &hdr, NULL, 0, NULL, 0);
	if (ret) {
		LOG_ERR("%s: SAE confirm success failed: %d", dev->name, ret);
		return -EIO;
	}

	return 0;
}

/* The co-processor is reachable: start the network processor, wait for it to
 * be ready, configure it as a station on the configured band and learn its
 * MAC address. The sequence is the one of the WiseConnect host SDK. A failure
 * reported by the network processor is logged and leaves the device usable,
 * as the firmware is still evolving.
 */
int wifi_ncp_sta_init(const struct device *dev)
{
	struct wifi_ncp_data *data = dev->data;
	struct wifi_ncp_nwp_hdr hdr;
	sl_wifi_system_boot_configuration_t boot = {
		.vif_type = SLI_VIF_TYPE_STA,
		.feature_bit_map = SL_WIFI_FEAT_AGGREGATION,
		.custom_feature_bit_map = SL_WIFI_SYSTEM_CUSTOM_FEAT_EXTENSION_VALID,
		/* Required by the WPA3 modes */
		.ext_custom_feature_bit_map = SL_WIFI_EXT_FEAT_IEEE_80211W,
	};
	/* The fields are reserved, or ignored in station mode */
	sli_wifi_feature_frame_request_t features = { };
	/* Dual band operation is not available yet */
	sl_wifi_band_mode_t band = IS_ENABLED(CONFIG_WIFI_SILABS_NCP_BAND_5GHZ)
					   ? SL_WIFI_BAND_MODE_5GHZ
					   : SL_WIFI_BAND_MODE_2_4GHZ;
	sli_wifi_set_region_request_t region = {
		.set_region_code_from_user_cmd = SET_REGION_CODE_FROM_USER,
		.region_code = SL_WIFI_DEFAULT_REGION,
	};
	/* One or two addresses, when the processor also runs an access point */
	uint8_t mac[2 * NET_ETH_ADDR_LEN];
	size_t mac_len = sizeof(mac);
	uint8_t region_code;
	size_t region_len = sizeof(region_code);
	const struct {
		uint8_t id;
		const void *req;
		size_t req_len;
		void *resp;
		size_t *resp_len;
	} steps[] = {
		{ SLI_WIFI_CMD_OPERMODE, &boot, sizeof(boot), NULL, NULL },
		{ SLI_WIFI_COMMON_CMD_FEATURE_FRAME, &features, sizeof(features), NULL, NULL },
		{ SLI_WIFI_CMD_BAND, &band, sizeof(band), NULL, NULL },
		{ SLI_WIFI_CMD_INIT, NULL, 0, mac, &mac_len },
		{ SLI_WIFI_CMD_SET_REGION, &region, sizeof(region), &region_code, &region_len },
	};
	int ret;

	/* The ready indication answers this frame */
	memset(&hdr, 0, sizeof(hdr));
	hdr.command_id = SLI_WIFI_REQ_NWP_INIT;
	ret = wifi_ncp_nwp_send(dev, &hdr, NULL, 0, NULL, 0);
	if (ret) {
		return ret;
	}
	if (k_sem_take(&data->card_ready, WIFI_NCP_CARD_READY_TIMEOUT) != 0) {
		LOG_WRN("%s: no ready indication from the network processor", dev->name);
	}

	for (size_t i = 0; i < ARRAY_SIZE(steps); i++) {
		memset(&hdr, 0, sizeof(hdr));
		hdr.command_id = steps[i].id;
		ret = wifi_ncp_nwp_cmd(dev, &hdr, steps[i].req, steps[i].req_len, NULL, 0,
				       steps[i].resp, steps[i].resp_len);
		if (ret < 0) {
			return ret;
		}
		if (ret > 0) {
			LOG_WRN("%s: command 0x%02x failed: 0x%04x", dev->name, steps[i].id, ret);
			return 0;
		}
	}

	if (mac_len < NET_ETH_ADDR_LEN) {
		LOG_WRN("%s: init returned no MAC address", dev->name);
	} else {
		memcpy(data->mac, mac, NET_ETH_ADDR_LEN);
		LOG_INF("%s: MAC %02x:%02x:%02x:%02x:%02x:%02x", dev->name, data->mac[0],
			data->mac[1], data->mac[2], data->mac[3], data->mac[4], data->mac[5]);
	}
	if (region_len == sizeof(region_code)) {
		data->sta.region = region_code;
		LOG_DBG("%s: region %u", dev->name, region_code);
	}

	return 0;
}
