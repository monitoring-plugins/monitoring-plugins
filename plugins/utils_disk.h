/* Header file for utils_disk */


struct name_list
{
  char *name;
  struct name_list *next;
};

void np_add_name (struct name_list **list, const char *name);
int np_find_name (struct name_list *list, const char *name);

