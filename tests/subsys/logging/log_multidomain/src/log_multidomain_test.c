/*
 * Copyright (c) 2018 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Test log core
 *
 */


#include <tc_util.h>
#include <stdbool.h>
#include <zephyr.h>
#include <ztest.h>
#include <logging/log_backend.h>
#include <logging/log_ctrl.h>
#include <logging/log.h>
#include "mock_log_link.h"

#define LOG_MODULE_NAME test
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

typedef void (*custom_put_callback_t)(struct log_backend const *const backend,
				      struct log_msg *msg, size_t counter);

static bool in_panic;

struct backend_cb {
	size_t counter;
	bool panic;
	bool keep_msgs;
	bool check_id;
	u32_t exp_id[100];
	bool check_timestamp;
	u32_t exp_timestamps[100];
	bool check_args;
	u32_t exp_nargs[100];
	bool check_strdup;
	bool exp_strdup[100];
	custom_put_callback_t callback;
	u32_t total_drops;
};

static void put(struct log_backend const *const backend,
		struct log_msg *msg)
{
	log_msg_get(msg);
	u32_t nargs = log_msg_nargs_get(msg);
	struct backend_cb *cb = (struct backend_cb *)backend->cb->ctx;

	if (cb->check_id) {
		u32_t exp_id = cb->exp_id[cb->counter];

		zassert_equal(log_msg_source_id_get(msg),
			      exp_id,
			      "Unexpected source_id");
	}

	if (cb->check_timestamp) {
		u32_t exp_timestamp = cb->exp_timestamps[cb->counter];

		zassert_equal(log_msg_timestamp_get(msg),
			      exp_timestamp,
			      "Unexpected message index");
	}

	/* Arguments in the test are fixed, 1,2,3,4,5,... */
	if (cb->check_args && log_msg_is_std(msg) && nargs > 0) {
		for (int i = 0; i < nargs; i++) {
			u32_t arg = log_msg_arg_get(msg, i);

			zassert_equal(i+1, arg,
				      "Unexpected argument in the message");
		}
	}

	if (cb->check_strdup) {
		zassert_false(cb->exp_strdup[cb->counter]
			      ^ log_is_strdup((void *)log_msg_arg_get(msg, 0)),
			      NULL);
	}

	if (cb->callback) {
		cb->callback(backend, msg, cb->counter);
	}

	cb->counter++;

	if (!cb->keep_msgs) {
		log_msg_put(msg);
	}
}

static void panic(struct log_backend const *const backend)
{
	struct backend_cb *cb = (struct backend_cb *)backend->cb->ctx;

	cb->panic = true;
}

static void dropped(struct log_backend const *const backend, u32_t cnt)
{
	struct backend_cb *cb = (struct backend_cb *)backend->cb->ctx;

	cb->total_drops += cnt;
}

const struct log_backend_api log_backend_test_api = {
	.put = put,
	.panic = panic,
	.dropped = dropped,
};

LOG_BACKEND_DEFINE(backend1, log_backend_test_api, false);
struct backend_cb backend1_cb;

LOG_BACKEND_DEFINE(backend2, log_backend_test_api, false);
struct backend_cb backend2_cb;

static u32_t stamp;

static u32_t test_source_id;

static u32_t timestamp_get(void)
{
	return stamp++;
}

/**
 * @brief Function for finding source ID based on source name.
 *
 * @param name Source name
 *
 * @return Source ID.
 */
static int log_source_id_get(const char *name)
{

	for (u16_t i = 0; i < log_sources_count(CONFIG_LOG_DOMAIN_ID); i++) {
		const char *source_name = log_source_name_get(NULL, 0,
					CONFIG_LOG_DOMAIN_ID, i);
		if (strcmp(source_name, name) == 0) {
			return i;
		}
	}
	return -1;
}

