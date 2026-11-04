// SPDX-License-Identifier: MIT
/*
 * libc / pthreads implementation of lkpi/os.h.
 *
 * This lets the whole C half of the LinuxKPI layer (and, through it, parts
 * of the vendored driver) run as an ordinary user-space program on Linux or
 * macOS for unit tests.  It mirrors the IOKit backend's contracts:
 * lost-wakeup-free thread wait/wake, one-shot timers on a timer thread,
 * lazily created locks, a size header in front of every allocation.
 *
 * PCI/MMIO/DMA are backed by an in-memory fake device that tests program
 * through lkpi_host_pci_* (see os_host.h).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <lkpi/os.h>
#include "os_host.h"

/* ------------------------------------------------------------------ util */
static uint64_t ns_of(clockid_t id)
{
	struct timespec ts;
	clock_gettime(id, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void deadline_from_ns(struct timespec *ts, uint64_t abs_ns_monotonic)
{
	/* pthread_cond_timedwait wants CLOCK_REALTIME unless a cond clock was set;
	 * we set CLOCK_MONOTONIC on every condvar we create. */
	ts->tv_sec = (time_t)(abs_ns_monotonic / 1000000000ULL);
	ts->tv_nsec = (long)(abs_ns_monotonic % 1000000000ULL);
}

static void cond_init_mono(pthread_cond_t *c)
{
	pthread_condattr_t a;
	pthread_condattr_init(&a);
#if !defined(__APPLE__)
	pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
#endif
	pthread_cond_init(c, &a);
	pthread_condattr_destroy(&a);
}

static int cond_wait_deadline(pthread_cond_t *c, pthread_mutex_t *m, uint64_t deadline_ns)
{
	struct timespec ts;
	if (deadline_ns == 0)
		return pthread_cond_wait(c, m);
#if defined(__APPLE__)
	{
		/* macOS condvars use CLOCK_REALTIME; convert the monotonic deadline. */
		uint64_t now_m = ns_of(CLOCK_MONOTONIC), now_r = ns_of(CLOCK_REALTIME);
		uint64_t rel = deadline_ns > now_m ? deadline_ns - now_m : 0;
		deadline_from_ns(&ts, now_r + rel);
	}
#else
	deadline_from_ns(&ts, deadline_ns);
#endif
	return pthread_cond_timedwait(c, m, &ts);
}

/* ---------------------------------------------------------------- memory */
struct hdr {
	size_t size;
	uint64_t magic;
};
#define HDR_MAGIC 0x4c4b50494d454d5fULL

void *lkpi_os_malloc(size_t size, bool zero, bool may_sleep)
{
	struct hdr *h;
	(void)may_sleep;
	h = malloc(sizeof(*h) + size);
	if (!h)
		return NULL;
	h->size = size;
	h->magic = HDR_MAGIC;
	if (zero)
		memset(h + 1, 0, size);
	return h + 1;
}

void lkpi_os_free(void *ptr)
{
	struct hdr *h;
	if (!ptr)
		return;
	h = (struct hdr *)ptr - 1;
	if (h->magic != HDR_MAGIC) {
		fprintf(stderr, "lkpi_os_free: bad header for %p\n", ptr);
		abort();
	}
	h->magic = 0;
	free(h);
}

size_t lkpi_os_malloc_size(const void *ptr)
{
	const struct hdr *h = (const struct hdr *)ptr - 1;
	return h->size;
}

void *lkpi_os_alloc_pages(size_t size, bool zero)
{
	void *p = NULL;
	size = (size + 4095) & ~(size_t)4095;
	if (posix_memalign(&p, 4096, size))
		return NULL;
	if (zero)
		memset(p, 0, size);
	return p;
}

void lkpi_os_free_pages(void *ptr, size_t size)
{
	(void)size;
	free(ptr);
}

size_t lkpi_os_page_size(void) { return 4096; }

/* --------------------------------------------------------------- logging */
static const char *const level_names[] = { "EMERG", "ERR", "WARN", "NOTICE", "INFO", "DEBUG" };
int lkpi_host_log_level = LKPI_LOG_INFO;

void lkpi_os_vlog(int level, const char *fmt, va_list ap)
{
	if (level > lkpi_host_log_level)
		return;
	flockfile(stderr);
	fprintf(stderr, "[%s] ", level_names[level < 6 ? level : 5]);
	vfprintf(stderr, fmt, ap);
	if (fmt[0] && fmt[strlen(fmt) - 1] != '\n')
		fputc('\n', stderr);
	funlockfile(stderr);
}

void lkpi_os_log(int level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	lkpi_os_vlog(level, fmt, ap);
	va_end(ap);
}

void lkpi_os_panic(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "PANIC: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	abort();
}

void lkpi_os_dump_stack(void) { fprintf(stderr, "  (stack dump not available on host)\n"); }

/* ------------------------------------------------------------------ time */
uint64_t lkpi_os_uptime_ns(void) { return ns_of(CLOCK_MONOTONIC); }
uint64_t lkpi_os_boottime_ns(void)
{
#if defined(CLOCK_BOOTTIME)
	return ns_of(CLOCK_BOOTTIME);
#else
	return ns_of(CLOCK_MONOTONIC);
#endif
}
uint64_t lkpi_os_realtime_ns(void) { return ns_of(CLOCK_REALTIME); }

void lkpi_os_delay_us(uint32_t us)
{
	uint64_t end = lkpi_os_uptime_ns() + (uint64_t)us * 1000;
	while (lkpi_os_uptime_ns() < end)
		sched_yield();
}

void lkpi_os_sleep_ms(uint32_t ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
	while (nanosleep(&ts, &ts) && errno == EINTR)
		;
}

bool lkpi_os_may_sleep(void) { return true; }

/* ----------------------------------------------------------------- locks */
struct lkpi_os_spinlock {
	pthread_mutex_t m;
};
struct lkpi_os_mutex {
	pthread_mutex_t m;
	pthread_cond_t c;
	pthread_t owner;
	bool locked;
};

lkpi_os_spinlock_t lkpi_os_spinlock_alloc(void)
{
	lkpi_os_spinlock_t l = calloc(1, sizeof(*l));
	if (l)
		pthread_mutex_init(&l->m, NULL);
	return l;
}
void lkpi_os_spinlock_free(lkpi_os_spinlock_t l) { pthread_mutex_destroy(&l->m); free(l); }
void lkpi_os_spin_lock(lkpi_os_spinlock_t l) { pthread_mutex_lock(&l->m); }
bool lkpi_os_spin_trylock(lkpi_os_spinlock_t l) { return pthread_mutex_trylock(&l->m) == 0; }
void lkpi_os_spin_unlock(lkpi_os_spinlock_t l) { pthread_mutex_unlock(&l->m); }
lkpi_os_irqstate_t lkpi_os_spin_lock_irq(lkpi_os_spinlock_t l) { pthread_mutex_lock(&l->m); return 1; }
void lkpi_os_spin_unlock_irq(lkpi_os_spinlock_t l, lkpi_os_irqstate_t s) { (void)s; pthread_mutex_unlock(&l->m); }

lkpi_os_mutex_t lkpi_os_mutex_alloc(void)
{
	lkpi_os_mutex_t m = calloc(1, sizeof(*m));
	if (m) {
		pthread_mutex_init(&m->m, NULL);
		cond_init_mono(&m->c);
	}
	return m;
}
void lkpi_os_mutex_free(lkpi_os_mutex_t m)
{
	pthread_cond_destroy(&m->c);
	pthread_mutex_destroy(&m->m);
	free(m);
}
void lkpi_os_mutex_lock(lkpi_os_mutex_t m)
{
	pthread_mutex_lock(&m->m);
	m->owner = pthread_self();
	m->locked = true;
}
bool lkpi_os_mutex_trylock(lkpi_os_mutex_t m)
{
	if (pthread_mutex_trylock(&m->m))
		return false;
	m->owner = pthread_self();
	m->locked = true;
	return true;
}
void lkpi_os_mutex_unlock(lkpi_os_mutex_t m)
{
	m->locked = false;
	pthread_mutex_unlock(&m->m);
}
bool lkpi_os_mutex_is_locked(lkpi_os_mutex_t m) { return m->locked; }

bool lkpi_os_mutex_sleep(lkpi_os_mutex_t m, const void *event, uint64_t timeout_ns)
{
	int rc;
	(void)event;	/* one condvar per mutex; callers re-check their condition */
	m->locked = false;
	rc = cond_wait_deadline(&m->c, &m->m, timeout_ns ? lkpi_os_uptime_ns() + timeout_ns : 0);
	m->owner = pthread_self();
	m->locked = true;
	return rc != ETIMEDOUT;
}

void lkpi_os_wakeup(lkpi_os_mutex_t m, const void *event, bool one)
{
	(void)event;
	if (one)
		pthread_cond_signal(&m->c);
	else
		pthread_cond_broadcast(&m->c);
}

/* --------------------------------------------------------------- threads */
struct lkpi_os_thread {
	pthread_t t;
	pthread_mutex_t m;
	pthread_cond_t c;
	bool pending_wake;
	bool exited;
	bool detached_wrapper;	/* foreign thread, not created by us */
	int refs;
	void *slot;
	lkpi_os_thread_fn fn;
	void *arg;
	char name[32];
};

static pthread_key_t self_key;
static pthread_once_t self_key_once = PTHREAD_ONCE_INIT;

static void self_key_create(void) { pthread_key_create(&self_key, NULL); }

static struct lkpi_os_thread *thread_new(const char *name)
{
	struct lkpi_os_thread *th = calloc(1, sizeof(*th));
	if (!th)
		return NULL;
	pthread_mutex_init(&th->m, NULL);
	cond_init_mono(&th->c);
	th->refs = 2;	/* one for the creator, one for the thread itself */
	snprintf(th->name, sizeof(th->name), "%s", name ? name : "lkpi");
	return th;
}

static void thread_put(struct lkpi_os_thread *th)
{
	if (__atomic_sub_fetch(&th->refs, 1, __ATOMIC_SEQ_CST) == 0) {
		pthread_cond_destroy(&th->c);
		pthread_mutex_destroy(&th->m);
		free(th);
	}
}

static void *thread_trampoline(void *p)
{
	struct lkpi_os_thread *th = p;
	pthread_once(&self_key_once, self_key_create);
	pthread_setspecific(self_key, th);
#if defined(__linux__)
	pthread_setname_np(pthread_self(), th->name);
#endif
	th->fn(th->arg);
	lkpi_os_thread_exit();
	return NULL;
}

lkpi_os_thread_t lkpi_os_thread_create(lkpi_os_thread_fn fn, void *arg, const char *name)
{
	struct lkpi_os_thread *th = thread_new(name);
	pthread_attr_t a;
	if (!th)
		return NULL;
	th->fn = fn;
	th->arg = arg;
	pthread_attr_init(&a);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&th->t, &a, thread_trampoline, th)) {
		pthread_attr_destroy(&a);
		th->refs = 1;
		thread_put(th);
		return NULL;
	}
	pthread_attr_destroy(&a);
	return th;
}

