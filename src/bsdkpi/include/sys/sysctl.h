/* SPDX-License-Identifier: MIT */
/*
 * sys/sysctl.h - sysctl(9).
 *
 * There is no sysctl(2) on macOS for a kext to hook into, but net80211's
 * tunables are worth keeping reachable from a debug user client, so this is
 * a real (small) implementation: static SYSCTL_* declarations register
 * themselves with a constructor, SYSCTL_ADD_* allocate dynamic nodes in a
 * sysctl_ctx_list, and bsdkpi_sysctl_find()/bsdkpi_sysctl_invoke() walk the
 * tree and run the handlers (src/sysctl.c).
 */
#ifndef _BSDKPI_SYS_SYSCTL_H_
#define _BSDKPI_SYS_SYSCTL_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <string.h>

#define CTL_MAXNAME	24

#define CTLTYPE		0xf
#define CTLTYPE_NODE	1
#define CTLTYPE_INT	2
#define CTLTYPE_STRING	3
#define CTLTYPE_S64	4
#define CTLTYPE_OPAQUE	5
#define CTLTYPE_STRUCT	CTLTYPE_OPAQUE
#define CTLTYPE_UINT	6
#define CTLTYPE_LONG	7
#define CTLTYPE_ULONG	8
#define CTLTYPE_U64	9
#define CTLTYPE_U8	0xa
#define CTLTYPE_U16	0xb
#define CTLTYPE_S8	0xc
#define CTLTYPE_S16	0xd
#define CTLTYPE_S32	0xe
#define CTLTYPE_U32	0xf

#define CTLFLAG_RD	0x80000000
#define CTLFLAG_WR	0x40000000
#define CTLFLAG_RW	(CTLFLAG_RD | CTLFLAG_WR)
#define CTLFLAG_DORMANT	0x20000000
#define CTLFLAG_ANYBODY	0x10000000
#define CTLFLAG_SECURE	0x08000000
#define CTLFLAG_PRISON	0x04000000
#define CTLFLAG_DYN	0x02000000
#define CTLFLAG_SKIP	0x01000000
#define CTLMASK_SECURE	0x00F00000
#define CTLFLAG_TUN	0x00080000
#define CTLFLAG_RDTUN	(CTLFLAG_RD | CTLFLAG_TUN)
#define CTLFLAG_RWTUN	(CTLFLAG_RW | CTLFLAG_TUN)
#define CTLFLAG_MPSAFE	0x00040000
#define CTLFLAG_VNET	0x00020000
#define CTLFLAG_DYING	0x00010000
#define CTLFLAG_CAPRD	0x00008000
#define CTLFLAG_CAPWR	0x00004000
#define CTLFLAG_STATS	0x00002000
#define CTLFLAG_NOFETCH	0x00001000
#define CTLFLAG_CAPRW	(CTLFLAG_CAPRD | CTLFLAG_CAPWR)
#define CTLFLAG_NEEDGIANT 0x00000800
#define CTLSHIFT_SECURE	20
#define CTLFLAG_SECURE1	(CTLFLAG_SECURE | (0 << CTLSHIFT_SECURE))
#define CTLFLAG_SECURE2	(CTLFLAG_SECURE | (1 << CTLSHIFT_SECURE))
#define CTLFLAG_SECURE3	(CTLFLAG_SECURE | (2 << CTLSHIFT_SECURE))

#define OID_AUTO	(-1)
#define CTL_AUTO_START	0x100

struct sysctl_req {
	struct thread	*td;
	int		 lock;
	void		*oldptr;
	size_t		 oldlen;
	size_t		 oldidx;
	int		(*oldfunc)(struct sysctl_req *, const void *, size_t);
	const void	*newptr;
	size_t		 newlen;
	size_t		 newidx;
	int		(*newfunc)(struct sysctl_req *, void *, size_t);
	size_t		 validlen;
	int		 flags;
};

#define SYSCTL_HANDLER_ARGS	struct sysctl_oid *oidp, void *arg1,	\
	intmax_t arg2, struct sysctl_req *req

SLIST_HEAD(sysctl_oid_list, sysctl_oid);

struct sysctl_oid {
	struct sysctl_oid_list	 oid_children;
	struct sysctl_oid_list	*oid_parent;
	SLIST_ENTRY(sysctl_oid)	 oid_link;
	int			 oid_number;
	u_int			 oid_kind;
	void			*oid_arg1;
	intmax_t		 oid_arg2;
	const char		*oid_name;
	int			(*oid_handler)(SYSCTL_HANDLER_ARGS);
	const char		*oid_fmt;
	int			 oid_refcnt;
	u_int			 oid_running;
	const char		*oid_descr;
	const char		*oid_label;
};

