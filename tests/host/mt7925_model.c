// SPDX-License-Identifier: MIT
/*
 * Software model of the MT7925 as the driver sees it through BAR0.
 *
 * The fake PCI device of os_host.c is plain memory: every register read
 * returns what was last written.  This file hooks the 32-bit MMIO writes
 * (lkpi_host_mmio_write_hook) and adds the behaviour the real chip has:
 *
 *   Registers
 *   - CONN_ON_LPCTL: driver/firmware ownership handshake, OWN_SYNC follows
 *     SET_OWN / CLR_OWN;
 *   - WFSYS_SW_RST_B: INIT_DONE once the reset bit is set; a reset also
 *     drops the loaded patch/firmware state (CONN_ON_MISC back to PWR_ON);
 *   - CONN_ON_MISC: FW_PWR_ON after reset, FW_N9_RDY after FW_START_REQ;
 *   - HIF_REMAP_L1 window: HW_CHIPID / HW_REV read 0x7925 / 0x0001;
 *   - WFDMA0 HOST_INT_STA (write-1-to-clear, set by the model),
 *     HOST_INT_ENA and the PCIe MAC INT_ENABLE gate: the interrupt line is
 *     (STA & ENA) != 0 && pcie gate != 0, re-evaluated on every ENA write;
 *   - WFDMA0 GLO_CFG TX/RX_DMA_EN, RST_DTX_PTR / RST_DRX_PTR.
 *
 *   WFDMA rings (struct mt76_queue_regs at MT_TX_RING_BASE / MT_RX_*_RING_BASE
 *   + idx * MT_RING_SIZE; descriptors are struct mt76_desc in host memory)
 *   - a cpu_idx write on a TX ring consumes the descriptors between dma_idx
 *     and cpu_idx: the FWDL ring (MT7925_TXQ_FWDL) takes FW_SCATTER chunks,
 *     the WM ring (MT7925_TXQ_MCU_WM) takes MCU commands; DMA_DONE is
 *     written back, dma_idx advanced and the ring's TX done bit raised;
 *   - MCU events are written into the next free buffer of the MCU RX ring
 *     (MT7925_RXQ_MCU_WM): SD_LEN0 + LAST_SEC0 + DMA_DONE in ctrl, dma_idx
 *     advanced, RX done raised.  Nothing is ever put on the data ring.
 *
 *   MCU: see cmd_table[] below.  Every command that waits for a response is
 *   answered synchronously, from inside the kick, with an event carrying the
 *   same seq (and cid/status 0 for unified commands) unless a handler builds
 *   a real payload: patch semaphore, NIC capabilities, EFUSE reads.
 *
 * Bus addresses in descriptors come from the 32-bit window of os_host.c and
 * are translated back with lkpi_host_dma_to_va().  Register offsets are
 * taken from the driver headers; addresses above the 2 MiB BAR go through
 * the same fixed map the driver uses (__mt7925_reg_addr in mt7925/pci.c).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <linux/kernel.h>
#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include "mt7925.h"			/* mt792x_regs.h, mt7925/regs.h, queue ids */
#include "mcu.h"			/* mt7925/mcu.h: mt7925_mcu_rxd, uni event */
#include "mac.h"			/* mt76_connac3_mac.h: MT_RXD0_PKT_TYPE */
#include "../dma.h"			/* struct mt76_desc, MT_DMA_CTL_* */

#include "os_host.h"
#include "mt7925_model.h"

/* ------------------------------------------------------------ address map */
/*
 * Subset of the mt7925 fixed map (mt7925/pci.c __mt7925_reg_addr) for the
 * registers this model touches.  Addresses below 0x200000 are BAR offsets.
 * Anything else here is reached through the L1 remap window.
 */
static u32 bar_off(u32 addr)
{
	static const struct { u32 phys, maps, size; } fixed_map[] = {
		{ 0x54000000, 0x002000, 0x1000 },	/* WFDMA PCIE0 MCU DMA0 */
		{ 0x7c060000, 0x0e0000, 0x10000 },	/* CONN_INFRA, conn_host_csr_top */
		{ 0x7c000000, 0x0f0000, 0x10000 },	/* CONN_INFRA */
	};
	size_t i;

	if (addr < 0x200000)
		return addr;
	for (i = 0; i < ARRAY_SIZE(fixed_map); i++)
		if (addr >= fixed_map[i].phys && addr - fixed_map[i].phys < fixed_map[i].size)
			return fixed_map[i].maps + (addr - fixed_map[i].phys);
	return MT_HIF_REMAP_BASE_L1 + FIELD_GET(MT_HIF_REMAP_L1_OFFSET, addr);
}

#define OFF_CONN_ON_LPCTL	bar_off(MT_CONN_ON_LPCTL)
#define OFF_CONN_ON_MISC	bar_off(MT_CONN_ON_MISC)
#define OFF_WFSYS_SW_RST_B	bar_off(MT_WFSYS_SW_RST_B)
#define OFF_HW_CHIPID		bar_off(MT_HW_CHIPID)	/* via the L1 window */
#define OFF_HW_REV		bar_off(MT_HW_REV)

