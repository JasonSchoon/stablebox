/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2, see file LICENSE in this tarball for details.
 */

#include <stdio.h>
#include <unistd.h>
#include "libbb.h"



#if (__GLIBC__ < 2)
int vdprintf(int d, const char *format, va_list ap)
{
	char buf[BUF_SIZE];
	int len;

	len = vsprintf(buf, format, ap);
	return write(d, buf, len);
}
#endif
