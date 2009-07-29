#include "cache.h"
#include "commit.h"
#include "notes.h"
#include "refs.h"
#include "utf8.h"
#include "strbuf.h"
#include "tree-walk.h"

struct entry {
	unsigned char commit_sha1[20];
	unsigned char notes_sha1[20];
};

struct hash_map {
	struct entry *entries;
	off_t count, size;
};

struct subtree_entry {
	/*
	 * SHA1 prefix is stored in the first 19 bytes (w/trailing NUL bytes);
	 * length of SHA1 prefix is stored in the last byte
	 */
	unsigned char sha1_prefix_w_len[20];
	unsigned char subtree_sha1[20];
	struct subtree_entry *next;
};

static int initialized;
static struct hash_map hash_map;
static struct subtree_entry *subtree_list;

static int hash_index(struct hash_map *map, const unsigned char *sha1)
{
	int i = ((*(unsigned int *)sha1) % map->size);

	for (;;) {
		unsigned char *current = map->entries[i].commit_sha1;

		if (!hashcmp(sha1, current))
			return i;

		if (is_null_sha1(current))
			return -1 - i;

		if (++i == map->size)
			i = 0;
	}
}

static void add_entry(const unsigned char *commit_sha1,
		const unsigned char *notes_sha1)
{
	int index;

	if (hash_map.count + 1 > hash_map.size >> 1) {
		int i, old_size = hash_map.size;
		struct entry *old = hash_map.entries;

		hash_map.size = old_size ? old_size << 1 : 64;
		hash_map.entries = (struct entry *)
			xcalloc(sizeof(struct entry), hash_map.size);

		for (i = 0; i < old_size; i++)
			if (!is_null_sha1(old[i].commit_sha1)) {
				index = -1 - hash_index(&hash_map,
						old[i].commit_sha1);
				memcpy(hash_map.entries + index, old + i,
					sizeof(struct entry));
			}
		free(old);
	}

	index = hash_index(&hash_map, commit_sha1);
	if (index < 0) {
		index = -1 - index;
		hash_map.count++;
	}

	hashcpy(hash_map.entries[index].commit_sha1, commit_sha1);
	hashcpy(hash_map.entries[index].notes_sha1, notes_sha1);
}

/*
 * Convert a partial SHA1 sum (hex format) to a SHA1 value.
 * - hex      - ASCII hex SHA1 segment
 * - hex_len  - Length of above segment. Must be multiple of 2 between 0 and 40
 * - sha1     - Value of SHA1 is written here
 * - sha1_len - Max #bytes to store in sha1, Must be between 0 and 20,
 *              and >= hex_len / 2
 * Returns -1 on error (invalid arguments or invalid ASCII hex SHA1 format).
 * Otherwise, returns number of bytes written to sha1 (hex_len / 2).
 * Pads sha1 with NULs up to sha1_len (not included in returned length).
 */
static int get_sha1_hex_segment(const char *hex, unsigned int hex_len,
		unsigned char *sha1, unsigned int sha1_len)
{
	unsigned int i, len = hex_len >> 1;
	if (hex_len % 2 != 0 || len > sha1_len)
		return -1;
	for (i = 0; i < len; i++) {
		unsigned int val = (hexval(hex[0]) << 4) | hexval(hex[1]);
		if (val & ~0xff)
			return -1;
		*sha1++ = val;
		hex += 2;
	}
	for (; i < sha1_len; i++)
		*sha1++ = 0;
	return len;
}

static void load_subtree(struct subtree_entry *se)
{
	unsigned char commit_sha1[20];
	unsigned int prefix_len;
	void *buf;
	struct tree_desc desc;
	struct name_entry entry;
	struct subtree_entry *tmp_list = NULL, *tmp_last = NULL;

	buf = fill_tree_descriptor(&desc, se->subtree_sha1);
	if (!buf)
		die("Could not read %s for notes-index",
		     sha1_to_hex(se->subtree_sha1));

	prefix_len = se->sha1_prefix_w_len[19];
	memcpy(commit_sha1, se->sha1_prefix_w_len, prefix_len);
	while (tree_entry(&desc, &entry)) {
		int len = get_sha1_hex_segment(entry.path, strlen(entry.path),
				commit_sha1 + prefix_len, 20 - prefix_len);
		if (len < 0)
			continue; /* entry.path is not a SHA1 sum. Skip */
		len += prefix_len;

		/* If commit SHA1 is complete, assume note object */
		if (len == 20)
			add_entry(commit_sha1, entry.sha1);
		/* If commit SHA1 is incomplete, assume note subtree */
		else if (len < 20 && entry.mode == S_IFDIR) {
			struct subtree_entry *n = (struct subtree_entry *)
				xcalloc(sizeof(struct subtree_entry), 1);
			hashcpy(n->sha1_prefix_w_len, commit_sha1);
			n->sha1_prefix_w_len[19] = (unsigned char) len;
			hashcpy(n->subtree_sha1, entry.sha1);

			if (!tmp_list) {
				tmp_list = n;
				tmp_last = n;
			}
			else {
				assert(!tmp_last->next);
				assert(hashcmp(n->sha1_prefix_w_len,
					tmp_last->sha1_prefix_w_len) > 0);
				tmp_last->next = n;
				tmp_last = n;
			}
		}
	}
	free(buf);
	if (tmp_list) {
		/* insert tmp_list immediately after se */
		assert(hashcmp(tmp_list->sha1_prefix_w_len,
				se->sha1_prefix_w_len) > 0);
		if (se->next) {
			assert(hashcmp(se->next->sha1_prefix_w_len,
					tmp_last->sha1_prefix_w_len) > 0);
			tmp_last->next = se->next;
		}
		se->next = tmp_list;
	}
}