#define MODEL_CHIPID		0x7925
#define MODEL_HW_REV		0x0001

#define MODEL_N_TX_RINGS	18	/* TX_RING0..17_EXT_CTRL exist */
#define MODEL_N_RX_RINGS	8	/* RX_RING0..7_EXT_CTRL exist */

/* ------------------------------------------------------------------ state */
static struct {
	lkpi_os_pci_t pci;
	u8 *bar0;
	size_t bar_len;
	int verbose;
	lkpi_os_spinlock_t lock;	/* serialises hook invocations from several threads */
	bool irq_pending;		/* raise the line once the hook has dropped the lock */

	u32 int_sta;			/* shadow of MT_WFDMA0_HOST_INT_STA */
	bool tx_dma_en;			/* MT_WFDMA0_GLO_CFG_TX_DMA_EN */
	bool patch_loaded;		/* PATCH_FINISH_REQ seen since the last WFSYS reset */
	u32 fwdl_expect;		/* bytes announced by the last download request */
	u32 fwdl_got;
	unsigned int fwdl_chunks;
	u8 eeprom[0x1000];		/* EFUSE content served to EFUSE_CTRL queries */
	struct mt7925_model_stats stats;
} m;

static void model_log(int level, const char *fmt, ...)
{
	va_list ap;

	if (m.verbose < level)
		return;
	flockfile(stderr);
	fputs("[MODEL] ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	funlockfile(stderr);
}

static void model_error(const char *fmt, ...)
{
	va_list ap;

	m.stats.errors++;
	flockfile(stderr);
	fputs("[MODEL] ERROR: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	funlockfile(stderr);
}

static inline volatile u32 *reg32(u32 off)
{
	return (volatile u32 *)(m.bar0 + off);
}

/* -------------------------------------------------------------- interrupt */
/*
 * Interrupt line = (STA & ENA) && PCIe gate.  The handler itself writes
 * HOST_INT_ENA (and so re-enters this hook), so the line is asserted after
 * the hook has released the model lock, see mt7925_model_mmio_write().
 */
static void int_update(void)
{
	u32 ena = *reg32(MT_WFDMA0_HOST_INT_ENA);
	u32 gate = *reg32(MT7925_PCIE_MAC_INT_ENABLE);

	*reg32(MT_WFDMA0_HOST_INT_STA) = m.int_sta;
	if ((m.int_sta & ena) && gate)
		m.irq_pending = true;
}

static void raise_int(u32 bits)
{
	m.int_sta |= bits;
	int_update();
}

/* ------------------------------------------------------------------ rings */
static volatile struct mt76_queue_regs *tx_ring_regs(int idx)
{
	return (volatile struct mt76_queue_regs *)(m.bar0 + MT_TX_RING_BASE + idx * MT_RING_SIZE);
}

static volatile struct mt76_queue_regs *rx_ring_regs(int idx)
{
	/* MT_RX_EVENT_RING_BASE == MT_RX_DATA_RING_BASE on mt7925 */
	return (volatile struct mt76_queue_regs *)(m.bar0 + MT_RX_EVENT_RING_BASE + idx * MT_RING_SIZE);
}

/* Descriptor array of a ring, or NULL while the ring is not programmed. */
static struct mt76_desc *ring_desc(volatile struct mt76_queue_regs *regs, u32 *ndesc)
{
	u32 n = regs->ring_size & ~MT_DMA_RRO_EN;
	struct mt76_desc *desc;

	*ndesc = n;
	if (!n || !regs->desc_base)
		return NULL;
	desc = lkpi_host_dma_to_va(regs->desc_base, (size_t)n * sizeof(*desc));
	if (!desc)
		model_error("ring descriptor base %#x (%u entries) is not DMA memory",
			    regs->desc_base, n);
	return desc;
}

static u32 tx_done_bit(int ring)
{
	/* HOST_TX_DONE_INT_ENA0..7 are bits 4..11, ENA15..17 bits 25..27 */
	if (ring < 8)
		return HOST_TX_DONE_INT_ENA0 << ring;
	if (ring >= 15 && ring <= 17)
		return HOST_TX_DONE_INT_ENA15 << (ring - 15);
	return 0;
}

static u32 rx_done_bit(int ring)
{
	/* HOST_RX_DONE_INT_ENA0..3 are bits 0..3, ENA4..11 bits 12..19 */
	if (ring < 4)
		return HOST_RX_DONE_INT_ENA0 << ring;
	return HOST_RX_DONE_INT_ENA4 << (ring - 4);
}

/*
 * Put one event into the next free buffer of an RX ring.  The driver filled
 * the ring with buffers and wrote cpu_idx; dma_idx == cpu_idx means empty.
 */
static int rx_deliver(int ring, const void *data, size_t len)
{
	volatile struct mt76_queue_regs *regs = rx_ring_regs(ring);
	struct mt76_desc *desc, *d;
	u32 ndesc, cpu, dma, ctrl, bufsz;
	u64 bus;
	void *buf;

	desc = ring_desc(regs, &ndesc);
	if (!desc)
		return -1;
	cpu = regs->cpu_idx;
	dma = regs->dma_idx;
	if (cpu >= ndesc || dma >= ndesc || dma == cpu) {
		model_error("RX ring %d has no free buffer (cpu %u dma %u of %u)", ring, cpu, dma, ndesc);
		return -1;
	}
	d = &desc[dma];
	ctrl = le32_to_cpu(d->ctrl);
	bufsz = FIELD_GET(MT_DMA_CTL_SD_LEN0, ctrl);
	bus = le32_to_cpu(d->buf0) |
	      (u64)FIELD_GET(MT_DMA_CTL_SDP0_H, le32_to_cpu(d->buf1)) << 32;
	buf = lkpi_host_dma_to_va(bus, bufsz);
	if (!buf) {
		model_error("RX ring %d desc %u: buffer %#llx (%u bytes) is not DMA memory",
			    ring, dma, (unsigned long long)bus, bufsz);
		return -1;
	}
	if (len > bufsz) {
		model_error("RX ring %d: event of %zu bytes does not fit a %u byte buffer", ring, len, bufsz);
		return -1;
	}
	memcpy(buf, data, len);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);	/* data before DMA_DONE */
	d->ctrl = cpu_to_le32(FIELD_PREP(MT_DMA_CTL_SD_LEN0, len) |
			      MT_DMA_CTL_LAST_SEC0 | MT_DMA_CTL_DMA_DONE);
	regs->dma_idx = (dma + 1) % ndesc;
	m.stats.events++;
	raise_int(rx_done_bit(ring));
	return 0;
}

/* -------------------------------------------------------------- MCU events */
/*
 * Legacy (non-unified) events carry the connac2 rxd without the ext_eid
 * trailer: mt7925_mcu_parse_response() reads the status byte of
 * PATCH_SEM_CONTROL / PATCH_FINISH_REQ at sizeof(struct mt7925_mcu_rxd) - 4.
 */
struct legacy_evt_hdr {
	__le32 rxd[8];
	__le16 len;
	__le16 pkt_type_id;
	u8 eid;
	u8 seq;
	u8 option;
	u8 rsv;
} __packed;

#define EVT_MAX		512

struct evt {
	u8 buf[EVT_MAX];
	size_t len;
};

static void evt_rxd0(struct evt *e)
{
	__le32 *rxd = (__le32 *)e->buf;

	rxd[0] = cpu_to_le32(FIELD_PREP(MT_RXD0_PKT_TYPE, PKT_TYPE_RX_EVENT) |
			     FIELD_PREP(MT_RXD0_LENGTH, e->len));
}

static void *evt_put(struct evt *e, const void *data, size_t len)
{
	void *p = e->buf + e->len;

	if (e->len + len > EVT_MAX) {
		model_error("event payload of %zu bytes too large", len);
		return NULL;
	}
	memset(p, 0, len);
	if (data)
		memcpy(p, data, len);
	e->len += len;
	return p;
}

static void evt_legacy_init(struct evt *e, u8 eid, u8 seq)
{
	struct legacy_evt_hdr *h;

	memset(e, 0, sizeof(*e));
	h = evt_put(e, NULL, sizeof(*h));
	h->eid = eid;
	h->seq = seq;
	h->len = cpu_to_le16(sizeof(*h) - sizeof(h->rxd));
	h->pkt_type_id = cpu_to_le16(MCU_PKT_ID);
}

static void evt_uni_init(struct evt *e, u8 eid, u8 seq)
{
	struct mt7925_mcu_rxd *h;

	memset(e, 0, sizeof(*e));
	h = evt_put(e, NULL, sizeof(*h));
	h->eid = eid;
	h->seq = seq;
	h->option = MCU_UNI_CMD_EVENT;
	h->len = cpu_to_le16(sizeof(*h) - sizeof(h->rxd));
	h->pkt_type_id = cpu_to_le16(MCU_PKT_ID);
	h->s2d_index = 0;		/* N2H; the driver does not look at it */
}

/* Finalise lengths and hand the event to the MCU RX ring. */
static void evt_send(struct evt *e)
{
	__le16 *len = (__le16 *)(e->buf + offsetof(struct legacy_evt_hdr, len));

	*len = cpu_to_le16(e->len - 8 * sizeof(__le32));
	evt_rxd0(e);
	rx_deliver(MT7925_RXQ_MCU_WM, e->buf, e->len);
}

static void evt_add_tlv(struct evt *e, u16 tag, const void *data, size_t len)
{
	struct tlv t = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(t) + len),
	};

	evt_put(e, &t, sizeof(t));
	evt_put(e, data, len);
}

