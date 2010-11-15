/* Header file for utils_disk */

#include "mountlist.h"
#include "utils_base.h"
#include "regex.h"

struct name_list
{
  char *name;
  struct name_list *next;
};

struct parameter_list
{
  char *name;
  thresholds *freespace_bytes;
  thresholds *freespace_units;
  thresholds *freespace_percent;
  thresholds *usedspace_bytes;
  thresholds *usedspace_units;
  thresholds *usedspace_percent;
  thresholds *usedinodes_percent;
  thresholds *freeinodes_percent;
  char *group;
  struct mount_entry *best_match;
  struct parameter_list *name_next;
  uintmax_t total, available, available_to_root, used, inodes_free, inodes_total;
  double dfree_pct, dused_pct;
  double dused_units, dfree_units, dtotal_units;
  double dused_inodes_percent, dfree_inodes_percent;
};

void np_add_name (struct name_list **list, const char *name);
int np_find_name (struct name_list *list, const char *name);
int np_seen_name (struct name_list *list, const char *name);
struct parameter_list *np_add_parameter(struct parameter_list **list, const char *name);
struct parameter_list *np_find_parameter(struct parameter_list *list, const char *name);
struct parameter_list *np_del_parameter(struct parameter_list *item, struct parameter_list *prev);
  
int search_parameter_list (struct parameter_list *list, const char *name);
void np_set_best_match(struct parameter_list *desired, struct mount_entry *mount_list, int exact);
int np_regex_match_mount_entry (struct mount_entry* me, regex_t* re);
