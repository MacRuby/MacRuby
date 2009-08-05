/**********************************************************************

  dln.c -

  $Author: nobu $
  created at: Tue Jan 18 17:05:06 JST 1994

  Copyright (C) 1993-2007 Yukihiro Matsumoto

**********************************************************************/

#include "ruby/ruby.h"
#include "dln.h"
#include <stdlib.h>
#include <alloca.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>

#define FUNCNAME_PATTERN "Init_%s"

static int
init_funcname_len(char **buf, const char *file)
{
    char *p;
    const char *slash;
    int len;

    /* Load the file as an object one */
    for (slash = file-1; *file; file++) /* Find position of last '/' */
	if (*file == '/') slash = file;

    len = strlen(FUNCNAME_PATTERN) + strlen(slash + 1);
    *buf = xmalloc(len);
    snprintf(*buf, len, FUNCNAME_PATTERN, slash + 1);
    for (p = *buf; *p; p++) {         /* Delete suffix if it exists */
	if (*p == '.') {
	    *p = '\0'; break;
	}
    }
    return p - *buf;
}

#define init_funcname(buf, file) do {\
    int len = init_funcname_len(buf, file);\
    char *tmp = ALLOCA_N(char, len+1);\
    if (tmp == NULL) {\
	xfree(*buf);\
	rb_memerror();\
    }\
    strcpy(tmp, *buf);\
    xfree(*buf);\
    *buf = tmp;\
} while (0)

#include <dlfcn.h>
#include <mach-o/dyld.h>

static const char *
dln_strerror(void)
{
    return (char*)dlerror();
}

void*
dln_load(const char *file)
{
    const char *error = 0;
#define DLN_ERROR() (error = dln_strerror(), strcpy(ALLOCA_N(char, strlen(error) + 1), error))

    char *buf;
    /* Load the file as an object one */
    init_funcname(&buf, file);

    {
	void *handle;
	void (*init_fct)();

	/* Load file */
	if ((handle = (void*)dlopen(file, RTLD_LAZY|RTLD_GLOBAL)) == NULL) {
	    error = dln_strerror();
	    goto failed;
	}

	init_fct = (void(*)())dlsym(handle, buf);
	if (init_fct == NULL) {
	    error = DLN_ERROR();
	    dlclose(handle);
	    goto failed;
	}
	/* Call the init code */
	(*init_fct)();

	return handle;
    }

  failed:
    rb_loaderror("%s - %s", error, file);

    return 0;			/* dummy return */
}

static char *dln_find_1(const char *fname, const char *path, char *buf, int size, int exe_flag);

char *
dln_find_exe_r(const char *fname, const char *path, char *buf, int size)
{
    if (path == NULL) {
	path = getenv(PATH_ENV);
    }

    if (path == NULL) {
	path = "/usr/local/bin:/usr/bin:/bin:.";
    }
    return dln_find_1(fname, path, buf, size, 1);
}

char *
dln_find_file_r(const char *fname, const char *path, char *buf, int size)
{
    if (path == NULL) {
	path = ".";
    }
    return dln_find_1(fname, path, buf, size, 0);
}

static char fbuf[MAXPATHLEN];

char *
dln_find_exe(const char *fname, const char *path)
{
    return dln_find_exe_r(fname, path, fbuf, sizeof(fbuf));
}

char *
dln_find_file(const char *fname, const char *path)
{
    return dln_find_file_r(fname, path, fbuf, sizeof(fbuf));
}

static char *
dln_find_1(const char *fname, const char *path, char *fbuf, int size,
	   int exe_flag /* non 0 if looking for executable. */)
{
    register const char *dp;
    register const char *ep;
    register char *bp;
    struct stat st;

#define RETURN_IF(expr) if (expr) return (char *)fname;

    RETURN_IF(!fname);
    RETURN_IF(fname[0] == '/');
    RETURN_IF(strncmp("./", fname, 2) == 0 || strncmp("../", fname, 3) == 0);
    RETURN_IF(exe_flag && strchr(fname, '/'));

#undef RETURN_IF

    for (dp = path;; dp = ++ep) {
	register int l;
	int i;
	int fspace;

	/* extract a component */
	ep = strchr(dp, PATH_SEP[0]);
	if (ep == NULL) {
	    ep = dp+strlen(dp);
	}

	/* find the length of that component */
	l = ep - dp;
	bp = fbuf;
	fspace = size - 2;
	if (l > 0) {
	    /*
	    **	If the length of the component is zero length,
	    **	start from the current directory.  If the
	    **	component begins with "~", start from the
	    **	user's $HOME environment variable.  Otherwise
	    **	take the path literally.
	    */

	    if (*dp == '~' && (l == 1 || dp[1] == '/')) {
		char *home;

		home = getenv("HOME");
		if (home != NULL) {
		    i = strlen(home);
		    if ((fspace -= i) < 0) {
			goto toolong;
		    }
		    memcpy(bp, home, i);
		    bp += i;
		}
		dp++;
		l--;
	    }
	    if (l > 0) {
		if ((fspace -= l) < 0) {
		    goto toolong;
		}
		memcpy(bp, dp, l);
		bp += l;
	    }

	    /* add a "/" between directory and filename */
	    if (ep[-1] != '/') {
		*bp++ = '/';
	    }
	}

	/* now append the file name */
	i = strlen(fname);
	if ((fspace -= i) < 0) {
	  toolong:
	    fprintf(stderr, "openpath: pathname too long (ignored)\n");
	    *bp = '\0';
	    fprintf(stderr, "\tDirectory \"%s\"\n", fbuf);
	    fprintf(stderr, "\tFile \"%s\"\n", fname);
	    goto next;
	}
	memcpy(bp, fname, i + 1);

	if (stat(fbuf, &st) == 0) {
	    if (exe_flag == 0) {
		return fbuf;
	    }
	    /* looking for executable */
	    if (!S_ISDIR(st.st_mode) && eaccess(fbuf, X_OK) == 0) {
		return fbuf;
	    }
	}

      next:
	/* if not, and no other alternatives, life is bleak */
	if (*ep == '\0') {
	    return NULL;
	}

	/* otherwise try the next component in the search path */
    }
}