#define SYSCTL_IN(r, p, l)	(r->newfunc)(r, p, l)
#define SYSCTL_OUT(r, p, l)	(r->oldfunc)(r, p, l)
#define SYSCTL_OUT_STR(r, p)	(r->oldfunc)(r, p, strlen(p) + 1)

int	sysctl_handle_bool(SYSCTL_HANDLER_ARGS);
int	sysctl_handle_8(SYSCTL_HANDLER_ARGS);
int	sysctl_handle_16(SYSCTL_HANDLER_ARGS);
int	sysctl_handle_32(SYSCTL_HANDLER_ARGS);
int	sysctl_handle_64(SYSCTL_HANDLER_ARGS);
int	sysctl_handle_int(SYSCTL_HANDLER_ARGS);
int	sysctl_handle_long(SYSCTL_HANDLER_ARGS);
int	sysctl_handle_string(SYSCTL_HANDLER_ARGS);
int	sysctl_handle_opaque(SYSCTL_HANDLER_ARGS);
int	sysctl_wire_old_buffer(struct sysctl_req *req, size_t len);

struct sysctl_ctx_entry {
	struct sysctl_oid		*entry;
	TAILQ_ENTRY(sysctl_ctx_entry)	 link;
};
TAILQ_HEAD(sysctl_ctx_list, sysctl_ctx_entry);

void	sysctl_ctx_init(struct sysctl_ctx_list *clist);
int	sysctl_ctx_free(struct sysctl_ctx_list *clist);
struct sysctl_oid *sysctl_add_oid(struct sysctl_ctx_list *clist,
	    struct sysctl_oid_list *parent, int nbr, const char *name, int kind,
	    void *arg1, intmax_t arg2, int (*handler)(SYSCTL_HANDLER_ARGS),
	    const char *fmt, const char *descr, const char *label);
void	sysctl_register_oid(struct sysctl_oid *oidp);
void	sysctl_unregister_oid(struct sysctl_oid *oidp);
int	sysctl_remove_oid(struct sysctl_oid *oidp, int del, int recurse);

/* Walk and invoke (for a debug user client / the host tests). */
struct sysctl_oid *bsdkpi_sysctl_find(const char *path);
int	bsdkpi_sysctl_invoke(struct sysctl_oid *oid, void *oldp, size_t *oldlenp,
	    const void *newp, size_t newlen);

#define SYSCTL_CHILDREN(oid_ptr)		(&(oid_ptr)->oid_children)
#define SYSCTL_PARENT(oid_ptr)			((oid_ptr)->oid_parent)
#define SYSCTL_STATIC_CHILDREN(oid_name)	(&sysctl__##oid_name.oid_children)
#define SYSCTL_NODE_CHILDREN(parent, name)	sysctl__##parent##_##name.oid_children
#define SYSCTL_DECL(name)			extern struct sysctl_oid sysctl__##name

/*
 * Static declarations: an __attribute__((used)) object plus a constructor
 * that links it under its parent.
 */
#define SYSCTL_OID_RAW(id, parent_child_head, nbr, name, kind, a1, a2,	\
    handler, fmt, descr, label)						\
	struct sysctl_oid id __attribute__((used)) = {			\
		.oid_children = SLIST_HEAD_INITIALIZER(id.oid_children),\
		.oid_parent = (parent_child_head),			\
		.oid_number = (nbr),					\
		.oid_kind = (kind),					\
		.oid_arg1 = (a1),					\
		.oid_arg2 = (a2),					\
		.oid_name = (name),					\
		.oid_handler = (handler),				\
		.oid_fmt = (fmt),					\
		.oid_descr = (descr),					\
		.oid_label = (label),					\
	};								\
	__attribute__((constructor)) static void			\
	id##_sysctl_register(void)					\
	{								\
		sysctl_register_oid(&id);				\
	}
#define SYSCTL_OID_WITH_LABEL(parent, nbr, name, kind, a1, a2, handler,	\
    fmt, descr, label)							\
	static SYSCTL_OID_RAW(sysctl__##parent##_##name,		\
	    SYSCTL_STATIC_CHILDREN(parent), nbr, #name, kind, a1, a2,	\
	    handler, fmt, descr, label)
#define SYSCTL_OID(parent, nbr, name, kind, a1, a2, handler, fmt, descr) \
	SYSCTL_OID_WITH_LABEL(parent, nbr, name, kind, a1, a2, handler, \
	    fmt, descr, NULL)
