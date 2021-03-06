/*
 * Copyright (C) 2017 John Crispin <john@phrozen.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "ustatusd.h"

struct wifi_iface {
	struct avl_node avl;
	uint8_t addr[6];
	char ifname[IF_NAMESIZE];
	struct uloop_timeout assoc;
	struct blob_attr *info;
};

struct wifi_station {
	struct avl_node avl;
	struct blob_attr *info;
	uint8_t addr[6];
	struct uloop_timeout status;
};

static struct status_socket nl80211_status;
static uint8_t nl80211_arg[4096];
struct uloop_timeout nl80211_enum_timer;

static int avl_addrcmp(const void *k1, const void *k2, void *ptr)
{
	return memcmp(k1, k2, 6);
}

static struct avl_tree wif_tree = AVL_TREE_INIT(wif_tree, avl_addrcmp, false, NULL);
static struct avl_tree sta_tree = AVL_TREE_INIT(sta_tree, avl_addrcmp, false, NULL);

static void nl80211_list_wif(void)
{
	struct nl_msg *msg;

	msg = nlmsg_alloc();
	if (!msg)
		return;

	if (!genlmsg_put(msg, 0, 0, genl_ctrl_resolve(nl80211_status.sock, "nl80211"),
			 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0)) {
		nlmsg_free(msg);
		return;
	}

	genl_send_and_recv(&nl80211_status, msg);
}

static void nl80211_assoc_list(struct uloop_timeout *t)
{
	struct wifi_iface *wif = container_of(t, struct wifi_iface, assoc);
	struct nl_msg *msg;
	int idx = if_nametoindex(wif->ifname);

	msg = nlmsg_alloc();
	if (!msg)
		goto out;

	if (!genlmsg_put(msg, 0, 0, genl_ctrl_resolve(nl80211_status.sock, "nl80211"),
			 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0) ||
		nla_put_u32(msg, NL80211_ATTR_IFINDEX, idx)) {
		nlmsg_free(msg);
		goto out;
	}

	genl_send_and_recv(&nl80211_status, msg);

out:
	uloop_timeout_set(t, config.station_poll * 1000);
}

static void nl80211_to_blob(struct nlattr **tb)
{
	blob_buf_init(&b, 0);

	if (tb[NL80211_ATTR_MAC]) {
		uint8_t *addr = nla_data(tb[NL80211_ATTR_MAC]);
		blobmsg_add_mac(&b, "mac", addr);
	}

	if (tb[NL80211_ATTR_IFINDEX]) {
		int idx = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
		blobmsg_add_iface(&b, "interface", idx);
	} else if (tb[NL80211_ATTR_IFNAME]) {
		blobmsg_add_string(&b, "interface", nla_get_string(tb[NL80211_ATTR_IFNAME]));
	}

	if (tb[NL80211_ATTR_WIPHY_TX_POWER_LEVEL])
		blobmsg_add_u32(&b, "dbm", nla_get_u32(tb[NL80211_ATTR_WIPHY_TX_POWER_LEVEL]) / 100);

	if (tb[NL80211_ATTR_IFTYPE])
		blobmsg_add_iftype(&b, "iftype", nla_get_u32(tb[NL80211_ATTR_IFTYPE]));

	if (tb[NL80211_ATTR_WIPHY_FREQ])
		blobmsg_add_u32(&b, "frequency", nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]));
}

static void nl80211_notify(struct nlattr **tb, char *status)
{
	nl80211_to_blob(tb);
	ubus_notify(&conn.ctx, &ubus_object, status, b.head, -1);
}

#ifdef DEBUG
static const char * nl80211_command_to_string(enum nl80211_commands cmd)
{
#define C2S(x) case x: return #x;
	switch (cmd) {
	C2S(NL80211_CMD_UNSPEC)
	C2S(NL80211_CMD_GET_WIPHY)
	C2S(NL80211_CMD_SET_WIPHY)
	C2S(NL80211_CMD_NEW_WIPHY)
	C2S(NL80211_CMD_DEL_WIPHY)
	C2S(NL80211_CMD_GET_INTERFACE)
	C2S(NL80211_CMD_SET_INTERFACE)
	C2S(NL80211_CMD_NEW_INTERFACE)
	C2S(NL80211_CMD_DEL_INTERFACE)
	C2S(NL80211_CMD_GET_KEY)
	C2S(NL80211_CMD_SET_KEY)
	C2S(NL80211_CMD_NEW_KEY)
	C2S(NL80211_CMD_DEL_KEY)
	C2S(NL80211_CMD_GET_BEACON)
	C2S(NL80211_CMD_SET_BEACON)
	C2S(NL80211_CMD_START_AP)
	C2S(NL80211_CMD_STOP_AP)
	C2S(NL80211_CMD_GET_STATION)
	C2S(NL80211_CMD_SET_STATION)
	C2S(NL80211_CMD_NEW_STATION)
	C2S(NL80211_CMD_DEL_STATION)
	C2S(NL80211_CMD_GET_MPATH)
	C2S(NL80211_CMD_SET_MPATH)
	C2S(NL80211_CMD_NEW_MPATH)
	C2S(NL80211_CMD_DEL_MPATH)
	C2S(NL80211_CMD_SET_BSS)
	C2S(NL80211_CMD_SET_REG)
	C2S(NL80211_CMD_REQ_SET_REG)
	C2S(NL80211_CMD_GET_MESH_CONFIG)
	C2S(NL80211_CMD_SET_MESH_CONFIG)
	C2S(NL80211_CMD_SET_MGMT_EXTRA_IE)
	C2S(NL80211_CMD_GET_REG)
	C2S(NL80211_CMD_GET_SCAN)
	C2S(NL80211_CMD_TRIGGER_SCAN)
	C2S(NL80211_CMD_NEW_SCAN_RESULTS)
	C2S(NL80211_CMD_SCAN_ABORTED)
	C2S(NL80211_CMD_REG_CHANGE)
	C2S(NL80211_CMD_AUTHENTICATE)
	C2S(NL80211_CMD_ASSOCIATE)
	C2S(NL80211_CMD_DEAUTHENTICATE)
	C2S(NL80211_CMD_DISASSOCIATE)
	C2S(NL80211_CMD_MICHAEL_MIC_FAILURE)
	C2S(NL80211_CMD_REG_BEACON_HINT)
	C2S(NL80211_CMD_JOIN_IBSS)
	C2S(NL80211_CMD_LEAVE_IBSS)
	C2S(NL80211_CMD_TESTMODE)
	C2S(NL80211_CMD_CONNECT)
	C2S(NL80211_CMD_ROAM)
	C2S(NL80211_CMD_DISCONNECT)
	C2S(NL80211_CMD_SET_WIPHY_NETNS)
	C2S(NL80211_CMD_GET_SURVEY)
	C2S(NL80211_CMD_NEW_SURVEY_RESULTS)
	C2S(NL80211_CMD_SET_PMKSA)
	C2S(NL80211_CMD_DEL_PMKSA)
	C2S(NL80211_CMD_FLUSH_PMKSA)
	C2S(NL80211_CMD_REMAIN_ON_CHANNEL)
	C2S(NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL)
	C2S(NL80211_CMD_SET_TX_BITRATE_MASK)
	C2S(NL80211_CMD_REGISTER_FRAME)
	C2S(NL80211_CMD_FRAME)
	C2S(NL80211_CMD_FRAME_TX_STATUS)
	C2S(NL80211_CMD_SET_POWER_SAVE)
	C2S(NL80211_CMD_GET_POWER_SAVE)
	C2S(NL80211_CMD_SET_CQM)
	C2S(NL80211_CMD_NOTIFY_CQM)
	C2S(NL80211_CMD_SET_CHANNEL)
	C2S(NL80211_CMD_SET_WDS_PEER)
	C2S(NL80211_CMD_FRAME_WAIT_CANCEL)
	C2S(NL80211_CMD_JOIN_MESH)
	C2S(NL80211_CMD_LEAVE_MESH)
	C2S(NL80211_CMD_UNPROT_DEAUTHENTICATE)
	C2S(NL80211_CMD_UNPROT_DISASSOCIATE)
	C2S(NL80211_CMD_NEW_PEER_CANDIDATE)
	C2S(NL80211_CMD_GET_WOWLAN)
	C2S(NL80211_CMD_SET_WOWLAN)
	C2S(NL80211_CMD_START_SCHED_SCAN)
	C2S(NL80211_CMD_STOP_SCHED_SCAN)
	C2S(NL80211_CMD_SCHED_SCAN_RESULTS)
	C2S(NL80211_CMD_SCHED_SCAN_STOPPED)
	C2S(NL80211_CMD_SET_REKEY_OFFLOAD)
	C2S(NL80211_CMD_PMKSA_CANDIDATE)
	C2S(NL80211_CMD_TDLS_OPER)
	C2S(NL80211_CMD_TDLS_MGMT)
	C2S(NL80211_CMD_UNEXPECTED_FRAME)
	C2S(NL80211_CMD_PROBE_CLIENT)
	C2S(NL80211_CMD_REGISTER_BEACONS)
	C2S(NL80211_CMD_UNEXPECTED_4ADDR_FRAME)
	C2S(NL80211_CMD_SET_NOACK_MAP)
	C2S(NL80211_CMD_CH_SWITCH_NOTIFY)
	C2S(NL80211_CMD_START_P2P_DEVICE)
	C2S(NL80211_CMD_STOP_P2P_DEVICE)
	C2S(NL80211_CMD_CONN_FAILED)
	C2S(NL80211_CMD_SET_MCAST_RATE)
	C2S(NL80211_CMD_SET_MAC_ACL)
	C2S(NL80211_CMD_RADAR_DETECT)
	C2S(NL80211_CMD_GET_PROTOCOL_FEATURES)
	C2S(NL80211_CMD_UPDATE_FT_IES)
	C2S(NL80211_CMD_FT_EVENT)
	C2S(NL80211_CMD_CRIT_PROTOCOL_START)
	C2S(NL80211_CMD_CRIT_PROTOCOL_STOP)
	C2S(NL80211_CMD_GET_COALESCE)
	C2S(NL80211_CMD_SET_COALESCE)
	C2S(NL80211_CMD_CHANNEL_SWITCH)
	C2S(NL80211_CMD_VENDOR)
	C2S(NL80211_CMD_SET_QOS_MAP)
	C2S(NL80211_CMD_ADD_TX_TS)
	C2S(NL80211_CMD_DEL_TX_TS)
	default:
		return "NL80211_CMD_UNKNOWN";
	}
#undef C2S
}
#endif

static void nl80211_status_station(struct uloop_timeout *t)
{
	struct wifi_station *sta = container_of(t, struct wifi_station, status);

	ubus_notify(&conn.ctx, &ubus_object, "wifi.status.station", sta->info, -1);

	uloop_timeout_set(t, config.station_status * 1000);
}

static void nl80211_add_station(struct nlattr **tb)
{
	struct wifi_station *sta;
	uint8_t *addr;
	int notify = 0;

	if (tb[NL80211_ATTR_MAC] == NULL)
		return;

	nl80211_to_blob(tb);
	addr = nla_data(tb[NL80211_ATTR_MAC]);

	sta = avl_find_element(&sta_tree, addr, sta, avl);
	if (!sta) {
		sta = malloc(sizeof(*sta));
		if (!sta)
			return;

		memset(sta, 0, sizeof(*sta));
		memcpy(sta->addr, addr, 6);
		sta->avl.key = sta->addr;
		avl_insert(&sta_tree, &sta->avl);
		notify = 1;
		if (config.station_status) {
			sta->status.cb = nl80211_status_station;
			uloop_timeout_set(&sta->status, config.station_status * 1000);
		}
	}
	if (sta->info)
		free(sta->info);
	sta->info = malloc(blob_pad_len(b.head));
	memcpy(sta->info, b.head, blob_pad_len(b.head));
	if (notify)
		ubus_notify(&conn.ctx, &ubus_object, "wifi.new.station", b.head, -1);
}

static void nl80211_del_station(struct nlattr **tb)
{
	struct wifi_station *sta;
	uint8_t *addr;

	if (tb[NL80211_ATTR_MAC] == NULL)
		return;

	addr = nla_data(tb[NL80211_ATTR_MAC]);
	sta = avl_find_element(&sta_tree, addr, sta, avl);
	if (!sta)
		return;
	nl80211_notify(tb, "wifi.del.station");
	avl_delete(&sta_tree, &sta->avl);
	if (sta->info)
		free(sta->info);
	free(sta);
	uloop_timeout_cancel(&sta->status);
}

static void nl80211_add_iface(struct nlattr **tb)
{
	struct wifi_iface *wif;
	uint8_t *addr;
	char *ifname;

	if (tb[NL80211_ATTR_IFNAME] == NULL || tb[NL80211_ATTR_MAC] == NULL)
		return;

	addr = nla_data(tb[NL80211_ATTR_MAC]);
	ifname = nla_get_string(tb[NL80211_ATTR_IFNAME]);

	wif = avl_find_element(&wif_tree, addr, wif, avl);
	if (wif)
		return;

	wif = malloc(sizeof(*wif));
	if (!wif)
		return;

	memset(wif, 0, sizeof(*wif));
	memcpy(wif->addr, addr, 6);
	wif->avl.key = wif->addr;
	strncpy(wif->ifname, ifname, IF_NAMESIZE);
	wif->assoc.cb = nl80211_assoc_list;
	nl80211_assoc_list(&wif->assoc);
	avl_insert(&wif_tree, &wif->avl);
	nl80211_notify(tb, "wifi.new.iface");
	wif->info = malloc(blob_pad_len(b.head));
	memcpy(wif->info, b.head, blob_pad_len(b.head));
}

static void nl80211_del_iface(struct nlattr **tb)
{
	struct wifi_iface *wif;
	uint8_t *addr;

	if (tb[NL80211_ATTR_MAC] == NULL)
		return;

	addr = nla_data(tb[NL80211_ATTR_MAC]);
	wif = avl_find_element(&wif_tree, addr, wif, avl);
	if (!wif)
		return;
	nl80211_notify(tb, "wifi.del.iface");
	avl_delete(&wif_tree, &wif->avl);
	uloop_timeout_cancel(&wif->assoc);
	free(wif->info);
	free(wif);
}

static void nl80211_handle_cqm(struct nlattr **tb, int iface)
{
	uint8_t *addr;
	static struct nla_policy cqm_policy[NL80211_ATTR_CQM_MAX + 1] = {
		[NL80211_ATTR_CQM_RSSI_THOLD] = { .type = NLA_U32 },
		[NL80211_ATTR_CQM_RSSI_HYST] = { .type = NLA_U8 },
		[NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT] = { .type = NLA_U32 },
		[NL80211_ATTR_CQM_PKT_LOSS_EVENT] = { .type = NLA_U32 },
	};
	struct nlattr *cqm[NL80211_ATTR_CQM_MAX + 1];

	if (tb[NL80211_ATTR_CQM] == NULL)
		return;
	if (nla_parse_nested(cqm, NL80211_ATTR_CQM_MAX, tb[NL80211_ATTR_CQM], cqm_policy))
		return;
	if (cqm[NL80211_ATTR_CQM_PKT_LOSS_EVENT]) {
		if (!tb[NL80211_ATTR_MAC])
			return;
		addr = nla_data(tb[NL80211_ATTR_MAC]);
		blob_buf_init(&b, 0);
		blobmsg_add_iface(&b, "interface", iface);
		blobmsg_add_mac(&b, "mac", addr);
		ubus_notify(&conn.ctx, &ubus_object, "packet.loss", b.head, -1);
		return;
	} else if (cqm[NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT]) {
		switch (nla_get_u32(cqm[NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT])) {
		case NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH:
			break;
		case NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW:
			break;
		}
	}
}

static int nl80211_mcast_grp(struct nlattr **tb, struct family_data *res)
{
	struct nlattr *mcgrp;
	int i;

	nla_for_each_nested(mcgrp, tb[CTRL_ATTR_MCAST_GROUPS], i) {
		struct nlattr *tb2[CTRL_ATTR_MCAST_GRP_MAX + 1];

		nla_parse(tb2, CTRL_ATTR_MCAST_GRP_MAX, nla_data(mcgrp),
		nla_len(mcgrp), NULL);

		if (!tb2[CTRL_ATTR_MCAST_GRP_NAME] ||
		    !tb2[CTRL_ATTR_MCAST_GRP_ID] ||
		    strncmp(nla_data(tb2[CTRL_ATTR_MCAST_GRP_NAME]), res->group, nla_len(tb2[CTRL_ATTR_MCAST_GRP_NAME])) != 0)
			continue;
		res->id = nla_get_u32(tb2[CTRL_ATTR_MCAST_GRP_ID]);
		break;
	}

	return NL_SKIP;
}

static int cb_nl80211_status(struct nl_msg *msg, void *arg)
{
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	int ifidx = -1;

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (tb[CTRL_ATTR_MCAST_GROUPS]) {
		return nl80211_mcast_grp(tb, arg);

	} else if (tb[NL80211_ATTR_IFINDEX]) {
		ifidx = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
	}

	switch (gnlh->cmd) {
	case NL80211_CMD_NEW_STATION:
		nl80211_add_station(tb);
		break;
	case NL80211_CMD_DEL_STATION:
		nl80211_del_station(tb);
		break;
	case NL80211_CMD_NEW_INTERFACE:
		nl80211_add_iface(tb);
		break;
	case NL80211_CMD_DEL_INTERFACE:
		nl80211_del_iface(tb);
		break;
	case NL80211_CMD_NOTIFY_CQM:
		nl80211_handle_cqm(tb, ifidx);
		break;
	case NL80211_CMD_FRAME_TX_STATUS:
		break;
	default:
#ifdef DEBUG
		fprintf(stderr, "nl80211: Drv Event %d (%s)\n",
			gnlh->cmd, nl80211_command_to_string(gnlh->cmd));
#endif
		break;
	}

	return 0;
}

static int
genl_get_multicast_id(struct status_socket *ev,
		      const char *family, const char *group)
{
	struct nl_msg *msg;
	struct family_data *genl_res = (struct family_data *) &nl80211_arg;
	genl_res->group = group;
	genl_res->id = -ENOENT;

	msg = nlmsg_alloc();
	if (!msg)
		return -ENOMEM;
	if (!genlmsg_put(msg, 0, 0, genl_ctrl_resolve(ev->sock, "nlctrl"),
			 0, 0, CTRL_CMD_GETFAMILY, 0) ||
		nla_put_string(msg, CTRL_ATTR_FAMILY_NAME, family)) {
		nlmsg_free(msg);
		return -1;
	}

	genl_send_and_recv(ev, msg);

	return genl_res->id;
}

static void nl80211_enum_tout(struct uloop_timeout *t)
{
	nl80211_list_wif();
}

void nl80211_enum(void)
{
	struct wifi_iface *wif;
	struct wifi_station *sta;

	avl_for_each_element(&wif_tree, wif, avl)
		ubus_notify(&conn.ctx, &ubus_object, "wifi.enum.iface", wif->info, -1);
	avl_for_each_element(&sta_tree, sta, avl)
		ubus_notify(&conn.ctx, &ubus_object, "wifi.enum.station", sta->info, -1);
}

int nl80211_init(void)
{
	int id;

	if (!nl_status_socket(&nl80211_status, NETLINK_GENERIC, cb_nl80211_status, &nl80211_arg))
		return -1;

	id = genl_get_multicast_id(&nl80211_status, "nl80211", "config");
	if (id >= 0)
		nl_socket_add_membership(nl80211_status.sock, id);

	id = genl_get_multicast_id(&nl80211_status, "nl80211", "mlme");
	if (id >= 0)
		nl_socket_add_membership(nl80211_status.sock, id);

	id = genl_get_multicast_id(&nl80211_status, "nl80211", "vendor");
	if (id >= 0)
		nl_socket_add_membership(nl80211_status.sock, id);

	nl80211_enum_timer.cb = nl80211_enum_tout;
	uloop_timeout_set(&nl80211_enum_timer, 2 * 1000);

	return 0;
}
