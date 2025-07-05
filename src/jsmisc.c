/* SPDX-License-Identifier: MIT */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/time.h>

#ifdef _GNU_SOURCE
#include <dlfcn.h>
#endif

#include "jsmisc.h"

static const char * const log_prio_map[] = {
	[LOG_EMERG]	= "emergency",
	[LOG_ALERT]	= "alert",
	[LOG_CRIT]	= "critical",
	[LOG_ERR]	= "error",
	[LOG_WARNING]	= "warning",
	[LOG_NOTICE]	= "notice",
	[LOG_INFO]	= "info",
	[LOG_DEBUG]	= "debug"
};

struct str {
	char *buf;
	size_t strlen;
	size_t bufsize;
};

#define STR_CHUNK 4096
#define STR_INIT {NULL, 0, 0}

/**
 * @return 0 on success, POSIX error code on error
 */
static int str_expand(struct str *str, size_t len)
{
	char *newbuf;
	size_t newsize;

	if (str->strlen + len <= str->bufsize)
		return 0;

	newsize = ((str->strlen + len + STR_CHUNK - 1) / STR_CHUNK) *
		STR_CHUNK;
	newbuf = realloc(str->buf, newsize);
	if (!newbuf)
		return ENOMEM;

	str->buf = newbuf;
	str->bufsize = newsize;

	return 0;
}

/**
 * @return 0 on success, POSIX error code on error
 */
static int str_printf(struct str *str, const char *format, ...)
{
	char buf[1024];
	va_list ap;
	int len, ret;

	va_start(ap, format);
	len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	if (len < 0 || len >= sizeof(buf))
		return EINVAL;

	if ((ret = str_expand(str, len + 1)))
		return ret;

	memcpy(str->buf + str->strlen, buf, len);
	str->strlen += len;
	str->buf[str->strlen] = '\0';

	return 0;
}

/**
 * @return 0 on success, POSIX error code on error
 */
static int str_put_indent(struct str *str, int indent)
{
	int i, ret;

	for (i = 0; i < indent; i++) {
		if ((ret = str_printf(str, "%s", "    ")))
			return ret;
	}

	return 0;
}

static void js_log_default_callback(int priority, const char *format,
		va_list ap)
{
	const char *prio_txt = "<default>";

	if (priority >= LOG_EMERG && priority <= LOG_DEBUG)
		prio_txt = log_prio_map[priority];

	fprintf(stderr, "[%s] ", prio_txt);
	vfprintf(stderr, format, ap);
}

static js_log_callback_t js_log_callback = js_log_default_callback;

void js_log_impl(int priority, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	if (js_log_callback)
		js_log_callback(priority, format, ap);
	va_end(ap);
}

void js_log_set_callback(js_log_callback_t callback)
{
	js_log_callback = callback;
}

duk_bool_t js_append_array_element(duk_context *ctx, duk_idx_t obj_idx)
{
	if (!duk_is_array(ctx, obj_idx))
		return 0;

	duk_put_prop_index(ctx, obj_idx, duk_get_length(ctx, obj_idx));
	return 1;
}

duk_bool_t js_create_array_map(duk_context *ctx, const js_array_map_element_t *e)
{
	duk_push_array(ctx);

	while (e && e->val) {
		duk_push_string(ctx, e->val);
		duk_put_prop_index(ctx, -2, e++->idx);
	}

	return 1;
}

static int js_sys_print(duk_context *ctx)
{
	int i, argc = duk_get_top(ctx);

	for (i = 0; i < argc; i++)
		fputs(duk_safe_to_string(ctx, i), stdout);

	return 0;
}

static int js_sys_println(duk_context *ctx)
{
	js_sys_print(ctx);
	putc('\n', stdout);

	return 0;
}

