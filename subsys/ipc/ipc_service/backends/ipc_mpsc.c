#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/hash_function.h>

#include <zephyr/drivers/mbox.h>
#include <zephyr/ipc/shmpsc_pbuf.h>
#include <zephyr/ipc/ipc_service_backend.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ipc_mpsc, LOG_LEVEL_DBG);
#include <nrf.h>

#define DT_DRV_COMPAT	zephyr_ipc_mpsc

#if CONFIG_SOC_NRF54H20_CPURAD
#define DBG(...) LOG_WRN(__VA_ARGS__)
#else
#define DBG(...)
#endif

struct ipc_mpsc_config {
	struct mbox_dt_spec mbox_tx;
	struct mbox_dt_spec mbox_rx;
	struct shmpsc_pbuf_config tx_pb_config;
	struct shmpsc_pbuf *tx_pb;
	struct shmpsc_pbuf *rx_pb;
};

#define STATE_IDLE 0
#define STATE_CONNECTING BIT(0)
#define STATE_REMOTE_CONNECTING BIT(1)
#define STATE_CONNECTED (STATE_CONNECTING | STATE_REMOTE_CONNECTING)

#define ID_BOUND UINT32_MAX
#define ID_UNBOUND INT32_MAX

#define EP_MAX CONFIG_IPC_SERVICE_BACKEND_MPSC_EP_MAX
#define INVALID_ID UINT32_MAX

struct ipc_mpsc_ep_data {
	uint32_t hash;
	const struct ipc_ept_cfg *ep;
	uint32_t remote_id;
};

struct ipc_mpsc_data {
	uint32_t cnt;
	uint32_t state;
	struct ipc_mpsc_ep_data ep_data[EP_MAX];
	uint32_t ep_cnt;
	struct ipc_mpsc_pkt *pkt;
	uint32_t pkt_len;
#ifdef CONFIG_IPC_SERVICE_BACKEND_MPSC_SYS_WORKQ
	struct k_work work;
	const struct ipc_mpsc_config *config;
#endif
	bool remote_reset;
};

struct ipc_mpsc_pkt {
	uint32_t id;
	uint8_t data[];
};

static const uint32_t magic[] = {0x1d438a8f, 0x782c0ba2};

#if defined(CONFIG_IPC_SERVICE_BACKEND_MPSC_THREAD)
static bool rx_process_packet(struct ipc_mpsc_data *data, const struct ipc_mpsc_config *conf, bool in_isr);

K_THREAD_STACK_DEFINE(ipc_mpsc_stack, CONFIG_IPC_SERVICE_BACKEND_MPSC_STACK_SIZE);
static struct k_thread ipc_mpsc_thread;
static struct k_sem sem;

static void thread_wakeup(void)
{
	k_sem_give(&sem);
}

static void rx_thread(void *arg0, void *arg1, void *arg2)
{
#define DEV_GET(_id) DEVICE_DT_GET(DT_DRV_INST(_id)),

	static const struct device *ipc[] = { DT_INST_FOREACH_STATUS_OKAY(DEV_GET) };
	bool more;

	while (1) {
		more = false;
		for (int i = 0; i < ARRAY_SIZE(ipc); i++) {
			const struct ipc_mpsc_config *conf = ipc[i]->config;
			struct ipc_mpsc_data *data = ipc[i]->data;

			more |= rx_process_packet(data, conf, false);
		}

		if (!more) {
			k_sem_take(&sem, K_FOREVER);
		}
	}
}

static void thread_init(void)
{
	k_thread_create(&ipc_mpsc_thread, ipc_mpsc_stack,
			K_THREAD_STACK_SIZEOF(ipc_mpsc_stack),
			rx_thread, NULL, NULL, NULL,
			CONFIG_IPC_SERVICE_BACKEND_MPSC_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&ipc_mpsc_thread, "ipc_mpsc");
	k_sem_init(&sem, 0, 1);
}
#endif

