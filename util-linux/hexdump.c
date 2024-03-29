/*
 * hexdump implementation for busybox
 * Based on code from util-linux v 2.11l
 *
 * Copyright (c) 1989
 *	The Regents of the University of California.  All rights reserved.
 *
 * Licensed under GPLv2, see file License in this tarball for details.
 */

#include "busybox.h"
#include <getopt.h>
#include <string.h>
#include "dump.h"

static void bb_dump_addfile(char *name)
{
	register char *p;
	FILE *fp;
	char *buf;

	fp = bb_xfopen(name, "r");

	while ((buf = bb_get_chomped_line_from_file(fp)) != NULL) {
		p = skip_whitespace(buf);

		if (*p && (*p != '#')) {
			bb_dump_add(p);
		}
		free(buf);
	}
	fclose(fp);
}

static const char * const add_strings[] = {
			"\"%07.7_ax \" 16/1 \"%03o \" \"\\n\"",		/* b */
			"\"%07.7_ax \" 16/1 \"%3_c \" \"\\n\"",		/* c */
			"\"%07.7_ax \" 8/2 \"  %05u \" \"\\n\"",	/* d */
			"\"%07.7_ax \" 8/2 \" %06o \" \"\\n\"",		/* o */
			"\"%07.7_ax \" 8/2 \"   %04x \" \"\\n\"",	/* x */
};

static const char add_first[] = "\"%07.7_Ax\n\"";

static const char hexdump_opts[] = "bcdoxCe:f:n:s:v";

static const struct suffix_mult suffixes[] = {
	{"b",  512 },
	{"k",  1024 },
	{"m",  1024*1024 },
	{NULL, 0 }
};

int hexdump_main(int argc, char **argv)
{
//	register FS *tfs;
	const char *p;
	int ch;

	bb_dump_vflag = FIRST;
	bb_dump_length = -1;

	while ((ch = getopt(argc, argv, hexdump_opts)) > 0) {
		if ((p = strchr(hexdump_opts, ch)) != NULL) {
			if ((p - hexdump_opts) < 5) {
				bb_dump_add(add_first);
				bb_dump_add(add_strings[(int)(p - hexdump_opts)]);
			} else if (ch == 'C') {
			        bb_dump_add("\"%08.8_Ax\n\"");
				bb_dump_add("\"%08.8_ax  \" 8/1 \"%02x \" \"  \" 8/1 \"%02x \" ");
			        bb_dump_add("\"  |\" 16/1 \"%_p\" \"|\\n\"");
			} else {
				/* Sae a little bit of space below by omitting the 'else's. */
				if (ch == 'e') {
					bb_dump_add(optarg);
				} /* else */
				if (ch == 'f') {
					bb_dump_addfile(optarg);
				} /* else */
				if (ch == 'n') {
					bb_dump_length = bb_xgetularg10_bnd(optarg, 0, INT_MAX);
				} /* else */
				if (ch == 's') {
					bb_dump_skip = bb_xgetularg_bnd_sfx(optarg, 10, 0, LONG_MAX, suffixes);
				} /* else */
				if (ch == 'v') {
					bb_dump_vflag = ALL;
				}
			}
		} else {
			bb_show_usage();
		}
	}

	if (!bb_dump_fshead) {
		bb_dump_add(add_first);
		bb_dump_add("\"%07.7_ax \" 8/2 \"%04x \" \"\\n\"");
	}

	argv += optind;

	return(bb_dump_dump(argv));
}
