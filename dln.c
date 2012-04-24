/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"
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

// In file.c
int eaccess(const char *path, int mode);

static size_t
init_funcname_len(char **buf, const char *file)
{
    char *p;
    const char *slash;
    size_t len;

    /* Load the file as an object one */
    for (slash = file-1; *file; file++) /* Find position of last '/' */
	if (*file == '/') slash = file;

    len = strlen(FUNCNAME_PATTERN) + strlen(slash + 1);
    *buf = xmalloc(len);
    snprintf(*buf, len, FUNCNAME_PATTERN, slash + 1);
    for (p = *buf; *p; p++) {         /* Delete suffix if it exists */
	if (*p == '-') {
	    *p = '_';
	}
    }
    for (p = *buf; *p; p++) {         /* Delete suffix if it exists */
	if (*p == '.') {
	    *p = '\0';
	    break;
	}
    }
    return p - *buf;
}

#define init_funcname(buf, file) do {\
    size_t len = init_funcname_len(buf, file);\
    char *tmp = ALLOCA_N(char, len+1);\
    if (tmp == NULL) {\
	xfree(*buf);\
	rb_memerror();\
    }\
    strlcpy(tmp, *buf, len + 1);\
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

bool ruby_is_miniruby = false;

// This function is called back from .rbo files' gcc constructors, passing a
// pointer to their entry point function, during dlopen(). The entry point
// function is called right after dlopen() directly. This is because C++
// exceptions raised within a gcc constructor are not properly propagated.
static void *__mrep__ = NULL;
void
rb_mrep_register(void *imp)
{
    __mrep__ = imp;
}

void*
dln_load(const char *file, bool call_init)
{
    if (ruby_is_miniruby) {
	rb_raise(rb_eLoadError,
		"miniruby can't load C extension bundles due to technical problems");
    }

    const char *error = 0;
#define DLN_ERROR() (error = dln_strerror(), strcpy(ALLOCA_N(char, strlen(error) + 1), error))

    char *buf;
    /* Load the file as an object one */
    init_funcname(&buf, file);

    {
	void *handle;

	/* Load file */
	__mrep__ = NULL;
	if ((handle = (void*)dlopen(file, RTLD_LAZY|RTLD_GLOBAL)) == NULL) {
	    error = dln_strerror();
	    goto failed;
	}

	if (call_init) {
	    void (*init_fct)();
	    init_fct = (void(*)())dlsym(handle, buf);
	    if (init_fct == NULL) {
		error = DLN_ERROR();
		dlclose(handle);
		goto failed;
	    }
	    /* Call the init code */
	    rb_vm_dln_load(init_fct, NULL);
	}
	else {
	    if (__mrep__ == NULL) {
		rb_raise(rb_eLoadError, "Can't load %s: entry point function not located (this can happen when you load twice the same .rbo file with a different case on a case-insensitive filesystem)", file);
	    }
	    rb_vm_dln_load(NULL, (IMP)__mrep__);
	}

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
	    if (!S_ISDIR(st.st_mode)
		    && (exe_flag == 0 || eaccess(fbuf, X_OK) == 0)) {
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