static void log_setup(bool backend2_enable)
{
	stamp = 0U;
	zassert_false(in_panic, "Logger in panic state.");

	log_init();

	zassert_equal(0, log_set_timestamp_func(timestamp_get, 0),
		      "Expects successful timestamp function setting.");

	memset(&backend1_cb, 0, sizeof(backend1_cb));

	log_backend_enable(&backend1, &backend1_cb, LOG_LEVEL_DBG);

	if (backend2_enable) {
		memset(&backend2_cb, 0, sizeof(backend2_cb));

		log_backend_enable(&backend2, &backend2_cb, LOG_LEVEL_INF);
	} else {
		log_backend_disable(&backend2);
	}

	test_source_id = log_source_id_get(STRINGIFY(LOG_MODULE_NAME));
}


struct mock_log_link_source domain1_sources[] = {
	{ .source = "abc", .clevel = 4, .rlevel = 4},
	{ .source = "xxx", .clevel = 3, .rlevel = 3},
	{ .source = "yyy", .clevel = 2, .rlevel = 2},
	{ .source = "zzz", .clevel = 4, .rlevel = 1},
};

struct mock_log_link_source domain2_sources[] = {
	{ .source = "abc2", .clevel = 2, .rlevel = 1},
	{ .source = "xxx2", .clevel = 2, .rlevel = 2},
	{ .source = "yyy2", .clevel = 3, .rlevel = 3},
	{ .source = "zzz2", .clevel = 4, .rlevel = 4},
};

struct mock_log_link_source domain3_sources[] = {
	{ .source = "abc", .clevel = 4, .rlevel = 4},
	{ .source = "xxx", .clevel = 3, .rlevel = 3},
	{ .source = "yyy", .clevel = 2, .rlevel = 2},
	{ .source = "zzz", .clevel = 2, .rlevel = 1},
};

struct mock_log_link_domain domain1 = {
	.source_cnt = ARRAY_SIZE(domain1_sources),
	.sources = domain1_sources,
	.name = "domain1"
};

struct mock_log_link_domain domain2 = {
	.source_cnt = ARRAY_SIZE(domain2_sources),
	.sources = domain2_sources,
	.name = "domain2"
};

struct mock_log_link_domain domain3 = {
	.source_cnt = ARRAY_SIZE(domain3_sources),
	.sources = domain3_sources,
	.name = "domain3"
};

struct mock_log_link_domain *domains_a[] = {&domain1, &domain2};
struct mock_log_link mock_link_a = {
	.domain_cnt = ARRAY_SIZE(domains_a),
	.domains = domains_a
};

struct mock_log_link_domain *domains_b[] = {&domain3};
struct mock_log_link mock_link_b = {
	.domain_cnt = ARRAY_SIZE(domains_b),
	.domains = domains_b
};

extern struct log_link_api mock_log_link_api;

LOG_LINK_DEF(mock_link1, mock_log_link_api, &mock_link_a);
LOG_LINK_DEF(mock_link2, mock_log_link_api, &mock_link_b);

static void test_log_domain_count(void)
{
	u8_t domains_cnt;
	u8_t exp_domains_cnt;

	log_setup(false);

	exp_domains_cnt = 1 + mock_link_a.domain_cnt + mock_link_b.domain_cnt;
	domains_cnt = log_domains_count();
	zassert_equal(domains_cnt, exp_domains_cnt,
			"Unexpected number of domains (%d)", domains_cnt);
}

static void test_log_source_count(void)
{
	u8_t exp_source_cnt[] = {
		log_const_source_id(__log_const_end),
		/*link1*/
		domains_a[0]->source_cnt,
		domains_a[1]->source_cnt,
		domains_b[0]->source_cnt,
	};

	log_setup(false);


	for (u8_t d = 0; d < log_domains_count(); d++) {
		u16_t source_cnt = log_sources_count(d);

		zassert_equal(source_cnt, exp_source_cnt[d],
			      "Unexpected source count (%d:%d)", d, source_cnt);
	}
}

static void test_single_compile_level(u8_t d, u16_t s, u32_t exp_level)
{
	u32_t level;

	level = log_compiled_level_get(d, s);
	zassert_equal(level, exp_level,
			"%d:%d Unexpected compiled level (%d vs %d)",
			d, s, level, exp_level);
	level = log_filter_get(NULL, d, s, false);
	zassert_equal(level, exp_level,
			"%d:%d Unexpected compiled level (%d vs %d)",
			d, s, level, exp_level);
}

