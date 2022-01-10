
typedef struct {
	char *label;
} perfdata;

typedef struct {
	perfdata data;
	perfdata* next;
} pd_list;
