/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_LOGGING_LOG_LINK_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_LINK_H_

#include <zephyr/types.h>
#include <sys/__assert.h>
#include <logging/log_msg2.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log link API
 * @defgroup log_link Log link API
 * @ingroup logger
 * @{
 */

struct log_link;

typedef void (*log_link_callback_t)(const struct log_link *link,
				    union log_msg2_generic *msg);

typedef void (*log_link_dropped_cb_t)(const struct log_link *link,
				      uint32_t dropped);

struct log_link_config {
	log_link_callback_t msg_cb;
	log_link_dropped_cb_t dropped_cb;
};

struct log_link_api {
	int (*initiate)(const struct log_link *link, struct log_link_config *config);
	int (*activate)(const struct log_link *link);
	int (*get_domain_name)(const struct log_link *link, uint32_t domain_id,
				char *buf, uint32_t *length);
	int (*get_source_name)(const struct log_link *link, uint32_t domain_id,
				uint16_t source_id, char *buf, uint32_t *length);
	int (*get_levels)(const struct log_link *link, uint32_t domain_id,
				uint16_t source_id, uint8_t *level,
				uint8_t *runtime_level);
	int (*set_runtime_level)(const struct log_link *link, uint32_t domain_id,
				uint16_t source_id, uint8_t level);
};

struct log_link_ctrl_blk {
	uint32_t domain_cnt;
	uint16_t source_cnt[1 + COND_CODE_1(CONFIG_LOG_MULTIDOMAIN,
					    (CONFIG_LOG_REMOTE_DOMAIN_MAX_COUNT),
					    (0))];
	uint32_t domain_offset;
	uint32_t *filters;
};

struct log_link {
	const struct log_link_api *api;
	const char *name;
	struct log_link_ctrl_blk *ctrl_blk;
	void *ctx;
};

extern const struct log_link __log_links_start[];
extern const struct log_link __log_links_end[];

#define LOG_LINK_DEF(_name, _api, _ctx) \
	static struct log_link_ctrl_blk _name##_ctrl_blk; \
	static const Z_STRUCT_SECTION_ITERABLE(log_link, _name) = \
	{ \
		.api = &_api, \
		.name = STRINGIFY(_name), \
		.ctrl_blk = &_name##_ctrl_blk, \
		.ctx = _ctx, \
	}

/** @brief Initiate log link.
 *
 * Function initiates the link. Since initialization procedure may be time
 * consuming, function returns before link is ready to not block logging
 * initialization. @ref log_link_activate is called to complete link initialization.
 *
 * @param link		Log link instance.
 * @param config	Configuration.
 *
 * @return 0 on success or error code.
 */
static inline int log_link_initiate(const struct log_link *link,
				   struct log_link_config *config)
{
	__ASSERT_NO_MSG(link);

	return link->api->initiate(link, config);
}

/** @brief Activate log link.
 *
 * Function checks if link is initilized and completes initialization process.
 * When successfully returns, link is ready with domain and sources count fetched
 * and timestamp details updated.
 *
 * @param link		Log link instance.
 *
 * @retval 0 When successfully activated.
 * @retval -EINPROGRESS Activation in progress.
 */
static inline int log_link_activate(const struct log_link *link)
{
	__ASSERT_NO_MSG(link);

	return link->api->activate(link);
}
/** @brief Get number of domains in the link.
 *
 * @param[in] link	Log link instance.
 *
 * @return Number of domains.
 */
static inline uint8_t log_link_domains_count(const struct log_link *link)
{
	__ASSERT_NO_MSG(link);

	return link->ctrl_blk->domain_cnt;
}

/** @brief Get number of sources in the domain.
 *
 * @param[in] link		Log link instance.
 * @param[in] domain_id		Domain ID.
 *
 * @return Source count.
 */
static inline uint16_t log_link_sources_count(const struct log_link *link,
					      uint32_t domain_id)
{
	__ASSERT_NO_MSG(link);

	return link->ctrl_blk->source_cnt[domain_id];
}

/** @brief Get domain name.
 *
 * @param[in] link		Log link instance.
 * @param[in] domain_id		Domain ID.
 * @param[out] buf		Output buffer filled with domain name. If NULL
 *				then name length is returned.
 * @param[in,out] length	Buffer size. Name is trimmed if it does not fit
 *				in the buffer and field is set to actual name
 *				length.
 *
 * @return 0 on success or error code.
 */
static inline int log_link_get_domain_name(const struct log_link *link,
					   uint32_t domain_id, char *buf,
					   uint32_t *length)
{
	__ASSERT_NO_MSG(link);

	return link->api->get_domain_name(link, domain_id, buf, length);
}

/** @brief Get source name.
 *
 * @param[in] link	Log link instance.
 * @param[in] domain_id	Domain ID.
 * @param[in] source_id	Source ID.
 * @param[out] buf	Output buffer filled with source name.
 * @param[in,out] length	Buffer size. Name is trimmed if it does not fit
 *				in the buffer and field is set to actual name
 *				length.
 *
 * @return 0 on success or error code.
 */
static inline int log_link_get_source_name(const struct log_link *link,
					   uint32_t domain_id, uint16_t source_id,
					   char *buf, uint32_t *length)
{
	__ASSERT_NO_MSG(link);
	__ASSERT_NO_MSG(buf);

	return link->api->get_source_name(link, domain_id, source_id,
					buf, length);
}

/** @brief Get compiled level of the given source.
 *
 * @param[in] link	Log link instance.
 * @param[in] domain_id	Domain ID.
 * @param[in] source_id	Source ID.
 * @param[out] level	Location to store requested level.
 *
 * @return 0 on success or error code.
 */
static inline int log_link_get_levels(const struct log_link *link,
				      uint32_t domain_id, uint16_t source_id,
				      uint8_t *level, uint8_t *runtime_level)
{
	__ASSERT_NO_MSG(link);

	return link->api->get_levels(link, domain_id, source_id,
				     level, runtime_level);
}

/** @brief Set runtime level of the given source.
 *
 * @param[in] link	Log link instance.
 * @param[in] domain_id	Domain ID.
 * @param[in] source_id	Source ID.
 * @param[out] level	Requested level.
 *
 * @return 0 on success or error code.
 */
static inline int log_link_set_runtime_level(const struct log_link *link,
					     uint32_t domain_id, uint16_t source_id,
					     uint8_t level)
{
	__ASSERT_NO_MSG(link);
	__ASSERT_NO_MSG(level);

	return link->api->set_runtime_level(link, domain_id, source_id, level);
}

/** @brief Get number of log links.
 *
 * @return Number of log links.
 */
static inline int log_link_count(void)
{
	return __log_links_end - __log_links_start;
}

/** @brief Get log link.
 *
 * @param[in] idx  Index of the log link instance.
 *
 * @return    Log link instance.
 */
static inline const struct log_link *log_link_get(uint32_t idx)
{
	return &__log_links_start[idx];
}

/**
 * @brief Enqueue external log message.
 *
 * Add log message to processing queue. Log message is created outside local
 * core. For example it maybe coming from external domain.
 *
 * @param link Log link instance.
 * @param data Message from remote domain.
 * @param len  Length in bytes.
 */
void z_log_msg_enqueue(const struct log_link *link, const void *data, size_t len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_LOGGING_LOG_LINK_H_ */
