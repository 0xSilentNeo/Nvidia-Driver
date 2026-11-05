/* SPDX-License-Identifier: MIT */
/* sys/sbuf.h - sbuf(9): safe string buffers (src/sbuf.c). */
#ifndef _BSDKPI_SYS_SBUF_H_
#define _BSDKPI_SYS_SBUF_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stdarg.h>

struct sbuf;
typedef int (sbuf_drain_func)(void *arg, const char *data, int len);

struct sbuf {
	char		*s_buf;
	sbuf_drain_func	*s_drain_func;
	void		*s_drain_arg;
	int		 s_error;
	ssize_t		 s_size;
	ssize_t		 s_len;
	int		 s_flags;
#define SBUF_FIXEDLEN	0x00000000
#define SBUF_AUTOEXTEND	0x00000001
#define SBUF_USRFLAGMSK	0x0000ffff
#define SBUF_DYNAMIC	0x00010000
#define SBUF_FINISHED	0x00020000
#define SBUF_DYNSTRUCT	0x00080000
#define SBUF_INSECTION	0x00100000
#define SBUF_NOWAIT	0x00000002
#define SBUF_INCLUDENUL	0x00000004
#define SBUF_DRAINTOEOR	0x00000008
	int		 s_sect_len;
	ssize_t		 s_rec_off;
};

struct sbuf *sbuf_new(struct sbuf *s, char *buf, int length, int flags);
#define sbuf_new_auto()	sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND)
void	sbuf_clear(struct sbuf *s);
int	sbuf_setpos(struct sbuf *s, ssize_t pos);
int	sbuf_bcat(struct sbuf *s, const void *buf, size_t len);
int	sbuf_bcpy(struct sbuf *s, const void *buf, size_t len);
int	sbuf_cat(struct sbuf *s, const char *str);
int	sbuf_cpy(struct sbuf *s, const char *str);
int	sbuf_printf(struct sbuf *s, const char *fmt, ...) __printflike(2, 3);
int	sbuf_vprintf(struct sbuf *s, const char *fmt, va_list ap) __printflike(2, 0);
int	sbuf_nl_terminate(struct sbuf *s);
int	sbuf_putc(struct sbuf *s, int c);
void	sbuf_set_drain(struct sbuf *s, sbuf_drain_func *func, void *arg);
int	sbuf_trim(struct sbuf *s);
int	sbuf_error(const struct sbuf *s);
int	sbuf_finish(struct sbuf *s);
char	*sbuf_data(struct sbuf *s);
ssize_t	sbuf_len(struct sbuf *s);
int	sbuf_done(const struct sbuf *s);
void	sbuf_delete(struct sbuf *s);
void	sbuf_start_section(struct sbuf *s, ssize_t *old_lenp);
ssize_t	sbuf_end_section(struct sbuf *s, ssize_t old_len, size_t pad, int c);
void	sbuf_hexdump(struct sbuf *sb, const void *ptr, int length,
	    const char *hdr, int flags);
int	sbuf_count_drain(void *arg, const char *data, int len);
int	sbuf_printf_drain(void *arg, const char *data, int len);
void	sbuf_putbuf(struct sbuf *s);
void	sbuf_set_flags(struct sbuf *s, int flags);
void	sbuf_clear_flags(struct sbuf *s, int flags);
int	sbuf_get_flags(struct sbuf *s);

struct sysctl_req;
struct sbuf *sbuf_new_for_sysctl(struct sbuf *s, char *buf, int length,
	    struct sysctl_req *req);
int	sbuf_bcopyin(struct sbuf *s, const void *uaddr, size_t len);
int	sbuf_copyin(struct sbuf *s, const void *uaddr, size_t len);
#endif /* _BSDKPI_SYS_SBUF_H_ */
