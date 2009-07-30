#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "run-command.h"

#define SEQ_DIR "rebase-merge"

#define PATCH_FILE	git_path(SEQ_DIR "/patch")

static const char * const git_sequencer_helper_usage[] = {
	"git sequencer--helper --make-patch <commit>",
	NULL
};

/* Generate purely informational patch file */
static void make_patch(struct commit *commit)
{
	struct commit_list *parents = commit->parents;
	const char **args;
	struct child_process chld;
	int i;
	int fd = open(PATCH_FILE, O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		return;

	memset(&chld, 0, sizeof(chld));
	if (!parents) {
		write(fd, "Root commit\n", 12);
		close(fd);
		return;
	} else if (!parents->next) {
		args = xcalloc(5, sizeof(char *));
		args[0] = "diff-tree";
		args[1] = "-p";
		args[2] = xstrdup(sha1_to_hex(parents->item->object.sha1));
		args[3] = xstrdup(sha1_to_hex(((struct object *)commit)->sha1));
	} else {
		int count = 1;

		for (; parents; parents = parents->next)
			++count;

		i = 0;
		args = xcalloc(count + 3, sizeof(char *));
		args[i++] = "diff";
		args[i++] = "--cc";
		args[i++] = xstrdup(sha1_to_hex(commit->object.sha1));

		for (parents = commit->parents; parents;
		     parents = parents->next) {
			char *hex = sha1_to_hex(parents->item->object.sha1);
			args[i++] = xstrdup(hex);
		}
	}

	chld.argv = args;
	chld.git_cmd = 1;
	chld.out = fd;

	/* Run, ignore errors. */
	if (!start_command(&chld))
		finish_command(&chld);

	for (i = 2; args[i]; i++)
		free((char *)args[i]);
	free(args);
}

/* Return a commit object of "arg" */
static struct commit *get_commit(const char *arg)
{
	unsigned char sha1[20];

	if (get_sha1(arg, sha1)) {
		error("Could not find '%s'", arg);
		return NULL;
	}
	return lookup_commit_reference(sha1);
}

int cmd_sequencer__helper(int argc, const char **argv, const char *prefix)
{
	char *commit = NULL;
	struct commit *c;
	struct option options[] = {
		OPT_STRING(0, "make-patch", &commit, "commit",
			   "create a patch from commit"),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_sequencer_helper_usage, 0);

	if (!commit)
		usage_with_options(git_sequencer_helper_usage, options);

	c = get_commit(commit);
	if (!c)
		return 1;

	make_patch(c);

	return 0;
}
