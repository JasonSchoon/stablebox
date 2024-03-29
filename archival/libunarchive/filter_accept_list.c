/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2002 by Glenn McGrath
 *
 * Licensed under GPLv2, see file LICENSE in this tarball for details.
 */

#include <stdlib.h>
#include "unarchive.h"

/*
 * Accept names that are in the accept list, ignoring reject list.
 */
char filter_accept_list(archive_handle_t *archive_handle)
{
	if (find_list_entry(archive_handle->accept, archive_handle->file_header->name)) {
		return(EXIT_SUCCESS);
	} else {
		return(EXIT_FAILURE);
	}
}
