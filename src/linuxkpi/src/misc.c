// SPDX-License-Identifier: MIT
/*
 * Small odds and ends: THIS_MODULE, init_net, kmem_cache, devcoredump, the
 * debugfs dummy dentry, seq_file and the handful of fs.h helpers that
 * debugfs-style code links against.
 *
 * There is no user space and no VFS here: a seq_file is just a growable
 * buffer that show() renders into when the "file" is opened, and seq_read()
 * copies out of it with memcpy.  Nothing is ever exposed, but the code in
 * mt76 that builds these files still compiles and links.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/device.h>
#include <linux/devcoredump.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/sprintf.h>
#include <lkpi/os.h>

struct module lkpi_this_module = { .name = "mt7925" };
struct net init_net;
struct dentry lkpi_debugfs_dummy_dentry;

/* ---- kmem_cache ------------------------------------------------------- */

struct kmem_cache *kmem_cache_create(const char *name, size_t size, size_t align,
    unsigned int flags, void (*ctor)(void *))
{
	struct kmem_cache *c;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return NULL;
	if (align > 1)
		size = (size + align - 1) & ~(align - 1);
	c->size = size ? size : 1;
	c->flags = flags;
	c->ctor = ctor;
	c->name = name;
	return c;
}

void kmem_cache_destroy(struct kmem_cache *c)
{
	kfree(c);
}

/* ---- devcoredump ------------------------------------------------------ */

void dev_coredumpv(struct device *dev, void *data, size_t datalen, gfp_t gfp)
{
	(void)gfp;
	if (dev)
		dev_warn(dev, "firmware coredump of %zu bytes discarded\n", datalen);
	else
		pr_warn("firmware coredump of %zu bytes discarded\n", datalen);
	kfree(data);
}

/* ---- seq_file --------------------------------------------------------- */

#define LKPI_SEQ_INITIAL	1024

/* Make room for @need more bytes plus the terminator; false on OOM. */
static bool lkpi_seq_reserve(struct seq_file *m, size_t need)
{
	size_t want = m->count + need + 1;
	size_t newsize;
	char *nb;

	if (m->buf && want <= m->size)
		return true;
	newsize = m->size ? m->size : LKPI_SEQ_INITIAL;
	while (newsize < want)
		newsize *= 2;
	nb = krealloc(m->buf, newsize, GFP_KERNEL);
	if (!nb) {
		m->count = m->size;	/* report overflow, as Linux does */
		return false;
	}
	m->buf = nb;
	m->size = newsize;
	return true;
}

void seq_write(struct seq_file *m, const void *s, size_t len)
{
	if (!m || !len)
		return;
	if (!lkpi_seq_reserve(m, len))
		return;
	memcpy(m->buf + m->count, s, len);
	m->count += len;
	m->buf[m->count] = '\0';
}

void seq_puts(struct seq_file *m, const char *s)
{
	if (s)
		seq_write(m, s, strlen(s));
}

void seq_putc(struct seq_file *m, char c)
{
	seq_write(m, &c, 1);
}

void seq_vprintf(struct seq_file *m, const char *fmt, va_list args)
{
	va_list ap2;
	int len;

	if (!m)
		return;
	va_copy(ap2, args);
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if (len < 0)
		return;
	if (!lkpi_seq_reserve(m, (size_t)len))
		return;
	vsnprintf(m->buf + m->count, m->size - m->count, fmt, args);
	m->count += (size_t)len;
}

void seq_printf(struct seq_file *m, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	seq_vprintf(m, fmt, ap);
	va_end(ap);
}

void seq_hex_dump(struct seq_file *m, const char *prefix_str, int prefix_type, int rowsize,
    int groupsize, const void *buf, size_t len, bool ascii)
{
	const u8 *ptr = buf;
	char linebuf[32 * 3 + 2 + 32 + 1];
	size_t i, linelen, remaining = len;

	if (rowsize != 16 && rowsize != 32)
		rowsize = 16;
	if (!prefix_str)
		prefix_str = "";
	for (i = 0; i < len; i += rowsize) {
		linelen = remaining < (size_t)rowsize ? remaining : (size_t)rowsize;
		remaining -= linelen;
		hex_dump_to_buffer(ptr + i, linelen, rowsize, groupsize, linebuf,
				   sizeof(linebuf), ascii);
		switch (prefix_type) {
		case DUMP_PREFIX_ADDRESS:
			seq_printf(m, "%s%p: %s\n", prefix_str, (const void *)(ptr + i), linebuf);
			break;
		case DUMP_PREFIX_OFFSET:
			seq_printf(m, "%s%.8zx: %s\n", prefix_str, i, linebuf);
			break;
		default:
			seq_printf(m, "%s%s\n", prefix_str, linebuf);
			break;
		}
	}
}

static struct seq_file *lkpi_seq_alloc(struct file *file)
{
	struct seq_file *m;

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return NULL;
	mutex_init(&m->lock);
	m->file = file;
	if (file)
		file->private_data = m;
	return m;
}

