// SPDX-License-Identifier: MIT
/*
 * mutex(9), condvar(9) and sleep(9)/wakeup(9) on LinuxKPI.
 *
 * MTX_DEF: struct mutex (IOLock) plus owner tracking, so that MTX_RECURSE
 * nests and mtx_owned()/mtx_assert() work.  MTX_SPIN: spinlock_t with
 * interrupts disabled.
 *
 * Sleep channels: FreeBSD's wakeup(chan) takes no lock, but the backend's
 * lkpi_os_mutex_sleep()/lkpi_os_wakeup() pair is tied to one mutex.  All
 * channel sleeps therefore go through one global "sleep queue" mutex: the
 * sleeper takes it, drops its own lock, sleeps (atomically releasing the
 * sleep queue mutex), and re-takes its lock afterwards; wakeup() takes the
 * sleep queue mutex before signalling, so a wakeup issued after the sleeper
 * decided to sleep cannot be lost.  The host backend wakes every sleeper
 * on the mutex (it ignores the channel); callers re-check their condition,
 * which FreeBSD semantics allow.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <lkpi/os.h>

struct mtx Giant;

void
mtx_init(struct mtx *m, const char *name, const char *type, int opts)
{
	(void)type;
	memset(m, 0, sizeof(*m));
	m->lock_object.lo_name = name;
	m->lock_object.lo_flags = LO_INITIALIZED |
	    ((opts & MTX_RECURSE) ? LO_RECURSABLE : 0) |
	    ((opts & MTX_SPIN) ? 0 : LO_SLEEPABLE);
	m->mtx_flags = opts;
	if (opts & MTX_SPIN)
		spin_lock_init(&m->mtx_spin);
	else
		mutex_init(&m->mtx_mutex);
}

void
mtx_destroy(struct mtx *m)
{
	KASSERT(m->mtx_owner == NULL, ("mtx_destroy: %s is locked",
	    m->lock_object.lo_name));
	if (m->mtx_flags & MTX_SPIN)
		spin_lock_destroy(&m->mtx_spin);
	else
		mutex_destroy(&m->mtx_mutex);
	m->lock_object.lo_flags = 0;
}

void
_mtx_destroy(void *arg)
{
	mtx_destroy((struct mtx *)arg);
}

void
mtx_sysinit(void *arg)
{
	struct mtx_args *margs = arg;

	mtx_init((struct mtx *)margs->ma_mtx, margs->ma_desc, NULL,
	    margs->ma_opts);
}

int
mtx_initialized(const struct mtx *m)
{
	return (lock_initialized(&m->lock_object));
}

static inline void
mtx_lazy_init(struct mtx *m)
{
	/*
	 * FreeBSD panics on an uninitialised mutex.  Zeroed storage is
	 * common in this port (embedded in kzalloc'd structs that were
	 * never mtx_init'ed on some error path); treat it as MTX_DEF.
	 */
	if (__predict_false(!lock_initialized(&m->lock_object)))
		mtx_init(m, "lazy", NULL, MTX_DEF);
}

void
mtx_lock(struct mtx *m)
{
	lkpi_os_thread_t self;

	mtx_lazy_init(m);
	if (m->mtx_flags & MTX_SPIN) {
		mtx_lock_spin(m);
		return;
	}
	self = lkpi_os_thread_self();
	if (m->mtx_owner == self) {
		KASSERT(m->lock_object.lo_flags & LO_RECURSABLE,
		    ("mtx_lock: %s recursed on non-recursive mutex",
		    m->lock_object.lo_name));
		m->mtx_recurse++;
		return;
	}
	mutex_lock(&m->mtx_mutex);
	m->mtx_owner = self;
}

void
mtx_unlock(struct mtx *m)
{
	if (m->mtx_flags & MTX_SPIN) {
		mtx_unlock_spin(m);
		return;
	}
	KASSERT(m->mtx_owner == lkpi_os_thread_self(),
	    ("mtx_unlock: %s not owned", m->lock_object.lo_name));
	if (m->mtx_recurse > 0) {
		m->mtx_recurse--;
		return;
	}
	m->mtx_owner = NULL;
	mutex_unlock(&m->mtx_mutex);
}

