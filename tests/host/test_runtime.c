// SPDX-License-Identifier: MIT
/*
 * Behavioural tests of the C runtime (src/linuxkpi/src) on the libc backend:
 * allocation, idr, skbuff, workqueues, delayed work, timers, completions,
 * wait queues, kthreads, tasklets, RCU deferral, devres and the PCI attach
 * path with a fake device.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/rcupdate.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <net/page_pool/helpers.h>
#include <lkpi/runtime.h>
#include "os_host.h"
#include "test.h"

static void test_alloc(void)
{
	char *p = kzalloc(100, GFP_KERNEL);
	int i;
	CHECK(p != NULL);
	for (i = 0; i < 100; i++)
		CHECK_EQ(p[i], 0);
	strcpy(p, "x");
	p = krealloc(p, 1000, GFP_KERNEL);
	CHECK(p && p[0] == 'x');
	kfree(p);
	kfree(NULL);
	kfree(ZERO_SIZE_PTR);
	{
		char *s = kstrdup("abc", GFP_KERNEL);
		CHECK(!strcmp(s, "abc"));
		kfree(s);
		s = kasprintf(GFP_KERNEL, "%s-%d", "mt", 7925);
		CHECK(s && !strcmp(s, "mt-7925"));
		kfree(s);
	}
}

static void test_idr(void)
{
	struct idr idr;
	int ids[5], i, id;
	void *p;

	idr_init(&idr);
	CHECK(idr_is_empty(&idr));
	for (i = 0; i < 5; i++) {
		ids[i] = idr_alloc(&idr, &ids[i], 0, 8, GFP_KERNEL);
		CHECK_EQ(ids[i], i);
	}
	CHECK(idr_find(&idr, 3) == &ids[3]);
	CHECK(idr_find(&idr, 7) == NULL);
	CHECK(idr_remove(&idr, 1) == &ids[1]);
	CHECK_EQ(idr_alloc(&idr, &ids[1], 0, 8, GFP_KERNEL), 1);
	CHECK_EQ(idr_alloc(&idr, &p, 5, 7, GFP_KERNEL), 5);
	CHECK_EQ(idr_alloc(&idr, &p, 5, 7, GFP_KERNEL), 6);
	CHECK_EQ(idr_alloc(&idr, &p, 5, 7, GFP_KERNEL), -ENOSPC);
	i = 0;
	idr_for_each_entry(&idr, p, id)
		i++;
	CHECK_EQ(i, 7);
	CHECK(idr_replace(&idr, &ids[0], 6) == &p);
	idr_destroy(&idr);
	CHECK(idr_is_empty(&idr));
	/* large sparse ids like mt76 tokens */
	idr_init(&idr);
	CHECK_EQ(idr_alloc(&idr, &p, 1000, 1100, GFP_ATOMIC), 1000);
	CHECK(idr_find(&idr, 1000) == &p);
	idr_destroy(&idr);
}

