/* OsmoBTS VTY interface */

/* (C) 2011 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <osmocom/core/talloc.h>
#include <osmocom/gsm/abis_nm.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/command.h>
#include <osmocom/vty/logging.h>

#include <osmocom/trau/osmo_ortp.h>


#include <osmo-bts/logging.h>
#include <osmo-bts/gsm_data.h>
#include <osmo-bts/abis.h>
#include <osmo-bts/bts.h>
#include <osmo-bts/rsl.h>
#include <osmo-bts/oml.h>
#include <osmo-bts/signal.h>
#include <osmo-bts/bts_model.h>
#include <osmo-bts/measurement.h>
#include <osmo-bts/vty.h>


enum node_type bts_vty_go_parent(struct vty *vty)
{
	switch (vty->node) {
	case TRX_NODE:
		vty->node = BTS_NODE;
		{
			struct gsm_bts_trx *trx = vty->index;
			vty->index = trx->bts;
		}
		break;
	default:
		vty->node = CONFIG_NODE;
	}
	return vty->node;
}

int bts_vty_is_config_node(struct vty *vty, int node)
{
	switch (node) {
	case TRX_NODE:
	case BTS_NODE:
		return 1;
	default:
		return 0;
	}
}

gDEFUN(ournode_exit, ournode_exit_cmd, "exit",
	"Exit current node, go down to provious node")
{
	switch (vty->node) {
	case TRX_NODE:
		vty->node = BTS_NODE;
		{
			struct gsm_bts_trx *trx = vty->index;
			vty->index = trx->bts;
		}
		break;
	default:
		break;
	}
	return CMD_SUCCESS;
}

gDEFUN(ournode_end, ournode_end_cmd, "end",
	"End current mode and change to enable mode")
{
	switch (vty->node) {
	default:
		vty_config_unlock(vty);
		vty->node = ENABLE_NODE;
		vty->index = NULL;
		vty->index_sub = NULL;
		break;
	}
	return CMD_SUCCESS;
}

struct vty_app_info bts_vty_info = {
	.name		= "OsmoBTS",
	.version	= PACKAGE_VERSION,
	.go_parent_cb	= bts_vty_go_parent,
	.is_config_node	= bts_vty_is_config_node,
};

const char *osmobts_copyright =
	"Copyright (C) 2010, 2011 by Harald Welte, Andreas Eversberg and On-Waves\r\n"
	"License AGPLv3+: GNU AGPL version 3 or later <http://gnu.org/licenses/agpl-3.0.html>\r\n"
	"This is free software: you are free to change and redistribute it.\r\n"
	 "There is NO WARRANTY, to the extent permitted by law.\r\n";

extern struct gsm_network bts_gsmnet;

struct gsm_network *gsmnet_from_vty(struct vty *v)
{
	return &bts_gsmnet;
}

struct gsm_bts *gsm_bts_num(struct gsm_network *net, int num)
{
	struct gsm_bts *bts;

	if (num >= net->num_bts)
		return NULL;

	llist_for_each_entry(bts, &net->bts_list, list) {
		if (bts->nr == num)
			return bts;
	}

	return NULL;
}

static struct cmd_node bts_node = {
	BTS_NODE,
	"%s(bts)#",
	1,
};

static struct cmd_node trx_node = {
	TRX_NODE,
	"%s(trx)#",
	1,
};

DEFUN(cfg_bts_trx, cfg_bts_trx_cmd,
	"trx <0-0>",
	"Select a TRX to configure\n" "TRX number\n")
{
	int trx_nr = atoi(argv[0]);
	struct gsm_bts *bts = vty->index;
	struct gsm_bts_trx *trx;

	trx = gsm_bts_trx_num(bts, trx_nr);
	if (!trx) {
		vty_out(vty, "Unknown TRX %u%s", trx_nr, VTY_NEWLINE);
		return CMD_WARNING;
	}

	vty->index = trx;
	vty->index_sub = &trx->description;
	vty->node = TRX_NODE;

	return CMD_SUCCESS;
}

static void config_write_bts_single(struct vty *vty, struct gsm_bts *bts)
{
	struct gsm_bts_role_bts *btsb = bts_role_bts(bts);
	struct gsm_bts_trx *trx;

	vty_out(vty, "bts %u%s", bts->nr, VTY_NEWLINE);
	if (bts->description)
		vty_out(vty, " description %s%s", bts->description, VTY_NEWLINE);
	vty_out(vty, " band %s%s", gsm_band_name(bts->band), VTY_NEWLINE);
	vty_out(vty, " ipa unit-id %u %u%s",
		bts->ip_access.site_id, bts->ip_access.bts_id, VTY_NEWLINE);
	vty_out(vty, " oml remote-ip %s%s", btsb->bsc_oml_host, VTY_NEWLINE);
	vty_out(vty, " rtp bind-ip %s%s", btsb->rtp_bind_host, VTY_NEWLINE);
	vty_out(vty, " rtp jitter-buffer %u%s", btsb->rtp_jitter_buf_ms,
		VTY_NEWLINE);

	bts_model_config_write_bts(vty, bts);

	llist_for_each_entry(trx, &bts->trx_list, list) {
		vty_out(vty, " trx %u%s", trx->nr, VTY_NEWLINE);
		bts_model_config_write_trx(vty, trx);
	}
}

static int config_write_bts(struct vty *vty)
{
	struct gsm_network *net = gsmnet_from_vty(vty);
	struct gsm_bts *bts;

	llist_for_each_entry(bts, &net->bts_list, list)
		config_write_bts_single(vty, bts);

	return CMD_SUCCESS;
}

static int config_write_dummy(struct vty *vty)
{
	return CMD_SUCCESS;
}

/* per-BTS configuration */
DEFUN(cfg_bts,
      cfg_bts_cmd,
      "bts BTS_NR",
      "Select a BTS to configure\n"
	"BTS Number\n")
{
	struct gsm_network *gsmnet = gsmnet_from_vty(vty);
	int bts_nr = atoi(argv[0]);
	struct gsm_bts *bts;

	if (bts_nr >= gsmnet->num_bts) {
		vty_out(vty, "%% Unknown BTS number %u (num %u)%s",
			bts_nr, gsmnet->num_bts, VTY_NEWLINE);
		return CMD_WARNING;
	} else
		bts = gsm_bts_num(gsmnet, bts_nr);

	vty->index = bts;
	vty->index_sub = &bts->description;
	vty->node = BTS_NODE;

	return CMD_SUCCESS;
}

