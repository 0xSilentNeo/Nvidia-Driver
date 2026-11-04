/* SPDX-License-Identifier: MIT */
/*
 * debugfs does not exist on macOS.  Every creation call returns a non-NULL
 * dummy dentry (so drivers that check for NULL do not bail) and nothing is
 * ever exposed.  Phase-4 may route selected files to an IOUserClient.
 */
#ifndef _LKPI_LINUX_DEBUGFS_H_
#define _LKPI_LINUX_DEBUGFS_H_
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
struct dentry {
	int dummy;
};
extern struct dentry lkpi_debugfs_dummy_dentry;
#define LKPI_DEBUGFS_DENTRY (&lkpi_debugfs_dummy_dentry)
struct debugfs_blob_wrapper {
	void *data;
	unsigned long size;
};
struct debugfs_reg32 {
	char *name;
	unsigned long offset;
};
struct debugfs_regset32 {
	const struct debugfs_reg32 *regs;
	int nregs;
	void __iomem *base;
	struct device *dev;
};
static inline struct dentry *debugfs_create_dir(const char *name, struct dentry *parent) { (void)name; (void)parent; return LKPI_DEBUGFS_DENTRY; }
static inline struct dentry *debugfs_create_file(const char *name, umode_t mode, struct dentry *parent,
    void *data, const struct file_operations *fops) { (void)name; (void)mode; (void)parent; (void)data; (void)fops; return LKPI_DEBUGFS_DENTRY; }
#define debugfs_create_file_unsafe debugfs_create_file
#define debugfs_create_file_size(n, m, p, d, f, s) debugfs_create_file(n, m, p, d, f)
static inline struct dentry *debugfs_create_symlink(const char *n, struct dentry *p, const char *t) { (void)n; (void)p; (void)t; return LKPI_DEBUGFS_DENTRY; }
static inline void debugfs_remove(struct dentry *d) { (void)d; }
#define debugfs_remove_recursive(d) debugfs_remove(d)
static inline void debugfs_create_u8(const char *n, umode_t m, struct dentry *p, u8 *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_u16(const char *n, umode_t m, struct dentry *p, u16 *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_u32(const char *n, umode_t m, struct dentry *p, u32 *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_u64(const char *n, umode_t m, struct dentry *p, u64 *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_x8(const char *n, umode_t m, struct dentry *p, u8 *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_x16(const char *n, umode_t m, struct dentry *p, u16 *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_x32(const char *n, umode_t m, struct dentry *p, u32 *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_x64(const char *n, umode_t m, struct dentry *p, u64 *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_bool(const char *n, umode_t m, struct dentry *p, bool *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_atomic_t(const char *n, umode_t m, struct dentry *p, atomic_t *v) { (void)n; (void)m; (void)p; (void)v; }
static inline void debugfs_create_size_t(const char *n, umode_t m, struct dentry *p, size_t *v) { (void)n; (void)m; (void)p; (void)v; }
static inline struct dentry *debugfs_create_blob(const char *n, umode_t m, struct dentry *p,
    struct debugfs_blob_wrapper *b) { (void)n; (void)m; (void)p; (void)b; return LKPI_DEBUGFS_DENTRY; }
static inline void debugfs_create_regset32(const char *n, umode_t m, struct dentry *p, struct debugfs_regset32 *r) { (void)n; (void)m; (void)p; (void)r; }
static inline void debugfs_create_devm_seqfile(struct device *dev, const char *name, struct dentry *parent,
    int (*read_fn)(struct seq_file *s, void *data)) { (void)dev; (void)name; (void)parent; (void)read_fn; }
static inline bool debugfs_initialized(void) { return false; }
#define debugfs_lookup(n, p) ((struct dentry *)NULL)
#define debugfs_lookup_and_remove(n, p) do { } while (0)

#define DEFINE_DEBUGFS_ATTRIBUTE(__fops, __get, __set, __fmt) \
	static const struct file_operations __fops = { .owner = THIS_MODULE }
#define DEFINE_DEBUGFS_ATTRIBUTE_SIGNED(__fops, __get, __set, __fmt) \
	DEFINE_DEBUGFS_ATTRIBUTE(__fops, __get, __set, __fmt)
#define DEFINE_SIMPLE_ATTRIBUTE(__fops, __get, __set, __fmt) \
	static const struct file_operations __fops = { .owner = THIS_MODULE }
#define DEFINE_SIMPLE_ATTRIBUTE_SIGNED(__fops, __get, __set, __fmt) \
	DEFINE_SIMPLE_ATTRIBUTE(__fops, __get, __set, __fmt)
#define DEFINE_SHOW_ATTRIBUTE(__name) \
	static const struct file_operations __name##_fops = { .owner = THIS_MODULE }
#define DEFINE_SHOW_STORE_ATTRIBUTE(__name) \
	static const struct file_operations __name##_fops = { .owner = THIS_MODULE }
#endif
