#ifndef SAFE_HEADER_H
#define SAFE_HEADER_H

// 强制先包含PostgreSQL头文件，然后处理libintl.h冲突
extern "C" {
#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/lsyscache.h"
#include "utils/elog.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "access/htup_details.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "postmaster/bgworker.h"
#include "catalog/namespace.h"
#include "parser/parse_type.h"
#include "utils/typcache.h"
#include "utils/errcodes.h"
#include "nodes/makefuncs.h"
#include "parser/parser.h"
#include "storage/large_object.h"
#include "libpq/be-fsstubs.h"
#include "libpq/libpq-fs.h"
#include "storage/fd.h"
}

// 在包含可能导致libintl.h包含的头文件之前，处理宏冲突
#ifdef gettext
#undef gettext
#undef dgettext  
#undef ngettext
#undef dngettext
#endif

// 为了避免后续问题，我们可以重新定义PostgreSQL的gettext宏
// 但只在确实需要时才定义
// #ifndef gettext
// #define gettext(x) (x)
// #define dgettext(d,x) (x)
// #define ngettext(s,p,n) ((n) == 1 ? (s) : (p))
// #define dngettext(d,s,p,n) ((n) == 1 ? (s) : (p))
// #endif

#endif // SAFE_HEADER_H 