#warning merge with OpenBSC?
DEFUN(cfg_bts_unit_id,
      cfg_bts_unit_id_cmd,
      "ipa unit-id <0-65534> <0-255>",
      "Set the ip.access BTS Unit ID of this BTS\n")
{
	struct gsm_bts *bts = vty->index;
	int site_id = atoi(argv[0]);
	int bts_id = atoi(argv[1]);

	bts->ip_access.site_id = site_id;
	bts->ip_access.bts_id = bts_id;

	return CMD_SUCCESS;
}

DEFUN(cfg_bts_band,
      cfg_bts_band_cmd,
      "band (450|GSM450|480|GSM480|750|GSM750|810|GSM810|850|GSM850|900|GSM900|1800|DCS1800|1900|PCS1900)",
      "Set the frequency band of this BTS\n" "Frequency band\n")
{
	struct gsm_bts *bts = vty->index;
	int band = gsm_band_parse(argv[0]);

	if (band < 0) {
		vty_out(vty, "%% BAND %d is not a valid GSM band%s",
			band, VTY_NEWLINE);
		return CMD_WARNING;
	}

	bts->band = band;

	return CMD_SUCCESS;
}

DEFUN(cfg_bts_oml_ip,
      cfg_bts_oml_ip_cmd,
      "oml remote-ip A.B.C.D",
      "OML Parameters\n" "OML IP Address\n" "OML IP Address\n")
{
	struct gsm_bts *bts = vty->index;
	struct gsm_bts_role_bts *btsb = bts_role_bts(bts);

	if (btsb->bsc_oml_host)
		talloc_free(btsb->bsc_oml_host);

	btsb->bsc_oml_host = talloc_strdup(btsb, argv[0]);

	return CMD_SUCCESS;
}