int
mtx_trylock(struct mtx *m)
{
	lkpi_os_thread_t self;

	mtx_lazy_init(m);
	if (m->mtx_flags & MTX_SPIN)
		return (mtx_trylock_spin(m));
	self = lkpi_os_thread_self();
	if (m->mtx_owner == self) {
		if ((m->lock_object.lo_flags & LO_RECURSABLE) == 0)
			return (0);
		m->mtx_recurse++;
		return (1);
	}
	if (!mutex_trylock(&m->mtx_mutex))
		return (0);
	m->mtx_owner = self;
	return (1);
}

#define BSDKPI_MTX_NOIRQ	0x40000000	/* taken by mtx_trylock_spin() */

void
mtx_lock_spin(struct mtx *m)
{
	unsigned long flags;

	mtx_lazy_init(m);
	if (m->mtx_owner == lkpi_os_thread_self()) {
		m->mtx_recurse++;
		return;
	}
	spin_lock_irqsave(&m->mtx_spin, flags);
	m->mtx_irqstate = flags;
	m->mtx_owner = lkpi_os_thread_self();
}

void
mtx_unlock_spin(struct mtx *m)
{
	if (m->mtx_recurse > 0) {
		m->mtx_recurse--;
		return;
	}
	m->mtx_owner = NULL;
	if (m->mtx_flags & BSDKPI_MTX_NOIRQ) {
		m->mtx_flags &= ~BSDKPI_MTX_NOIRQ;
		spin_unlock(&m->mtx_spin);
	} else
		spin_unlock_irqrestore(&m->mtx_spin, m->mtx_irqstate);
}

/*
 * The backend has no try-lock-with-interrupts-disabled primitive, so a
 * spin mutex taken with mtx_trylock_spin() is held with interrupts enabled
 * and released with the matching plain unlock (BSDKPI_MTX_NOIRQ).
 */
int
mtx_trylock_spin(struct mtx *m)
{
	mtx_lazy_init(m);
	if (m->mtx_owner == lkpi_os_thread_self()) {
		m->mtx_recurse++;
		return (1);
	}
	if (!spin_trylock(&m->mtx_spin))
		return (0);
	m->mtx_flags |= BSDKPI_MTX_NOIRQ;
	m->mtx_owner = lkpi_os_thread_self();
	return (1);
}

int
mtx_owned(const struct mtx *m)
{
	return (m->mtx_owner != NULL && m->mtx_owner == lkpi_os_thread_self());
}

int
mtx_recursed(const struct mtx *m)
{
	return (m->mtx_recurse > 0);
}

void
_mtx_assert(const struct mtx *m, int what, const char *file, int line)
{
	switch (what & LA_MASKASSERT) {
	case MA_OWNED:
	case MA_OWNED | MA_RECURSED:
	case MA_OWNED | MA_NOTRECURSED:
		if (!mtx_owned(m))
			panic("mutex %s not owned at %s:%d",
			    m->lock_object.lo_name, file, line);
		if (mtx_recursed(m)) {
			if ((what & MA_NOTRECURSED) != 0)
				panic("mutex %s recursed at %s:%d",
				    m->lock_object.lo_name, file, line);
		} else if ((what & MA_RECURSED) != 0) {
			panic("mutex %s unrecursed at %s:%d",
			    m->lock_object.lo_name, file, line);
		}
		break;
	case MA_NOTOWNED:
		if (mtx_owned(m))
			panic("mutex %s owned at %s:%d",
			    m->lock_object.lo_name, file, line);
		break;
	default:
		panic("unknown mtx_assert at %s:%d", file, line);
	}
}

/* ---- sleep channels ------------------------------------------------- */

static DEFINE_MUTEX(bsdkpi_sleepq_mtx);

/*
 * Release @mtx completely (remembering the recursion depth), sleep on
 * @chan for @timo ticks (0 = forever), re-acquire @mtx unless PDROP.
 * Returns 0 on wakeup, EWOULDBLOCK on timeout.
 */
int
bsdkpi_msleep(const void *chan, struct mtx *mtx, int pri, const char *wmesg,
    int timo)
{
	lkpi_os_mutex_t sq = __lkpi_mutex_get(&bsdkpi_sleepq_mtx);
	int recurse = 0;
	bool woke;

	(void)wmesg;
	lkpi_os_mutex_lock(sq);
	if (mtx != NULL) {
		KASSERT(mtx_owned(mtx), ("msleep: %s not owned",
		    mtx->lock_object.lo_name));
		recurse = mtx->mtx_recurse;
		mtx->mtx_recurse = 0;
		mtx_unlock(mtx);
	}
	woke = lkpi_os_mutex_sleep(sq, chan,
	    timo > 0 ? (uint64_t)timo * (1000000000ULL / HZ) : 0);
	lkpi_os_mutex_unlock(sq);
	if (mtx != NULL && (pri & PDROP) == 0) {
		mtx_lock(mtx);
		mtx->mtx_recurse = recurse;
	}
	return (woke ? 0 : EWOULDBLOCK);
}