void lkpi_os_thread_exit(void)
{
	struct lkpi_os_thread *th = lkpi_os_thread_self();
	pthread_mutex_lock(&th->m);
	th->exited = true;
	pthread_cond_broadcast(&th->c);
	pthread_mutex_unlock(&th->m);
	thread_put(th);
	pthread_exit(NULL);
}

void lkpi_os_thread_set_realtime(lkpi_os_thread_t th, bool rt) { (void)th; (void)rt; }

lkpi_os_thread_t lkpi_os_thread_self(void)
{
	struct lkpi_os_thread *th;
	pthread_once(&self_key_once, self_key_create);
	th = pthread_getspecific(self_key);
	if (!th) {
		th = thread_new("foreign");
		th->t = pthread_self();
		th->detached_wrapper = true;
		th->refs = 1;
		pthread_setspecific(self_key, th);
	}
	return th;
}

void lkpi_os_thread_yield(void) { sched_yield(); }

bool lkpi_os_thread_wait(lkpi_os_thread_t th, uint64_t timeout_ns)
{
	uint64_t deadline = timeout_ns ? lkpi_os_uptime_ns() + timeout_ns : 0;
	bool woke = true;
	pthread_mutex_lock(&th->m);
	while (!th->pending_wake) {
		if (cond_wait_deadline(&th->c, &th->m, deadline) == ETIMEDOUT) {
			woke = th->pending_wake;
			break;
		}
	}
	th->pending_wake = false;
	pthread_mutex_unlock(&th->m);
	return woke;
}

