/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_FS_H_
#define _LKPI_LINUX_FS_H_
#include <linux/types.h>
#include <linux/mutex.h>
struct module;
struct poll_table_struct;
struct inode {
	void *i_private;
	unsigned short i_mode;
};
struct file {
	void *private_data;
	struct inode *f_inode;
	unsigned int f_flags;
	loff_t f_pos;
	const struct file_operations *f_op;
	void *f_path;
};
typedef unsigned short umode_t;
struct file_operations {
	struct module *owner;
	loff_t (*llseek)(struct file *, loff_t, int);
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
	unsigned int (*poll)(struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	int (*open)(struct inode *, struct file *);
	int (*release)(struct inode *, struct file *);
	int (*mmap)(struct file *, void *);
};
static inline struct inode *file_inode(const struct file *f) { return f->f_inode; }
static inline loff_t no_llseek(struct file *f, loff_t off, int whence) { (void)f; (void)off; (void)whence; return -1; }
loff_t default_llseek(struct file *file, loff_t offset, int whence);
loff_t noop_llseek(struct file *file, loff_t offset, int whence);
int simple_open(struct inode *inode, struct file *file);
ssize_t simple_read_from_buffer(void *to, size_t count, loff_t *ppos, const void *from, size_t available);
ssize_t simple_write_to_buffer(void *to, size_t available, loff_t *ppos, const void *from, size_t count);
int nonseekable_open(struct inode *inode, struct file *filp);
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IRUGO 0444
#define S_IWUGO 0222
#define S_IRWXU 0700
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define FMODE_READ 0x1
#define FMODE_WRITE 0x2
#define SEEK_SET_LKPI 0
#endif