static void test_skb(void)
{
	struct sk_buff *skb, *clone, *frag;
	struct sk_buff_head q;
	u8 *d;
	int i;

	skb = alloc_skb(256, GFP_KERNEL);
	CHECK(skb != NULL);
	CHECK_EQ(skb->len, 0);
	CHECK(skb_tailroom(skb) >= 256);
	CHECK(skb_shinfo(skb)->nr_frags == 0 && skb_shinfo(skb)->frag_list == NULL);
	skb_reserve(skb, 32);
	CHECK_EQ(skb_headroom(skb), 32);
	d = skb_put(skb, 100);
	for (i = 0; i < 100; i++)
		d[i] = (u8)i;
	CHECK_EQ(skb->len, 100);
	CHECK_EQ(skb_headlen(skb), 100);
	d = skb_push(skb, 8);
	memset(d, 0xaa, 8);
	CHECK_EQ(skb->len, 108);
	CHECK_EQ(skb_headroom(skb), 24);
	skb_pull(skb, 8);
	CHECK_EQ(skb->len, 100);
	CHECK_EQ(skb->data[5], 5);
	skb_trim(skb, 50);
	CHECK_EQ(skb->len, 50);
	CHECK(skb_tailroom(skb) > 0);
	skb_put_zero(skb, 4);
	CHECK_EQ(skb->data[50], 0);
	skb_set_queue_mapping(skb, 3);
	CHECK_EQ(skb_get_queue_mapping(skb), 3);

	clone = skb_clone(skb, GFP_KERNEL);
	CHECK(clone && clone->len == skb->len && clone->data == skb->data);
	kfree_skb(clone);
	CHECK_EQ(skb->data[5], 5);

	/* headroom expansion keeps data */
	CHECK_EQ(skb_cow_head(skb, 200), 0);
	CHECK(skb_headroom(skb) >= 200);
	CHECK_EQ(skb->data[5], 5);
	CHECK_EQ(skb->len, 54);

	/* frag list */
	frag = alloc_skb(64, GFP_KERNEL);
	skb_put_data(frag, "frag", 4);
	skb_shinfo(skb)->frag_list = frag;
	skb->len += frag->len;
	skb->data_len += frag->len;
	CHECK(skb_is_nonlinear(skb));
	CHECK_EQ(skb_linearize(skb), 0);
	CHECK(!skb_is_nonlinear(skb));
	CHECK_EQ(skb->len, 58);
	CHECK_EQ(memcmp(skb->data + 54, "frag", 4), 0);

	/* queues */
	skb_queue_head_init(&q);
	CHECK(skb_queue_empty(&q));
	skb_queue_tail(&q, skb);
	skb_queue_tail(&q, alloc_skb(16, GFP_KERNEL));
	CHECK_EQ(skb_queue_len(&q), 2);
	CHECK(skb_peek(&q) == skb);
	CHECK(skb_dequeue(&q) == skb);
	kfree_skb(skb);
	skb_queue_purge(&q);
	CHECK(skb_queue_empty(&q));

	/* build_skb over a page */
	{
		struct page *page = alloc_pages(GFP_KERNEL, 0);
		void *buf = page_address(page);
		CHECK(page != NULL);
		CHECK(virt_to_head_page((u8 *)buf + 100) == page);
		skb = build_skb(buf, 2048);
		CHECK(skb != NULL);
		CHECK(skb->head == buf);
		skb_put(skb, 10);
		kfree_skb(skb);		/* frees the page */
	}
	/* refcounting */
	skb = alloc_skb(16, GFP_KERNEL);
	skb_get(skb);
	kfree_skb(skb);
	CHECK_EQ(refcount_read(&skb->users), 1);
	kfree_skb(skb);
}

static void test_page_pool(void)
{
	struct page_pool_params pp = {
		.order = 0,
		.pool_size = 16,
		.flags = PP_FLAG_PAGE_FRAG,
		.dma_dir = DMA_FROM_DEVICE,
	};
	struct page_pool *pool = page_pool_create(&pp);
	struct page *p1, *p2, *p3;
	unsigned int off1, off2;

	CHECK(pool != NULL);
	p1 = page_pool_dev_alloc_frag(pool, &off1, 1024);
	p2 = page_pool_dev_alloc_frag(pool, &off2, 1024);
	CHECK(p1 && p2);
	CHECK(p1 == p2);			/* two fragments of the same page */
	CHECK(off1 != off2);
	p3 = page_pool_dev_alloc_frag(pool, &off1, 4096);
	CHECK(p3 && p3 != p1);
	CHECK(p1->pp == pool);
	CHECK(virt_to_head_page((u8 *)page_address(p1) + off2) == p1);
	page_pool_put_full_page(pool, p1, false);
	page_pool_put_full_page(pool, p2, false);
	page_pool_put_full_page(pool, p3, false);
	page_pool_destroy(pool);
}

/* ---- work ---- */
static int work_counter;
static struct completion work_done;
static void work_fn(struct work_struct *w)
{
	(void)w;
	__atomic_fetch_add(&work_counter, 1, __ATOMIC_SEQ_CST);
	complete(&work_done);
}

static struct work_struct requeue_work;
static int requeue_left = 3;
static void requeue_fn(struct work_struct *w)
{
	__atomic_fetch_add(&work_counter, 1, __ATOMIC_SEQ_CST);
	if (--requeue_left > 0)
		schedule_work(w);
	else
		complete(&work_done);
}

