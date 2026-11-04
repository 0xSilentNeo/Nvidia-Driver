/* SPDX-License-Identifier: MIT */
/*
 * lkpi_os_iokit.cpp - IOKit/libkern implementation of lkpi/os.h.
 *
 * This is the only file in the kext that touches XNU primitives on behalf of
 * the C LinuxKPI layer.  Every function here is reachable from C through the
 * C ABI declared in src/linuxkpi/include/lkpi/os.h; the semantics each one
 * must honour are in the comments of that header.
 *
 * Nothing in this file includes a Linux header.  See README.md for the
 * list of approximations (IOMalloc may sleep, mutex_is_locked is a hint,
 * cpu_count is 1, ...).
 */

#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSAtomic.h>
#include <libkern/OSDebug.h>
#include <libkern/OSKextLib.h>
#include <libkern/libkern.h>
#include <libkern/c++/OSObject.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>	/* THREAD_UNINT, THREAD_TIMED_OUT, thread_block */
#include <kern/task.h>
#include <mach/thread_act.h>	/* thread_terminate() */
#include <kern/debug.h>
#include <mach/mach_time.h>
#include <mach/mach_types.h>
#include <mach/clock_types.h>
#include <machine/machine_routines.h>
#include <sys/random.h>
#include <sys/errno.h>
#include <pexpert/pexpert.h>
#include <string.h>

#if LKPI_OS_HAVE_THREAD_POLICY
#include <mach/thread_policy.h>
#endif

#include "lkpi_os_iokit.hpp"

/* Prototypes the public headers may not expose but the KPI exports. */
extern "C" {
#if LKPI_OS_HAVE_MACH_CONTINUOUS_TIME
uint64_t mach_continuous_time(void);
#endif
#if LKPI_OS_HAVE_CPU_NUMBER
int cpu_number(void);
#endif
#if LKPI_OS_HAVE_THREAD_POLICY
kern_return_t thread_policy_set(thread_t thread, thread_policy_flavor_t flavor,
    thread_policy_t policy_info, mach_msg_type_number_t count);
#endif
/* Advances `jiffies`; implemented in the C layer (linux/jiffies.h). */
void lkpi_jiffies_tick(void);
}

#define LKPI_TAG "MT7925: "

/* =====================================================================
 * Small helpers
 * ===================================================================== */

static mach_timebase_info_data_t lkpi_tb;	/* mach ticks -> ns */
static bool lkpi_tb_ready;
static int lkpi_debug_level;			/* boot-arg mt7925_debug=N */

static void
lkpi_timebase_init(void)
{
	if (lkpi_tb_ready)
		return;
	clock_timebase_info(&lkpi_tb);
	if (lkpi_tb.numer == 0 || lkpi_tb.denom == 0) {
		lkpi_tb.numer = 1;
		lkpi_tb.denom = 1;
	}
	lkpi_tb_ready = true;
}

static inline uint64_t
lkpi_abs_to_ns(uint64_t abs)
{
	lkpi_timebase_init();
	if (lkpi_tb.numer == lkpi_tb.denom)
		return abs;
	/* split to avoid overflow for large tick counts */
	uint64_t q = abs / lkpi_tb.denom;
	uint64_t r = abs % lkpi_tb.denom;
	return q * lkpi_tb.numer + (r * lkpi_tb.numer) / lkpi_tb.denom;
}

static inline uint64_t
lkpi_ns_to_abs(uint64_t ns)
{
	uint64_t abs = 0;
	nanoseconds_to_absolutetime(ns, &abs);
	return abs;
}

/* AbsoluteTime is a uint64_t on every 64-bit SDK but spelled through a
 * typedef; use the official accessor so this also compiles if it is ever a
 * struct again. */
static inline AbsoluteTime
lkpi_make_abstime(uint64_t abs)
{
	AbsoluteTime at;
	AbsoluteTime_to_scalar(&at) = abs;
	return at;
}

/* Deadline (absolute mach time) that is `ns` from now. */
static inline AbsoluteTime
lkpi_deadline_from_now_ns(uint64_t ns)
{
	uint64_t deadline = 0;
	clock_absolutetime_interval_to_deadline(lkpi_ns_to_abs(ns), &deadline);
	return lkpi_make_abstime(deadline);
}

/*
 * OSObject used as the "owner" of IOKit event sources.  IOInterruptEventSource
 * and IOTimerEventSource need an OSObject owner for their actions; our handles
 * are plain structs, so this carries a context pointer back to them.
 */
class LKPIEventOwner : public OSObject {
	OSDeclareDefaultStructors(LKPIEventOwner);
public:
	void *ctx;
	static LKPIEventOwner *withContext(void *c)
	{
		LKPIEventOwner *o = new LKPIEventOwner;
		if (o && !o->init()) {
			o->release();
			return NULL;
		}
		if (o)
			o->ctx = c;
		return o;
	}
};
OSDefineMetaClassAndStructors(LKPIEventOwner, OSObject);

/* =====================================================================
 * Memory
 * ===================================================================== */

#define LKPI_MALLOC_MAGIC	0x4C4B5049u	/* 'LKPI' */
#define LKPI_PAGES_MAGIC	0x5047534Du	/* 'PGSM' */

struct lkpi_malloc_hdr {
	uint64_t size;		/* payload size */
	uint32_t magic;
	uint32_t pad;
};
static_assert(sizeof(struct lkpi_malloc_hdr) == 16, "header must keep 16-byte alignment");

/*
 * IOMalloc has no non-sleeping variant; XNU's kalloc may block for large
 * requests while it grows a zone.  In practice small allocations (< a few
 * KiB) are served from zones without blocking, which is what the atomic
 * callers in the driver (skb allocation in the RX path) request.  The
 * `may_sleep` flag is therefore only used for diagnostics.
 */
void *
lkpi_os_malloc(size_t size, bool zero, bool may_sleep)
{
	(void)may_sleep;
	if (size == 0)
		size = 1;
	size_t total = size + sizeof(struct lkpi_malloc_hdr);
	if (total < size)		/* overflow */
		return NULL;
	struct lkpi_malloc_hdr *h = (struct lkpi_malloc_hdr *)IOMalloc(total);
	if (!h)
		return NULL;
	h->size = size;
	h->magic = LKPI_MALLOC_MAGIC;
	h->pad = 0;
	void *p = (void *)(h + 1);
	if (zero)
		bzero(p, size);
	return p;
}

void
lkpi_os_free(void *ptr)
{
	if (!ptr)
		return;
	struct lkpi_malloc_hdr *h = (struct lkpi_malloc_hdr *)ptr - 1;
	if (h->magic != LKPI_MALLOC_MAGIC) {
		panic(LKPI_TAG "lkpi_os_free(%p): bad header magic 0x%08x", ptr, h->magic);
	}
	size_t total = (size_t)h->size + sizeof(*h);
	h->magic = 0;
	IOFree(h, total);
}

size_t
lkpi_os_malloc_size(const void *ptr)
{
	if (!ptr)
		return 0;
	const struct lkpi_malloc_hdr *h = (const struct lkpi_malloc_hdr *)ptr - 1;
	if (h->magic != LKPI_MALLOC_MAGIC)
		return 0;
	return (size_t)h->size;
}

size_t
lkpi_os_page_size(void)
{
	return (size_t)PAGE_SIZE;
}

/* =====================================================================
 * Pointer-keyed registry (used for page allocations and DMA mappings)
 *
 * Open hash table with chaining, protected by a spinlock.  Nodes are
 * allocated outside the lock.  Keys are kernel VAs or bus addresses; both
 * are unique while live.
 * ===================================================================== */

struct lkpi_reg_node {
	struct lkpi_reg_node *next;
	uint64_t key;
	void *val;
};

#define LKPI_REG_BUCKETS 1024

struct lkpi_registry {
	IOSimpleLock *lock;
	struct lkpi_reg_node *buckets[LKPI_REG_BUCKETS];
};

static inline unsigned
lkpi_reg_hash(uint64_t key)
{
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdULL;
	key ^= key >> 33;
	return (unsigned)(key & (LKPI_REG_BUCKETS - 1));
}

static bool
lkpi_reg_init(struct lkpi_registry *r)
{
	bzero(r->buckets, sizeof(r->buckets));
	r->lock = IOSimpleLockAlloc();
	return r->lock != NULL;
}

