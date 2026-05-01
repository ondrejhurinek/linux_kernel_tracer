#ifndef TP_LOOKUP_H
#define TP_LOOKUP_H

#include <linux/tracepoint.h>

struct tracepoint *find_tracepoint_by_name(const char *name);

#endif /* TP_LOOKUP_H */
