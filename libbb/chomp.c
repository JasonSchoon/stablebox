/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) many different people.
 * If you wrote this, please acknowledge your work.
 *
 * Licensed under GPLv2, see file LICENSE in this tarball for details.
 */

#include <stdio.h>
#include <string.h>
#include "libbb.h"


void chomp(char *s)
{
	char *lc = last_char_is(s, '\n');

	if(lc)
		*lc = 0;
}
