/*
 * Copyright (c) 2022 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mock_backend.h"
#include <ztest.h>
#include <logging/log_ctrl.h>

struct mock_log_frontend {
	bool do_check;
	bool panic;
	struct mock_log_backend_msg exp_msgs[64];
	int msg_rec_idx;
	int msg_proc_idx;
};

static struct mock_log_backend mock;
static struct log_backend_control_block cb = {
	.ctx = &mock
};

static const struct log_backend backend = {
	.cb = &cb
};

void mock_log_frontend_dummy_record(int cnt)
{
	mock_log_backend_dummy_record(&backend, cnt);
}

void mock_log_frontend_generic_record(uint16_t source_id,
				      uint16_t domain_id,
				      uint8_t level,
				      const char *str,
				      uint8_t *data,
				      uint32_t data_len)
{
	if (!IS_ENABLED(CONFIG_LOG_FRONTEND)) {
		return;
	}

	mock_log_backend_generic_record(&backend, source_id, domain_id, level,
					(log_timestamp_t)UINT32_MAX, str, data, data_len);
}

void mock_log_frontend_validate(bool panic)
{
	if (!IS_ENABLED(CONFIG_LOG_FRONTEND)) {
		return;
	}
	mock_log_backend_validate(&backend, panic);
}

void mock_log_frontend_reset(void)
{
	mock_log_backend_reset(&backend);
}

struct test_str {
	char *str;
	int cnt;
};

static int out(int c, void *ctx)
{
	struct test_str *s = ctx;

	s->str[s->cnt++] = (char)c;

	return c;
}

void log_frontend_msg(const void *source,
		      const struct log_msg2_desc desc,
		      uint8_t *package, const void *data)
{
	struct mock_log_backend_msg *exp = &mock.exp_msgs[mock.msg_proc_idx];

	mock.msg_proc_idx++;

	if (!exp->check) {
		return;
	}

	zassert_equal(desc.level, exp->level, NULL);
	zassert_equal(desc.domain, exp->domain_id, NULL);

	uint32_t source_id = IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ?
		log_dynamic_source_id((struct log_source_dynamic_data *)source) :
		log_const_source_id((const struct log_source_const_data *)source);

	zassert_equal(source_id, exp->source_id, NULL);

	zassert_equal(exp->data_len, desc.data_len, NULL);
	if (exp->data_len <= sizeof(exp->data)) {
		zassert_equal(memcmp(data, exp->data, desc.data_len), 0, NULL);
	}

	char str[128];
	struct test_str s = { .str = str };

	size_t len = cbpprintf(out, &s, package);
	if (len > 0) {
		str[len] = '\0';
	}

	zassert_equal(strcmp(str, exp->str), 0, "Got \"%s\", Expected:\"%s\"",
			str, exp->str);
}

void log_frontend_panic(void)
{
	mock.panic = true;
}

void log_frontend_init(void)
{

}