void lkpi_os_thread_wake(lkpi_os_thread_t th)
{
	pthread_mutex_lock(&th->m);
	th->pending_wake = true;
	pthread_cond_broadcast(&th->c);
	pthread_mutex_unlock(&th->m);
}

void lkpi_os_thread_release(lkpi_os_thread_t th)
{
	/* Join semantics for tests: wait for exit before dropping our reference. */
	if (!th->detached_wrapper) {
		pthread_mutex_lock(&th->m);
		while (!th->exited)
			pthread_cond_wait(&th->c, &th->m);
		pthread_mutex_unlock(&th->m);
	}
	thread_put(th);
}

void *lkpi_os_thread_get_slot(void) { return lkpi_os_thread_self()->slot; }
void lkpi_os_thread_set_slot(void *p) { lkpi_os_thread_self()->slot = p; }

/* ---------------------------------------------------------------- timers */
struct lkpi_os_timer {
	lkpi_os_timer_fn fn;
	void *arg;
	uint64_t deadline;	/* 0 = not armed */
	bool running;
	bool destroyed;
	struct lkpi_os_timer *next;
};

static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t timer_cond;
static struct lkpi_os_timer *timer_list;
static pthread_t timer_thread;
static bool timer_thread_started;
static lkpi_os_thread_t timer_thread_wrapper;