static void
lkpi_reg_fini(struct lkpi_registry *r)
{
	if (r->lock) {
		IOSimpleLockFree(r->lock);
		r->lock = NULL;
	}
}

/* Returns false if the key already exists (caller frees val). */
static bool
lkpi_reg_insert(struct lkpi_registry *r, uint64_t key, void *val)
{
	struct lkpi_reg_node *n = (struct lkpi_reg_node *)IOMalloc(sizeof(*n));
	if (!n)
		return false;
	n->key = key;
	n->val = val;
	unsigned b = lkpi_reg_hash(key);
	IOInterruptState is = IOSimpleLockLockDisableInterrupt(r->lock);
	for (struct lkpi_reg_node *c = r->buckets[b]; c; c = c->next) {
		if (c->key == key) {
			IOSimpleLockUnlockEnableInterrupt(r->lock, is);
			IOFree(n, sizeof(*n));
			return false;
		}
	}
	n->next = r->buckets[b];
	r->buckets[b] = n;
	IOSimpleLockUnlockEnableInterrupt(r->lock, is);
	return true;
}

static void *
lkpi_reg_lookup(struct lkpi_registry *r, uint64_t key)
{
	unsigned b = lkpi_reg_hash(key);
	void *val = NULL;
	IOInterruptState is = IOSimpleLockLockDisableInterrupt(r->lock);
	for (struct lkpi_reg_node *c = r->buckets[b]; c; c = c->next) {
		if (c->key == key) {
			val = c->val;
			break;
		}
	}
	IOSimpleLockUnlockEnableInterrupt(r->lock, is);
	return val;
}

static void *
lkpi_reg_remove(struct lkpi_registry *r, uint64_t key)
{
	unsigned b = lkpi_reg_hash(key);
	struct lkpi_reg_node *found = NULL;
	IOInterruptState is = IOSimpleLockLockDisableInterrupt(r->lock);
	struct lkpi_reg_node **pp = &r->buckets[b];
	while (*pp) {
		if ((*pp)->key == key) {
			found = *pp;
			*pp = found->next;
			break;
		}
		pp = &(*pp)->next;
	}
	IOSimpleLockUnlockEnableInterrupt(r->lock, is);
	if (!found)
		return NULL;
	void *val = found->val;
	IOFree(found, sizeof(*found));
	return val;
}

static struct lkpi_registry lkpi_pages_reg;	/* key: kernel VA */
static struct lkpi_registry lkpi_dma_reg;	/* key: bus address */

/* =====================================================================
 * Page allocations
 *
 * Linux alloc_pages() returns physically contiguous memory; IOMallocAligned
 * does not guarantee that beyond one page and IODMACommand cannot stitch
 * discontiguous pages together on a machine without an IOMMU.  Every
 * request therefore goes through IOBufferMemoryDescriptor with
 * kIOMemoryPhysicallyContiguous, restricted to LKPI_OS_PAGES_PHYS_MASK so
 * RX buffers never need a bounce.  The descriptor is kept in a registry
 * keyed by the returned VA so lkpi_os_free_pages() can find it.
 * ===================================================================== */

struct lkpi_pages_rec {
	uint32_t magic;
	IOBufferMemoryDescriptor *md;
	size_t size;
};

void *
lkpi_os_alloc_pages(size_t size, bool zero)
{
	if (size == 0)
		return NULL;
	size = (size + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1);

	IOOptionBits opts = kIODirectionInOut | kIOMemoryPhysicallyContiguous;
	IOBufferMemoryDescriptor *md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
	    kernel_task, opts, size, LKPI_OS_PAGES_PHYS_MASK);
	if (!md) {
		/* Retry anywhere in memory; IODMACommand will bounce if needed. */
		md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, opts, size,
		    ~((uint64_t)PAGE_SIZE - 1));
		if (!md)
			return NULL;
	}
	if (md->prepare() != kIOReturnSuccess) {
		md->release();
		return NULL;
	}
	void *va = md->getBytesNoCopy();
	if (!va) {
		md->complete();
		md->release();
		return NULL;
	}
	struct lkpi_pages_rec *rec = (struct lkpi_pages_rec *)IOMalloc(sizeof(*rec));
	if (!rec) {
		md->complete();
		md->release();
		return NULL;
	}
	rec->magic = LKPI_PAGES_MAGIC;
	rec->md = md;
	rec->size = size;
	if (!lkpi_reg_insert(&lkpi_pages_reg, (uint64_t)(uintptr_t)va, rec)) {
		IOFree(rec, sizeof(*rec));
		md->complete();
		md->release();
		return NULL;
	}
	if (zero)
		bzero(va, size);	/* IOBufferMemoryDescriptor already zeroes, cheap to be sure */
	return va;
}

void
lkpi_os_free_pages(void *ptr, size_t size)
{
	if (!ptr)
		return;
	struct lkpi_pages_rec *rec =
	    (struct lkpi_pages_rec *)lkpi_reg_remove(&lkpi_pages_reg, (uint64_t)(uintptr_t)ptr);
	if (!rec || rec->magic != LKPI_PAGES_MAGIC) {
		panic(LKPI_TAG "lkpi_os_free_pages(%p, %zu): unknown buffer", ptr, size);
	}
	rec->md->complete();
	rec->md->release();
	rec->magic = 0;
	IOFree(rec, sizeof(*rec));
}

/* =====================================================================
 * Logging
 * ===================================================================== */

void
lkpi_os_vlog(int level, const char *fmt, va_list ap)
{
	char buf[512];

	if (level >= LKPI_LOG_DEBUG && lkpi_debug_level <= 0)
		return;
	vsnprintf(buf, sizeof(buf), fmt, ap);
	/* IOLog adds no newline; most Linux format strings end with one. */
	IOLog(LKPI_TAG "%s", buf);
}

void
lkpi_os_log(int level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	lkpi_os_vlog(level, fmt, ap);
	va_end(ap);
}

void
lkpi_os_panic(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	panic(LKPI_TAG "%s", buf);
	/* panic() is noreturn in XNU, but the header attribute may be missing
	 * on some SDKs; keep the compiler honest. */
	for (;;)
		IOSleep(1000);
}

void
lkpi_os_dump_stack(void)
{
	OSReportWithBacktrace(LKPI_TAG "dump_stack");
}

/* =====================================================================
 * Time
 * ===================================================================== */

uint64_t
lkpi_os_uptime_ns(void)
{
	return lkpi_abs_to_ns(mach_absolute_time());
}

uint64_t
lkpi_os_boottime_ns(void)
{
#if LKPI_OS_HAVE_MACH_CONTINUOUS_TIME
	return lkpi_abs_to_ns(mach_continuous_time());
#else
	return lkpi_abs_to_ns(mach_absolute_time());
#endif
}

uint64_t
lkpi_os_realtime_ns(void)
{
	clock_sec_t secs = 0;
	clock_nsec_t nsecs = 0;
	clock_get_calendar_nanotime(&secs, &nsecs);
	return (uint64_t)secs * 1000000000ULL + (uint64_t)nsecs;
}

void
lkpi_os_delay_us(uint32_t us)
{
	IODelay(us);
}

void
lkpi_os_sleep_ms(uint32_t ms)
{
	if (ms == 0)
		ms = 1;
	IOSleep(ms);
}

bool
lkpi_os_may_sleep(void)
{
	return !ml_at_interrupt_context() && ml_get_interrupts_enabled();
}

/* =====================================================================
 * Spinlocks
 * ===================================================================== */

struct lkpi_os_spinlock {
	IOSimpleLock *lk;
};

lkpi_os_spinlock_t
lkpi_os_spinlock_alloc(void)
{
	struct lkpi_os_spinlock *s = (struct lkpi_os_spinlock *)IOMalloc(sizeof(*s));
	if (!s)
		return NULL;
	s->lk = IOSimpleLockAlloc();
	if (!s->lk) {
		IOFree(s, sizeof(*s));
		return NULL;
	}
	return s;
}

void
lkpi_os_spinlock_free(lkpi_os_spinlock_t s)
{
	if (!s)
		return;
	IOSimpleLockFree(s->lk);
	IOFree(s, sizeof(*s));
}

void
lkpi_os_spin_lock(lkpi_os_spinlock_t s)
{
	IOSimpleLockLock(s->lk);
}

bool
lkpi_os_spin_trylock(lkpi_os_spinlock_t s)
{
	return IOSimpleLockTryLock(s->lk);
}

