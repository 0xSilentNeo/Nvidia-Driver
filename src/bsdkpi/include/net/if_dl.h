/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_NET_IF_DL_H_
#define _BSDKPI_NET_IF_DL_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/socket.h>

struct sockaddr_dl {
	u_char		sdl_len;	/* Total length of sockaddr */
	sa_family_t	sdl_family;	/* AF_LINK */
	u_short		sdl_index;	/* if != 0, system given index for interface */
	u_char		sdl_type;	/* interface type */
	u_char		sdl_nlen;	/* interface name length, no trailing 0 reqd. */
	u_char		sdl_alen;	/* link level address length */
	u_char		sdl_slen;	/* link layer selector length */
	char		sdl_data[46];	/* minimum work area, can be larger */
};
#define SDL_DATA_LEN	46
#define LLADDR(s)	((caddr_t)((s)->sdl_data + (s)->sdl_nlen))
#define CLLADDR(s)	((c_caddr_t)((s)->sdl_data + (s)->sdl_nlen))
#define LLINDEX(s)	((s)->sdl_index)

struct ifnet;
struct sockaddr_dl *link_alloc_sdl(size_t size, int flags);
void	link_free_sdl(struct sockaddr *sa);
struct sockaddr_dl *link_init_sdl(struct ifnet *ifp, struct sockaddr *paddr,
	    u_char iftype);
#endif
