#include "dl_list.h"

/* Remove head node from list (doesn't free node)
 * Returns 0 for empty list
 */
void *DLL_decap (DL_List *list)
{
    DLL_Node *node = list->first;
    if (! node)
        return 0;
    
    list->first = node->next;
    if (list->last == node)
        list->last = 0;
    return node;
}

