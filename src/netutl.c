/*
    netutl.c -- some supporting network utility code
    Copyright (C) 1998-2003 Ivo Timmermans <ivo@o2w.nl>
                  2000-2003 Guus Sliepen <guus@sliepen.eu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    $Id: netutl.c,v 1.12.4.48 2003/07/22 20:55:20 guus Exp $
*/

#include "system.h"

#include "net.h"
#include "netutl.h"
#include "logger.h"
#include "utils.h"
#include "xalloc.h"

bool hostnames = false;

/*
  Turn a string into a struct addrinfo.
  Return NULL on failure.
*/
struct addrinfo *str2addrinfo(char *address, char *service, int socktype)
{
	struct addrinfo hint, *ai;
	int err;

	cp();

	memset(&hint, 0, sizeof(hint));

	hint.ai_family = addressfamily;
	hint.ai_socktype = socktype;

	err = getaddrinfo(address, service, &hint, &ai);

	if(err) {
		logger(LOG_WARNING, _("Error looking up %s port %s: %s\n"), address,
				   service, gai_strerror(err));
		return NULL;
	}

	return ai;
}

sockaddr_t str2sockaddr(char *address, char *port)
{
	struct addrinfo hint, *ai;
	sockaddr_t result;
	int err;

	cp();

	memset(&hint, 0, sizeof(hint));

	hint.ai_family = AF_UNSPEC;
	hint.ai_flags = AI_NUMERICHOST;
	hint.ai_socktype = SOCK_STREAM;

	err = getaddrinfo(address, port, &hint, &ai);

	if(err || !ai) {
		logger(LOG_ERR, _("Error looking up %s port %s: %s\n"), address, port,
			   gai_strerror(err));
		cp_trace();
		raise(SIGFPE);
		exit(0);
	}

	result = *(sockaddr_t *) ai->ai_addr;
	freeaddrinfo(ai);

	return result;
}

void sockaddr2str(sockaddr_t *sa, char **addrstr, char **portstr)
{
	char address[NI_MAXHOST];
	char port[NI_MAXSERV];
	char *scopeid;
	int err;

	cp();

	err = getnameinfo(&sa->sa, SALEN(sa->sa), address, sizeof(address), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);

	if(err) {
		logger(LOG_ERR, _("Error while translating addresses: %s"),
			   gai_strerror(err));
		cp_trace();
		raise(SIGFPE);
		exit(0);
	}

	scopeid = strchr(address, '%');

	if(scopeid)
		*scopeid = '\0';		/* Descope. */

	*addrstr = xstrdup(address);
	*portstr = xstrdup(port);
}

char *sockaddr2hostname(sockaddr_t *sa)
{
	char *str;
	char address[NI_MAXHOST] = "unknown";
	char port[NI_MAXSERV] = "unknown";
	int err;

	cp();

	err = getnameinfo(&sa->sa, SALEN(sa->sa), address, sizeof(address), port, sizeof(port),
					hostnames ? 0 : (NI_NUMERICHOST | NI_NUMERICSERV));
	if(err) {
		logger(LOG_ERR, _("Error while looking up hostname: %s"),
			   gai_strerror(err));
	}

	asprintf(&str, _("%s port %s"), address, port);

	return str;
}

int sockaddrcmp(sockaddr_t *a, sockaddr_t *b)
{
	int result;

	cp();

	result = a->sa.sa_family - b->sa.sa_family;

	if(result)
		return result;

	switch (a->sa.sa_family) {
		case AF_UNSPEC:
			return 0;

		case AF_INET:
			result = memcmp(&a->in.sin_addr, &b->in.sin_addr, sizeof(a->in.sin_addr));

			if(result)
				return result;

			return memcmp(&a->in.sin_port, &b->in.sin_port, sizeof(a->in.sin_port));

		case AF_INET6:
			result = memcmp(&a->in6.sin6_addr, &b->in6.sin6_addr, sizeof(a->in6.sin6_addr));

			if(result)
				return result;

			return memcmp(&a->in6.sin6_port, &b->in6.sin6_port, sizeof(a->in6.sin6_port));

		default:
			logger(LOG_ERR, _("sockaddrcmp() was called with unknown address family %d, exitting!"),
				   a->sa.sa_family);
			cp_trace();
			raise(SIGFPE);
			exit(0);
	}
}

void sockaddrunmap(sockaddr_t *sa)
{
	if(sa->sa.sa_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&sa->in6.sin6_addr)) {
		sa->in.sin_addr.s_addr = ((uint32_t *) & sa->in6.sin6_addr)[3];
		sa->in.sin_family = AF_INET;
	}
}

/* Subnet mask handling */

int maskcmp(void *va, void *vb, int masklen, int len)
{
	int i, m, result;
	char *a = va;
	char *b = vb;

	cp();

	for(m = masklen, i = 0; m >= 8; m -= 8, i++) {
		result = a[i] - b[i];
		if(result)
			return result;
	}

	if(m)
		return (a[i] & (0x100 - (1 << (8 - m)))) -
			(b[i] & (0x100 - (1 << (8 - m))));

	return 0;
}

void mask(void *va, int masklen, int len)
{
	int i;
	char *a = va;

	cp();

	i = masklen / 8;
	masklen %= 8;

	if(masklen)
		a[i++] &= (0x100 - (1 << masklen));

	for(; i < len; i++)
		a[i] = 0;
}

void maskcpy(void *va, void *vb, int masklen, int len)
{
	int i, m;
	char *a = va;
	char *b = vb;

	cp();

	for(m = masklen, i = 0; m >= 8; m -= 8, i++)
		a[i] = b[i];

	if(m) {
		a[i] = b[i] & (0x100 - (1 << m));
		i++;
	}

	for(; i < len; i++)
		a[i] = 0;
}

bool maskcheck(void *va, int masklen, int len)
{
	int i;
	char *a = va;

	cp();

	i = masklen / 8;
	masklen %= 8;

	if(masklen && a[i++] & (0xff >> masklen))
		return false;

	for(; i < len; i++)
		if(a[i] != 0)
			return false;

	return true;
}
