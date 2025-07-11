
#define DEVICES_TIMER_H
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "stdio.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include "stabs.h"

#ifdef DEBUG
#include "clib/debug_protos.h"
#else
#define KPrintF(a,...)
#endif

/* objects in ___filelist */
#include <exec/lists.h>
struct filenode {
  struct MinNode node;
  FILE theFILE;
};

/* own stuff */
extern struct MinList ___filelist; /* List of all fopen'ed files */

extern struct ExecBase * SysBase;

extern unsigned *__BUFSIZE;

/*
 * SBF: I put all functions into this file which are linked anyway if stdio is used.
 */
extern int __fflush(FILE *);
extern void __seterrno(void);

extern int * __errno;
extern FILE **__sF;
/*
 ** the internal storage
 */
StdFileDes **__stdfiledes;
unsigned __stdfilesize;
unsigned __stdfilesize_max;
#ifdef __posix_threads__
unsigned __stdLock[2];
#endif
/* The normal Amiga shell sets no process->pr_CES stream -
 * we use Open("*",MODE_NEWFILE) in this case
 */
static BPTR stderrdes;

/*
 **
 */
void _setup_file(StdFileDes *fp) {
	fp->lx_inuse = 1;
	if (IsInteractive(fp->lx_fh))
		fp->lx_flags = LX_FILE | LX_ATTY;
	else
		fp->lx_flags = LX_FILE;
	fp->lx_fx = 0;
}


int isatty(int d) {
	StdFileDes *sfd = _lx_fhfromfd(d);
	return sfd ? (sfd->lx_flags & LX_ATTY) : 0;
}

/*
 **
 */
static int _lx_addflags(int d, int oflags) {
	StdFileDes *sfd = _lx_fhfromfd(d);

	return sfd ? sfd->lx_oflags |= oflags : 0;
}

/**
 * convert fd to a StdFileDes
 */
StdFileDes *_lx_fhfromfd(int d) {
	if ((unsigned) d < __stdfilesize) {
		StdFileDes *sfd = __stdfiledes[d];
		if (sfd && sfd->lx_inuse) {
			KPrintF("lx_fhfromfd %ld -> %ld [__stdfiledes=%ld, __stdfilecount=%ld]\n", d, sfd, __stdfiledes, __stdfilesize);
			return sfd;
		}
	}
	return NULL;
}

/**
 * Open the file stream for the file descriptor.
 */
FILE *fdopen(int filedes, const char *mode) {
	if (mode != NULL) {
		struct filenode *node = (struct filenode *) malloc(sizeof(*node));
		if (node != NULL) {
			FILE * f = &node->theFILE;
			if ((f->_bf._base = (unsigned char *) malloc(*__BUFSIZE)) != NULL) {
				AddHead((struct List * )&___filelist, (struct Node * )&node->node);
				f->_p = 0;
				f->_r = 0;
				f->_w = 0;
				f->_flags = __SMBF; /* Buffer is malloc'ed */
				if (isatty(filedes))
					f->_flags |= __SLBF; /* set linebuffered flag */
				if (_lx_addflags(filedes, *mode == 'a' ? O_APPEND : 0) & O_WRONLY)
					f->_flags |= __SWO; /* set write-only flag */
				f->file = filedes;
				f->_bf._size = *__BUFSIZE;
				f->linebufsize = 0;
				f->tmpp = 0;
				f->tmpinc = 0;
				f->tmpdir = 0;
				f->name = 0;
#ifdef __posix_threads__
				f->__spinlock[0] = 0;
#endif
				return f;
			}
			free(node);
		}
	}
	errno = ENOMEM;
	return NULL;
}

int __fclose(FILE * stream) {
	__STDIO_LOCK(stream);

	int error = __fflush(stream);

	if (stream->file >= 0)
		close(stream->file);
	if (stream->name != NULL && (stream->_flags & __BPTRS) == 0) /* file is temporary */
	{
		BPTR cd = CurrentDir(stream->tmpdir); /* cd t: */
		if (!DeleteFile((CONST_STRPTR)stream->name)) /* delete file */
		{
			__seterrno();
			error = 1;
		}
		free(stream->name); /* free filename */
		stream->name = NULL;
		UnLock(CurrentDir(cd)); /* cd back, unlock t: */
	}
	stream->file = 0;
	__STDIO_UNLOCK(stream);
	return error;
}

static StdFileDes * __mk_stdfiledes(BPTR fh) {
	StdFileDes *sfd = (StdFileDes *) malloc(sizeof(StdFileDes));
	if (sfd) {
		__stdfiledes[__stdfilesize] = sfd;
		sfd->lx_pos = __stdfilesize++;
		sfd->lx_fh = fh;
		sfd->lx_oflags = O_WRONLY;
		_setup_file(sfd);
		sfd->lx_flags |= LX_SYS;
	}
	KPrintF("__mk_stdfiledes [__stdfiledes=%ld, __stdfilecount=%ld]\n", __stdfiledes, __stdfilesize);
	return sfd;
}

