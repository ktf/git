#include "cache.h"
#include "transport.h"

#include "run-command.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"

struct shim_data
{
	const char *name;
	struct child_process *shim;
};

static struct child_process *get_shim(struct transport *transport)
{
	struct shim_data *data = transport->data;
	if (!data->shim) {
		struct strbuf buf = STRBUF_INIT;
		struct child_process *shim = xcalloc(1, sizeof(*shim));
		shim->in = -1;
		shim->out = -1;
		shim->err = 0;
		shim->argv = xcalloc(4, sizeof(*shim->argv));
		strbuf_addf(&buf, "shim-%s", data->name);
		shim->argv[0] = buf.buf;
		shim->argv[1] = transport->remote->name;
		shim->argv[2] = transport->url;
		shim->git_cmd = 1;
		start_command(shim);
		data->shim = shim;
	}
	return data->shim;
}

static int disconnect_shim(struct transport *transport)
{
	struct shim_data *data = transport->data;
	if (data->shim) {
		write(data->shim->in, "\n", 1);
		close(data->shim->in);
		finish_command(data->shim);
		free(data->shim->argv);
		free(data->shim);
		transport->data = NULL;
	}
	return 0;
}

static int fetch_refs_via_shim(struct transport *transport,
			       int nr_heads, struct ref **to_fetch)
{
	struct child_process *shim;
	const struct ref *posn;
	struct strbuf buf = STRBUF_INIT;
	int i, count;
	FILE *file;

	count = 0;
	for (i = 0; i < nr_heads; i++) {
		posn = to_fetch[i];
		if (posn->status & REF_STATUS_UPTODATE)
			continue;
		count++;
	}

	if (count) {
		shim = get_shim(transport);
		for (i = 0; i < nr_heads; i++) {
			posn = to_fetch[i];
			if (posn->status & REF_STATUS_UPTODATE)
				continue;
			write(shim->in, "fetch ", 6);
			write(shim->in, sha1_to_hex(posn->old_sha1), 40);
			write(shim->in, " ", 1);
			write(shim->in, posn->name, strlen(posn->name));
			write(shim->in, "\n", 1);
		}
		file = fdopen(shim->out, "r");
		while (count) {
			if (strbuf_getline(&buf, file, '\n') == EOF)
				exit(128); /* child died, message supplied already */

			count--;
		}
	}
	return 0;
}

static struct ref *get_refs_via_shim(struct transport *transport, int for_push)
{
	struct child_process *shim;
	struct ref *ret = NULL;
	struct ref **end = &ret;
	struct ref *posn;
	struct strbuf buf = STRBUF_INIT;
	FILE *file;

	shim = get_shim(transport);
	write(shim->in, "list\n", 5);

	file = fdopen(shim->out, "r");
	while (1) {
		char *eov;
		if (strbuf_getline(&buf, file, '\n') == EOF)
			exit(128); /* child died, message supplied already */

		if (!*buf.buf)
			break;

		eov = strchr(buf.buf, ' ');
		if (!eov)
			die("Malformed response in ref list: %s", buf.buf);
		*eov = '\0';
		*end = alloc_ref(eov + 1);
		if (eov) {
			if (buf.buf[0] == '@')
				(*end)->symref = xstrdup(buf.buf + 1);
			else
				get_sha1_hex(buf.buf, (*end)->old_sha1);
		}
		end = &((*end)->next);
		strbuf_reset(&buf);
	}
	strbuf_release(&buf);

	for (posn = ret; posn; posn = posn->next)
		resolve_remote_symref(posn, ret);

	return ret;
}

void transport_shim_init(struct transport *transport, const char *name)
{
	struct shim_data *data = xmalloc(sizeof(*data));
	data->shim = NULL;
	data->name = name;
	transport->data = data;
	transport->get_refs_list = get_refs_via_shim;
	transport->fetch = fetch_refs_via_shim;
	transport->disconnect = disconnect_shim;
}