static void bound_req_handle(struct ipc_mpsc_data *data, uint8_t *pkt, uint32_t len)
{
	uint32_t *d32 = (uint32_t *)pkt;
	uint32_t hash = d32[0];
	uint32_t id = d32[1];
	bool bound = false;
	uint32_t key = irq_lock();
	uint32_t i;

	for (i = 0; i < data->ep_cnt; i++) {
		if ((data->ep_data[i].ep != NULL) && (data->ep_data[i].hash == hash)) {
			data->ep_data[i].remote_id = id;
			LOG_DBG("Bound request bounded, remote id:%d local:%d", id, i);
			bound = true;
			goto bail;
		}
	}

	/* Worth checking if same hash already does not exist. */
	data->ep_data[i].hash = hash;
	data->ep_data[i].remote_id = id;
	data->ep_cnt++;
	LOG_DBG("Bound request pending, remote id:%d local:%d", id, i);
bail:
	irq_unlock(key);

	__ASSERT_NO_MSG(data->ep_cnt <= EP_MAX);

	if (bound) {
		data->ep_data[i].ep->cb.bound(data->ep_data[i].ep->priv);
	}
}

static void remove_ep(struct ipc_mpsc_data *data, uint32_t id)
{
	const struct ipc_ept_cfg *ep = data->ep_data[id].ep;

	data->ep_data[id].ep = NULL;
	data->ep_data[id].remote_id = INVALID_ID;
	ep->cb.unbound(ep->priv);
}

static void remote_reset(struct ipc_mpsc_data *data)
{
	LOG_WRN("Remote got reset");

	for (int i = 0; i < data->ep_cnt; i++) {
		if (data->ep_data[i].remote_id != INVALID_ID) {
			remove_ep(data, i);
		}
	}
	data->state = STATE_IDLE;
}

static bool rx_process_packet(struct ipc_mpsc_data *data, const struct ipc_mpsc_config *conf, bool in_isr)
{
	uint32_t cnt;
	struct ipc_mpsc_pkt *pkt = data->pkt;

	if (!pkt) {
		return false;
	}

	if (data->remote_reset) {
		remote_reset(data);
	}

	if (data->state == STATE_CONNECTED) {
		uint32_t pkt_len = data->pkt_len - offsetof(struct ipc_mpsc_pkt, data);

		if (pkt->id < EP_MAX) {
			const struct ipc_ept_cfg *ep = data->ep_data[pkt->id].ep;

			if (ep) {
				ep->cb.received(pkt->data, pkt_len, ep->priv);
			}
		} else if (pkt->id == ID_BOUND) {
			/* bound  request */
			bound_req_handle(data, pkt->data, pkt_len);
		} else if (pkt->id == ID_UNBOUND) {
			/* unbound  request */
			__ASSERT(data->pkt_len ==
				 sizeof(struct ipc_mpsc_pkt) + sizeof(uint32_t),
				"Unexpected packet");
			uint32_t id = *(uint32_t *)pkt->data;
			const struct ipc_ept_cfg *ep = data->ep_data[id].ep;

			if (ep) {
				remove_ep(data, id);
			}
		} else {
			__ASSERT(0, "Unexpected message %d", pkt->id);
		}
	} else {
		/* IDLE state. */
		if ((pkt->id == ID_BOUND) &&
		    (memcmp(pkt->data, magic, sizeof(magic)) == 0) &&
		    (data->pkt_len == (sizeof(magic) + sizeof(struct ipc_mpsc_pkt)))) {
			data->state |= STATE_REMOTE_CONNECTING;
			LOG_DBG("Received remote connection request. State: connect%s",
				data->state == STATE_CONNECTED ? "ed" : "ing");
		} else {
			LOG_WRN("Unexpected packet");
		}
	}
	shmpsc_pbuf_free(conf->rx_pb, pkt);

	cnt = data->cnt;
	data->pkt = shmpsc_pbuf_claim(conf->rx_pb, &data->pkt_len, &cnt);
	if ((int)(cnt - data->cnt) < 0) {
		data->remote_reset = true;
	}

	if (data->pkt == NULL) {
		if (!in_isr) {
			int ret;

			DBG("en");
			ret = mbox_set_enabled_dt(&conf->mbox_rx, true);
			(void)ret;
			__ASSERT_NO_MSG(ret == 0);
		}

		return false;
	}

	return true;
}

