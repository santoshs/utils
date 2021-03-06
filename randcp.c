/*
 * This file is part of randcp - A random file copy program
 *
 * Copyright (C) 2013  Santosh Sivaraj <santosh@fossix.org>
 *
 *  This program is distributed under the GNU General Public License version 2,
 *  or (at your option) any later version, as published by the Free Software
 *  Foundation. See http://www.gnu.org/licenses/gpl-2.0.html for the full GPL V2
 *  license.
 */

#define _ISOC99_SOURCE
#define _DEFAULT_SOURCE		/* for d_type constants */
#define _XOPEN_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <argp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/time.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>
#include <libgen.h>
#include <pthread.h>
#include <regex.h>

#include "randcp.h"

char *program_name;
const char *argp_program_version = "randcp 0.9\n"
	"Copyright (C) 2013 Santosh Sivaraj.\n"
	"License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl-2.0.html>.\n"
	"This is free software: you are free to change and redistribute it.\n"
	"There is NO WARRANTY, to the extent permitted by law.";
const char *argp_program_bug_address = "<santosh@fossix.org>";
static char doc[] = "randcp -- Copy random files";
static char args_doc[] =
	"SOURCE DEST";

static struct argp_option options[] = {
	{"limit", 'l', "LIMIT", 0, "Limit number of copied files"},
	{"pattern", 'p', "PATTERN", 0, "Copy files matching PATTERN"},
	{"insensitive", 'i', 0, 0, "Case insensitive match"},
	{"recursive", 'r', 0, 0, "Copy files by scanning directories recursively."},
	{"depth", 'd', "DEPTH", 0, "Copy only to a DEPTH depth in the folder hierarchy, only works with -r (recursive) option"},
	{"dry-run", 'y', 0, 0 , "Do not copy files -- useful to test patterns"},
	{"echo", 'e', 0, 0, "Echo files being copied"},
	{},
};

