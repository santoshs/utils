/*
 * randcp.c - A random file copy program
 *
 * Copyright (C) 2013  Santosh Sivaraj <santosh@fossix.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA
 */

#define _ISOC99_SOURCE
#define _BSD_SOURCE		/* for d_type constants */

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

#define SRC 0
#define DEST 1

char *program_name;
const char *argp_program_version = "randcp 0.1";
const char *argp_program_bug_address = "<santosh@fossix.org>";
static char doc[] = "randcp -- Copy random files";
static char args_doc[] =
	"SOURCE DEST";

static struct argp_option options[] = {
	{"limit", 'l', "LIMIT", 0, "Limit number of copied files"},
};

struct arguments {
	unsigned long limit;
	char *paths[2];
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *arguments = state->input;

	switch (key) {
	case 'l':
		arguments->limit = strtoul(arg, NULL, 10);
		if (errno)
			err(errno, "Invalid limit");
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

static struct argp argp = {options, parse_opt, args_doc, doc};

struct node {
	int type;
	char *name;
	struct node *parent;
};

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

struct node *build_tree(char *dir, struct node ***leaves,
			unsigned *size, unsigned int *current)
{
	DIR *d;
	struct dirent *ent;
	struct node *root = NULL, *node;
	char path[PATH_MAX];

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

		snprintf(path, NAME_MAX, "%s/%s", dir, ent->d_name);
		if (is_dir(ent, path)) {
			node = build_tree(path, leaves, size, current);
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
	int s = 0;

	if (leaf->parent == NULL) {
		sprintf(path, "%s/", leaf->name);
		return;
	}
	get_path(path, leaf->parent);
	strcat(path, leaf->name);
	if (leaf->type == DT_DIR)
		strcat(path, "/");
}

int copy_random(struct node **list, unsigned length, unsigned num,
		const char *dest)
{
	unsigned int i = 0, n;
	struct timeval tv;
	char path[PATH_MAX], dpath[PATH_MAX];

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	while (i < length && i < num) {
		n = random() % length;
		get_path(path, list[n]);
		sprintf(dpath, "%s/%s", dest, list[n]->name);
		printf("%s to %s\n", path, dpath);
		cp(path, dpath);
		i++;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct arguments args;
	DIR *src, *dst;
	struct node **leaves = NULL;
	unsigned int size = 0, length = 0;

	program_name = argv[0];
	args.limit = 1;
	args.paths[SRC] = NULL;
	args.paths[DEST] = NULL;

	argp_parse(&argp, argc, argv, 0, 0, &args);

	src = opendir(args.paths[SRC]);
	if (!src)
		err(errno, "%s", args.paths[SRC]);

	dst = opendir(args.paths[DEST]);
	if (!dst)
		err(errno, "%s", args.paths[DEST]);

	/* Both the arguments are directories, lets close the src, since
	 * scandir will open it */
	closedir(src);
	closedir(dst);

	/* remove trailing '/' if any */
	if (args.paths[SRC][strlen(args.paths[SRC]) - 1] == '/')
		args.paths[SRC][strlen(args.paths[SRC]) - 1] = '\0';

	if (args.paths[DEST][strlen(args.paths[DEST]) - 1] == '/')
		args.paths[DEST][strlen(args.paths[DEST]) - 1] = '\0';


	build_tree(args.paths[SRC], &leaves, &size, &length);

	copy_random(leaves, length, args.limit, args.paths[DEST]);

	release_tree(&leaves, length);

	return 0;
}