#ifndef CONFIG_IPC_SERVICE_BACKEND_MPSC_IRQ
static void to_thread(const struct ipc_mpsc_config *conf, struct ipc_mpsc_data *data, bool dis_mbox)
{
	if (dis_mbox) {
		int ret;

		ret = mbox_set_enabled_dt(&conf->mbox_rx, false);
		(void)ret;
		__ASSERT_NO_MSG(ret == 0);
	}

#ifdef CONFIG_IPC_SERVICE_BACKEND_MPSC_THREAD
	thread_wakeup();
#else
	if (k_work_submit(&data->work) < 0) {
		/* The mbox processing work is never canceled.
		 * The negative error code should never be seen.
		 */
		__ASSERT_NO_MSG(false);
	}
#endif
}
#endif

#if defined(CONFIG_IPC_SERVICE_BACKEND_MPSC_SYS_WORKQ)
static void rx_process(struct k_work *work)
{
	struct ipc_mpsc_data *data = CONTAINER_OF(work, struct ipc_mpsc_data, work);

	if (rx_process_packet(data, data->config, false)) {
		to_thread(data->config, data, false);
	}
}
#endif

static void rx_isr_handle(const struct device *dev)
{
	const struct ipc_mpsc_config *conf = dev->config;
	struct ipc_mpsc_data *data = dev->data;
	uint32_t cnt = data->cnt;
	bool cont;

	data->pkt = shmpsc_pbuf_claim(conf->rx_pb, &data->pkt_len, &cnt);
	if (!data->pkt) {
		return;
	}

	if ((int)(cnt - data->cnt) < 0) {
		data->remote_reset = true;
	}

	do {
#ifdef CONFIG_IPC_SERVICE_BACKEND_MPSC_IRQ
		cont = rx_process_packet(data, conf, true);
#else
		if ((data->state == STATE_CONNECTED) && (data->pkt->id < EP_MAX) &&
			 (data->ep_data[data->pkt->id].ep->prio > 0)) {
			cont = rx_process_packet(data, conf, true);
		} else {
			cont = false;
			to_thread(conf, data, true);
		}
#endif
	} while (cont);
}

static void mbox_callback(const struct device *dev, uint32_t channel,
			  void *user_data, struct mbox_msg *msg_data)
{
#if CONFIG_SOC_NRF54H20_CPUAPP
	/*LOG_WRN("isr");*/
#endif
	rx_isr_handle(user_data);
}

static int commit(const struct ipc_mpsc_config *conf, const void *data, size_t len)
{
	shmpsc_pbuf_commit(conf->tx_pb, (void *)data, len);

	return mbox_send_dt(&conf->mbox_tx, NULL);
}


static int send_bound_request(const struct ipc_mpsc_config *conf, uint32_t hash, uint32_t id)
{
	uint32_t pkt_len = 3 * sizeof(uint32_t);
	uint32_t *pkt = shmpsc_pbuf_alloc(conf->tx_pb, pkt_len);

	if (pkt == NULL) {
		LOG_ERR("Failed to allocate");
		return -ENOMEM;
	}

	pkt[0] = ID_BOUND;
	pkt[1] = hash;
	pkt[2] = id;
	commit(conf, pkt, pkt_len);

	return 0;
}

