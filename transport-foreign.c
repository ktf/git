#include "cache.h"
#include "transport.h"

#include "run-command.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"

struct foreign_data
{
	struct child_process *importer;
};

static struct child_process *get_importer(struct transport *transport)
{
	struct child_process *importer = transport->data;
	if (!importer) {
		struct strbuf buf;
		importer = xcalloc(1, sizeof(*importer));
		importer->in = -1;
		importer->out = -1;
		importer->err = 0;
		importer->argv = xcalloc(3, sizeof(*importer->argv));
		strbuf_init(&buf, 80);
		strbuf_addf(&buf, "vcs-%s", transport->remote->foreign_vcs);
		importer->argv[0] = buf.buf;
		importer->argv[1] = transport->remote->name;
		importer->git_cmd = 1;
		start_command(importer);
		transport->data = importer;
	}
	return importer;
}

static int disconnect_foreign(struct transport *transport)
{
	struct child_process *importer = transport->data;
	if (importer) {
		write(importer->in, "\n", 1);
		close(importer->in);
		finish_command(importer);
		free(importer);
		transport->data = NULL;
	}
	return 0;
}

static int fetch_refs_via_foreign(struct transport *transport,
				  int nr_heads, struct ref **to_fetch)
{
	struct child_process *importer;
	struct child_process fastimport;
	struct ref *posn;
	int i, count;

	count = 0;
	for (i = 0; i < nr_heads; i++) {
		posn = to_fetch[i];
		if (posn->status & REF_STATUS_UPTODATE)
			continue;
		count++;
	}
	if (count) {
		importer = get_importer(transport);

		memset(&fastimport, 0, sizeof(fastimport));
		fastimport.in = importer->out;
		fastimport.argv = xcalloc(3, sizeof(*fastimport.argv));
		fastimport.argv[0] = "fast-import";
		fastimport.argv[1] = "--quiet";
		fastimport.git_cmd = 1;
		start_command(&fastimport);

		for (i = 0; i < nr_heads; i++) {
			posn = to_fetch[i];
			if (posn->status & REF_STATUS_UPTODATE)
				continue;
			write(importer->in, "import ", 7);
			write(importer->in, posn->name, strlen(posn->name));
			write(importer->in, "\n", 1);
		}
		disconnect_foreign(transport);
		finish_command(&fastimport);
	}
	for (i = 0; i < nr_heads; i++) {
		posn = to_fetch[i];
		if (posn->status & REF_STATUS_UPTODATE)
			continue;
		read_ref(posn->name, posn->old_sha1);
	}
	return 0;
}

static struct ref *get_refs_via_foreign(struct transport *transport, int for_push)
{
	struct child_process *importer;
	struct ref *ret = NULL;
	struct ref **end = &ret;
	struct strbuf buf;
	FILE *file;

	importer = get_importer(transport);
	write(importer->in, "list\n", 5);

	strbuf_init(&buf, 0);
	file = fdopen(importer->out, "r");
	while (1) {
		char *eon;
		if (strbuf_getline(&buf, file, '\n') == EOF)
			break;

		if (!*buf.buf)
			break;

		eon = strchr(buf.buf, ' ');
		if (eon)
			*eon = '\0';
		*end = alloc_ref(buf.buf);
		if (eon) {
			if (strstr(eon + 1, "unchanged")) {
				(*end)->status |= REF_STATUS_UPTODATE;
				if (read_ref((*end)->name, (*end)->old_sha1))
					die("Unchanged?");
				fprintf(stderr, "Old: %p %s\n", *end, sha1_to_hex((*end)->old_sha1));
			}
		}
		end = &((*end)->next);
		strbuf_reset(&buf);
	}

	strbuf_release(&buf);
	return ret;
}

static int foreign_push(struct transport *transport, struct ref *remote_refs, int flags) {
	struct ref *ref, *has;
	struct child_process *importer;
	struct rev_info revs;
	struct commit *commit;
	struct child_process fastimport;

	importer = get_importer(transport);

	memset(&fastimport, 0, sizeof(fastimport));
	fastimport.in = importer->out;
	fastimport.argv = xcalloc(3, sizeof(*fastimport.argv));
	fastimport.argv[0] = "fast-import";
	fastimport.argv[1] = "--quiet";
	fastimport.git_cmd = 1;
	start_command(&fastimport);
	for (ref = remote_refs; ref; ref = ref->next) {
		if (!ref->peer_ref) {
			ref->status = REF_STATUS_NONE;
			continue;
		}
		init_revisions(&revs, NULL);
		revs.reverse = 1;
		for (has = remote_refs; has; has = has->next) {
			commit = lookup_commit(has->old_sha1);
			commit->object.flags |= UNINTERESTING;
			add_pending_object(&revs, &commit->object, has->name);
		}
		commit = lookup_commit(ref->peer_ref->new_sha1);
		add_pending_object(&revs, &commit->object, ref->name);

		if (prepare_revision_walk(&revs))
			die("Something wrong");

		ref->status = REF_STATUS_UPTODATE;
		while ((commit = get_revision(&revs))) {
			ref->status = REF_STATUS_EXPECTING_REPORT;
			fprintf(stderr, "export %s %s\n", sha1_to_hex(commit->object.sha1), ref->name);
			write(importer->in, "export ", 7);
			write(importer->in, sha1_to_hex(commit->object.sha1), 40);
			write(importer->in, " ", 1);
			write(importer->in, ref->name, strlen(ref->name));
			write(importer->in, "\n", 1);
		}
	}

	disconnect_foreign(transport);
	finish_command(&fastimport);

	for (ref = remote_refs; ref; ref = ref->next) {
		read_ref(ref->name, ref->new_sha1);
		if (ref->status == REF_STATUS_EXPECTING_REPORT)
			ref->status = REF_STATUS_OK;
	}

	return 0;
}

void transport_foreign_init(struct transport *transport)
{
	transport->get_refs_list = get_refs_via_foreign;
	transport->fetch = fetch_refs_via_foreign;
	transport->push_refs = foreign_push;
	transport->disconnect = disconnect_foreign;
	transport->url = transport->remote->foreign_vcs;
}