static void test_log_compiled_level_get(void)
{
	test_single_compile_level(1, 0, domains_a[0]->sources[0].clevel);
	test_single_compile_level(1, 1, domains_a[0]->sources[1].clevel);
	test_single_compile_level(1, 3, domains_a[0]->sources[3].clevel);
	test_single_compile_level(2, 2, domains_a[1]->sources[2].clevel);
	test_single_compile_level(3, 2, domains_b[0]->sources[2].clevel);
}

static void test_single_runtime_level(u8_t d, u16_t s, u8_t *link_level)
{
	u32_t level1;
	u32_t level2;

	level1 = log_filter_get(&backend1, d, s, true);
	level2 = log_filter_get(&backend2, d, s, true);
	zassert_equal(level1, *link_level,
			"%d:%d Unexpected compiled level (%d vs %d)",
			d, s, level1, *link_level);
	zassert_equal(level2, MIN(*link_level, LOG_LEVEL_INF),
			"%d:%d Unexpected compiled level (%d vs %d)",
			d, s, level2, MIN(*link_level, LOG_LEVEL_INF));

	log_filter_set(&backend1, d, s, 1);
	log_filter_set(&backend2, d, s, 2);
	level1 = log_filter_get(&backend1, d, s, true);
	level2 = log_filter_get(&backend2, d, s, true);
	zassert_equal(level1, 1, "%d:%d Unexpected compiled level (%d vs %d)",
			d, s, level1, 1);
	zassert_equal(level2, 2, "%d:%d Unexpected compiled level (%d vs %d)",
			d, s, level2, 2);

	/* level set in link should be the max of both level set */
	zassert_equal(*link_level, 2,
			"%d:%d Unexpected compiled level (%d vs %d)",
			d, s, *link_level, 2);


}

static void test_log_runtime_level_set(void)
{
	log_setup(true);

	test_single_runtime_level(1, 0, &domains_a[0]->sources[0].rlevel);
	test_single_runtime_level(1, 3, &domains_a[0]->sources[3].rlevel);
	test_single_runtime_level(2, 1, &domains_a[1]->sources[1].rlevel);
	test_single_runtime_level(3, 1, &domains_b[0]->sources[1].rlevel);
}

static void test_log_domain_name_get(void)
{
	zassert_equal(strcmp(log_domain_name_get(0), ""), 0,
			"Unexpected domain name");
	zassert_equal(strcmp(log_domain_name_get(1), "domain1"), 0,
			"Unexpected domain name");
	zassert_equal(strcmp(log_domain_name_get(2), "domain2"), 0,
			"Unexpected domain name");
	zassert_equal(strcmp(log_domain_name_get(3), "domain3"), 0,
			"Unexpected domain name");
}

static void test_single_log_source_name_get(u8_t d, u16_t s,
						const char *exp_name)
{
	char buf[96];

	const char *name = log_source_name_get(buf, sizeof(buf), d, s);
	zassert_equal(strcmp(name, exp_name), 0, "Unexpected source name");
}

static void test_log_source_name_get(void)
{
	test_single_log_source_name_get(1,0, domains_a[0]->sources[0].source);
	test_single_log_source_name_get(1,1, domains_a[0]->sources[1].source);
	test_single_log_source_name_get(2,2, domains_a[1]->sources[2].source);
	test_single_log_source_name_get(3,3, domains_b[0]->sources[3].source);
}

/*test case main entry*/
void test_main(void)
{
	ztest_test_suite(test_log_multidomain,
			 ztest_unit_test(test_log_source_name_get),
			 ztest_unit_test(test_log_domain_name_get),
			 ztest_unit_test(test_log_runtime_level_set),
			 ztest_unit_test(test_log_compiled_level_get),
			 ztest_unit_test(test_log_domain_count),
			 ztest_unit_test(test_log_source_count)
			 );
	ztest_run_test_suite(test_log_multidomain);
}