static void initialize_hash_map(const char *notes_ref_name)
{
	unsigned char sha1[20], commit_sha1[20];
	unsigned mode;
	struct subtree_entry root_tree;

	if (!notes_ref_name || read_ref(notes_ref_name, commit_sha1) ||
	    get_tree_entry(commit_sha1, "", sha1, &mode))
		return;

	hashclr(root_tree.sha1_prefix_w_len);
	hashcpy(root_tree.subtree_sha1, sha1);
	root_tree.next = NULL;
	load_subtree(&root_tree);
	subtree_list = root_tree.next;
}

/*
 * Compare the given commit SHA1 against the given subtree entry.
 * Return -1 if the commit SHA1 cannot exist within the given subtree, or any
 * subtree following it.
 * Return 0 if the commit SHA1 _may_ exist within the given subtree.
 * Return 1 if the commit SHA1 cannot exist within the given subtree, but may
 * exist within a subtree following it.
 */
static int commit_subtree_cmp(const unsigned char *commit_sha1,
		const struct subtree_entry *entry)
{
	unsigned int prefix_len = entry->sha1_prefix_w_len[19];
	return memcmp(commit_sha1, entry->sha1_prefix_w_len, prefix_len);
}

static struct subtree_entry *lookup_subtree(const unsigned char *commit_sha1)
{
	struct subtree_entry *found = NULL, *cur = subtree_list;
	while (cur) {
		int cmp = commit_subtree_cmp(commit_sha1, cur);
		if (!cmp)
			found = cur;
		if (cmp < 0)
			break;
		cur = cur->next;
	}
	return found;
}

static unsigned char *lookup_notes(const unsigned char *commit_sha1)
{
	int index;
	struct subtree_entry *subtree;

	/* First, try to find the commit SHA1 directly in hash map */
	index = hash_map.size ? hash_index(&hash_map, commit_sha1) : -1;
	if (index >= 0)
		return hash_map.entries[index].notes_sha1;

	/* Next, try finding a subtree that may contain the commit SHA1 */
	subtree = lookup_subtree(commit_sha1);

	/* Give up if no subtree found, or if subtree is already loaded */
	if (!subtree || is_null_sha1(subtree->subtree_sha1))
		return NULL;

	/* Load subtree into hash_map, and retry lookup recursively */
	load_subtree(subtree);
	hashclr(subtree->subtree_sha1);
	return lookup_notes(commit_sha1);
}

void get_commit_notes(const struct commit *commit, struct strbuf *sb,
		const char *output_encoding)
{
	static const char utf8[] = "UTF-8";
	unsigned char *sha1;
	char *msg, *msg_p;
	unsigned long linelen, msglen;
	enum object_type type;

	if (!initialized) {
		const char *env = getenv(GIT_NOTES_REF_ENVIRONMENT);
		if (env)
			notes_ref_name = getenv(GIT_NOTES_REF_ENVIRONMENT);
		else if (!notes_ref_name)
			notes_ref_name = GIT_NOTES_DEFAULT_REF;
		initialize_hash_map(notes_ref_name);
		initialized = 1;
	}

	sha1 = lookup_notes(commit->object.sha1);
	if (!sha1)
		return;

	if (!(msg = read_sha1_file(sha1, &type, &msglen)) || !msglen ||
			type != OBJ_BLOB)
		return;

	if (output_encoding && *output_encoding &&
			strcmp(utf8, output_encoding)) {
		char *reencoded = reencode_string(msg, output_encoding, utf8);
		if (reencoded) {
			free(msg);
			msg = reencoded;
			msglen = strlen(msg);
		}
	}

	/* we will end the annotation by a newline anyway */
	if (msglen && msg[msglen - 1] == '\n')
		msglen--;

	strbuf_addstr(sb, "\nNotes:\n");

	for (msg_p = msg; msg_p < msg + msglen; msg_p += linelen + 1) {
		linelen = strchrnul(msg_p, '\n') - msg_p;

		strbuf_addstr(sb, "    ");
		strbuf_add(sb, msg_p, linelen);
		strbuf_addch(sb, '\n');
	}

	free(msg);
}