/* ------------------------------------------------------------ MCU commands */
struct cmd {
	bool uni;
	u16 cid;
	u8 seq;
	u8 option;		/* uni: MCU_CMD_ACK etc.; legacy: set_query */
	u8 ext_cid;		/* legacy only */
	u16 tag;		/* uni: tag of the first TLV, 0 when none */
	const u8 *payload;
	size_t plen;
	const char *name;
};

/* Generic unified ACK: MCU_UNI_EVENT_RESULT with cid and status 0. */
static void ack_uni_status(const struct cmd *c, u32 status)
{
	struct mt7925_mcu_uni_event ev = {
		.cid = c->cid,
		.status = cpu_to_le32(status),
	};
	struct evt e;

	evt_uni_init(&e, MCU_UNI_EVENT_RESULT, c->seq);
	evt_put(&e, &ev, sizeof(ev));
	evt_send(&e);
}

/* Generic legacy ACK: 4-byte status payload the driver skips past. */
static void ack_legacy_status(const struct cmd *c, u8 status)
{
	u8 st[4] = { status };
	struct evt e;

	evt_legacy_init(&e, c->cid, c->seq);
	evt_put(&e, st, sizeof(st));
	evt_send(&e);
}

static void cmd_patch_sem(const struct cmd *c)
{
	u32 op = c->plen >= 4 ? le32_to_cpup((const __le32 *)c->payload) : 0;
	u8 st;

	if (op == PATCH_SEM_GET)
		st = m.patch_loaded ? PATCH_IS_DL : PATCH_NOT_DL_SEM_SUCCESS;
	else
		st = PATCH_REL_SEM_SUCCESS;
	model_log(1, "  patch semaphore %s -> %u", op == PATCH_SEM_GET ? "get" : "release", st);
	ack_legacy_status(c, st);
}