#define RTP_STR "RTP parameters\n"

DEFUN(cfg_bts_rtp_bind_ip,
      cfg_bts_rtp_bind_ip_cmd,
      "rtp bind-ip A.B.C.D",
      RTP_STR "RTP local bind IP Address\n" "RTP local bind IP Address\n")
{
	struct gsm_bts *bts = vty->index;
	struct gsm_bts_role_bts *btsb = bts_role_bts(bts);

	if (btsb->rtp_bind_host)
		talloc_free(btsb->rtp_bind_host);

	btsb->rtp_bind_host = talloc_strdup(btsb, argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_bts_rtp_jitbuf,
	cfg_bts_rtp_jitbuf_cmd,
	"rtp jitter-buffer <0-10000>",
	RTP_STR "RTP jitter buffer\n" "jitter buffer in ms\n")
{
	struct gsm_bts *bts = vty->index;
	struct gsm_bts_role_bts *btsb = bts_role_bts(bts);

	btsb->rtp_jitter_buf_ms = atoi(argv[0]);

	return CMD_SUCCESS;
}

/* ======================================================================
 * SHOW
 * ======================================================================*/

static void net_dump_nmstate(struct vty *vty, struct gsm_nm_state *nms)
{
	vty_out(vty,"Oper '%s', Admin %u, Avail '%s'%s",
		abis_nm_opstate_name(nms->operational), nms->administrative,
		abis_nm_avail_name(nms->availability), VTY_NEWLINE);
}

static void bts_dump_vty(struct vty *vty, struct gsm_bts *bts)
{
	vty_out(vty, "BTS %u is of %s type in band %s, has CI %u LAC %u, "
		"BSIC %u, TSC %u and %u TRX%s",
		bts->nr, "FIXME", gsm_band_name(bts->band),
		bts->cell_identity,
		bts->location_area_code, bts->bsic, bts->tsc,
		bts->num_trx, VTY_NEWLINE);
	vty_out(vty, "  Description: %s%s",
		bts->description ? bts->description : "(null)", VTY_NEWLINE);
	vty_out(vty, "  Unit ID: %u/%u/0, OML Stream ID 0x%02x%s",
			bts->ip_access.site_id, bts->ip_access.bts_id,
			bts->oml_tei, VTY_NEWLINE);
	vty_out(vty, "  NM State: ");
	net_dump_nmstate(vty, &bts->mo.nm_state);
	vty_out(vty, "  Site Mgr NM State: ");
	net_dump_nmstate(vty, &bts->site_mgr.mo.nm_state);
#if 0
	vty_out(vty, "  Paging: %u pending requests, %u free slots%s",
		paging_pending_requests_nr(bts),
		bts->paging.available_slots, VTY_NEWLINE);
	if (is_ipaccess_bts(bts)) {
		vty_out(vty, "  OML Link state: %s.%s",
			bts->oml_link ? "connected" : "disconnected", VTY_NEWLINE);
	} else {
		vty_out(vty, "  E1 Signalling Link:%s", VTY_NEWLINE);
		e1isl_dump_vty(vty, bts->oml_link);
	}

	/* FIXME: chan_desc */
	memset(&pl, 0, sizeof(pl));
	bts_chan_load(&pl, bts);
	vty_out(vty, "  Current Channel Load:%s", VTY_NEWLINE);
	dump_pchan_load_vty(vty, "    ", &pl);
#endif
}


DEFUN(show_bts, show_bts_cmd, "show bts <0-255>",
	SHOW_STR "Display information about a BTS\n"
		"BTS number")
{
	struct gsm_network *net = gsmnet_from_vty(vty);
	int bts_nr;

	if (argc != 0) {
		/* use the BTS number that the user has specified */
		bts_nr = atoi(argv[0]);
		if (bts_nr >= net->num_bts) {
			vty_out(vty, "%% can't find BTS '%s'%s", argv[0],
				VTY_NEWLINE);
			return CMD_WARNING;
		}
		bts_dump_vty(vty, gsm_bts_num(net, bts_nr));
		return CMD_SUCCESS;
	}
	/* print all BTS's */
	for (bts_nr = 0; bts_nr < net->num_bts; bts_nr++)
		bts_dump_vty(vty, gsm_bts_num(net, bts_nr));

	return CMD_SUCCESS;
}

static struct gsm_lchan *resolve_lchan(struct gsm_network *net,
					const char **argv, int idx)
{
	int bts_nr = atoi(argv[idx+0]);
	int trx_nr = atoi(argv[idx+1]);
	int ts_nr = atoi(argv[idx+2]);
	int lchan_nr = atoi(argv[idx+3]);
	struct gsm_bts *bts;
	struct gsm_bts_trx *trx;
	struct gsm_bts_trx_ts *ts;

	bts = gsm_bts_num(net, bts_nr);
	if (!bts)
		return NULL;

	trx = gsm_bts_trx_num(bts, trx_nr);
	if (!trx)
		return NULL;

	if (ts_nr >= ARRAY_SIZE(trx->ts))
		return NULL;
	ts = &trx->ts[ts_nr];

	if (lchan_nr >= ARRAY_SIZE(ts->lchan))
		return NULL;

	return &ts->lchan[lchan_nr];
}

#define BTS_T_T_L_STR			\
	"BTS related commands\n"	\
	"BTS number\n"			\
	"TRX related commands\n"	\
	"TRX number\n"			\
	"timeslot related commands\n"	\
	"timeslot number\n"		\
	"logical channel commands\n"	\
	"logical channel number\n"

DEFUN(bts_t_t_l_jitter_buf,
	bts_t_t_l_jitter_buf_cmd,
	"bts <0-0> trx <0-0> ts <0-7> lchan <0-1> rtp jitter-buffer <0-10000>",
	BTS_T_T_L_STR "RTP settings\n"
	"Jitter buffer\n" "Size of jitter buffer in (ms)\n")
{
	struct gsm_network *net = gsmnet_from_vty(vty);
	struct gsm_lchan *lchan;
	int jitbuf_ms = atoi(argv[4]);

	lchan = resolve_lchan(net, argv, 0);
	if (!lchan) {
		vty_out(vty, "%% can't find BTS%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (!lchan->abis_ip.rtp_socket) {
		vty_out(vty, "%% this channel has no active RTP stream%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}
	osmo_rtp_socket_set_param(lchan->abis_ip.rtp_socket,
				  OSMO_RTP_P_JITBUF, jitbuf_ms);

	return CMD_SUCCESS;
}

int bts_vty_init(const struct log_info *cat)
{
	install_element_ve(&show_bts_cmd);

	logging_vty_add_cmds(cat);

	install_node(&bts_node, config_write_bts);
	install_element(CONFIG_NODE, &cfg_bts_cmd);
	install_default(BTS_NODE);
	install_element(BTS_NODE, &cfg_bts_unit_id_cmd);
	install_element(BTS_NODE, &cfg_bts_oml_ip_cmd);
	install_element(BTS_NODE, &cfg_bts_rtp_bind_ip_cmd);
	install_element(BTS_NODE, &cfg_bts_rtp_jitbuf_cmd);
	install_element(BTS_NODE, &cfg_bts_band_cmd);
	install_element(BTS_NODE, &cfg_description_cmd);
	install_element(BTS_NODE, &cfg_no_description_cmd);

	/* add and link to TRX config node */
	install_element(BTS_NODE, &cfg_bts_trx_cmd);
	install_node(&trx_node, config_write_dummy);
	install_default(TRX_NODE);

	install_element(ENABLE_NODE, &bts_t_t_l_jitter_buf_cmd);

	return 0;
}