static void lkpi_seq_free(struct seq_file *m)
{
	if (!m)
		return;
	mutex_destroy(&m->lock);
	kfree(m->buf);
	kfree(m);
}

/* Renders the whole sequence into m->buf through the start/show/next/stop ops. */
static void lkpi_seq_render(struct seq_file *m)
{
	const struct seq_operations *op = m->op;
	loff_t pos = 0;
	void *v;

	if (!op || !op->start || !op->show)
		return;
	v = op->start(m, &pos);
	while (v && !IS_ERR(v)) {
		if (op->show(m, v))
			break;
		if (!op->next)
			break;
		v = op->next(m, v, &pos);
	}
	if (op->stop)
		op->stop(m, IS_ERR(v) ? NULL : v);
	m->index = pos;
}

int single_open(struct file *file, int (*show)(struct seq_file *, void *), void *data)
{
	struct seq_file *m;

	if (!file || !show)
		return -EINVAL;
	m = lkpi_seq_alloc(file);
	if (!m)
		return -ENOMEM;
	m->private = data;
	show(m, NULL);
	return 0;
}

int single_release(struct inode *inode, struct file *file)
{
	(void)inode;
	if (!file)
		return -EINVAL;
	lkpi_seq_free(file->private_data);
	file->private_data = NULL;
	return 0;
}

int seq_open(struct file *file, const struct seq_operations *op)
{
	struct seq_file *m;

	if (!file || !op)
		return -EINVAL;
	m = lkpi_seq_alloc(file);
	if (!m)
		return -ENOMEM;
	m->op = op;
	lkpi_seq_render(m);
	return 0;
}

int seq_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

ssize_t seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	struct seq_file *m;
	loff_t pos;
	size_t n;

	if (!file || !file->private_data || !buf || !ppos)
		return -EINVAL;
	m = file->private_data;
	pos = *ppos;
	if (pos < 0)
		return -EINVAL;
	if ((size_t)pos >= m->count)
		return 0;
	n = m->count - (size_t)pos;
	if (n > size)
		n = size;
	memcpy(buf, m->buf + pos, n);
	*ppos = pos + (loff_t)n;
	m->from = (size_t)*ppos;
	return (ssize_t)n;
}

loff_t seq_lseek(struct file *file, loff_t offset, int whence)
{
	struct seq_file *m;
	loff_t pos;

	if (!file || !file->private_data)
		return -EINVAL;
	m = file->private_data;
	switch (whence) {
	case 0:		/* SEEK_SET */
		pos = offset;
		break;
	case 1:		/* SEEK_CUR */
		pos = file->f_pos + offset;
		break;
	case 2:		/* SEEK_END */
		pos = (loff_t)m->count + offset;
		break;
	default:
		return -EINVAL;
	}
	if (pos < 0)
		return -EINVAL;
	file->f_pos = pos;
	m->from = (size_t)pos;
	return pos;
}

/* ---- fs.h helpers ----------------------------------------------------- */

int simple_open(struct inode *inode, struct file *file)
{
	if (inode && inode->i_private)
		file->private_data = inode->i_private;
	return 0;
}

ssize_t simple_read_from_buffer(void *to, size_t count, loff_t *ppos, const void *from,
    size_t available)
{
	loff_t pos = *ppos;
	size_t n;

	if (pos < 0)
		return -EINVAL;
	if ((size_t)pos >= available || !count)
		return 0;
	n = available - (size_t)pos;
	if (n > count)
		n = count;
	memcpy(to, (const u8 *)from + pos, n);
	*ppos = pos + (loff_t)n;
	return (ssize_t)n;
}

ssize_t simple_write_to_buffer(void *to, size_t available, loff_t *ppos, const void *from,
    size_t count)
{
	loff_t pos = *ppos;
	size_t n;

	if (pos < 0)
		return -EINVAL;
	if ((size_t)pos >= available || !count)
		return 0;
	n = available - (size_t)pos;
	if (n > count)
		n = count;
	memcpy((u8 *)to + pos, from, n);
	*ppos = pos + (loff_t)n;
	return (ssize_t)n;
}

loff_t default_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t pos;

	switch (whence) {
	case 0:		/* SEEK_SET */
		pos = offset;
		break;
	case 1:		/* SEEK_CUR */
		pos = file->f_pos + offset;
		break;
	case 2:		/* SEEK_END: size unknown without an inode, treat as SEEK_SET */
		pos = offset;
		break;
	default:
		return -EINVAL;
	}
	if (pos < 0)
		return -EINVAL;
	file->f_pos = pos;
	return pos;
}

loff_t noop_llseek(struct file *file, loff_t offset, int whence)
{
	(void)offset; (void)whence;
	return file->f_pos;
}

int nonseekable_open(struct inode *inode, struct file *filp)
{
	(void)inode; (void)filp;
	return 0;
}