static void cmd_patch_finish(const struct cmd *c)
{
	m.patch_loaded = true;
	ack_legacy_status(c, 0);
}

/* PATCH_START_REQ / TARGET_ADDRESS_LEN_REQ: {addr, len, mode}. */
static void cmd_dl_req(const struct cmd *c)
{
	const __le32 *req = (const __le32 *)c->payload;

	if (c->plen >= 12) {
		m.fwdl_expect = le32_to_cpu(req[1]);
		m.fwdl_got = 0;
		m.fwdl_chunks = 0;
		model_log(1, "  download region addr %#x len %u mode %#x",
			  le32_to_cpu(req[0]), le32_to_cpu(req[1]), le32_to_cpu(req[2]));
	}
	ack_legacy_status(c, 0);
}

static void cmd_fw_start(const struct cmd *c)
{
	const __le32 *req = (const __le32 *)c->payload;

	if (c->plen >= 8)
		model_log(1, "  fw start option %#x addr %#x",
			  le32_to_cpu(req[0]), le32_to_cpu(req[1]));
	ack_legacy_status(c, 0);
	/* the RAM code is "running": mt792x_load_firmware polls for N9_RDY */
	*reg32(OFF_CONN_ON_MISC) = MT_TOP_MISC2_FW_N9_RDY;
}

/*
 * CHIP_CONFIG: NIC_CAPA query gets the capability TLVs
 * mt7925_mcu_get_nic_capability() parses; CHIP_CFG is a fire-and-forget
 * text command ("KeepFullPwr 1", "ThermalProt...") that gets no answer.
 */
static void cmd_chip_config(const struct cmd *c)
{
	struct {
		__le16 n_element;
		u8 rsv[2];
	} __packed hdr = {};
	/* layout of struct mt7925_mcu_phy_cap in mt7925/mcu.c */
	const u8 phy_cap[13] = {
		[0] = 1,	/* ht */
		[1] = 1,	/* vht */
		[2] = 1,	/* 5g */
		[3] = 3,	/* max_bw: 160 MHz */
		[4] = 2,	/* nss */
		[5] = 0,	/* dbdc */
		[6] = 1, [7] = 1,	/* tx/rx ldpc */
		[8] = 1, [9] = 1,	/* tx/rx stbc */
		[10] = BIT(0) | BIT(1),	/* hw_path: WF0 2.4G | WF0 5G */
		[11] = 1,	/* he */
		[12] = 1,	/* eht */
	};
	const u8 has_6g = 1;
	const __le64 chip_cap = cpu_to_le64(0);	/* no CLC/REGD/11D/AXIDMA events to serve */
	const u8 eml_cap[12] = {};		/* rsv[4], eml_cap, rsv2[6] */
	struct evt e;

	if (c->tag != UNI_CHIP_CONFIG_NIC_CAPA) {
		/* rsv[4], tlv, then struct mt76_connac_config with the text in .data */
		const size_t cfg = 4 + sizeof(struct tlv);
		const struct mt76_connac_config *conf = (const void *)(c->payload + cfg);

		if (c->tag == UNI_CHIP_CONFIG_CHIP_CFG && c->plen >= cfg + sizeof(*conf))
			model_log(1, "  chip_cfg \"%.*s\" (no answer expected)",
				  (int)min_t(size_t, le16_to_cpu(conf->data_size), sizeof(conf->data)),
				  (const char *)conf->data);
		return;
	}

	evt_uni_init(&e, MCU_UNI_EVENT_NIC_CAPAB, c->seq);
	hdr.n_element = cpu_to_le16(5);
	evt_put(&e, &hdr, sizeof(hdr));
	evt_add_tlv(&e, MT_NIC_CAP_MAC_ADDR, m.eeprom + MT_EE_MAC_ADDR, ETH_ALEN);
	evt_add_tlv(&e, MT_NIC_CAP_PHY, phy_cap, sizeof(phy_cap));
	evt_add_tlv(&e, MT_NIC_CAP_6G, &has_6g, sizeof(has_6g));
	evt_add_tlv(&e, MT_NIC_CAP_CHIP_CAP, &chip_cap, sizeof(chip_cap));
	evt_add_tlv(&e, MT_NIC_CAP_EML_CAP, eml_cap, sizeof(eml_cap));
	model_log(1, "  nic capability: mac %02x:%02x:%02x:%02x:%02x:%02x, 2 nss, 2.4/5/6 GHz, chip_cap 0",
		  m.eeprom[MT_EE_MAC_ADDR], m.eeprom[MT_EE_MAC_ADDR + 1], m.eeprom[MT_EE_MAC_ADDR + 2],
		  m.eeprom[MT_EE_MAC_ADDR + 3], m.eeprom[MT_EE_MAC_ADDR + 4], m.eeprom[MT_EE_MAC_ADDR + 5]);
	evt_send(&e);
}