static int js_inspect_recursive(duk_context *ctx, duk_idx_t idx, struct str *str,
		unsigned int indent)
{
	int ret;

	switch (duk_get_type(ctx, idx)) {
	case DUK_TYPE_UNDEFINED:
		ret = str_printf(str, "undefined");
		break;
	case DUK_TYPE_NULL:
		ret = str_printf(str, "null");
		break;
	case DUK_TYPE_BOOLEAN:
		ret = str_printf(str, "Boolean(%s)",
				duk_get_boolean(ctx, idx) ? "true" : "false");
		break;
	case DUK_TYPE_NUMBER:
		ret = str_printf(str, "Number(%lf)",
				(double)duk_get_number(ctx, idx));
		break;
	case DUK_TYPE_STRING:
		/* FIXME Use duk_get_lstring and convert 0-bytes to "\0" */
		ret = str_printf(str, "String(%s)", duk_get_string(ctx, idx));
		break;
	case DUK_TYPE_OBJECT:
		if (duk_is_array(ctx, idx)) {
			duk_size_t aidx, alen = duk_get_length(ctx, idx);
			duk_idx_t tidx;

			str_printf(str, "Array(%ld) [", (long)alen);
			for (aidx = 0; aidx < alen; aidx++) {
				duk_get_prop_index(ctx, idx, aidx);
				str_printf(str, "\n");
				str_put_indent(str, indent + 1);
				str_printf(str, "[%ld]: ", (long)aidx);
				tidx = duk_get_top_index(ctx);
				js_inspect_recursive(ctx, tidx, str, indent + 1);
				duk_pop(ctx); /* property value */
			}
			str_printf(str, "\n");
			str_put_indent(str, indent);
			ret = str_printf(str, "]");
			break;
		}
		if (duk_is_c_function(ctx, idx)) {
			str_printf(str, "NativeFn");
			break;
		}
		if (duk_is_function(ctx, idx)) {
			str_printf(str, "Function");
			break;
		}
		str_printf(str, "Object {");
		duk_enum(ctx, idx, DUK_ENUM_OWN_PROPERTIES_ONLY);
		while (duk_next(ctx, -1, 1)) {
			duk_idx_t tidx = duk_get_top_index(ctx);

			str_printf(str, "\n");
			str_put_indent(str, indent + 1);
			str_printf(str, "%s: ", duk_get_string(ctx, -2));
			js_inspect_recursive(ctx, tidx, str, indent + 1);
			duk_pop_2(ctx); /* key and value */
		}
		duk_pop(ctx); /* enum object */
		str_printf(str, "\n");
		str_put_indent(str, indent);
		ret = str_printf(str, "}");
		break;
	default:
		ret = str_printf(str, "<unknown>");
		break;
	}

	return ret;
}

static int js_inspect_root(duk_context *ctx, struct str *str)
{
	duk_idx_t idx, argc = duk_get_top(ctx);
	int ret = 0;

	for (idx = 0; idx < argc; idx++) {
		if ((ret = str_printf(str, "$%u = ", idx)))
			break;
		if (js_inspect_recursive(ctx, idx, str, 0))
			break;
		if ((ret = str_printf(str, "\n")))
			break;
	}

	return ret;
}

static int js_sys_inspect(duk_context *ctx)
{
	struct str c_str = STR_INIT;

	if (js_inspect_root(ctx, &c_str))
		duk_push_null(ctx);
	else
		duk_push_lstring(ctx, c_str.buf, c_str.strlen);

	free(c_str.buf);

	return 1;
}

char *js_inspect(duk_context *ctx, duk_idx_t idx)
{
	struct str c_str = STR_INIT;

	js_inspect_recursive(ctx, idx, &c_str, 0);

	return c_str.buf;
}

void js_dump(duk_context *ctx, duk_idx_t idx)
{
	char *str = js_inspect(ctx, idx);

	puts(str);
	free(str);
}

static int js_sys_dump(duk_context *ctx)
{
	struct str c_str = STR_INIT;

	if (!js_inspect_root(ctx, &c_str))
		fputs(c_str.buf, stdout);

	free(c_str.buf);

	return 0;
}

static int js_sys_openlog(duk_context *ctx)
{
	int argc = duk_get_top(ctx);
	const char *ident = "jsmisc";
	int facility = LOG_USER;

	if (argc >= 1)
		ident = duk_safe_to_string(ctx, 0);

	if (argc >= 2)
		facility = duk_to_int(ctx, 1);

	openlog(ident, LOG_PID, facility);
	js_log_set_callback(vsyslog);

	return 0;
}