static int register_ept(const struct device *instance, void **token,
			const struct ipc_ept_cfg *cfg)
{
	const struct ipc_mpsc_config *conf = instance->config;
	struct ipc_mpsc_data *data = instance->data;
	uint32_t name_hash;
	uint32_t key;
	uint8_t id;
	bool bound = false;
	int ret = 0;

	name_hash = sys_hash32(cfg->name, strlen(cfg->name));

	key = irq_lock();

	/* Start by searching if there is already pending request with a given hash. If
	 * yes then we are bounded on that side.
	 */
	for (int i = 0; i < data->ep_cnt; i++) {
		if ((data->ep_data[i].ep == NULL) && (data->ep_data[i].hash == name_hash)) {
			/* Remote bound request for that endpoint is already received. */
			LOG_DBG("Register EP (bounded), remote id:%d local:%d",
					data->ep_data[i].remote_id, i);
			bound = true;
			data->ep_data[i].ep = cfg;
			*(uintptr_t *)token = i;
			id = i;
			break;
		}
	}

	/* If there is no pending bounding request then store the current one. In both cases
	 * send bound request to the remote.
	 */
	if (!bound) {
		if (data->ep_cnt >= EP_MAX) {
			ret = -ENOMEM;
		} else {
			LOG_DBG("Register EP (pending), local:%d", data->ep_cnt);
			data->ep_data[data->ep_cnt].ep = cfg;
			data->ep_data[data->ep_cnt].hash = name_hash;
			*(uintptr_t *)token = data->ep_cnt;
			id = data->ep_cnt;
			data->ep_cnt++;
		}
	}

	irq_unlock(key);

	if (ret != 0) {
		return ret;
	}

	ret = send_bound_request(conf, name_hash, id);
	if (ret != 0) {
		return ret;
	}

	if (bound) {
		cfg->cb.bound(cfg->priv);
	}

	return 0;
}

static int deregister_ept(const struct device *instance, void *token)
{
	return -ENOTSUP;
}
static int send(const struct device *instance, void *token,
		const void *msg, size_t len)
{
	const struct ipc_mpsc_config *conf = instance->config;
	struct ipc_mpsc_data *data = instance->data;
	uintptr_t id = (uintptr_t)token;
	uint32_t pkt_len = len + sizeof(uint32_t);
	uint32_t *buf = shmpsc_pbuf_alloc(conf->tx_pb, pkt_len);
	int ret;

	if (buf == NULL) {
		return -ENOMEM;
	}

	buf[0] = data->ep_data[id].remote_id;
	memcpy(&buf[1], msg, len);
	ret = commit(conf, buf, pkt_len);

	return (ret != 0) ? ret : len;
}

static int get_tx_buffer(const struct device *instance, void *token,
			 void **buffer, uint32_t *len, k_timeout_t wait)
{
#define WAIT_DIV 4

	const struct ipc_mpsc_config *conf = instance->config;
	struct ipc_mpsc_data *data = instance->data;
	uintptr_t id = (uintptr_t)token;
	uint32_t *buf;
	k_timeout_t wait_chunk = { .ticks = wait.ticks / WAIT_DIV };
	uint32_t rpt = WAIT_DIV;

	while ((rpt > 0) && !(buf = shmpsc_pbuf_alloc(conf->tx_pb, *len + sizeof(uint32_t)))) {
		k_sleep(wait_chunk);
		rpt--;
	}
	if (buf == NULL) {
		return -ENOBUFS;
	}

	buf[0] = data->ep_data[id].remote_id;
	*(void **)buffer = &buf[1];

	return 0;
}

static int drop_tx_buffer(const struct device *instance, void *token, const void *data)
{
	const struct ipc_mpsc_config *conf = instance->config;

	return commit(conf, &((uint32_t *)data)[-1], 0);
}

static int send_nocopy(const struct device *instance, void *token,
		       const void *data, size_t len)
{
	const struct ipc_mpsc_config *conf = instance->config;
	int ret;

	ret = commit(conf, &((uint32_t *)data)[-1], len + sizeof(uint32_t));
	return (ret != 0) ? ret : len;
}

