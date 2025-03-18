/* Header file for utils_disk */

#include "../../config.h"
#include "../../gl/mountlist.h"
#include "../../lib/utils_base.h"
#include "regex.h"
#include <stdint.h>

struct name_list {
	char *name;
	struct name_list *next;
};

struct regex_list {
	regex_t regex;
	struct regex_list *next;
};

struct parameter_list {
	char *name;
	char *group;

	thresholds *freespace_units;
	thresholds *freespace_percent;
	thresholds *usedspace_units;
	thresholds *usedspace_percent;

	thresholds *usedinodes_percent;
	thresholds *freeinodes_percent;

	struct mount_entry *best_match;

	uintmax_t total;
	uintmax_t available;
	uintmax_t available_to_root;
	uintmax_t used;
	uintmax_t inodes_free;
	uintmax_t inodes_free_to_root;
	uintmax_t inodes_used;
	uintmax_t inodes_total;

	double dfree_pct;
	double dused_pct;

	uint64_t dused_units;
	uint64_t dfree_units;
	uint64_t dtotal_units;

	double dused_inodes_percent;
	double dfree_inodes_percent;

	struct parameter_list *name_next;
	struct parameter_list *name_prev;
};

void np_add_name(struct name_list **list, const char *name);
bool np_find_name(struct name_list *list, const char *name);
bool np_seen_name(struct name_list *list, const char *name);
int np_add_regex(struct regex_list **list, const char *regex, int cflags);
bool np_find_regmatch(struct regex_list *list, const char *name);

struct parameter_list *np_add_parameter(struct parameter_list **list, const char *name);
struct parameter_list *np_find_parameter(struct parameter_list *list, const char *name);
struct parameter_list *np_del_parameter(struct parameter_list *item, struct parameter_list *prev);
struct parameter_list parameter_list_init(const char *);

int search_parameter_list(struct parameter_list *list, const char *name);
void np_set_best_match(struct parameter_list *desired, struct mount_entry *mount_list, bool exact);
bool np_regex_match_mount_entry(struct mount_entry *, regex_t *);