static void test_work(void)
{
	struct work_struct w;
	struct delayed_work dw;
	struct workqueue_struct *wq;
	unsigned long t0;

	init_completion(&work_done);
	INIT_WORK(&w, work_fn);
	CHECK(schedule_work(&w));
	CHECK(wait_for_completion_timeout(&work_done, msecs_to_jiffies(2000)) > 0);
	CHECK_EQ(work_counter, 1);
	flush_work(&w);
	CHECK(!work_pending(&w));

	/* re-queue from within the handler */
	work_counter = 0;
	reinit_completion(&work_done);
	INIT_WORK(&requeue_work, requeue_fn);
	schedule_work(&requeue_work);
	CHECK(wait_for_completion_timeout(&work_done, msecs_to_jiffies(2000)) > 0);
	CHECK_EQ(work_counter, 3);

	/* delayed work fires after the delay, not before */
	work_counter = 0;
	reinit_completion(&work_done);
	INIT_DELAYED_WORK(&dw, work_fn);
	t0 = jiffies;
	CHECK(schedule_delayed_work(&dw, msecs_to_jiffies(100)));
	CHECK(delayed_work_pending(&dw));
	CHECK(wait_for_completion_timeout(&work_done, msecs_to_jiffies(2000)) > 0);
	CHECK(jiffies_to_msecs(jiffies - t0) >= 80);
	CHECK_EQ(work_counter, 1);

	/* cancel before it runs */
	work_counter = 0;
	CHECK(schedule_delayed_work(&dw, msecs_to_jiffies(500)));
	CHECK(cancel_delayed_work_sync(&dw));
	msleep(50);
	CHECK_EQ(work_counter, 0);
	CHECK(!cancel_delayed_work_sync(&dw));

	/* own ordered queue */
	wq = alloc_ordered_workqueue("test-%s", 0, "wq");
	CHECK(wq != NULL);
	reinit_completion(&work_done);
	INIT_WORK(&w, work_fn);
	queue_work(wq, &w);
	CHECK(wait_for_completion_timeout(&work_done, msecs_to_jiffies(2000)) > 0);
	flush_workqueue(wq);
	destroy_workqueue(wq);
}

/* ---- timers ---- */
static int timer_hits;
static struct completion timer_done;
static void timer_fn(struct timer_list *t)
{
	(void)t;
	timer_hits++;
	complete(&timer_done);
}

static void test_timer(void)
{
	struct timer_list t;
	init_completion(&timer_done);
	timer_setup(&t, timer_fn, 0);
	CHECK(!timer_pending(&t));
	CHECK_EQ(mod_timer(&t, jiffies + msecs_to_jiffies(50)), 0);
	CHECK(timer_pending(&t));
	CHECK_EQ(mod_timer(&t, jiffies + msecs_to_jiffies(60)), 1);
	CHECK(wait_for_completion_timeout(&timer_done, msecs_to_jiffies(2000)) > 0);
	CHECK_EQ(timer_hits, 1);
	CHECK(!timer_pending(&t));
	mod_timer(&t, jiffies + msecs_to_jiffies(500));
	CHECK_EQ(timer_delete_sync(&t), 1);
	CHECK_EQ(timer_delete_sync(&t), 0);
	msleep(20);
	CHECK_EQ(timer_hits, 1);
	timer_destroy(&t);
}

/* ---- completion / waitqueue ---- */
static DECLARE_WAIT_QUEUE_HEAD(test_wq);
static int wq_flag;
static void waker_fn(struct work_struct *w)
{
	(void)w;
	msleep(30);
	WRITE_ONCE(wq_flag, 1);
	wake_up(&test_wq);
}

static void test_wait(void)
{
	struct work_struct w;
	long r;
	INIT_WORK(&w, waker_fn);
	schedule_work(&w);
	r = wait_event_timeout(test_wq, READ_ONCE(wq_flag) == 1, msecs_to_jiffies(2000));
	CHECK(r > 0);
	flush_work(&w);
	r = wait_event_timeout(test_wq, READ_ONCE(wq_flag) == 2, msecs_to_jiffies(50));
	CHECK_EQ(r, 0);
	{
		struct completion c;
		init_completion(&c);
		CHECK_EQ(wait_for_completion_timeout(&c, msecs_to_jiffies(30)), 0);
		complete(&c);
		CHECK(try_wait_for_completion(&c));
		CHECK(!try_wait_for_completion(&c));
		complete_all(&c);
		CHECK(try_wait_for_completion(&c));
		CHECK(try_wait_for_completion(&c));
	}
}

