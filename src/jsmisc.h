/* SPDX-License-Identifier: MIT */

#ifndef _JSMISC_H
#define _JSMISC_H

#include <stdarg.h>
#include <string.h>
#include <duktape.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logging priorities. These correspond 1:1 to the syslog priorities
 * (see /usr/include/sys/syslog.h), but are redefined to avoid an
 * explicit dependency against syslog.
 */
#define	JS_LOG_EMERG	0	/* system is unusable */
#define	JS_LOG_ALERT	1	/* action must be taken immediately */
#define	JS_LOG_CRIT	2	/* critical conditions */
#define	JS_LOG_ERR	3	/* error conditions */
#define	JS_LOG_WARNING	4	/* warning conditions */
#define	JS_LOG_NOTICE	5	/* normal but significant condition */
#define	JS_LOG_INFO	6	/* informational */
#define	JS_LOG_DEBUG	7	/* debug-level messages */

typedef void (*js_log_callback_t)(int priority, const char *format,
		va_list ap);

#ifdef JS_DEBUG
#define JS_NATIVE
#define js_log(priority, format, ...)					\
	js_log_impl(priority, "[%s %s:%d] " format, __func__, __FILE__,	\
			__LINE__, ##__VA_ARGS__)
#else
#define JS_NATIVE static
#define js_log(priority, format, ...) do {				\
	if (priority < JS_LOG_DEBUG)					\
		js_log_impl(priority, "[%s] " format,			\
				__func__, ##__VA_ARGS__);		\
} while (0)
#endif

/*
 * This is defined as a macro because duk_push_error_object() is also
 * defined as a macro and uses __FILE__ and __LINE__ to prepend the
 * filename and line to the error string, in a similar way to what we
 * do for js_log(). Unfortunately, there is no clean way to prevent
 * that and add the filename/line/function externally in a consistent
 * way to js_log(). Internally, it is supported by Duktape through the
 * DUK_AUGMENT_FLAG_NOBLAME_FILELINE flag, which can be or'ed into the
 * err_code parameter, but the flag definition is not publicly exposed.
 */
#define js_report_error(ctx, fmt, ...) ({				\
	duk_push_error_object(ctx, DUK_ERR_ERROR, fmt, ##__VA_ARGS__);	\
	duk_throw(ctx);							\
})

#define js_report_errno(ctx, errnum)					\
	js_report_error(ctx, "%s", strerror(errnum))

#define js_ret_error(ctx, format, ...) ({				\
	js_report_error(ctx, format, ##__VA_ARGS__);			\
})

#define js_ret_errno(ctx, errnum) ({					\
	js_report_errno(ctx, errnum);					\
})

void js_log_impl(int priority, const char *format, ...);
void js_log_set_callback(js_log_callback_t callback);

/**
 * @brief Append an element at the end of an array
 */
duk_bool_t js_append_array_element(duk_context *ctx, duk_idx_t obj_idx);

duk_bool_t js_misc_init(duk_context *ctx, duk_idx_t obj_idx);
void js_log_error(duk_context *ctx, duk_idx_t obj_idx);
char *js_inspect(duk_context *ctx, duk_idx_t idx);
void js_dump(duk_context *ctx, duk_idx_t idx);

#ifdef __cplusplus
}
#endif

#endif
