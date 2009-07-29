#include "cache.h"
#include "branch.h"
#include "refs.h"
#include "remote.h"
#include "commit.h"

struct tracking {
	struct refspec spec;
	char *src;
	struct remote *remote;
	int matches;
};

static int find_tracked_branch(struct remote *remote, void *priv)
{
	struct tracking *tracking = priv;

	if (!remote_find_tracking(remote, &tracking->spec)) {
		if (++tracking->matches == 1) {
			tracking->src = tracking->spec.src;
			tracking->remote = remote;
		} else {
			free(tracking->spec.src);
			if (tracking->src) {
				free(tracking->src);
				tracking->src = NULL;
			}
		}
		tracking->spec.src = NULL;
	}

	return 0;
}

static int should_setup_rebase(struct remote *origin)
{
	switch (origin ? origin->track.rebase : git_branch_track.rebase) {
	case AUTOREBASE_NEVER:
		return 0;
	case AUTOREBASE_LOCAL:
		return origin == NULL;
	case AUTOREBASE_REMOTE:
		return origin != NULL;
	case AUTOREBASE_ALWAYS:
		return 1;
	}
	return 0;
}

void install_branch_config(int flag, const char *local, struct remote *remote,
			   const char *merge)
{
	struct strbuf key = STRBUF_INIT;
	struct strbuf value = STRBUF_INIT;
	int rebasing = should_setup_rebase(remote);

	strbuf_addf(&key, "branch.%s.remote", local);
	git_config_set(key.buf, remote ? remote->name : ".");

	strbuf_reset(&key);
	strbuf_addf(&key, "branch.%s.merge", local);
	git_config_set(key.buf, merge);

	if (remote && remote->track.push) {
		strbuf_reset(&key);
		strbuf_addf(&key, "remote.%s.push", remote->name);
		strbuf_addf(&value, "refs/heads/%s:%s", local, merge);
		git_config_set_multivar(key.buf, value.buf, "^$", 0);
	}

	if (rebasing) {
		strbuf_reset(&key);
		strbuf_addf(&key, "branch.%s.rebase", local);
		git_config_set(key.buf, "true");
	}

	if (flag & BRANCH_CONFIG_VERBOSE) {
		strbuf_reset(&key);

		strbuf_addstr(&key, remote ? "remote" : "local");

		/* Are we tracking a proper "branch"? */
		if (!prefixcmp(merge, "refs/heads/"))
			strbuf_addf(&key, " branch %s", merge + 11);
		else
			strbuf_addf(&key, " ref %s", merge);
		if (remote)
			strbuf_addf(&key, " from %s", remote->name);
		printf("Branch %s set up to track %s%s.\n",
		       local, key.buf,
		       rebasing ? " by rebasing" : "");
	}
	strbuf_release(&key);
	strbuf_release(&value);
}

static void strbuf_addstr_escape_re (struct strbuf *buf, const char *add)
{
	const char *p = add;
	while ((add = strpbrk(add, ".*?+^$(){}[]")) != NULL) {
		strbuf_add(buf, p, add - p);
		strbuf_addf(buf, "\\%c", *add++);
		p = add;
	}
	strbuf_addstr(buf, p);
}

void delete_branch_config (const char *name)
{
	struct strbuf buf = STRBUF_INIT;
	struct strbuf push_re = STRBUF_INIT;
	struct branch *branch;

	if (prefixcmp(name, "refs/heads/"))
		return;

	/* git config --unset-all remote.foo.push ^\+?refs/heads/bar:  */
	branch = branch_get(name + 11);
	strbuf_addf(&buf, "remote.%s.push", branch->remote_name);
	strbuf_addstr(&push_re, "^\\+?");
	strbuf_addstr_escape_re(&push_re, name);
	strbuf_addch(&push_re, ':');
	if (git_config_set_multivar(buf.buf, NULL, push_re.buf, 1) < 0) {
		warning("Update of config-file failed");
		goto fail;
	}
	strbuf_reset(&buf);
	strbuf_addf(&buf, "branch.%s", name + 11);
	if (git_config_rename_section(buf.buf, NULL) < 0)
		warning("Update of config-file failed");

fail:
	strbuf_release(&push_re);
	strbuf_release(&buf);
}