/* ---- kthread (mt76_worker pattern) ---- */
static int kt_iterations;
static int kt_fn(void *data)
{
	struct completion *started = data;
	complete(started);
	while (!kthread_should_stop()) {
		kthread_parkme();
		if (kthread_should_stop())
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(10));
		kt_iterations++;
	}
	return 42;
}

static void test_kthread(void)
{
	struct completion started;
	struct task_struct *t;
	init_completion(&started);
	t = kthread_run(kt_fn, &started, "test-kt-%d", 1);
	CHECK(!IS_ERR_OR_NULL(t));
	CHECK(wait_for_completion_timeout(&started, msecs_to_jiffies(2000)) > 0);
	msleep(100);
	CHECK(kt_iterations > 3);
	kthread_park(t);
	{
		int snap = kt_iterations;
		msleep(50);
		CHECK_EQ(kt_iterations, snap);	/* parked: no progress */
	}
	kthread_unpark(t);
	msleep(50);
	CHECK_EQ(kthread_stop(t), 42);
	CHECK(current != NULL);
}

/* ---- tasklet ---- */
static int tasklet_runs;
static struct completion tasklet_done;
static void tasklet_fn(unsigned long data)
{
	tasklet_runs += (int)data;
	complete(&tasklet_done);
}

static void test_tasklet(void)
{
	struct tasklet_struct t;
	init_completion(&tasklet_done);
	tasklet_init(&t, tasklet_fn, 1);
	tasklet_schedule(&t);
	tasklet_schedule(&t);			/* coalesces while scheduled */
	CHECK(wait_for_completion_timeout(&tasklet_done, msecs_to_jiffies(2000)) > 0);
	msleep(20);
	CHECK(tasklet_runs >= 1 && tasklet_runs <= 2);
	tasklet_disable(&t);
	tasklet_schedule(&t);
	msleep(20);
	{
		int snap = tasklet_runs;
		tasklet_enable(&t);
		reinit_completion(&tasklet_done);
		CHECK(wait_for_completion_timeout(&tasklet_done, msecs_to_jiffies(2000)) > 0);
		CHECK_EQ(tasklet_runs, snap + 1);
	}
	tasklet_kill(&t);
}

/* ---- rcu ---- */
struct rcu_obj {
	int v;
	struct rcu_head rcu;
};
static void test_rcu(void)
{
	struct rcu_obj *o = kzalloc(sizeof(*o), GFP_KERNEL);
	struct rcu_obj __rcu *ptr = NULL;
	o->v = 7;
	rcu_assign_pointer(ptr, o);
	rcu_read_lock();
	CHECK_EQ(rcu_dereference(ptr)->v, 7);
	rcu_read_unlock();
	o = rcu_replace_pointer(ptr, NULL, 1);
	kfree_rcu(o, rcu);
	synchronize_rcu();
	msleep(100);	/* deferred free runs; a leak checker would catch a miss */
	CHECK(rcu_access_pointer(ptr) == NULL);
}

/* ---- devres ---- */
static int action_ran;
static void action_fn(void *d) { action_ran += *(int *)d; }
static void test_devres(void)
{
	struct device dev;
	int v = 5;
	char *p;
	lkpi_device_init(&dev, NULL, "testdev");
	CHECK(!strcmp(dev_name(&dev), "testdev"));
	p = devm_kzalloc(&dev, 64, GFP_KERNEL);
	CHECK(p != NULL);
	strcpy(p, "x");
	CHECK(devm_kasprintf(&dev, GFP_KERNEL, "%d", 1) != NULL);
	CHECK_EQ(devm_add_action(&dev, action_fn, &v), 0);
	devm_kfree(&dev, p);
	lkpi_device_fini(&dev);
	CHECK_EQ(action_ran, 5);
}