void
lkpi_os_spin_unlock(lkpi_os_spinlock_t s)
{
	IOSimpleLockUnlock(s->lk);
}

lkpi_os_irqstate_t
lkpi_os_spin_lock_irq(lkpi_os_spinlock_t s)
{
	IOInterruptState is = IOSimpleLockLockDisableInterrupt(s->lk);
	return (lkpi_os_irqstate_t)is;
}

void
lkpi_os_spin_unlock_irq(lkpi_os_spinlock_t s, lkpi_os_irqstate_t state)
{
	IOSimpleLockUnlockEnableInterrupt(s->lk, (IOInterruptState)state);
}

/* =====================================================================
 * Mutexes
 *
 * `owner` is written by the thread that holds the lock, right after
 * acquiring and right before releasing it.  lkpi_os_mutex_is_locked() reads
 * it without the lock, which is what Linux's mutex_is_locked() debugging
 * asserts expect (a hint, not a synchronisation primitive).
 * ===================================================================== */

struct lkpi_os_mutex {
	IOLock *lk;
	thread_t volatile owner;
};

lkpi_os_mutex_t
lkpi_os_mutex_alloc(void)
{
	struct lkpi_os_mutex *m = (struct lkpi_os_mutex *)IOMalloc(sizeof(*m));
	if (!m)
		return NULL;
	m->lk = IOLockAlloc();
	m->owner = NULL;
	if (!m->lk) {
		IOFree(m, sizeof(*m));
		return NULL;
	}
	return m;
}

void
lkpi_os_mutex_free(lkpi_os_mutex_t m)
{
	if (!m)
		return;
	IOLockFree(m->lk);
	IOFree(m, sizeof(*m));
}

void
lkpi_os_mutex_lock(lkpi_os_mutex_t m)
{
	IOLockLock(m->lk);
	m->owner = current_thread();
}

bool
lkpi_os_mutex_trylock(lkpi_os_mutex_t m)
{
	if (!IOLockTryLock(m->lk))
		return false;
	m->owner = current_thread();
	return true;
}

void
lkpi_os_mutex_unlock(lkpi_os_mutex_t m)
{
	m->owner = NULL;
	IOLockUnlock(m->lk);
}

bool
lkpi_os_mutex_is_locked(lkpi_os_mutex_t m)
{
	return m->owner != NULL;
}

bool
lkpi_os_mutex_sleep(lkpi_os_mutex_t m, const void *event, uint64_t timeout_ns)
{
	int ret;
	void *ev = (void *)(uintptr_t)event;

	/* IOLockSleep* release and re-acquire the lock around the wait. */
	m->owner = NULL;
	if (timeout_ns == 0)
		ret = IOLockSleep(m->lk, ev, THREAD_UNINT);
	else
		ret = IOLockSleepDeadline(m->lk, ev, lkpi_deadline_from_now_ns(timeout_ns), THREAD_UNINT);
	m->owner = current_thread();
	return ret != THREAD_TIMED_OUT;
}

void
lkpi_os_wakeup(lkpi_os_mutex_t m, const void *event, bool one)
{
	IOLockWakeup(m->lk, (void *)(uintptr_t)event, one);
}

/* =====================================================================
 * Threads
 *
 * XNU offers no thread-local storage to kexts, so the association between a
 * thread_t and its lkpi_os_thread wrapper lives in a small hash table.
 * Threads created elsewhere (IOKit start thread, workloop threads) get a
 * lightweight "foreign" wrapper on first use; those are never freed, which
 * is bounded by the number of distinct foreign threads that ever call in.
 * ===================================================================== */

struct lkpi_os_thread {
	struct lkpi_os_thread *hnext;	/* table chain */
	thread_t t;
	IOLock *lk;
	bool pending_wake;
	bool exited;
	bool foreign;
	bool started;
	void *slot;
	char name[32];
	lkpi_os_thread_fn fn;
	void *arg;
	volatile SInt32 refs;
};

#define LKPI_THREAD_BUCKETS 256

static IOSimpleLock *lkpi_thread_lock;
static struct lkpi_os_thread *lkpi_thread_table[LKPI_THREAD_BUCKETS];

static inline unsigned
lkpi_thread_hash(thread_t t)
{
	uintptr_t v = (uintptr_t)t;
	return (unsigned)((v >> 4) ^ (v >> 12)) & (LKPI_THREAD_BUCKETS - 1);
}

static struct lkpi_os_thread *
lkpi_thread_lookup_locked(thread_t t)
{
	for (struct lkpi_os_thread *c = lkpi_thread_table[lkpi_thread_hash(t)]; c; c = c->hnext)
		if (c->t == t)
			return c;
	return NULL;
}

static void
lkpi_thread_insert_locked(struct lkpi_os_thread *th)
{
	unsigned b = lkpi_thread_hash(th->t);
	th->hnext = lkpi_thread_table[b];
	lkpi_thread_table[b] = th;
}

static void
lkpi_thread_remove_locked(struct lkpi_os_thread *th)
{
	struct lkpi_os_thread **pp = &lkpi_thread_table[lkpi_thread_hash(th->t)];
	while (*pp) {
		if (*pp == th) {
			*pp = th->hnext;
			th->hnext = NULL;
			return;
		}
		pp = &(*pp)->hnext;
	}
}

static struct lkpi_os_thread *
lkpi_thread_new(const char *name)
{
	struct lkpi_os_thread *th = (struct lkpi_os_thread *)IOMalloc(sizeof(*th));
	if (!th)
		return NULL;
	bzero(th, sizeof(*th));
	th->lk = IOLockAlloc();
	if (!th->lk) {
		IOFree(th, sizeof(*th));
		return NULL;
	}
	th->refs = 1;
	if (name)
		strlcpy(th->name, name, sizeof(th->name));
	return th;
}

static void
lkpi_thread_destroy(struct lkpi_os_thread *th)
{
	IOLockFree(th->lk);
	IOFree(th, sizeof(*th));
}

static void
lkpi_thread_put(struct lkpi_os_thread *th)
{
	if (OSDecrementAtomic(&th->refs) == 1)
		lkpi_thread_destroy(th);
}

/* Find (or lazily create) the wrapper of the calling thread. */
static struct lkpi_os_thread *
lkpi_thread_current(void)
{
	thread_t t = current_thread();
	IOInterruptState is = IOSimpleLockLockDisableInterrupt(lkpi_thread_lock);
	struct lkpi_os_thread *th = lkpi_thread_lookup_locked(t);
	IOSimpleLockUnlockEnableInterrupt(lkpi_thread_lock, is);
	if (th)
		return th;

	/* Foreign thread: allocate outside the lock, then insert if still absent. */
	struct lkpi_os_thread *nw = lkpi_thread_new("foreign");
	if (!nw)
		return NULL;
	nw->t = t;
	nw->foreign = true;
	nw->started = true;
	is = IOSimpleLockLockDisableInterrupt(lkpi_thread_lock);
	th = lkpi_thread_lookup_locked(t);
	if (!th) {
		lkpi_thread_insert_locked(nw);
		th = nw;
		nw = NULL;
	}
	IOSimpleLockUnlockEnableInterrupt(lkpi_thread_lock, is);
	if (nw)
		lkpi_thread_destroy(nw);
	return th;
}

static void
lkpi_thread_trampoline(void *param, wait_result_t wr)
{
	(void)wr;
	struct lkpi_os_thread *th = (struct lkpi_os_thread *)param;

	/* The creator registered us before kernel_thread_start() returned the
	 * thread_t, but the entry's `t` is filled in by the creator; wait for it
	 * so lkpi_os_thread_self() works from the very first instruction. */
	IOLockLock(th->lk);
	while (!th->started)
		IOLockSleep(th->lk, &th->started, THREAD_UNINT);
	IOLockUnlock(th->lk);

	th->fn(th->arg);
	lkpi_os_thread_exit();
}

