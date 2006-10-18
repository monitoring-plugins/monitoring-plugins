/* Header file for utils_disk */

#include "mountlist.h"
#include "utils_base.h"

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
  struct mount_entry *best_match;
  struct parameter_list *name_next;
};

void np_add_name (struct name_list **list, const char *name);
int np_find_name (struct name_list *list, const char *name);
int np_seen_name (struct name_list *list, const char *name);
struct parameter_list *np_add_parameter(struct parameter_list **list, const char *name);
int search_parameter_list (struct parameter_list *list, const char *name);
void np_set_best_match(struct parameter_list *desired, struct mount_entry *mount_list, int exact);
