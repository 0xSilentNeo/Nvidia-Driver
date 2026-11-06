/* SPDX-License-Identifier: MIT */
/* net/ifq.h - the classic struct ifqueue and IF_* macros. */
#ifndef _BSDKPI_NET_IFQ_H_
#define _BSDKPI_NET_IFQ_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>

struct ifqueue {
	struct mbuf	*ifq_head;
	struct mbuf	*ifq_tail;
	int		 ifq_len;
	int		 ifq_maxlen;
	struct mtx	 ifq_mtx;
};

#define IF_LOCK(ifq)		mtx_lock(&(ifq)->ifq_mtx)
#define IF_UNLOCK(ifq)		mtx_unlock(&(ifq)->ifq_mtx)
#define IF_LOCK_ASSERT(ifq)	mtx_assert(&(ifq)->ifq_mtx, MA_OWNED)
#define _IF_QFULL(ifq)		((ifq)->ifq_len >= (ifq)->ifq_maxlen)
#define _IF_QLEN(ifq)		((ifq)->ifq_len)

#define _IF_ENQUEUE(ifq, m) do {					\
	(m)->m_nextpkt = NULL;						\
	if ((ifq)->ifq_tail == NULL)					\
		(ifq)->ifq_head = m;					\
	else								\
		(ifq)->ifq_tail->m_nextpkt = m;				\
	(ifq)->ifq_tail = m;						\
	(ifq)->ifq_len++;						\
} while (0)
#define IF_ENQUEUE(ifq, m) do {						\
	IF_LOCK(ifq);							\
	_IF_ENQUEUE(ifq, m);						\
	IF_UNLOCK(ifq);							\
} while (0)
#define _IF_PREPEND(ifq, m) do {					\
	(m)->m_nextpkt = (ifq)->ifq_head;				\
	if ((ifq)->ifq_tail == NULL)					\
		(ifq)->ifq_tail = (m);					\
	(ifq)->ifq_head = (m);						\
	(ifq)->ifq_len++;						\
} while (0)
#define IF_PREPEND(ifq, m) do {						\
	IF_LOCK(ifq);							\
	_IF_PREPEND(ifq, m);						\
	IF_UNLOCK(ifq);							\
} while (0)
#define _IF_DEQUEUE(ifq, m) do {					\
	(m) = (ifq)->ifq_head;						\
	if (m) {							\
		if (((ifq)->ifq_head = (m)->m_nextpkt) == NULL)		\
			(ifq)->ifq_tail = NULL;				\
		(m)->m_nextpkt = NULL;					\
		(ifq)->ifq_len--;					\
	}								\
} while (0)
#define IF_DEQUEUE(ifq, m) do {						\
	IF_LOCK(ifq);							\
	_IF_DEQUEUE(ifq, m);						\
	IF_UNLOCK(ifq);							\
} while (0)
#define _IF_DEQUEUE_ALL(ifq, m) do {					\
	(m) = (ifq)->ifq_head;						\
	(ifq)->ifq_head = (ifq)->ifq_tail = NULL;			\
	(ifq)->ifq_len = 0;						\
} while (0)
#define IF_DEQUEUE_ALL(ifq, m) do {					\
	IF_LOCK(ifq);							\
	_IF_DEQUEUE_ALL(ifq, m);					\
	IF_UNLOCK(ifq);							\
} while (0)
#define _IF_POLL(ifq, m)	((m) = (ifq)->ifq_head)
#define IF_POLL(ifq, m)		_IF_POLL(ifq, m)
#define _IF_DRAIN(ifq) do {						\
	struct mbuf *_m;						\
	for (;;) {							\
		_IF_DEQUEUE(ifq, _m);					\
		if (_m == NULL)						\
			break;						\
		m_freem(_m);						\
	}								\
} while (0)
#define IF_DRAIN(ifq) do {						\
	IF_LOCK(ifq);							\
	_IF_DRAIN(ifq);							\
	IF_UNLOCK(ifq);							\
} while (0)

static __inline void
IFQ_SET_MAXLEN(struct ifqueue *ifq, int len)
{
	ifq->ifq_maxlen = len;
}

static __inline void
ifq_init(struct ifqueue *ifq, struct ifnet *ifp)
{
	(void)ifp;
	mtx_init(&ifq->ifq_mtx, "ifqueue", NULL, MTX_DEF);
	ifq->ifq_head = ifq->ifq_tail = NULL;
	ifq->ifq_len = 0;
	if (ifq->ifq_maxlen == 0)
		ifq->ifq_maxlen = 50;
}

static __inline void
ifq_delete(struct ifqueue *ifq)
{
	mtx_destroy(&ifq->ifq_mtx);
}
#endif /* _BSDKPI_NET_IFQ_H_ */