lkpi_os_thread_t
lkpi_os_thread_create(lkpi_os_thread_fn fn, void *arg, const char *name)
{
	struct lkpi_os_thread *th = lkpi_thread_new(name);
	if (!th)
		return NULL;
	th->fn = fn;
	th->arg = arg;
	th->refs = 2;	/* one for the creator, one for the thread itself */

	thread_t t = THREAD_NULL;
	kern_return_t kr = kernel_thread_start(lkpi_thread_trampoline, th, &t);
	if (kr != KERN_SUCCESS) {
		lkpi_thread_destroy(th);
		return NULL;
	}
	th->t = t;
	IOInterruptState is = IOSimpleLockLockDisableInterrupt(lkpi_thread_lock);
	lkpi_thread_insert_locked(th);
	IOSimpleLockUnlockEnableInterrupt(lkpi_thread_lock, is);

	IOLockLock(th->lk);
	th->started = true;
	IOLockWakeup(th->lk, &th->started, true);
	IOLockUnlock(th->lk);

	/* kernel_thread_start() hands back a reference we do not need; the
	 * thread_t stays a valid key until the thread removes itself. */
	thread_deallocate(t);
	return th;
}

void
lkpi_os_thread_exit(void)
{
	thread_t self = current_thread();
	IOInterruptState is = IOSimpleLockLockDisableInterrupt(lkpi_thread_lock);
	struct lkpi_os_thread *th = lkpi_thread_lookup_locked(self);
	if (th)
		lkpi_thread_remove_locked(th);
	IOSimpleLockUnlockEnableInterrupt(lkpi_thread_lock, is);

	if (th) {
		IOLockLock(th->lk);
		th->exited = true;
		IOLockWakeup(th->lk, &th->exited, false);
		IOLockUnlock(th->lk);
		lkpi_thread_put(th);	/* the thread's own reference */
	}
	/* Terminating the calling thread never returns (IOExitThread does
	 * exactly this). */
	(void)thread_terminate(self);
	for (;;)
		IOSleep(1000);
}

void
lkpi_os_thread_set_realtime(lkpi_os_thread_t th, bool rt)
{
#if LKPI_OS_HAVE_THREAD_POLICY
	if (!th || !th->t)
		return;
	thread_precedence_policy_data_t prec;
	prec.importance = rt ? 63 : 0;
	(void)thread_policy_set(th->t, THREAD_PRECEDENCE_POLICY,
	    (thread_policy_t)&prec, THREAD_PRECEDENCE_POLICY_COUNT);
#else
	/* Documented no-op: thread_policy_set() is only reachable through a
	 * private prototype.  The NAPI/tasklet threads just run at the default
	 * kernel priority, which is already above user space. */
	(void)th;
	(void)rt;
#endif
}

lkpi_os_thread_t
lkpi_os_thread_self(void)
{
	return lkpi_thread_current();
}

void
lkpi_os_thread_yield(void)
{
#if LKPI_OS_HAVE_THREAD_BLOCK
	(void)thread_block(THREAD_CONTINUE_NULL);
#else
	/* thread_block() is declared in kern/sched_prim.h but its export status
	 * differs between SDKs; a 1 ms sleep is the conservative equivalent.
	 * cond_resched()/yield() call sites in mt76 are not hot. */
	IOSleep(1);
#endif
}

bool
lkpi_os_thread_wait(lkpi_os_thread_t th, uint64_t timeout_ns)
{
	bool woke;

	if (!th)
		return false;
	IOLockLock(th->lk);
	if (!th->pending_wake) {
		if (timeout_ns == 0)
			IOLockSleep(th->lk, &th->pending_wake, THREAD_UNINT);
		else
			IOLockSleepDeadline(th->lk, &th->pending_wake,
			    lkpi_deadline_from_now_ns(timeout_ns), THREAD_UNINT);
	}
	woke = th->pending_wake;
	th->pending_wake = false;
	IOLockUnlock(th->lk);
	return woke;
}

void
lkpi_os_thread_wake(lkpi_os_thread_t th)
{
	if (!th)
		return;
	IOLockLock(th->lk);
	th->pending_wake = true;
	IOLockWakeup(th->lk, &th->pending_wake, true);
	IOLockUnlock(th->lk);
}

void
lkpi_os_thread_release(lkpi_os_thread_t th)
{
	if (!th)
		return;
	if (th->foreign) {
		/* Foreign wrappers stay in the table for the life of the kext. */
		return;
	}
	/* Normally called after the thread exited (kthread_stop joined on it);
	 * if not, the thread's own reference keeps the struct alive. */
	lkpi_thread_put(th);
}

void *
lkpi_os_thread_get_slot(void)
{
	struct lkpi_os_thread *th = lkpi_thread_current();
	return th ? th->slot : NULL;
}

void
lkpi_os_thread_set_slot(void *p)
{
	struct lkpi_os_thread *th = lkpi_thread_current();
	if (th)
		th->slot = p;
}

/* =====================================================================
 * Timers
 *
 * One shared IOWorkLoop hosts every IOTimerEventSource.  Callbacks run on
 * the thread_call thread with the workloop gate held, so they are
 * serialised with each other and with the jiffies tick.
 * ===================================================================== */

static IOWorkLoop *lkpi_timer_wl;

struct lkpi_os_timer {
	IOTimerEventSource *src;
	LKPIEventOwner *owner;
	IOLock *lk;
	lkpi_os_timer_fn fn;
	void *arg;
	bool armed;	/* wakeAtTime() issued and callback not yet entered */
	bool running;	/* callback in progress */
	bool dying;
};

static void
lkpi_timer_action(OSObject *owner, IOTimerEventSource *sender)
{
	(void)sender;
	LKPIEventOwner *o = OSDynamicCast(LKPIEventOwner, owner);
	if (!o)
		return;
	struct lkpi_os_timer *t = (struct lkpi_os_timer *)o->ctx;
	if (!t)
		return;

	IOLockLock(t->lk);
	if (t->dying || !t->armed) {
		/* Cancelled after the thread_call fired but before we got the lock. */
		IOLockUnlock(t->lk);
		return;
	}
	t->armed = false;
	t->running = true;
	IOLockUnlock(t->lk);

	t->fn(t->arg);

	IOLockLock(t->lk);
	t->running = false;
	IOLockWakeup(t->lk, &t->running, false);
	IOLockUnlock(t->lk);
}

lkpi_os_timer_t
lkpi_os_timer_create(lkpi_os_timer_fn fn, void *arg)
{
	if (!lkpi_timer_wl)
		return NULL;
	struct lkpi_os_timer *t = (struct lkpi_os_timer *)IOMalloc(sizeof(*t));
	if (!t)
		return NULL;
	bzero(t, sizeof(*t));
	t->fn = fn;
	t->arg = arg;
	t->lk = IOLockAlloc();
	t->owner = LKPIEventOwner::withContext(t);
	if (!t->lk || !t->owner)
		goto fail;
	t->src = IOTimerEventSource::timerEventSource(t->owner, lkpi_timer_action);
	if (!t->src)
		goto fail;
	if (lkpi_timer_wl->addEventSource(t->src) != kIOReturnSuccess)
		goto fail;
	return t;
fail:
	if (t->src)
		t->src->release();
	if (t->owner)
		t->owner->release();
	if (t->lk)
		IOLockFree(t->lk);
	IOFree(t, sizeof(*t));
	return NULL;
}

void
lkpi_os_timer_arm(lkpi_os_timer_t t, uint64_t deadline_uptime_ns)
{
	if (!t)
		return;
	IOLockLock(t->lk);
	if (!t->dying) {
		t->armed = true;
		/* deadline is on the lkpi_os_uptime_ns() clock == mach_absolute_time */
		t->src->wakeAtTime(lkpi_make_abstime(lkpi_ns_to_abs(deadline_uptime_ns)));
	}
	IOLockUnlock(t->lk);
}

bool
lkpi_os_timer_cancel(lkpi_os_timer_t t)
{
	if (!t)
		return false;
	IOLockLock(t->lk);
	bool was = t->armed;
	t->armed = false;
	t->src->cancelTimeout();
	IOLockUnlock(t->lk);
	return was;
}

bool
lkpi_os_timer_cancel_sync(lkpi_os_timer_t t)
{
	if (!t)
		return false;
	bool was = lkpi_os_timer_cancel(t);
	/* A callback that already passed the `armed` check keeps running; wait
	 * for it unless we are that callback's own thread (would deadlock). */
	if (lkpi_timer_wl && lkpi_timer_wl->inGate())
		return was;
	IOLockLock(t->lk);
	while (t->running)
		IOLockSleep(t->lk, &t->running, THREAD_UNINT);
	IOLockUnlock(t->lk);
	return was;
}

