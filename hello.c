#include <assert.h>
#include <stdio.h>
#include "server.h"

static char* page_template =
"<html>\n"
" <head>\n"
" </head>\n"
" <body>\n"
"  <p>%s.</p>\n"
" </body>\n"
"</html>\n";

void module_generate(int fd) {
	FILE* fp;
	fp = fdopen(fd, "w");
	assert(fp != NULL);
	fprintf(fp, page_template, "hello");
	fflush(fp);
}

