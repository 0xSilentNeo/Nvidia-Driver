/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_SEQ_FILE_H_
#define _LKPI_LINUX_SEQ_FILE_H_
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mutex.h>
struct seq_file {
	char *buf;
	size_t size;
	size_t from;
	size_t count;
	loff_t index;
	struct mutex lock;
	const struct seq_operations *op;
	void *private;
	struct file *file;
};
struct seq_operations {
	void *(*start)(struct seq_file *m, loff_t *pos);
	void (*stop)(struct seq_file *m, void *v);
	void *(*next)(struct seq_file *m, void *v, loff_t *pos);
	int (*show)(struct seq_file *m, void *v);
};
void seq_printf(struct seq_file *m, const char *fmt, ...) __printf(2, 3);
void seq_vprintf(struct seq_file *m, const char *fmt, va_list args);
void seq_puts(struct seq_file *m, const char *s);
void seq_putc(struct seq_file *m, char c);
void seq_write(struct seq_file *m, const void *s, size_t len);
void seq_hex_dump(struct seq_file *m, const char *prefix_str, int prefix_type, int rowsize,
    int groupsize, const void *buf, size_t len, bool ascii);
static inline bool seq_has_overflowed(struct seq_file *m) { return m->count == m->size; }
int single_open(struct file *file, int (*show)(struct seq_file *, void *), void *data);
int single_release(struct inode *inode, struct file *file);
int seq_open(struct file *file, const struct seq_operations *op);
int seq_release(struct inode *inode, struct file *file);
ssize_t seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos);
loff_t seq_lseek(struct file *file, loff_t offset, int whence);
#define seq_puts_array(m, name, arr, len) do { } while (0)
#define DEFINE_SEQ_ATTRIBUTE(__name) \
	static const struct file_operations __name##_fops = { .owner = THIS_MODULE }
#endif