void
lkpi_os_timer_destroy(lkpi_os_timer_t t)
{
	if (!t)
		return;
	IOLockLock(t->lk);
	t->dying = true;
	IOLockUnlock(t->lk);
	(void)lkpi_os_timer_cancel_sync(t);
	t->src->disable();
	if (lkpi_timer_wl)
		lkpi_timer_wl->removeEventSource(t->src);
	t->src->release();
	t->owner->ctx = NULL;
	t->owner->release();
	IOLockFree(t->lk);
	IOFree(t, sizeof(*t));
}

/* ---- jiffies tick ---------------------------------------------------- */

static IOTimerEventSource *lkpi_jiffies_src;
static LKPIEventOwner *lkpi_jiffies_owner;
static uint64_t lkpi_jiffies_next_abs;
static uint64_t lkpi_jiffies_period_abs;
static volatile bool lkpi_jiffies_running;

static void
lkpi_jiffies_action(OSObject *owner, IOTimerEventSource *sender)
{
	(void)owner;
	if (!lkpi_jiffies_running)
		return;
	uint64_t now = mach_absolute_time();
	/* Catch up if we were late by more than one period, so jiffies keeps
	 * tracking wall time rather than accumulating drift. */
	do {
		lkpi_jiffies_tick();
		lkpi_jiffies_next_abs += lkpi_jiffies_period_abs;
	} while (lkpi_jiffies_next_abs <= now);
	sender->wakeAtTime(lkpi_make_abstime(lkpi_jiffies_next_abs));
}

static int
lkpi_jiffies_start(void)
{
	lkpi_jiffies_owner = LKPIEventOwner::withContext(NULL);
	if (!lkpi_jiffies_owner)
		return -ENOMEM;
	lkpi_jiffies_src = IOTimerEventSource::timerEventSource(lkpi_jiffies_owner, lkpi_jiffies_action);
	if (!lkpi_jiffies_src)
		return -ENOMEM;
	if (lkpi_timer_wl->addEventSource(lkpi_jiffies_src) != kIOReturnSuccess)
		return -EIO;
	lkpi_jiffies_period_abs = lkpi_ns_to_abs(10ULL * 1000 * 1000);	/* HZ = 100 */
	lkpi_jiffies_next_abs = mach_absolute_time() + lkpi_jiffies_period_abs;
	lkpi_jiffies_running = true;
	lkpi_jiffies_src->wakeAtTime(lkpi_make_abstime(lkpi_jiffies_next_abs));
	return 0;
}

static void
lkpi_jiffies_stop(void)
{
	lkpi_jiffies_running = false;
	if (lkpi_jiffies_src) {
		lkpi_jiffies_src->cancelTimeout();
		lkpi_jiffies_src->disable();
		if (lkpi_timer_wl)
			lkpi_timer_wl->removeEventSource(lkpi_jiffies_src);
		lkpi_jiffies_src->release();
		lkpi_jiffies_src = NULL;
	}
	if (lkpi_jiffies_owner) {
		lkpi_jiffies_owner->release();
		lkpi_jiffies_owner = NULL;
	}
}

/* =====================================================================
 * MMIO
 * ===================================================================== */

static inline void
lkpi_mmio_wmb(void)
{
#if defined(__arm64__) || defined(__aarch64__)
	__asm__ __volatile__("dsb sy" ::: "memory");
#else
	OSSynchronizeIO();
	__asm__ __volatile__("" ::: "memory");
#endif
}

uint32_t
lkpi_os_mmio_read32(const volatile void *addr)
{
	uint32_t v = *(const volatile uint32_t *)addr;
	lkpi_mmio_wmb();
	return v;
}

void
lkpi_os_mmio_write32(volatile void *addr, uint32_t val)
{
	*(volatile uint32_t *)addr = val;
	lkpi_mmio_wmb();
}

uint16_t
lkpi_os_mmio_read16(const volatile void *addr)
{
	uint16_t v = *(const volatile uint16_t *)addr;
	lkpi_mmio_wmb();
	return v;
}

void
lkpi_os_mmio_write16(volatile void *addr, uint16_t val)
{
	*(volatile uint16_t *)addr = val;
	lkpi_mmio_wmb();
}

uint8_t
lkpi_os_mmio_read8(const volatile void *addr)
{
	uint8_t v = *(const volatile uint8_t *)addr;
	lkpi_mmio_wmb();
	return v;
}

void
lkpi_os_mmio_write8(volatile void *addr, uint8_t val)
{
	*(volatile uint8_t *)addr = val;
	lkpi_mmio_wmb();
}

void
lkpi_os_mmio_barrier(void)
{
	lkpi_mmio_wmb();
}

/* =====================================================================
 * PCI
 * ===================================================================== */

lkpi_os_pci_t
lkpi_os_pci_create(IOPCIDevice *dev, IOWorkLoop *wl, IOService *provider)
{
	struct lkpi_os_pci *p = (struct lkpi_os_pci *)IOMalloc(sizeof(*p));
	if (!p)
		return NULL;
	bzero(p, sizeof(*p));
	p->dev = dev;
	p->wl = wl;
	p->provider = provider;
	p->irq_index = -1;
	p->lock = IOLockAlloc();
	if (!p->lock) {
		IOFree(p, sizeof(*p));
		return NULL;
	}
	return p;
}

void
lkpi_os_pci_destroy(lkpi_os_pci_t p)
{
	if (!p)
		return;
	lkpi_os_pci_irq_disable(p);
	for (int i = 0; i < LKPI_OS_MAX_BARS; i++)
		lkpi_os_pci_unmap_bar(p, i);
	IOLockFree(p->lock);
	IOFree(p, sizeof(*p));
}

void
lkpi_os_pci_get_info(lkpi_os_pci_t p, struct lkpi_os_pci_info *info)
{
	IOPCIDevice *d = p->dev;
	bzero(info, sizeof(*info));
	info->vendor = d->configRead16(kIOPCIConfigVendorID);
	info->device = d->configRead16(kIOPCIConfigDeviceID);
	info->subsystem_vendor = d->configRead16(kIOPCIConfigSubSystemVendorID);
	info->subsystem_device = d->configRead16(kIOPCIConfigSubSystemID);
	info->revision = d->configRead8(kIOPCIConfigRevisionID);
	uint32_t cls = d->configRead32(kIOPCIConfigRevisionID) >> 8;	/* class code is bytes 9..11 */
	info->class_code[0] = (uint8_t)(cls & 0xff);		/* prog-if */
	info->class_code[1] = (uint8_t)((cls >> 8) & 0xff);	/* sub-class */
	info->class_code[2] = (uint8_t)((cls >> 16) & 0xff);	/* base class */
	info->bus = d->getBusNumber();
	info->slot = d->getDeviceNumber();
	info->func = d->getFunctionNumber();
	snprintf(info->name, sizeof(info->name), "0000:%02x:%02x.%u",
	    info->bus, info->slot, info->func);
}

uint32_t
lkpi_os_pci_cfg_read(lkpi_os_pci_t p, uint32_t off, int width)
{
	switch (width) {
	case 1:
		return p->dev->configRead8(off);
	case 2:
		return p->dev->configRead16(off);
	default:
		return p->dev->configRead32(off);
	}
}

void
lkpi_os_pci_cfg_write(lkpi_os_pci_t p, uint32_t off, int width, uint32_t val)
{
	switch (width) {
	case 1:
		p->dev->configWrite8(off, (UInt8)val);
		break;
	case 2:
		p->dev->configWrite16(off, (UInt16)val);
		break;
	default:
		p->dev->configWrite32(off, val);
		break;
	}
}

void *
lkpi_os_pci_map_bar(lkpi_os_pci_t p, int bar, size_t *size)
{
	if (size)
		*size = 0;
	if (bar < 0 || bar >= LKPI_OS_MAX_BARS)
		return NULL;

	IOLockLock(p->lock);
	IOMemoryMap *map = p->bars[bar];
	if (!map) {
		map = p->dev->mapDeviceMemoryWithRegister(
		    (UInt8)(kIOPCIConfigBaseAddress0 + bar * 4), kIOMapInhibitCache);
		p->bars[bar] = map;
	}
	IOLockUnlock(p->lock);
	if (!map) {
		IOLog(LKPI_TAG "BAR%d: mapDeviceMemoryWithRegister failed\n", bar);
		return NULL;
	}
	if (size)
		*size = (size_t)map->getLength();
	return (void *)map->getVirtualAddress();
}