#define SYSCTL_NODE_WITH_LABEL(parent, nbr, name, access, handler, descr, label) \
	SYSCTL_OID_RAW(sysctl__##parent##_##name,			\
	    SYSCTL_STATIC_CHILDREN(parent), nbr, #name,			\
	    CTLTYPE_NODE | (access), NULL, 0, handler, "N", descr, label)
#define SYSCTL_NODE(parent, nbr, name, access, handler, descr)		\
	SYSCTL_NODE_WITH_LABEL(parent, nbr, name, access, handler, descr, NULL)
#define SYSCTL_ROOT_NODE(nbr, name, access, handler, descr)		\
	SYSCTL_OID_RAW(sysctl___##name, &sysctl__children, nbr, #name,	\
	    CTLTYPE_NODE | (access), NULL, 0, handler, "N", descr, NULL)
#define SYSCTL_STRING(parent, nbr, name, access, arg, len, descr)	\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_STRING | (access),	\
	    arg, len, sysctl_handle_string, "A", descr)
#define SYSCTL_BOOL(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_U8 | (access),		\
	    ptr, val, sysctl_handle_bool, "CU", descr)
#define SYSCTL_S8(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_S8 | (access),		\
	    ptr, val, sysctl_handle_8, "C", descr)
#define SYSCTL_U8(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_U8 | (access),		\
	    ptr, val, sysctl_handle_8, "CU", descr)
#define SYSCTL_S16(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_S16 | (access),		\
	    ptr, val, sysctl_handle_16, "S", descr)
#define SYSCTL_U16(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_U16 | (access),		\
	    ptr, val, sysctl_handle_16, "SU", descr)
#define SYSCTL_S32(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_S32 | (access),		\
	    ptr, val, sysctl_handle_32, "I", descr)
#define SYSCTL_U32(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_U32 | (access),		\
	    ptr, val, sysctl_handle_32, "IU", descr)
#define SYSCTL_INT(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_INT | (access),		\
	    ptr, val, sysctl_handle_int, "I", descr)
#define SYSCTL_UINT(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_UINT | (access),		\
	    ptr, val, sysctl_handle_int, "IU", descr)
#define SYSCTL_LONG(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_LONG | (access),		\
	    ptr, val, sysctl_handle_long, "L", descr)
#define SYSCTL_ULONG(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_ULONG | (access),		\
	    ptr, val, sysctl_handle_long, "LU", descr)
#define SYSCTL_S64(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_S64 | (access),		\
	    ptr, val, sysctl_handle_64, "Q", descr)
#define SYSCTL_U64(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_U64 | (access),		\
	    ptr, val, sysctl_handle_64, "QU", descr)
#define SYSCTL_QUAD(parent, nbr, name, access, ptr, val, descr)		\
	SYSCTL_S64(parent, nbr, name, access, ptr, val, descr)
#define SYSCTL_UQUAD(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_U64(parent, nbr, name, access, ptr, val, descr)
#define SYSCTL_COUNTER_U64(parent, nbr, name, access, ptr, descr)	\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_U64 | CTLFLAG_MPSAFE | (access), \
	    ptr, 0, sysctl_handle_counter_u64, "QU", descr)
#define SYSCTL_OPAQUE(parent, nbr, name, access, ptr, len, fmt, descr)	\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_OPAQUE | (access),	\
	    ptr, len, sysctl_handle_opaque, fmt, descr)
#define SYSCTL_STRUCT(parent, nbr, name, access, ptr, type, descr)	\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_OPAQUE | (access),	\
	    ptr, sizeof(struct type), sysctl_handle_opaque, "S," #type, descr)
#define SYSCTL_PROC(parent, nbr, name, access, ptr, arg, handler, fmt, descr) \
	SYSCTL_OID(parent, nbr, name, (access), ptr, arg, handler, fmt, descr)

/* Dynamic declarations. */
#define SYSCTL_ADD_OID(ctx, parent, nbr, name, kind, a1, a2, handler, fmt, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, kind, a1, a2, handler, fmt, descr, NULL)
#define SYSCTL_ADD_NODE_WITH_LABEL(ctx, parent, nbr, name, access, handler, descr, label) \
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_NODE | (access),	\
	    NULL, 0, handler, "N", descr, label)
#define SYSCTL_ADD_NODE(ctx, parent, nbr, name, access, handler, descr)	\
	SYSCTL_ADD_NODE_WITH_LABEL(ctx, parent, nbr, name, access, handler, descr, NULL)