/*
 * EFUSE_CTRL: a query (tag 1, mt7925_mcu_read_eeprom) gets one
 * MT7925_EEPROM_BLOCK_SIZE block out of the modelled EFUSE; the set
 * (UNI_EFUSE_BUFFER_MODE, mt7925_mcu_set_eeprom) is just acknowledged.
 */
static void cmd_efuse(const struct cmd *c)
{
	struct {
		u8 rsv[4];
		__le16 tag;
		__le16 len;
		__le32 addr;
		__le32 valid;
		u8 data[MT7925_EEPROM_BLOCK_SIZE];
	} __packed req;
	struct {
		u8 rsv[4];
		__le16 tag;
		__le16 len;
		__le32 ver;
		__le32 addr;
		__le32 valid;
		__le32 size;
		__le32 magic_num;
		__le32 type;
		__le32 rsv1[4];
		u8 data[32];
	} __packed res = {};
	struct evt e;
	u32 addr;

	if ((c->option & MCU_CMD_SET) || c->plen < sizeof(req)) {
		ack_uni_status(c, 0);		/* buffer mode set: just acknowledged */
		return;
	}
	memcpy(&req, c->payload, sizeof(req));
	addr = le32_to_cpu(req.addr) & ~(MT7925_EEPROM_BLOCK_SIZE - 1);
	res.tag = req.tag;
	res.len = cpu_to_le16(sizeof(res) - 4);
	res.addr = cpu_to_le32(addr);
	res.valid = cpu_to_le32(1);
	res.size = cpu_to_le32(MT7925_EEPROM_BLOCK_SIZE);
	if (addr + MT7925_EEPROM_BLOCK_SIZE <= sizeof(m.eeprom))
		memcpy(res.data, m.eeprom + addr, MT7925_EEPROM_BLOCK_SIZE);
	model_log(1, "  efuse read block %#x", addr);

	evt_uni_init(&e, MCU_UNI_EVENT_RESULT, c->seq);
	evt_put(&e, &res, sizeof(res));
	evt_send(&e);
}

enum ack_kind {
	ACK_NONE,		/* driver does not wait: no event */
	ACK_STATUS,		/* generic event: legacy status byte / uni cid+status 0 */
	ACK_HANDLER,		/* the handler builds the event */
};

/*
 * Table of handled commands.  Commands not listed here still get a generic
 * ACK when the driver asked for one (option & MCU_CMD_ACK) and are counted
 * in stats.cmds_unknown.
 */