void
lkpi_os_pci_unmap_bar(lkpi_os_pci_t p, int bar)
{
	if (bar < 0 || bar >= LKPI_OS_MAX_BARS)
		return;
	IOLockLock(p->lock);
	IOMemoryMap *map = p->bars[bar];
	p->bars[bar] = NULL;
	IOLockUnlock(p->lock);
	if (map)
		map->release();
}

static void
lkpi_irq_action(OSObject *owner, IOInterruptEventSource *sender, int count)
{
	(void)sender;
	(void)count;
	LKPIEventOwner *o = OSDynamicCast(LKPIEventOwner, owner);
	if (!o)
		return;
	struct lkpi_os_pci *p = (struct lkpi_os_pci *)o->ctx;
	if (!p || !p->irq_fn)
		return;
	/* Runs on the device workloop thread, gate held: the Linux hardirq
	 * contract (no sleeping, spinlocks allowed) is honoured by the C side. */
	p->irq_fn(p->irq_arg);
}

/* Pick the interrupt source index: the first MSI-capable one when asked
 * for MSI, else the legacy INTx source (index 0). */
static int
lkpi_pci_find_irq_index(IOPCIDevice *dev, bool want_msi, bool *is_msi)
{
	int legacy = -1;
	int msi = -1;
	int type = 0;

	for (int i = 0; dev->getInterruptType(i, &type) == kIOReturnSuccess; i++) {
		if (type & kIOInterruptTypePCIMessaged) {
			if (msi < 0)
				msi = i;
		} else if (legacy < 0) {
			legacy = i;
		}
		if (i > 32)
			break;
	}
	if (want_msi && msi >= 0) {
		*is_msi = true;
		return msi;
	}
	*is_msi = false;
	if (legacy >= 0)
		return legacy;
	if (msi >= 0) {	/* MSI only: better than nothing */
		*is_msi = true;
		return msi;
	}
	return -1;
}

int
lkpi_os_pci_irq_enable(lkpi_os_pci_t p, lkpi_os_irq_fn fn, void *arg, bool want_msi)
{
	int ret = 0;

	IOLockLock(p->lock);
	if (p->irq_src) {
		ret = -EBUSY;
		goto out;
	}
	p->irq_index = lkpi_pci_find_irq_index(p->dev, want_msi, &p->irq_msi);
	if (p->irq_index < 0) {
		IOLog(LKPI_TAG "no interrupt source on the device\n");
		ret = -ENODEV;
		goto out;
	}
	p->irq_fn = fn;
	p->irq_arg = arg;
	p->irq_owner = LKPIEventOwner::withContext(p);
	if (!p->irq_owner) {
		ret = -ENOMEM;
		goto out;
	}
	/* A plain IOInterruptEventSource (not the filter variant): the
	 * primary interrupt is acknowledged by IOKit and the handler runs on
	 * the workloop thread, where IOSimpleLocks are fine and sleeping is not. */
	p->irq_src = IOInterruptEventSource::interruptEventSource(p->irq_owner,
	    lkpi_irq_action, p->dev, p->irq_index);
	if (!p->irq_src) {
		IOLog(LKPI_TAG "interruptEventSource(index %d) failed\n", p->irq_index);
		ret = -ENODEV;
		goto out;
	}
	if (p->wl->addEventSource(p->irq_src) != kIOReturnSuccess) {
		ret = -EIO;
		goto out;
	}
	p->irq_src->enable();
	p->irq_enabled = true;
	IOLog(LKPI_TAG "IRQ enabled (source %d, %s)\n", p->irq_index, p->irq_msi ? "MSI" : "INTx");
out:
	if (ret) {
		if (p->irq_src) {
			p->irq_src->release();
			p->irq_src = NULL;
		}
		if (p->irq_owner) {
			p->irq_owner->release();
			p->irq_owner = NULL;
		}
		p->irq_fn = NULL;
		p->irq_arg = NULL;
		p->irq_index = -1;
	}
	IOLockUnlock(p->lock);
	return ret;
}

void
lkpi_os_pci_irq_disable(lkpi_os_pci_t p)
{
	IOLockLock(p->lock);
	IOInterruptEventSource *src = p->irq_src;
	LKPIEventOwner *owner = (LKPIEventOwner *)p->irq_owner;
	p->irq_src = NULL;
	p->irq_owner = NULL;
	p->irq_enabled = false;
	IOLockUnlock(p->lock);

	if (src) {
		src->disable();	/* IOInterruptEventSource::disable() waits for a running handler */
		p->wl->removeEventSource(src);
		src->release();
	}
	/* Clear the callback only after the handler can no longer run. */
	IOLockLock(p->lock);
	p->irq_fn = NULL;
	p->irq_arg = NULL;
	p->irq_index = -1;
	IOLockUnlock(p->lock);
	if (owner) {
		owner->ctx = NULL;
		owner->release();
	}
}

static IOReturn
lkpi_noop_action(OSObject *target, void *a0, void *a1, void *a2, void *a3)
{
	(void)target; (void)a0; (void)a1; (void)a2; (void)a3;
	return kIOReturnSuccess;
}

void
lkpi_os_pci_irq_sync(lkpi_os_pci_t p)
{
	/* The workloop runs one event source at a time with its gate closed.
	 * From the workloop thread itself there is nothing to wait for; from
	 * anywhere else, running a no-op action through the gate blocks until
	 * the in-flight handler (if any) has returned. */
	if (!p->wl || p->wl->inGate())
		return;
	(void)p->wl->runAction(lkpi_noop_action, p->provider);
}

void
lkpi_os_pci_set_bus_master(lkpi_os_pci_t p, bool on)
{
	p->dev->setBusMasterEnable(on);
}

void
lkpi_os_pci_set_priv(lkpi_os_pci_t p, void *priv)
{
	p->priv = priv;
}

void *
lkpi_os_pci_get_priv(lkpi_os_pci_t p)
{
	return p->priv;
}

void *
lkpi_os_pci_provider(lkpi_os_pci_t p)
{
	return p->provider;
}

/* =====================================================================
 * DMA
 * ===================================================================== */

struct lkpi_dma_rec {
	uint32_t magic;
	bool coherent;
	IOMemoryDescriptor *md;		/* IOBufferMemoryDescriptor for coherent */
	IODMACommand *dma;
	lkpi_os_dma_addr_t bus;
	size_t len;
	void *va;
};
#define LKPI_DMA_MAGIC 0x444D4131u

static inline UInt8
lkpi_dma_address_bits(uint64_t mask)
{
	if (mask == 0 || mask == ~0ULL)
		return 64;
	int bits = 64 - __builtin_clzll(mask);
	if (bits < 32)
		bits = 32;
	return (UInt8)bits;
}

static inline IOOptionBits
lkpi_dma_direction(int dir)
{
	switch (dir) {
	case LKPI_DMA_TO_DEVICE:
		return kIODirectionOut;
	case LKPI_DMA_FROM_DEVICE:
		return kIODirectionIn;
	default:
		return kIODirectionInOut;
	}
}

/*
 * Build an IODMACommand for `md` and return the single bus segment that
 * covers [0, len).  Fails if the mapper/physical layout needs more than one
 * segment (Linux's dma_map_single contract is one contiguous bus range).
 */
static IODMACommand *
lkpi_dma_prepare(IOMemoryDescriptor *md, size_t len, uint64_t mask, lkpi_os_dma_addr_t *bus_out)
{
	IODMACommand *dma = IODMACommand::withSpecification(kIODMACommandOutputHost64,
	    lkpi_dma_address_bits(mask), 0 /* no max segment */, IODMACommand::kMapped,
	    0 /* no max transfer */, 1 /* alignment */, NULL /* system mapper */, NULL);
	if (!dma)
		return NULL;
	if (dma->setMemoryDescriptor(md, true /* autoPrepare */) != kIOReturnSuccess) {
		dma->release();
		return NULL;
	}
	UInt64 offset = 0;
	IODMACommand::Segment64 seg[2];
	UInt32 nseg = 2;
	bzero(seg, sizeof(seg));
	if (dma->gen64IOVMSegments(&offset, seg, &nseg) != kIOReturnSuccess ||
	    nseg != 1 || seg[0].fLength < len || seg[0].fIOVMAddr == 0) {
		if (nseg > 1)
			IOLog(LKPI_TAG "DMA: buffer of %zu bytes is not contiguous on the bus "
			    "(%u segments); enable the IOMMU/mapper or shrink the request\n",
			    len, nseg);
		dma->clearMemoryDescriptor(true);
		dma->release();
		return NULL;
	}
	*bus_out = seg[0].fIOVMAddr;
	return dma;
}