#define SYSCTL_ADD_ROOT_NODE(ctx, nbr, name, access, handler, descr)	\
	sysctl_add_oid(ctx, &sysctl__children, nbr, name,		\
	    CTLTYPE_NODE | (access), NULL, 0, handler, "N", descr, NULL)
#define SYSCTL_ADD_STRING(ctx, parent, nbr, name, access, arg, len, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_STRING | (access), \
	    arg, len, sysctl_handle_string, "A", descr, NULL)
#define SYSCTL_ADD_BOOL(ctx, parent, nbr, name, access, ptr, val, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_U8 | (access),	\
	    ptr, val, sysctl_handle_bool, "CU", descr, NULL)
#define SYSCTL_ADD_S8(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_S8 | (access),	\
	    ptr, val, sysctl_handle_8, "C", descr, NULL)
#define SYSCTL_ADD_U8(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_U8 | (access),	\
	    ptr, val, sysctl_handle_8, "CU", descr, NULL)
#define SYSCTL_ADD_S16(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_S16 | (access),	\
	    ptr, val, sysctl_handle_16, "S", descr, NULL)
#define SYSCTL_ADD_U16(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_U16 | (access),	\
	    ptr, val, sysctl_handle_16, "SU", descr, NULL)
#define SYSCTL_ADD_S32(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_S32 | (access),	\
	    ptr, val, sysctl_handle_32, "I", descr, NULL)
#define SYSCTL_ADD_U32(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_U32 | (access),	\
	    ptr, val, sysctl_handle_32, "IU", descr, NULL)
#define SYSCTL_ADD_INT(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_INT | (access),	\
	    ptr, val, sysctl_handle_int, "I", descr, NULL)
#define SYSCTL_ADD_UINT(ctx, parent, nbr, name, access, ptr, val, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_UINT | (access),	\
	    ptr, val, sysctl_handle_int, "IU", descr, NULL)
#define SYSCTL_ADD_LONG(ctx, parent, nbr, name, access, ptr, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_LONG | (access),	\
	    ptr, 0, sysctl_handle_long, "L", descr, NULL)
#define SYSCTL_ADD_ULONG(ctx, parent, nbr, name, access, ptr, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_ULONG | (access), \
	    ptr, 0, sysctl_handle_long, "LU", descr, NULL)
#define SYSCTL_ADD_S64(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_S64 | (access),	\
	    ptr, val, sysctl_handle_64, "Q", descr, NULL)
#define SYSCTL_ADD_U64(ctx, parent, nbr, name, access, ptr, val, descr)	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_U64 | (access),	\
	    ptr, val, sysctl_handle_64, "QU", descr, NULL)
#define SYSCTL_ADD_QUAD(ctx, parent, nbr, name, access, ptr, descr)	\
	SYSCTL_ADD_S64(ctx, parent, nbr, name, access, ptr, 0, descr)
#define SYSCTL_ADD_UQUAD(ctx, parent, nbr, name, access, ptr, descr)	\
	SYSCTL_ADD_U64(ctx, parent, nbr, name, access, ptr, 0, descr)
#define SYSCTL_ADD_COUNTER_U64(ctx, parent, nbr, name, access, ptr, descr) \
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_U64 | CTLFLAG_MPSAFE | (access), ptr, 0,		\
	    sysctl_handle_counter_u64, "QU", descr, NULL)
#define SYSCTL_ADD_OPAQUE(ctx, parent, nbr, name, access, ptr, len, fmt, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_OPAQUE | (access), \
	    ptr, len, sysctl_handle_opaque, fmt, descr, NULL)
#define SYSCTL_ADD_STRUCT(ctx, parent, nbr, name, access, ptr, type, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_OPAQUE | (access), \
	    ptr, sizeof(struct type), sysctl_handle_opaque, "S," #type, descr, NULL)
#define SYSCTL_ADD_PROC(ctx, parent, nbr, name, access, ptr, arg, handler, fmt, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, (access), ptr, arg, handler, fmt, descr, NULL)

/* The root and the top-level nodes net80211 hangs off. */
extern struct sysctl_oid_list sysctl__children;
SYSCTL_DECL(_net);
SYSCTL_DECL(_kern);
SYSCTL_DECL(_debug);
SYSCTL_DECL(_hw);
SYSCTL_DECL(_dev);
SYSCTL_DECL(_compat);

struct sysctl_oid;
int	sysctl_handle_counter_u64(SYSCTL_HANDLER_ARGS);
#endif /* _BSDKPI_SYS_SYSCTL_H_ */