static const struct cmd_entry {
	bool uni;
	u16 cid;
	const char *name;
	enum ack_kind ack;
	void (*handler)(const struct cmd *c);
} cmd_table[] = {
	/* firmware download (mt76_connac_mcu.c) */
	{ false, MCU_CMD_NIC_POWER_CTRL,	"NIC_POWER_CTRL",	ACK_NONE },
	{ false, MCU_CMD_PATCH_SEM_CONTROL,	"PATCH_SEM_CONTROL",	ACK_HANDLER, cmd_patch_sem },
	{ false, MCU_CMD_PATCH_START_REQ,	"PATCH_START_REQ",	ACK_HANDLER, cmd_dl_req },
	{ false, MCU_CMD_TARGET_ADDRESS_LEN_REQ, "TARGET_ADDRESS_LEN_REQ", ACK_HANDLER, cmd_dl_req },
	{ false, MCU_CMD_FW_SCATTER,		"FW_SCATTER",		ACK_NONE },
	{ false, MCU_CMD_PATCH_FINISH_REQ,	"PATCH_FINISH_REQ",	ACK_HANDLER, cmd_patch_finish },
	{ false, MCU_CMD_FW_START_REQ,		"FW_START_REQ",		ACK_HANDLER, cmd_fw_start },
	/* mt7925_run_firmware() / mt7925_register_device() */
	{ true, MCU_UNI_CMD_CHIP_CONFIG,	"CHIP_CONFIG",		ACK_HANDLER, cmd_chip_config },
	{ true, MCU_UNI_CMD_EFUSE_CTRL,		"EFUSE_CTRL",		ACK_HANDLER, cmd_efuse },
	{ true, MCU_UNI_CMD_WSYS_CONFIG,	"WSYS_CONFIG",		ACK_STATUS },
	{ true, MCU_UNI_CMD_HIF_CTRL,		"HIF_CTRL",		ACK_NONE },
	/* regulatory / CLC (mt7925_regd_*, mt7925_mcu_set_clc) */
	{ true, MCU_UNI_CMD_SET_DOMAIN_INFO,	"SET_DOMAIN_INFO",	ACK_STATUS },
	{ true, MCU_UNI_CMD_SET_POWER_LIMIT,	"SET_POWER_LIMIT",	ACK_STATUS },
	/* generic set commands of the mac/phy init and of a later start/stop */
	{ true, MCU_UNI_CMD_DEV_INFO_UPDATE,	"DEV_INFO_UPDATE",	ACK_STATUS },
	{ true, MCU_UNI_CMD_BSS_INFO_UPDATE,	"BSS_INFO_UPDATE",	ACK_STATUS },
	{ true, MCU_UNI_CMD_STA_REC_UPDATE,	"STA_REC_UPDATE",	ACK_STATUS },
	{ true, MCU_UNI_CMD_EDCA_UPDATE,	"EDCA_UPDATE",		ACK_STATUS },
	{ true, MCU_UNI_CMD_SUSPEND,		"SUSPEND",		ACK_STATUS },
	{ true, MCU_UNI_CMD_OFFLOAD,		"OFFLOAD",		ACK_STATUS },
	{ true, MCU_UNI_CMD_BAND_CONFIG,	"BAND_CONFIG",		ACK_STATUS },
	{ true, MCU_UNI_CMD_POWER_CTRL,		"POWER_CTRL",		ACK_STATUS },
	{ true, MCU_UNI_CMD_RX_HDR_TRANS,	"RX_HDR_TRANS",		ACK_STATUS },
	{ true, MCU_UNI_CMD_TXPOWER,		"TXPOWER",		ACK_STATUS },
	{ true, MCU_UNI_CMD_FIXED_RATE_TABLE,	"FIXED_RATE_TABLE",	ACK_STATUS },
	{ true, MCU_UNI_CMD_RSSI_MONITOR,	"RSSI_MONITOR",		ACK_STATUS },
	{ true, MCU_UNI_CMD_SNIFFER,		"SNIFFER",		ACK_STATUS },
	{ true, MCU_UNI_CMD_SCAN_REQ,		"SCAN_REQ",		ACK_STATUS },
	{ true, MCU_UNI_CMD_ROC,		"ROC",			ACK_STATUS },
	{ true, MCU_UNI_CMD_REG_ACCESS,		"REG_ACCESS",		ACK_STATUS },
	{ true, MCU_UNI_CMD_SET_DBDC_PARMS,	"SET_DBDC_PARMS",	ACK_STATUS },
	{ true, MCU_UNI_CMD_THERMAL,		"THERMAL",		ACK_STATUS },
	{ true, MCU_UNI_CMD_MLD,		"MLD",			ACK_STATUS },
	{ true, MCU_UNI_CMD_AXIDMA,		"AXIDMA",		ACK_STATUS },
	{ true, MCU_UNI_CMD_FACT_CAL,		"FACT_CAL",		ACK_STATUS },
};

static const struct cmd_entry *cmd_lookup(bool uni, u16 cid)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(cmd_table); i++)
		if (cmd_table[i].uni == uni && cmd_table[i].cid == cid)
			return &cmd_table[i];
	return NULL;
}

/* One descriptor of the WM ring: a command framed by mt7925_mcu_fill_message(). */
static void mcu_command(const u8 *data, size_t len)
{
	const struct mt76_connac2_mcu_uni_txd *uni = (const void *)data;
	const struct mt76_connac2_mcu_txd *legacy = (const void *)data;
	const struct cmd_entry *ent;
	struct cmd c = {};

	m.stats.cmds++;
	if (len < sizeof(*uni) || uni->pkt_type != MCU_PKT_ID) {
		model_error("WM ring: %zu byte frame is not an MCU command", len);
		return;
	}
	/*
	 * Both headers put pkt_type at the same place; the byte before it is
	 * the command id in the legacy header (never 0) and reserved (0) in
	 * the unified one.
	 */
	if (uni->rsv == 0) {
		c.uni = true;
		c.cid = le16_to_cpu(uni->cid);
		c.seq = uni->seq;
		c.option = uni->option;
		c.payload = data + sizeof(*uni);
		c.plen = len - sizeof(*uni);
		/* unified payloads start with 4 reserved bytes and a TLV */
		if (c.plen >= 4 + sizeof(struct tlv))
			c.tag = le16_to_cpu(((const struct tlv *)(c.payload + 4))->tag);
	} else {
		if (len < sizeof(*legacy)) {
			model_error("WM ring: short legacy command (%zu bytes)", len);
			return;
		}
		c.cid = legacy->cid;
		c.seq = legacy->seq;
		c.option = legacy->set_query;
		c.ext_cid = legacy->ext_cid;
		c.payload = data + sizeof(*legacy);
		c.plen = len - sizeof(*legacy);
	}

	ent = cmd_lookup(c.uni, c.cid);
	c.name = ent ? ent->name : "?";
	if (c.uni)
		model_log(1, "cmd %3u: UNI %-22s cid 0x%02x tag 0x%04x seq %2u opt %#x len %zu",
			  m.stats.cmds, c.name, c.cid, c.tag, c.seq, c.option, c.plen);
	else
		model_log(1, "cmd %3u: %-26s cid 0x%02x ext 0x%02x seq %2u len %zu",
			  m.stats.cmds, c.name, c.cid, c.ext_cid, c.seq, c.plen);

	if (!ent) {
		m.stats.cmds_unknown++;
		if (c.uni && !(c.option & MCU_CMD_ACK)) {
			model_log(1, "  unknown command, no ack requested");
			return;
		}
		model_log(1, "  unknown command, generic ack");
		if (c.uni)
			ack_uni_status(&c, 0);
		else
			ack_legacy_status(&c, 0);
		return;
	}

	switch (ent->ack) {
	case ACK_NONE:
		break;
	case ACK_STATUS:
		if (c.uni)
			ack_uni_status(&c, 0);
		else
			ack_legacy_status(&c, 0);
		break;
	case ACK_HANDLER:
		ent->handler(&c);
		break;
	}
}