static int js_sys_log(duk_context *ctx)
{
	long lineno = 0;
	const char *filename = "<unknown>";

	duk_inspect_callstack_entry(ctx, -2);
	// FIXME: Duktape has no script name information ??
#if 0
	duk_get_prop_string(ctx, -1, "fileName");
	filename = duk_to_string(ctx, -1);
	duk_pop(ctx);
#endif
	duk_get_prop_string(ctx, -1, "lineNumber");
	lineno = duk_to_int(ctx, -1);
	duk_pop_2(ctx);

	js_log_impl(duk_to_int(ctx, 0), "[%s:%u] %s\n",
		filename, lineno,
		duk_safe_to_string(ctx, 1));

	return 0;
}

static duk_function_list_entry js_sys_functions[] = {
	{"print",	js_sys_print,	DUK_VARARGS},
	{"println",	js_sys_println,	DUK_VARARGS},
	{"inspect",	js_sys_inspect,	DUK_VARARGS},
	{"dump",	js_sys_dump,	DUK_VARARGS},
	{"openlog",	js_sys_openlog,	DUK_VARARGS},
	{"log",		js_sys_log,	2},
	{NULL,		NULL,		0}
};

static const duk_number_list_entry js_sys_props[] = {
	/* syslog priorities */
	{"LOG_EMERG",	 LOG_EMERG},
	{"LOG_ALERT",	 LOG_ALERT},
	{"LOG_CRIT",	 LOG_CRIT},
	{"LOG_ERR",	 LOG_ERR},
	{"LOG_WARNING",	 LOG_WARNING},
	{"LOG_NOTICE",	 LOG_NOTICE},
	{"LOG_INFO",	 LOG_INFO},
	{"LOG_DEBUG",	 LOG_DEBUG},
	{"LOG_PRIMASK",	 LOG_PRIMASK},
	/* syslog facilities */
	{"LOG_KERN",	 LOG_KERN},
	{"LOG_USER",	 LOG_USER},
	{"LOG_MAIL",	 LOG_MAIL},
	{"LOG_DAEMON",	 LOG_DAEMON},
	{"LOG_AUTH",	 LOG_AUTH},
	{"LOG_SYSLOG",	 LOG_SYSLOG},
	{"LOG_LPR",	 LOG_LPR},
	{"LOG_NEWS",	 LOG_NEWS},
	{"LOG_UUCP",	 LOG_UUCP},
	{"LOG_CRON",	 LOG_CRON},
	{"LOG_AUTHPRIV", LOG_AUTHPRIV},
	{"LOG_FTP",	 LOG_FTP},
	{"LOG_LOCAL0",	 LOG_LOCAL0},
	{"LOG_LOCAL1",	 LOG_LOCAL1},
	{"LOG_LOCAL2",	 LOG_LOCAL2},
	{"LOG_LOCAL3",	 LOG_LOCAL3},
	{"LOG_LOCAL4",	 LOG_LOCAL4},
	{"LOG_LOCAL5",	 LOG_LOCAL5},
	{"LOG_LOCAL6",	 LOG_LOCAL6},
	{"LOG_LOCAL7",	 LOG_LOCAL7},
	{"LOG_FACMASK",	 LOG_FACMASK},
	/* sentinel */
	{NULL,		0.0}
};

duk_bool_t js_misc_init(duk_context *ctx, duk_idx_t obj_idx)
{
	js_log(LOG_INFO, "%s\n", VERSION_STR);

	duk_put_number_list(ctx, obj_idx, js_sys_props);
	duk_put_function_list(ctx, obj_idx, js_sys_functions);

	return 1;
}

void js_log_error(duk_context *ctx, duk_idx_t obj_idx)
{
	const char *name = NULL;
	const char *message = NULL;
	const char *file = NULL;
	int line = 0;

	if (!duk_is_object(ctx, obj_idx)) {
		js_log(LOG_ERR, "value is not an object\n");
		return;
	}

	if (duk_get_prop_string(ctx, obj_idx, "name"))
		name = duk_safe_to_string(ctx, -1);
	duk_pop(ctx);

	if (duk_get_prop_string(ctx, obj_idx, "message"))
		message = duk_safe_to_string(ctx, -1);
	duk_pop(ctx);

	if (duk_get_prop_string(ctx, obj_idx, "fileName"))
		file = duk_safe_to_string(ctx, -1);
	duk_pop(ctx);

	if (duk_get_prop_string(ctx, obj_idx, "lineNumber"))
		line = duk_to_int(ctx, -1);
	duk_pop(ctx);

	js_log_impl(LOG_ERR, "[%s:%d] %s: %s\n", file, line, name, message);
}