/*
 **
 */
void __initstdio(void) {
	StdFileDes *sfd;
#ifdef __KICK13__
	__stdfilesize = 0;
#endif
	__stdfilesize_max = 4;
	if ((__stdfiledes = (StdFileDes **) malloc(4 * sizeof(StdFileDes *)))) {
		unsigned __bufsiz = *__BUFSIZE;
		*__BUFSIZE = 512;
		if ((sfd = __mk_stdfiledes(Input()))) {
			sfd->lx_oflags = O_RDONLY;
			*__BUFSIZE = __bufsiz;
			if ((sfd = __mk_stdfiledes(Output()))) {
				BPTR bstderr;
				struct Process * proc = (struct Process *) SysBase->ThisTask;
#ifdef __KICK13__
				struct CommandLineInterface * cli = (struct CommandLineInterface *)BADDR(proc->pr_CLI);
				bstderr = cli ? cli->cli_StandardOutput : 0;
#else
				bstderr = proc->pr_CES;
#endif
				if(bstderr == 0 &&
				  (!proc->pr_CLI || (bstderr = stderrdes = Open((CONST_STRPTR)"*", MODE_OLDFILE)) == 0))
					bstderr = __stdfiledes[STDOUT_FILENO]->lx_fh;

				if ((sfd = __mk_stdfiledes(bstderr))) {
					__stdfiledes[3] = 0; // have a free one
					{
						short flags;
						// fdopen stdin, stdout and stderr and make stderr unbuffered
						FILE **f = __sF, *err;
						if (((*f++ = fdopen(STDIN_FILENO, "r")) == NULL) || ((*f++ = fdopen(STDOUT_FILENO, "w")) == NULL))
							exit(20);

						if ((*f = err = fdopen(STDERR_FILENO, "w")) == NULL)
							exit(20);
						*__BUFSIZE = 4;

						free(err->_bf._base);
						flags = err->_flags & ~(__SMBF | __SLBF);
						err->_flags = flags | __SNBF;
						err->_bf._base = err->unget;
						err->_bf._size = 3;
					}
					*__BUFSIZE = __bufsiz;
					return;
				}
			}
		}
	}
	exit(20);
}


/**
 * Close the file descriptor d.
 *
 * Located here since it's called from __exitstdio
 */
int close(int d) {
	StdFileDes *sfd = _lx_fhfromfd(d);

	if (sfd) {

		if (sfd->lx_fx)
			return sfd->lx_fx->lx_close(sfd);

		if (!(sfd->lx_inuse -= 1)) {
			if (!(sfd->lx_flags & LX_SYS)) {
				if (!Close(sfd->lx_fh)) {
#ifndef __KICK13__
					__seterrno();
					return EOF;
#endif
				}
			}
		}
	}

	return 0;
}

/**
 * Close the file handle stream.
 *
 * Located here since it's called from __exitstdio
 */
int fclose(FILE *stream) {
	struct filenode *node;
	int retval;
	if (!stream) {
		return EOF;
	}

	__STDIO_LOCK(stream);
	retval = __fclose(stream) ? EOF : 0;
	if (stream->_flags & __SMBF) /* Free buffer if necessary */
	{
		free(stream->_bf._base);
		stream->_bf._base = NULL;
	}

	if (stream->_flags & __BPTRS) {
		Close((BPTR )stream->name);
		Close((BPTR )stream->tmpdir);
	}

	node = (struct filenode *) ((struct MinNode *) stream - 1);
	Remove((struct Node * )&node->node);
	free(node);
	__STDIO_UNLOCK(stream);
	return retval;
}

struct MinList ___filelist = { /* list of open files (fflush() needs also access) */
(struct MinNode *) &___filelist.mlh_Tail,
NULL, (struct MinNode *) &___filelist.mlh_Head };

void __exitstdio(void) {
	int i, max;
	struct MinNode *node;
	while ((node = ___filelist.mlh_Head)->mln_Succ != NULL) {
		fclose(&((struct filenode *) node)->theFILE);
	}

	for (max = __stdfilesize, i = 0; i < max; i++) {
		StdFileDes *sfd = __stdfiledes[i];
		if (sfd && sfd->lx_inuse) {
			sfd->lx_inuse = 1; // force closing
			close(i);
		}
	}

	if (stderrdes)
		Close(stderrdes);
}

ADD2INIT(__initstdio, -30);
ADD2EXIT(__exitstdio, -30);