/* ---- pci attach with a fake device ---- */
static int probed, removed;
static irqreturn_t fake_irq(int irq, void *d) { (void)irq; *(int *)d += 1; return IRQ_HANDLED; }
static int irq_count;
static int fake_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	u16 cmd;
	void __iomem *regs;
	int ret;
	probed++;
	CHECK_EQ(id->driver_data, 0x1234);
	CHECK_EQ(pdev->vendor, 0x14c3);
	CHECK_EQ(pdev->device, 0x7925);
	CHECK_EQ(pcim_enable_device(pdev), 0);
	CHECK_EQ(pcim_iomap_regions(pdev, BIT(0), "fake"), 0);
	regs = pcim_iomap_table(pdev)[0];
	CHECK(regs != NULL);
	writel(0xabcd, regs + 0x10);
	CHECK_EQ(readl(regs + 0x10), 0xabcd);
	CHECK_EQ(pci_read_config_word(pdev, PCI_VENDOR_ID, &cmd), 0);
	CHECK_EQ(cmd, 0x14c3);
	pci_set_master(pdev);
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	CHECK(ret >= 1);
	CHECK_EQ(dma_set_mask(&pdev->dev, DMA_BIT_MASK(32)), 0);
	CHECK_EQ(devm_request_irq(&pdev->dev, pdev->irq, fake_irq, IRQF_SHARED, "fake", &irq_count), 0);
	{
		dma_addr_t bus;
		void *va = dma_alloc_coherent(&pdev->dev, 8192, &bus, GFP_KERNEL);
		CHECK(va != NULL && bus != 0);
		dma_free_coherent(&pdev->dev, 8192, va, bus);
		bus = dma_map_single(&pdev->dev, &cmd, sizeof(cmd), DMA_TO_DEVICE);
		CHECK(!dma_mapping_error(&pdev->dev, bus));
		dma_unmap_single(&pdev->dev, bus, sizeof(cmd), DMA_TO_DEVICE);
	}
	CHECK_EQ(pci_is_pcie(pdev), 1);
	pci_set_drvdata(pdev, &probed);
	return 0;
}
static void fake_remove(struct pci_dev *pdev)
{
	CHECK(pci_get_drvdata(pdev) == &probed);
	removed++;
}
static const struct pci_device_id fake_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7925), .driver_data = 0x1234 },
	{ },
};
static struct pci_driver fake_driver = {
	.name = "fake",
	.id_table = fake_ids,
	.probe = fake_probe,
	.remove = fake_remove,
};

static void test_pci(void)
{
	lkpi_os_pci_t os = lkpi_host_pci_create(0x14c3, 0x7925, 65536);
	struct pci_dev *pdev;
	CHECK_EQ(pci_register_driver(&fake_driver), 0);
	pdev = lkpi_pci_attach(os);
	CHECK(pdev != NULL);
	CHECK_EQ(probed, 1);
	lkpi_host_pci_raise_irq(os);
	lkpi_host_pci_raise_irq(os);
	CHECK_EQ(irq_count, 2);
	lkpi_pci_detach(pdev);
	CHECK_EQ(removed, 1);
	pci_unregister_driver(&fake_driver);
	lkpi_host_pci_destroy(os);
}

static void test_firmware(void)
{
	const struct firmware *fw = NULL;
	struct device dev;
	lkpi_device_init(&dev, NULL, "fwdev");
	CHECK_EQ(request_firmware(&fw, "does/not/exist.bin", &dev), -ENOENT);
	CHECK(fw == NULL);
	release_firmware(NULL);
	lkpi_device_fini(&dev);
}

int main(void)
{
	lkpi_host_log_level = LKPI_LOG_WARN;
	lkpi_host_start();
	CHECK_EQ(lkpi_runtime_init(), 0);
	RUN(test_alloc);
	RUN(test_idr);
	RUN(test_skb);
	RUN(test_page_pool);
	RUN(test_work);
	RUN(test_timer);
	RUN(test_wait);
	RUN(test_kthread);
	RUN(test_tasklet);
	RUN(test_rcu);
	RUN(test_devres);
	RUN(test_pci);
	RUN(test_firmware);
	lkpi_runtime_fini();
	lkpi_host_stop();
	return test_report("test_runtime");
}