static void *timer_thread_fn(void *unused)
{
	(void)unused;
	pthread_once(&self_key_once, self_key_create);
	timer_thread_wrapper = lkpi_os_thread_self();
	pthread_mutex_lock(&timer_lock);
	for (;;) {
		struct lkpi_os_timer *t, *best = NULL;
		uint64_t now = lkpi_os_uptime_ns();
		for (t = timer_list; t; t = t->next)
			if (t->deadline && (!best || t->deadline < best->deadline))
				best = t;
		if (!best) {
			pthread_cond_wait(&timer_cond, &timer_lock);
			continue;
		}
		if (best->deadline > now) {
			cond_wait_deadline(&timer_cond, &timer_lock, best->deadline);
			continue;
		}
		best->deadline = 0;
		best->running = true;
		pthread_mutex_unlock(&timer_lock);
		best->fn(best->arg);
		pthread_mutex_lock(&timer_lock);
		best->running = false;
		pthread_cond_broadcast(&timer_cond);
	}
	return NULL;
}

static void timer_thread_ensure(void)
{
	if (!timer_thread_started) {
		cond_init_mono(&timer_cond);
		pthread_create(&timer_thread, NULL, timer_thread_fn, NULL);
		timer_thread_started = true;
	}
}

lkpi_os_timer_t lkpi_os_timer_create(lkpi_os_timer_fn fn, void *arg)
{
	struct lkpi_os_timer *t = calloc(1, sizeof(*t));
	if (!t)
		return NULL;
	t->fn = fn;
	t->arg = arg;
	pthread_mutex_lock(&timer_lock);
	timer_thread_ensure();
	t->next = timer_list;
	timer_list = t;
	pthread_mutex_unlock(&timer_lock);
	return t;
}

void lkpi_os_timer_arm(lkpi_os_timer_t t, uint64_t deadline)
{
	pthread_mutex_lock(&timer_lock);
	t->deadline = deadline ? deadline : 1;
	pthread_cond_broadcast(&timer_cond);
	pthread_mutex_unlock(&timer_lock);
}

bool lkpi_os_timer_cancel(lkpi_os_timer_t t)
{
	bool was;
	pthread_mutex_lock(&timer_lock);
	was = t->deadline != 0;
	t->deadline = 0;
	pthread_mutex_unlock(&timer_lock);
	return was;
}

bool lkpi_os_timer_cancel_sync(lkpi_os_timer_t t)
{
	bool was;
	pthread_mutex_lock(&timer_lock);
	was = t->deadline != 0;
	t->deadline = 0;
	while (t->running && lkpi_os_thread_self() != timer_thread_wrapper)
		pthread_cond_wait(&timer_cond, &timer_lock);
	pthread_mutex_unlock(&timer_lock);
	return was;
}

