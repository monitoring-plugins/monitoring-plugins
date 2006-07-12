/* Header file for utils_disk */

#include "mountlist.h"

struct name_list
{
  char *name;
  struct name_list *next;
};

struct parameter_list
{
  char *name;
  int found;
  int found_len;
  uintmax_t w_df;
  uintmax_t c_df;
  double w_dfp;
  double c_dfp;
  double w_idfp;
  double c_idfp;
  struct mount_entry *best_match;
  struct parameter_list *name_next;
};

void np_add_name (struct name_list **list, const char *name);
int np_find_name (struct name_list *list, const char *name);
int np_seen_name (struct name_list *list, const char *name);
struct parameter_list *np_add_parameter(struct parameter_list **list, const char *name);
int search_parameter_list (struct parameter_list *list, const char *name);