/* One descriptor of the FWDL ring: a raw FW_SCATTER chunk. */
static void fwdl_chunk(const u8 *data, size_t len)
{
	m.stats.fwdl_chunks++;
	m.stats.fwdl_bytes += len;
	m.fwdl_chunks++;
	m.fwdl_got += len;
	model_log(2, "  fw scatter chunk %u: %zu bytes (%u/%u)", m.fwdl_chunks, len,
		  m.fwdl_got, m.fwdl_expect);
	if (m.fwdl_expect && m.fwdl_got >= m.fwdl_expect) {
		model_log(1, "  FW_SCATTER: %u chunks, %u bytes%s", m.fwdl_chunks, m.fwdl_got,
			  m.fwdl_got == m.fwdl_expect ? "" : " (MORE THAN ANNOUNCED)");
		m.fwdl_expect = 0;
	}
}

/* cpu_idx was written on TX ring idx: consume what the driver queued. */
static void tx_ring_kick(int ring)
{
	volatile struct mt76_queue_regs *regs = tx_ring_regs(ring);
	struct mt76_desc *desc;
	u32 ndesc, cpu, dma;

	desc = ring_desc(regs, &ndesc);
	if (!desc)
		return;
	cpu = regs->cpu_idx;
	dma = regs->dma_idx;
	if (cpu >= ndesc || dma >= ndesc) {
		model_error("TX ring %d indices out of range (cpu %u dma %u of %u)", ring, cpu, dma, ndesc);
		return;
	}
	if (dma == cpu)
		return;
	if (!m.tx_dma_en) {
		model_log(1, "TX ring %d kicked while TX DMA is disabled; left pending", ring);
		return;
	}

	while (dma != cpu) {
		struct mt76_desc *d = &desc[dma];
		u32 ctrl = le32_to_cpu(d->ctrl);
		u32 len = FIELD_GET(MT_DMA_CTL_SD_LEN0, ctrl);
		u64 bus = le32_to_cpu(d->buf0) |
			  (u64)FIELD_GET(MT_DMA_CTL_SDP0_H, le32_to_cpu(d->info)) << 32;
		const u8 *data = lkpi_host_dma_to_va(bus, len);

		if (!data)
			model_error("TX ring %d desc %u: buffer %#llx (%u bytes) is not DMA memory",
				    ring, dma, (unsigned long long)bus, len);
		else if (ring == MT7925_TXQ_MCU_WM)
			mcu_command(data, len);
		else if (ring == MT7925_TXQ_FWDL)
			fwdl_chunk(data, len);
		else
			model_log(1, "TX ring %d: %u byte frame dropped (no MAC model)", ring, len);

		d->ctrl = cpu_to_le32(ctrl | MT_DMA_CTL_DMA_DONE);	/* TX_WB_DDONE */
		dma = (dma + 1) % ndesc;
		regs->dma_idx = dma;
	}
	raise_int(tx_done_bit(ring));
}

/* --------------------------------------------------------- register hooks */
static void reset_pointers(bool tx)
{
	int i;

	if (tx)
		for (i = 0; i < MODEL_N_TX_RINGS; i++)
			tx_ring_regs(i)->dma_idx = 0;
	else
		for (i = 0; i < MODEL_N_RX_RINGS; i++)
			rx_ring_regs(i)->dma_idx = 0;
}

static void wfsys_reset(void)
{
	m.stats.wfsys_resets++;
	m.patch_loaded = false;
	m.fwdl_expect = 0;
	*reg32(OFF_CONN_ON_MISC) = MT_TOP_MISC2_FW_PWR_ON;	/* ROM is up, waiting for download */
	model_log(1, "WFSYS reset: firmware state back to PWR_ON");
}

