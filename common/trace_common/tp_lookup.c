#include <linux/string.h>
#include <linux/tracepoint.h>
#include "tp_lookup.h"

/* Helper structure for the lookup macro to find tracepoint */
struct tp_lookup_ctx {
	const char *name;
	struct tracepoint *found;
};

/* Callback for for_each_kernel_tracepoint() */
static void tp_lookup_callback(struct tracepoint *tp, void *priv)
{
	struct tp_lookup_ctx *ctx = priv;

	if (ctx->found)
		return;

	if (tp && tp->name && strcmp(tp->name, ctx->name) == 0)
		ctx->found = tp;
}

/* Find a kernel tracepoint by name.
 *
 * par name -> pointer to the char[] name
 * ret -> Pointer to the tracepoint if found, or NULL otherwise */
struct tracepoint *find_tracepoint_by_name(const char *name)
{
	struct tp_lookup_ctx ctx = {
		.name = name,
		.found = NULL,
	};

	for_each_kernel_tracepoint(tp_lookup_callback, &ctx);
	return ctx.found;
}

MODULE_LICENSE("GPL");