static error_t parse_opt(int, char *, struct argp_state *);
static struct argp argp = {options, parse_opt, args_doc, doc};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *arguments = state->input;

	switch (key) {
	case 'l':
		arguments->limit = strtoul(arg, NULL, 10);
		if (errno)
			err(errno, "Invalid limit");
		break;

	case 'p':
		arguments->pattern = arg;
		break;

	case 'i':
		arguments->icase = TRUE;
		break;

	case 'r':
		arguments->recursive = 1;
		break;

	case 'd':
		arguments->depth = strtoul(arg, NULL, 10);
		break;

	case 'y':
		arguments->dry_run = 1;
		break;

	case 'e':
		arguments->echo = 1;
		break;

	case ARGP_KEY_ARG:
		if (state->arg_num >= 2)
			argp_usage(state);

		arguments->paths[state->arg_num] = arg;

		break;

	case ARGP_KEY_END:
		if (state->arg_num < 2)
			argp_usage(state);

		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

int cp(const char * const spath, const char * const dpath)
{
	char buf[1024];
	int s, d, l;
	struct stat st;

	if (stat(spath, &st)) {
		warn("%s", spath);
		return 1;
	}

	if ((s = open(spath, O_RDONLY)) < 0) {
		warn("%s", spath);
		return 1;
	}

	if ((d = open(dpath, O_CREAT|O_EXCL|O_WRONLY, st.st_mode)) < 0) {
		warn("%s", dpath);
		close(s);
		return 1;
	}

	while ((l = read(s, buf, 512)) > 0) {
		if (l < 0) {
			warn("%s", spath);
			goto cp_err;
		}
		l = write(d, buf, l);
		if (l < 0) {
			warn("%s", dpath);
			goto cp_err;
		}
	}

cp_err:
	close(s);
	close(d);
	if (l < 0) {
		unlink(dpath);
		return 2;
	}

	return 0;
}

int is_dir(const struct dirent *ent, const char *path)
{
#ifdef _DIRENT_HAVE_D_TYPE
	if (ent->d_type == DT_DIR)
		return 1;
#else
	struct stat status;
	/* have to use stat!! */
	if (stat(path, &status) < 0) {
		warn("%s", ent->d_name);
		return 0;
	}

	if (S_ISDIR(status.st_mode))
		return 1;
#endif
	return 0;
}

int exists_p(const char *path)
{
	struct stat status;

	if (stat(path, &status) < 0)
		return 0;

	return 1;
}

struct node *__build_tree(char *dir, struct node ***leaves, unsigned *size,
			  unsigned int *current, struct arguments *args,
			  unsigned int depth)
{
	DIR *d;
	struct dirent *ent;
	struct node *root = NULL, *node;
	char path[PATH_MAX + 2];

	if (args && args->recursive && args->depth > 0)
		if (depth > args->depth)
			return NULL;

	if (!leaves) {
		printf("leaves cannot be NULL");
		return NULL;
	}

	if (!dir || !*dir)
		return NULL;

	d = opendir(dir);
	if (!d)
		goto out;

	root = malloc(sizeof(struct node));
	if (!root)
		goto out;

	root->type = DT_DIR;
	root->parent = NULL;
	root->name = basename(strdup(dir));

	if (!*leaves) {
		*leaves = malloc(512 * sizeof(struct node *));
		if (!*leaves)
			goto out;
		*size = 512;
	}

	while (1) {
		errno = 0;
		ent = readdir(d);
		if (ent == NULL)
			goto out;

		/* Avoid that terible recursion */
		if (strcmp(ent->d_name, "..") == 0 || strcmp(ent->d_name, ".") == 0)
			continue;

		snprintf(path, NAME_MAX + 2, "%s/%s", dir, ent->d_name);
		if (is_dir(ent, path)) {
			if (!args->recursive)
				continue;

			node = __build_tree(path, leaves, size, current, args,
					    depth+1);
		} else {
			node = malloc(sizeof(struct node));
			if (!node)
				goto out;

			node->name = strdup(ent->d_name);
			node->type = DT_REG;

			if (*size == *current) {
				struct node **nleaves;
				nleaves = realloc(*leaves,
						  (*size + 512) *
						  sizeof(struct node *));
				if (!nleaves)
					goto out;

				*size += 512;
				*leaves = nleaves;
			}
			(*leaves)[(*current)++] = node;
		}

		if (node)
			node->parent = root;
	}

out:
	closedir(d);
	if (errno)
		err(errno, "Some error\n");
	return root;
}

static inline
struct node *build_tree(char *dir, struct node ***leaves,
			unsigned *size, unsigned int *current,
			struct arguments *args)
{
	return __build_tree(dir, leaves, size, current, args, 0);
}

void release_tree(struct node ***leaves_array, int length)
{
	struct node **leaves = *leaves_array;
	int i;

	for (i = 0; i < length; i++) {
		free(leaves[i]->name);
		free(leaves[i]);
	}
	free(leaves);
}

void get_path(char *path, struct node *leaf)
{
	if (leaf->parent == NULL) {
		sprintf(path, "%s/", leaf->name);
		return;
	}
	get_path(path, leaf->parent);
	strcat(path, leaf->name);
	if (leaf->type == DT_DIR)
		strcat(path, "/");
}

/* Shuffle array based on Fisher-Yates Algorithm */
void shuffle_leaves(struct node **array, size_t n)
{
    struct timeval tv;
    struct node *t;
    unsigned int i, j;

    gettimeofday(&tv, NULL);
    srand48(tv.tv_usec);

    if (n > 1) {
	    for (i = n - 1; i > 0; i--) {
		    j = (unsigned int) (drand48()*(i+1));
		    t = array[j];
		    array[j] = array[i];
		    array[i] = t;
	    }
    }
}

int matches_p(const char *string, const regex_t *pattern)
{
	if (regexec(pattern, string, 1, NULL, 0) == 0)
		return 1;

	return 0;
}

pthread_cond_t copy_wait;
pthread_mutex_t copy_lock;
int copied;
int current;

int copy_random(struct node **list, unsigned length, const char *src,
		const char *dest, const regex_t *pattern,
		struct arguments *args)
{
	unsigned int i = 0, n = 0, num = args->limit;
	struct timeval tv;
	char path[PATH_MAX], dpath[PATH_MAX], spath[PATH_MAX + 1];

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	for (; i < length && n < num; i++) {
		get_path(path, list[i]);
		sprintf(spath, "%s/%s", src, path);
		sprintf(dpath, "%s/%s", dest, list[i]->name);

		if (pattern && !matches_p(list[i]->name, pattern))
			continue;

		if (exists_p(dpath))
			continue;

		if (args->echo)
			printf("%s\n", path);

		if (!args->dry_run)
			cp(spath, dpath);
		n++;
		pthread_mutex_lock(&copy_lock);
		copied++;
		pthread_cond_signal(&copy_wait);
		pthread_mutex_unlock(&copy_lock);
	}

	return n;
}

void * print_progress(void *arg)
{
	struct progress_args *pargs = (struct progress_args *)arg;
	int limit = pargs->args->limit;

	pthread_mutex_lock(&copy_lock);
	while (copied != limit) {
		pthread_cond_wait(&copy_wait, &copy_lock);

		if (!pargs->args->echo) {
			printf("\rcopied %3.0f%%", (((float)copied/(float)limit)) * 100);
			fflush(stdout);
		}
	}
	pthread_mutex_unlock(&copy_lock);

	return NULL;
}

int main(int argc, char **argv)
{
	struct arguments args;
	DIR *src, *dst;
	struct node **leaves = NULL;
	unsigned int size = 0, length = 0, n;
	char *parent_dir;
	pthread_t progress;
	int ret, *retp, rflags = 0;
	regex_t regex;
	struct progress_args pargs;

	memset(&args, 0, sizeof(struct arguments));
	retp = &ret;
	program_name = argv[0];
	args.limit = 1;

	argp_parse(&argp, argc, argv, 0, 0, &args);

	src = opendir(args.paths[SRC]);
	if (!src)
		err(errno, "%s", args.paths[SRC]);

	dst = opendir(args.paths[DEST]);
	if (!dst)
		err(errno, "%s", args.paths[DEST]);

	/* Both the arguments are directories, lets close the src, since
	 * we will open it again*/
	closedir(src);
	closedir(dst);

	/* remove trailing '/' if any */
	if (args.paths[SRC][strlen(args.paths[SRC]) - 1] == '/')
		args.paths[SRC][strlen(args.paths[SRC]) - 1] = '\0';

	if (args.paths[DEST][strlen(args.paths[DEST]) - 1] == '/')
		args.paths[DEST][strlen(args.paths[DEST]) - 1] = '\0';

	/* We will compile the regexp before building the tree, so we will bail
	 * out early if the regex is invalid */
	rflags = REG_EXTENDED|REG_NOSUB;
	if (args.icase)
		rflags |= REG_ICASE;

	if (args.pattern && (ret = regcomp(&regex, args.pattern, rflags))) {
		char errbuf[REGERR_BUFSIZ];
		regerror(ret, &regex, errbuf, REGERR_BUFSIZ);
		fprintf(stderr, "%s\n", errbuf);
		regfree(&regex);
		return 2;
	}
	build_tree(args.paths[SRC], &leaves, &size, &length, &args);

	shuffle_leaves(leaves, length);
	parent_dir = dirname(args.paths[SRC]);

	pargs.leaves = leaves;
	pargs.args = &args;

	/* Lets start a progress bar, in a new thread */
	if ((ret = pthread_create(&progress, NULL, print_progress,
				  (void *)&pargs))) {
		errno = ret;
		warn("Cannot start progress thread");
	}

	n = copy_random(leaves, length, parent_dir, args.paths[DEST],
			args.pattern?&regex:NULL, &args);

	if (n < args.limit)
		pthread_cancel(progress);
	else
		pthread_join(progress, (void **)&retp);

	release_tree(&leaves, length);

	pthread_mutex_destroy(&copy_lock);
	pthread_cond_destroy(&copy_wait);

	if (args.pattern)
		regfree(&regex);

	printf("\rCopied %d files.\n", n);

	return 0;
}