void lkpi_os_timer_destroy(lkpi_os_timer_t t)
{
	struct lkpi_os_timer **pp;
	lkpi_os_timer_cancel_sync(t);
	pthread_mutex_lock(&timer_lock);
	for (pp = &timer_list; *pp; pp = &(*pp)->next)
		if (*pp == t) {
			*pp = t->next;
			break;
		}
	pthread_mutex_unlock(&timer_lock);
	free(t);
}

/* ------------------------------------------------------------------ MMIO */
uint32_t lkpi_os_mmio_read32(const volatile void *a) { return *(const volatile uint32_t *)a; }
void lkpi_os_mmio_write32(volatile void *a, uint32_t v) { *(volatile uint32_t *)a = v; }
uint16_t lkpi_os_mmio_read16(const volatile void *a) { return *(const volatile uint16_t *)a; }
void lkpi_os_mmio_write16(volatile void *a, uint16_t v) { *(volatile uint16_t *)a = v; }
uint8_t lkpi_os_mmio_read8(const volatile void *a) { return *(const volatile uint8_t *)a; }
void lkpi_os_mmio_write8(volatile void *a, uint8_t v) { *(volatile uint8_t *)a = v; }
void lkpi_os_mmio_barrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

/* ------------------------------------------------------------ fake PCI */
struct lkpi_os_pci {
	struct lkpi_os_pci_info info;
	uint8_t cfg[4096];
	void *bar[6];
	size_t bar_len[6];
	lkpi_os_irq_fn irq_fn;
	void *irq_arg;
	bool irq_enabled;
	bool busmaster;
	void *priv;
	void *provider;
};

lkpi_os_pci_t lkpi_host_pci_create(uint16_t vendor, uint16_t device, size_t bar0_len)
{
	struct lkpi_os_pci *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->info.vendor = vendor;
	p->info.device = device;
	p->info.revision = 1;
	snprintf(p->info.name, sizeof(p->info.name), "fake:%04x:%04x", vendor, device);
	memcpy(&p->cfg[0], &vendor, 2);
	memcpy(&p->cfg[2], &device, 2);
	p->cfg[0x06] |= 0x10;		/* PCI_STATUS: capability list present */
	p->cfg[0x34] = 0x40;		/* capability list -> PCIe cap at 0x40 */
	p->cfg[0x40] = 0x10;		/* PCI_CAP_ID_EXP */
	p->cfg[0x41] = 0x00;
	p->bar[0] = lkpi_os_alloc_pages(bar0_len, true);
	p->bar_len[0] = bar0_len;
	return p;
}

void lkpi_host_pci_destroy(lkpi_os_pci_t p)
{
	lkpi_os_free_pages(p->bar[0], p->bar_len[0]);
	free(p);
}

void *lkpi_host_pci_bar0(lkpi_os_pci_t p) { return p->bar[0]; }

void lkpi_host_pci_raise_irq(lkpi_os_pci_t p)
{
	if (p->irq_enabled && p->irq_fn)
		p->irq_fn(p->irq_arg);
}

void lkpi_os_pci_get_info(lkpi_os_pci_t p, struct lkpi_os_pci_info *i) { *i = p->info; }

uint32_t lkpi_os_pci_cfg_read(lkpi_os_pci_t p, uint32_t off, int width)
{
	uint32_t v = 0;
	if (off + width > sizeof(p->cfg))
		return ~0U;
	memcpy(&v, &p->cfg[off], width);
	return v;
}

void lkpi_os_pci_cfg_write(lkpi_os_pci_t p, uint32_t off, int width, uint32_t v)
{
	if (off + width <= sizeof(p->cfg))
		memcpy(&p->cfg[off], &v, width);
}

void *lkpi_os_pci_map_bar(lkpi_os_pci_t p, int bar, size_t *size)
{
	if (bar < 0 || bar >= 6 || !p->bar[bar]) {
		*size = 0;
		return NULL;
	}
	*size = p->bar_len[bar];
	return p->bar[bar];
}

void lkpi_os_pci_unmap_bar(lkpi_os_pci_t p, int bar) { (void)p; (void)bar; }