static void
lkpi_dma_release(struct lkpi_dma_rec *rec)
{
	if (rec->dma) {
		rec->dma->clearMemoryDescriptor(true);	/* also completes the md */
		rec->dma->release();
	}
	if (rec->md) {
		rec->md->complete();	/* balances our own prepare(); autoPrepare's is undone above */
		rec->md->release();
	}
	rec->magic = 0;
	IOFree(rec, sizeof(*rec));
}

void *
lkpi_os_dma_alloc_coherent(lkpi_os_pci_t p, size_t size, uint64_t mask, lkpi_os_dma_addr_t *bus_addr)
{
	(void)p;
	if (bus_addr)
		*bus_addr = 0;
	if (size == 0)
		return NULL;

	IOOptionBits opts = kIODirectionInOut | kIOMemoryPhysicallyContiguous;
#if LKPI_OS_COHERENT_INHIBIT_CACHE
	opts |= kIOMapInhibitCache;
#endif
	/* dma_alloc_coherent() returns page-aligned memory; the low zero bits
	 * of the physical mask request that alignment. */
	uint64_t pmask = (mask ? mask : ~0ULL) & ~((uint64_t)PAGE_SIZE - 1);
	IOBufferMemoryDescriptor *md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
	    kernel_task, opts, size, pmask);
	if (!md) {
		IOLog(LKPI_TAG "dma_alloc_coherent(%zu, mask %#llx) failed\n", size, (unsigned long long)mask);
		return NULL;
	}
	if (md->prepare() != kIOReturnSuccess) {
		md->release();
		return NULL;
	}
	struct lkpi_dma_rec *rec = (struct lkpi_dma_rec *)IOMalloc(sizeof(*rec));
	if (!rec) {
		md->complete();
		md->release();
		return NULL;
	}
	bzero(rec, sizeof(*rec));
	rec->magic = LKPI_DMA_MAGIC;
	rec->coherent = true;
	rec->md = md;
	rec->len = size;
	rec->va = md->getBytesNoCopy();
	rec->dma = lkpi_dma_prepare(md, size, mask, &rec->bus);
	if (!rec->dma || !rec->va) {
		rec->dma = NULL;
		lkpi_dma_release(rec);
		return NULL;
	}
	if (!lkpi_reg_insert(&lkpi_dma_reg, rec->bus, rec)) {
		IOLog(LKPI_TAG "dma_alloc_coherent: duplicate bus address %#llx\n", (unsigned long long)rec->bus);
		lkpi_dma_release(rec);
		return NULL;
	}
	bzero(rec->va, size);
	if (bus_addr)
		*bus_addr = rec->bus;
	return rec->va;
}

void
lkpi_os_dma_free_coherent(lkpi_os_pci_t p, size_t size, void *va, lkpi_os_dma_addr_t bus_addr)
{
	(void)p;
	(void)size;
	(void)va;
	struct lkpi_dma_rec *rec = (struct lkpi_dma_rec *)lkpi_reg_remove(&lkpi_dma_reg, bus_addr);
	if (!rec || rec->magic != LKPI_DMA_MAGIC || !rec->coherent) {
		IOLog(LKPI_TAG "dma_free_coherent: unknown bus address %#llx\n", (unsigned long long)bus_addr);
		return;
	}
	lkpi_dma_release(rec);
}

lkpi_os_dma_addr_t
lkpi_os_dma_map(lkpi_os_pci_t p, void *va, size_t len, int dir, uint64_t mask)
{
	(void)p;
	if (!va || len == 0)
		return 0;

	IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
	    (mach_vm_address_t)(uintptr_t)va, len, lkpi_dma_direction(dir), kernel_task);
	if (!md)
		return 0;
	if (md->prepare() != kIOReturnSuccess) {
		md->release();
		return 0;
	}
	struct lkpi_dma_rec *rec = (struct lkpi_dma_rec *)IOMalloc(sizeof(*rec));
	if (!rec) {
		md->complete();
		md->release();
		return 0;
	}
	bzero(rec, sizeof(*rec));
	rec->magic = LKPI_DMA_MAGIC;
	rec->coherent = false;
	rec->md = md;
	rec->len = len;
	rec->va = va;
	rec->dma = lkpi_dma_prepare(md, len, mask, &rec->bus);
	if (!rec->dma) {
		lkpi_dma_release(rec);
		return 0;
	}
	/* Bounce buffers (address-limited, no IOMMU) are filled by prepare();
	 * make sure the device sees the CPU's data for outbound mappings. */
	if (dir == LKPI_DMA_TO_DEVICE || dir == LKPI_DMA_BIDIRECTIONAL)
		(void)rec->dma->synchronize(kIODirectionOut);
	if (!lkpi_reg_insert(&lkpi_dma_reg, rec->bus, rec)) {
		IOLog(LKPI_TAG "dma_map: duplicate bus address %#llx\n", (unsigned long long)rec->bus);
		lkpi_dma_release(rec);
		return 0;
	}
	return rec->bus;
}

void
lkpi_os_dma_unmap(lkpi_os_pci_t p, lkpi_os_dma_addr_t bus, size_t len, int dir)
{
	(void)p;
	(void)len;
	struct lkpi_dma_rec *rec = (struct lkpi_dma_rec *)lkpi_reg_remove(&lkpi_dma_reg, bus);
	if (!rec || rec->magic != LKPI_DMA_MAGIC || rec->coherent) {
		IOLog(LKPI_TAG "dma_unmap: unknown bus address %#llx\n", (unsigned long long)bus);
		return;
	}
	/* Copy a bounce buffer back before tearing the mapping down. */
	if (dir == LKPI_DMA_FROM_DEVICE || dir == LKPI_DMA_BIDIRECTIONAL)
		(void)rec->dma->synchronize(kIODirectionIn);
	lkpi_dma_release(rec);
}

void
lkpi_os_dma_sync(lkpi_os_pci_t p, lkpi_os_dma_addr_t bus, size_t len, int dir, bool for_cpu)
{
	(void)p;
	(void)len;
	(void)dir;
	struct lkpi_dma_rec *rec = (struct lkpi_dma_rec *)lkpi_reg_lookup(&lkpi_dma_reg, bus);
	if (!rec || !rec->dma)
		return;
	/* On x86 this is a no-op unless IODMACommand had to double-buffer; on
	 * arm64 it also performs the cache maintenance. Called unconditionally. */
	(void)rec->dma->synchronize(for_cpu ? kIODirectionIn : kIODirectionOut);
}

/* =====================================================================
 * Firmware
 * ===================================================================== */

struct lkpi_fw_req {
	IOLock *lk;
	bool done;
	OSReturn result;
	void *data;	/* lkpi_os_malloc'd copy */
	size_t size;
};

static void
lkpi_fw_callback(OSKextRequestTag tag, OSReturn result, const void *data, uint32_t len, void *context)
{
	(void)tag;
	struct lkpi_fw_req *req = (struct lkpi_fw_req *)context;

	IOLockLock(req->lk);
	req->result = result;
	if (result == kOSReturnSuccess && data && len) {
		req->data = lkpi_os_malloc(len, false, true);
		if (req->data) {
			memcpy(req->data, data, len);
			req->size = len;
		} else {
			req->result = kOSKextReturnNoMemory;
		}
	} else if (result == kOSReturnSuccess) {
		req->result = kOSKextReturnNoResources;
	}
	req->done = true;
	IOLockWakeup(req->lk, &req->done, false);
	IOLockUnlock(req->lk);
}

static int
lkpi_fw_osreturn_to_errno(OSReturn r)
{
	switch (r) {
	case kOSReturnSuccess:
		return 0;
	case kOSKextReturnNoResources:
	case kOSKextReturnNotFound:
		return -ENOENT;
	case kOSKextReturnNoMemory:
		return -ENOMEM;
	case kOSKextReturnTimeout:
		return -ETIMEDOUT;
	case kOSKextReturnInvalidArgument:
		return -EINVAL;
	default:
		return -EIO;
	}
}