int
_sleep(const void *chan, struct lock_object *lock, int pri, const char *wmesg,
    sbintime_t sbt, sbintime_t pr, int flags)
{
	int timo;

	(void)pr;
	(void)flags;
	timo = sbt > 0 ? (int)(sbt / (SBT_1S / HZ)) : 0;
	if (sbt > 0 && timo == 0)
		timo = 1;
	return (bsdkpi_msleep(chan, (struct mtx *)lock, pri, wmesg, timo));
}

static void
bsdkpi_wakeup(const void *chan, bool one)
{
	lkpi_os_mutex_t sq = __lkpi_mutex_get(&bsdkpi_sleepq_mtx);

	lkpi_os_mutex_lock(sq);
	lkpi_os_wakeup(sq, chan, one);
	lkpi_os_mutex_unlock(sq);
}

void
wakeup(const void *chan)
{
	bsdkpi_wakeup(chan, false);
}

void
wakeup_one(const void *chan)
{
	bsdkpi_wakeup(chan, true);
}

void
wakeup_any(const void *chan)
{
	bsdkpi_wakeup(chan, true);
}

int
pause(const char *wmesg, int timo)
{
	(void)wmesg;
	if (timo <= 0) {
		lkpi_os_thread_yield();
		return (0);
	}
	lkpi_os_sleep_ms((uint32_t)timo * (1000 / HZ));
	return (0);
}

int
pause_sbt(const char *wmesg, sbintime_t sbt, sbintime_t pr, int flags)
{
	(void)pr;
	(void)flags;
	return (pause(wmesg, (int)(sbt / (SBT_1S / HZ))));
}

/* ---- condvar(9) ----------------------------------------------------- */

void
cv_init(struct cv *cvp, const char *desc)
{
	cvp->cv_description = desc;
	cvp->cv_waiters = 0;
}

void
cv_destroy(struct cv *cvp)
{
	KASSERT(cvp->cv_waiters == 0, ("cv_destroy: %s has waiters",
	    cvp->cv_description));
}

static int
cv_wait_common(struct cv *cvp, struct mtx *mtx, int timo, int pri)
{
	int error;

	__atomic_fetch_add(&cvp->cv_waiters, 1, __ATOMIC_SEQ_CST);
	error = bsdkpi_msleep(cvp, mtx, pri, cvp->cv_description, timo);
	__atomic_fetch_sub(&cvp->cv_waiters, 1, __ATOMIC_SEQ_CST);
	return (error);
}

void
cv_wait(struct cv *cvp, struct mtx *mtx)
{
	(void)cv_wait_common(cvp, mtx, 0, 0);
}

void
cv_wait_unlock(struct cv *cvp, struct mtx *mtx)
{
	(void)cv_wait_common(cvp, mtx, 0, PDROP);
}

int
cv_wait_sig(struct cv *cvp, struct mtx *mtx)
{
	return (cv_wait_common(cvp, mtx, 0, 0));
}

int
cv_timedwait(struct cv *cvp, struct mtx *mtx, int timo)
{
	return (cv_wait_common(cvp, mtx, timo > 0 ? timo : 1, 0));
}

int
cv_timedwait_sig(struct cv *cvp, struct mtx *mtx, int timo)
{
	return (cv_timedwait(cvp, mtx, timo));
}

int
cv_timedwait_sbt(struct cv *cvp, struct mtx *mtx, sbintime_t sbt,
    sbintime_t pr, int flags)
{
	int timo;

	(void)pr;
	(void)flags;
	timo = (int)(sbt / (SBT_1S / HZ));
	return (cv_timedwait(cvp, mtx, timo > 0 ? timo : 1));
}

void
cv_signal(struct cv *cvp)
{
	if (__atomic_load_n(&cvp->cv_waiters, __ATOMIC_SEQ_CST) > 0)
		bsdkpi_wakeup(cvp, true);
}

void
cv_broadcastpri(struct cv *cvp, int pri)
{
	(void)pri;
	if (__atomic_load_n(&cvp->cv_waiters, __ATOMIC_SEQ_CST) > 0)
		bsdkpi_wakeup(cvp, false);
}