int lkpi_os_pci_irq_enable(lkpi_os_pci_t p, lkpi_os_irq_fn fn, void *arg, bool want_msi)
{
	(void)want_msi;
	p->irq_fn = fn;
	p->irq_arg = arg;
	p->irq_enabled = true;
	return 0;
}
void lkpi_os_pci_irq_disable(lkpi_os_pci_t p) { p->irq_enabled = false; }
void lkpi_os_pci_irq_sync(lkpi_os_pci_t p) { (void)p; }
void lkpi_os_pci_set_bus_master(lkpi_os_pci_t p, bool on) { p->busmaster = on; }
void lkpi_os_pci_set_priv(lkpi_os_pci_t p, void *priv) { p->priv = priv; }
void *lkpi_os_pci_get_priv(lkpi_os_pci_t p) { return p->priv; }
void *lkpi_os_pci_provider(lkpi_os_pci_t p) { return p->provider; }

/* ------------------------------------------------------------------- DMA */
void *lkpi_os_dma_alloc_coherent(lkpi_os_pci_t p, size_t size, uint64_t mask, lkpi_os_dma_addr_t *bus)
{
	void *va = lkpi_os_alloc_pages(size, true);
	(void)p; (void)mask;
	if (!va)
		return NULL;
	*bus = (lkpi_os_dma_addr_t)(uintptr_t)va;
	return va;
}
void lkpi_os_dma_free_coherent(lkpi_os_pci_t p, size_t size, void *va, lkpi_os_dma_addr_t bus)
{
	(void)p; (void)bus;
	lkpi_os_free_pages(va, size);
}
lkpi_os_dma_addr_t lkpi_os_dma_map(lkpi_os_pci_t p, void *va, size_t len, int dir, uint64_t mask)
{
	(void)p; (void)len; (void)dir; (void)mask;
	return (lkpi_os_dma_addr_t)(uintptr_t)va;
}
void lkpi_os_dma_unmap(lkpi_os_pci_t p, lkpi_os_dma_addr_t a, size_t len, int dir) { (void)p; (void)a; (void)len; (void)dir; }
void lkpi_os_dma_sync(lkpi_os_pci_t p, lkpi_os_dma_addr_t a, size_t len, int dir, bool for_cpu)
{
	(void)p; (void)a; (void)len; (void)dir; (void)for_cpu;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

/* -------------------------------------------------------------- firmware */
int lkpi_os_firmware_load(const char *name, void **data, size_t *size)
{
	const char *dir = getenv("LKPI_FIRMWARE_DIR");
	char path[1024];
	FILE *f;
	long len;
	void *buf;

	snprintf(path, sizeof(path), "%s/%s", dir ? dir : "firmware", name);
	f = fopen(path, "rb");
	if (!f)
		return -2;	/* -ENOENT */
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = lkpi_os_malloc((size_t)len, false, true);
	if (!buf) {
		fclose(f);
		return -12;	/* -ENOMEM */
	}
	if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
		fclose(f);
		lkpi_os_free(buf);
		return -5;	/* -EIO */
	}
	fclose(f);
	*data = buf;
	*size = (size_t)len;
	return 0;
}

/* ------------------------------------------------------------------ misc */
void lkpi_os_random_bytes(void *buf, size_t len)
{
	static int fd = -1;
	if (fd < 0)
		fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 || read(fd, buf, len) != (ssize_t)len) {
		uint8_t *b = buf;
		while (len--)
			*b++ = (uint8_t)rand();
	}
}

int lkpi_os_cpu_count(void)
{
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	return n > 0 ? (int)n : 1;
}
int lkpi_os_cpu_id(void) { return 0; }

/* ------------------------------------------------------- jiffies tick */
/* weak: test_core links without the runtime */
extern void lkpi_jiffies_tick(void) __attribute__((weak));
static pthread_t tick_thread;
static volatile bool tick_run;

static void *tick_fn(void *unused)
{
	(void)unused;
	while (tick_run) {
		lkpi_os_sleep_ms(10);
		if (lkpi_jiffies_tick)
			lkpi_jiffies_tick();
	}
	return NULL;
}

void lkpi_host_start(void)
{
	if (!tick_run) {
		tick_run = true;
		pthread_create(&tick_thread, NULL, tick_fn, NULL);
	}
}

void lkpi_host_stop(void)
{
	if (tick_run) {
		tick_run = false;
		pthread_join(tick_thread, NULL);
	}
}