/*
 * This is called when new_ref is branched off of orig_ref, and tries
 * to infer the settings for branch.<new_ref>.{remote,merge} from the
 * config.
 */
static int setup_tracking(const char *new_ref, const char *orig_ref,
                          enum branch_track track)
{
	struct tracking tracking;

	if (strlen(new_ref) > 1024 - 7 - 7 - 1)
		return error("Tracking not set up: name too long: %s",
				new_ref);

	memset(&tracking, 0, sizeof(tracking));
	tracking.spec.dst = (char *)orig_ref;
	if (for_each_remote(find_tracked_branch, &tracking))
		return 1;

	if (track == BRANCH_TRACK_UNSPECIFIED) {
		track = (tracking.remote
			 ? tracking.remote->track.merge : git_branch_track.merge);
		if (!track)
			return 0;
	}

	if (!tracking.matches)
		switch (track) {
		case BRANCH_TRACK_ALWAYS:
		case BRANCH_TRACK_EXPLICIT:
			break;
		default:
			return 1;
		}

	if (tracking.matches > 1)
		return error("Not tracking: ambiguous information for ref %s",
				orig_ref);

	if (!tracking.src)
		tracking.src = xstrdup (orig_ref);

	install_branch_config(BRANCH_CONFIG_VERBOSE, new_ref, tracking.remote,
			      tracking.src);
	free(tracking.src);
	return 0;
}

void create_branch(const char *head,
		   const char *name, const char *start_name,
		   int force, int reflog, enum branch_track track)
{
	struct ref_lock *lock;
	struct commit *commit;
	unsigned char sha1[20];
	char *real_ref, msg[PATH_MAX + 20];
	struct strbuf ref = STRBUF_INIT;
	int forcing = 0;

	if (strbuf_check_branch_ref(&ref, name))
		die("'%s' is not a valid branch name.", name);

	if (resolve_ref(ref.buf, sha1, 1, NULL)) {
		if (!force)
			die("A branch named '%s' already exists.", name);
		else if (!is_bare_repository() && !strcmp(head, name))
			die("Cannot force update the current branch.");
		forcing = 1;
	}

	real_ref = NULL;
	if (get_sha1(start_name, sha1))
		die("Not a valid object name: '%s'.", start_name);

	switch (dwim_ref(start_name, strlen(start_name), sha1, &real_ref)) {
	case 0:
		/* Not branching from any existing branch */
		if (track == BRANCH_TRACK_EXPLICIT)
			die("Cannot setup tracking information; starting point is not a branch.");
		break;
	case 1:
		/* Unique completion -- good, only if it is a real ref */
		if (track == BRANCH_TRACK_EXPLICIT && !strcmp(real_ref, "HEAD"))
			die("Cannot setup tracking information; starting point is not a branch.");
		break;
	default:
		die("Ambiguous object name: '%s'.", start_name);
		break;
	}

	if ((commit = lookup_commit_reference(sha1)) == NULL)
		die("Not a valid branch point: '%s'.", start_name);
	hashcpy(sha1, commit->object.sha1);

	lock = lock_any_ref_for_update(ref.buf, NULL, 0);
	if (!lock)
		die_errno("Failed to lock ref for update");

	if (reflog)
		log_all_ref_updates = 1;

	if (forcing)
		snprintf(msg, sizeof msg, "branch: Reset from %s",
			 start_name);
	else
		snprintf(msg, sizeof msg, "branch: Created from %s",
			 start_name);

	if (real_ref && track)
		setup_tracking(name, real_ref, track);

	if (write_ref_sha1(lock, sha1, msg) < 0)
		die_errno("Failed to write ref");

	strbuf_release(&ref);
	free(real_ref);
}

void remove_branch_state(void)
{
	unlink(git_path("MERGE_HEAD"));
	unlink(git_path("MERGE_RR"));
	unlink(git_path("MERGE_MSG"));
	unlink(git_path("MERGE_MODE"));
	unlink(git_path("SQUASH_MSG"));
}