static int open(const struct device *instance)
{
	const struct ipc_mpsc_config *conf = instance->config;
	struct ipc_mpsc_data *data = instance->data;
	int ret;

	if (data->state & STATE_CONNECTING) {
		return -EALREADY;
	}

	data->state |= STATE_CONNECTING;

	LOG_DBG("Open request state: connect%s",
		data->state == STATE_CONNECTED ? "ed" : "ing");
	ret = mbox_set_enabled_dt(&conf->mbox_rx, true);
	if (ret != 0) {
		return ret;
	}

	return send_bound_request(conf, magic[0], magic[1]);
}

static int close(const struct device *instance)
{
	return 0;
}

const static struct ipc_service_backend backend_ops = {
	.open_instance = open,
	.close_instance = close,
	.register_endpoint = register_ept,
	.deregister_endpoint = deregister_ept,
	.send = send,
	.get_tx_buffer = IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_MPSC_NO_COPY) ?
		get_tx_buffer : NULL,
	.drop_tx_buffer = IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_MPSC_NO_COPY) ?
		drop_tx_buffer : NULL,
	.send_nocopy = IS_ENABLED(CONFIG_IPC_SERVICE_BACKEND_MPSC_NO_COPY) ?
		send_nocopy : NULL,
};

static int backend_init(const struct device *instance)
{
	const struct ipc_mpsc_config *conf = instance->config;
#if defined(CONFIG_IPC_SERVICE_BACKEND_MPSC_THREAD)
	static bool once;

	if (!once) {
		thread_init();
		once = true;
	}
#elif defined(CONFIG_IPC_SERVICE_BACKEND_MPSC_SYS_WORKQ)
	struct ipc_mpsc_data *data = instance->data;

	data->config = conf;
	k_work_init(&data->work, rx_process);
#endif

	LOG_WRN("buffer:%p size:%d", (void *)conf->tx_pb_config.buf, conf->tx_pb_config.size);
	shmpsc_pbuf_init(conf->tx_pb, &conf->tx_pb_config);

	return mbox_register_callback_dt(&conf->mbox_rx, mbox_callback, (void *)instance);
}

#define DEFINE_BACKEND_DEVICE(i)								   \
	static struct k_spinlock backend_lock_##i;						   \
	static const struct ipc_mpsc_config backend_config_##i = {				   \
		.mbox_tx = MBOX_DT_SPEC_INST_GET(i, tx),					   \
		.mbox_rx = MBOX_DT_SPEC_INST_GET(i, rx),					   \
		.tx_pb_config = {								   \
			.buf = (uint32_t *)(DT_REG_ADDR(DT_INST_PHANDLE(i, tx_region)) +	   \
					sizeof(struct shmpsc_pbuf)),				   \
			.size = (DT_REG_SIZE(DT_INST_PHANDLE(i, tx_region)) -			   \
					sizeof(struct shmpsc_pbuf)),				   \
			.lock = &backend_lock_##i,						   \
		},										   \
		.tx_pb = (struct shmpsc_pbuf *)DT_REG_ADDR(DT_INST_PHANDLE(i, tx_region)),	   \
		.rx_pb = (struct shmpsc_pbuf *)DT_REG_ADDR(DT_INST_PHANDLE(i, rx_region)),	   \
	};											   \
	static struct ipc_mpsc_data backend_data_##i;						   \
	DEVICE_DT_INST_DEFINE(i,								   \
			 &backend_init,								   \
			 NULL,									   \
			 &backend_data_##i,							   \
			 &backend_config_##i,							   \
			 POST_KERNEL,								   \
			 CONFIG_IPC_SERVICE_REG_BACKEND_PRIORITY,				   \
			 &backend_ops);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_BACKEND_DEVICE)