static void remap_l1(u32 val)
{
	/* only the page holding HW_CHIPID / HW_REV has modelled contents */
	if (FIELD_GET(MT_HIF_REMAP_L1_MASK, val) == FIELD_GET(MT_HIF_REMAP_L1_BASE, MT_HW_CHIPID)) {
		*reg32(OFF_HW_CHIPID) = MODEL_CHIPID;
		*reg32(OFF_HW_REV) = MODEL_HW_REV;
	}
}

void mt7925_model_mmio_write(volatile void *addr, uint32_t val)
{
	size_t off = (size_t)((volatile u8 *)addr - m.bar0);
	volatile u32 *reg = (volatile u32 *)addr;

	bool raise;

	if (!m.bar0 || off >= m.bar_len)
		return;

	lkpi_os_spin_lock(m.lock);
	m.stats.mmio_writes++;

	if (off == OFF_CONN_ON_LPCTL) {
		m.stats.lpctl_writes++;
		if (val & PCIE_LPCR_HOST_SET_OWN)
			*reg = PCIE_LPCR_HOST_OWN_SYNC;		/* firmware owns the chip */
		else if (val & PCIE_LPCR_HOST_CLR_OWN)
			*reg = 0;				/* driver owns it */
	} else if (off == OFF_WFSYS_SW_RST_B) {
		if (val & WFSYS_SW_RST_B) {
			*reg = val | WFSYS_SW_INIT_DONE;
			wfsys_reset();
		}
	} else if (off == MT_HIF_REMAP_L1) {
		remap_l1(val);
	} else if (off == MT_WFDMA0_HOST_INT_STA) {
		m.int_sta &= ~val;				/* write 1 to clear */
		*reg = m.int_sta;
	} else if (off == MT_WFDMA0_HOST_INT_ENA || off == MT7925_PCIE_MAC_INT_ENABLE) {
		int_update();					/* re-assert if still pending */
	} else if (off == MT_WFDMA0_GLO_CFG) {
		bool en = val & MT_WFDMA0_GLO_CFG_TX_DMA_EN;
		int i;

		if (en && !m.tx_dma_en) {
			m.tx_dma_en = true;
			for (i = 0; i < MODEL_N_TX_RINGS; i++)
				tx_ring_kick(i);		/* pick up pending descriptors */
		}
		m.tx_dma_en = en;
	} else if (off == MT_WFDMA0_RST_DTX_PTR) {
		reset_pointers(true);
	} else if (off == MT_WFDMA0_RST_DRX_PTR) {
		reset_pointers(false);
	} else if (off >= MT_TX_RING_BASE &&
		   off < MT_TX_RING_BASE + MODEL_N_TX_RINGS * MT_RING_SIZE) {
		if ((off & (MT_RING_SIZE - 1)) == offsetof(struct mt76_queue_regs, cpu_idx))
			tx_ring_kick((off - MT_TX_RING_BASE) / MT_RING_SIZE);
	} else if (off >= MT_RX_EVENT_RING_BASE &&
		   off < MT_RX_EVENT_RING_BASE + MODEL_N_RX_RINGS * MT_RING_SIZE) {
		/* RX cpu_idx: more buffers; events are only generated on demand */
	}

	raise = m.irq_pending;
	m.irq_pending = false;
	if (raise)
		m.stats.irqs++;
	lkpi_os_spin_unlock(m.lock);

	if (raise)
		lkpi_host_pci_raise_irq(m.pci);
}

/* ------------------------------------------------------------- lifecycle */
void mt7925_model_init(lkpi_os_pci_t pci, int verbose)
{
	static const u8 mac[ETH_ALEN] = { 0x00, 0x0c, 0xe7, 0x79, 0x25, 0x01 };

	memset(&m, 0, sizeof(m));
	m.lock = lkpi_os_spinlock_alloc();

	m.pci = pci;
	m.verbose = verbose;
	m.bar0 = lkpi_host_pci_bar0(pci);
	m.bar_len = 2 * 1024 * 1024;	/* mt7925 BAR0 is 2 MiB; the fixed map ends at 0x200000 */

	/* EFUSE: chip id, MAC address, HW type (encapsulation 0) */
	m.eeprom[MT_EE_CHIP_ID] = MODEL_CHIPID & 0xff;
	m.eeprom[MT_EE_CHIP_ID + 1] = MODEL_CHIPID >> 8;
	memcpy(m.eeprom + MT_EE_MAC_ADDR, mac, ETH_ALEN);
	m.eeprom[MT_EE_HW_TYPE] = 0;

	/* power-on state: the ROM code is running, remap window shows the chip id */
	*reg32(OFF_HW_CHIPID) = MODEL_CHIPID;
	*reg32(OFF_HW_REV) = MODEL_HW_REV;
	*reg32(OFF_CONN_ON_MISC) = MT_TOP_MISC2_FW_PWR_ON;

	lkpi_host_mmio_write_hook = mt7925_model_mmio_write;
}

void mt7925_model_fini(void)
{
	if (lkpi_host_mmio_write_hook == mt7925_model_mmio_write)
		lkpi_host_mmio_write_hook = NULL;
	lkpi_os_spinlock_free(m.lock);
	m.lock = NULL;
	m.bar0 = NULL;
}

const struct mt7925_model_stats *mt7925_model_stats(void)
{
	return &m.stats;
}