/* One attempt through kernelmanagerd/kextd for a given resource name. */
static int
lkpi_fw_request_one(const char *kext_id, const char *name, void **data, size_t *size)
{
	struct lkpi_fw_req req;
	bzero(&req, sizeof(req));
	req.lk = IOLockAlloc();
	if (!req.lk)
		return -ENOMEM;

	OSKextRequestTag tag = kOSKextRequestTagInvalid;
	OSReturn r = OSKextRequestResource(kext_id, name, lkpi_fw_callback, &req, &tag);
	if (r != kOSReturnSuccess) {
		IOLockFree(req.lk);
		return lkpi_fw_osreturn_to_errno(r);
	}

	/* The user-space side has its own timeout; add a generous local one in
	 * case the daemon never answers (early boot, SIP consent pending). */
	IOLockLock(req.lk);
	while (!req.done) {
		int sl = IOLockSleepDeadline(req.lk, &req.done,
		    lkpi_deadline_from_now_ns(60ULL * 1000 * 1000 * 1000), THREAD_UNINT);
		if (sl == THREAD_TIMED_OUT && !req.done) {
			IOLockUnlock(req.lk);
			void *ctx = NULL;
			if (OSKextCancelRequest(tag, &ctx) == kOSReturnSuccess) {
				/* cancelled before the callback ran */
				IOLockFree(req.lk);
				return -ETIMEDOUT;
			}
			/* callback is running or already ran; wait for it */
			IOLockLock(req.lk);
		}
	}
	IOLockUnlock(req.lk);
	IOLockFree(req.lk);

	if (req.result != kOSReturnSuccess) {
		if (req.data)
			lkpi_os_free(req.data);
		return lkpi_fw_osreturn_to_errno(req.result);
	}
	*data = req.data;
	*size = req.size;
	return 0;
}

static const char *
lkpi_basename(const char *name)
{
	const char *slash = strrchr(name, '/');
	return slash ? slash + 1 : name;
}

int
lkpi_os_firmware_load(const char *name, void **data, size_t *size)
{
	if (!name || !data || !size)
		return -EINVAL;
	*data = NULL;
	*size = 0;

	/* 1. Blobs linked into the kext (EMBED_FIRMWARE=1).  This is the path
	 *    that works for OpenCore-injected kexts, which have no bundle on
	 *    disk for kernelmanagerd to serve resources from. */
	for (unsigned i = 0; i < lkpi_fw_blob_count; i++) {
		const struct lkpi_fw_blob *b = &lkpi_fw_blobs[i];
		if (strcmp(b->name, name) != 0 && strcmp(lkpi_basename(b->name), lkpi_basename(name)) != 0)
			continue;
		size_t len = (size_t)(b->end - b->start);
		void *copy = lkpi_os_malloc(len, false, true);
		if (!copy)
			return -ENOMEM;
		memcpy(copy, b->start, len);
		*data = copy;
		*size = len;
		IOLog(LKPI_TAG "firmware %s: %zu bytes (embedded)\n", name, len);
		return 0;
	}

	/* 2. Contents/Resources/<name> through OSKextRequestResource.  The
	 *    resource name is passed to CFBundleCopyResourceURL in user space;
	 *    a path with directories may or may not resolve depending on the
	 *    OS version, so the Makefile installs each blob twice (full
	 *    relative path and flat basename) and we try both spellings. */
	const char *kext_id = OSKextGetCurrentIdentifier();
	if (!kext_id)
		return -ENOENT;
	int ret = lkpi_fw_request_one(kext_id, name, data, size);
	if (ret == -ENOENT || ret == -EINVAL) {
		const char *base = lkpi_basename(name);
		if (base != name)
			ret = lkpi_fw_request_one(kext_id, base, data, size);
	}
	if (ret == 0)
		IOLog(LKPI_TAG "firmware %s: %zu bytes (bundle resource)\n", name, *size);
	else
		IOLog(LKPI_TAG "firmware %s: not available (%d)\n", name, ret);
	return ret;
}

/* =====================================================================
 * Randomness, CPU
 * ===================================================================== */

void
lkpi_os_random_bytes(void *buf, size_t len)
{
	while (len) {
		u_int chunk = len > 0x7fffffffu ? 0x7fffffffu : (u_int)len;
		read_random(buf, chunk);
		buf = (char *)buf + chunk;
		len -= chunk;
	}
}

int
lkpi_os_cpu_count(void)
{
	/* Only used as a sizing hint by the C layer (number of NAPI threads,
	 * per-cpu counters).  There is no supported KPI for the CPU count that
	 * is present on every macOS 11..15 SDK, so report one CPU. */
	return 1;
}

int
lkpi_os_cpu_id(void)
{
#if LKPI_OS_HAVE_CPU_NUMBER
	return cpu_number();
#else
	return 0;
#endif
}

/* =====================================================================
 * Backend start/stop
 * ===================================================================== */

static int lkpi_os_users;

/* Called from the first MT7925PCIe::start(); not thread-safe against
 * itself, which is fine because IOKit serialises start() of one driver's
 * personalities through the provider's workloop... except across devices,
 * so a lock is used anyway. */
static IOLock *
lkpi_os_users_lock_get(void)
{
	/* Allocated once; the kext's static constructors do not run before
	 * IOLockAlloc is usable, so do it lazily under an atomic swap. */
	static IOLock *volatile lk;
	if (!lk) {
		IOLock *nw = IOLockAlloc();
		if (nw && !OSCompareAndSwapPtr(NULL, nw, (void *volatile *)&lk))
			IOLockFree(nw);
	}
	return lk;
}

int
lkpi_os_start(void)
{
	int ret = 0;
	IOLock *ulk = lkpi_os_users_lock_get();
	if (!ulk)
		return -ENOMEM;
	IOLockLock(ulk);
	if (lkpi_os_users++ > 0)
		goto out;

	lkpi_timebase_init();
	(void)PE_parse_boot_argn("mt7925_debug", &lkpi_debug_level, sizeof(lkpi_debug_level));

	lkpi_thread_lock = IOSimpleLockAlloc();
	if (!lkpi_thread_lock) {
		ret = -ENOMEM;
		goto fail;
	}
	bzero(lkpi_thread_table, sizeof(lkpi_thread_table));
	if (!lkpi_reg_init(&lkpi_pages_reg) || !lkpi_reg_init(&lkpi_dma_reg)) {
		ret = -ENOMEM;
		goto fail;
	}
	lkpi_timer_wl = IOWorkLoop::workLoop();
	if (!lkpi_timer_wl) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = lkpi_jiffies_start();
	if (ret)
		goto fail;
	IOLog(LKPI_TAG "OS backend started (page size %lu, debug %d)\n",
	    (unsigned long)PAGE_SIZE, lkpi_debug_level);
	goto out;

fail:
	lkpi_jiffies_stop();
	if (lkpi_timer_wl) {
		lkpi_timer_wl->release();
		lkpi_timer_wl = NULL;
	}
	lkpi_reg_fini(&lkpi_pages_reg);
	lkpi_reg_fini(&lkpi_dma_reg);
	if (lkpi_thread_lock) {
		IOSimpleLockFree(lkpi_thread_lock);
		lkpi_thread_lock = NULL;
	}
	lkpi_os_users = 0;
out:
	IOLockUnlock(ulk);
	return ret;
}

void
lkpi_os_stop(void)
{
	IOLock *ulk = lkpi_os_users_lock_get();
	if (!ulk)
		return;
	IOLockLock(ulk);
	if (lkpi_os_users == 0 || --lkpi_os_users > 0) {
		IOLockUnlock(ulk);
		return;
	}
	lkpi_jiffies_stop();
	if (lkpi_timer_wl) {
		lkpi_timer_wl->release();
		lkpi_timer_wl = NULL;
	}
	/* Foreign thread wrappers are the only entries that may remain. */
	for (unsigned b = 0; b < LKPI_THREAD_BUCKETS; b++) {
		struct lkpi_os_thread *c = lkpi_thread_table[b];
		while (c) {
			struct lkpi_os_thread *n = c->hnext;
			if (c->foreign)
				lkpi_thread_destroy(c);
			c = n;
		}
		lkpi_thread_table[b] = NULL;
	}
	lkpi_reg_fini(&lkpi_pages_reg);
	lkpi_reg_fini(&lkpi_dma_reg);
	if (lkpi_thread_lock) {
		IOSimpleLockFree(lkpi_thread_lock);
		lkpi_thread_lock = NULL;
	}
	IOLockUnlock(ulk);
}
