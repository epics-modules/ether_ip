#ifndef DLL_init
/* Double-linked list */
   
typedef struct __dll_node DLL_Node;
struct __dll_node
{
    DLL_Node *prev;
    DLL_Node *next;
};

typedef struct
{
    DLL_Node *first;
    DLL_Node *last;
}   DL_List;

#define DLL_init(list)  \
{                       \
    (list)->first = 0;  \
    (list)->last = 0;   \
}

#define DLL_first(T, list) \
    (T*)(list)->first

#define DLL_last(T, list) \
    (T*)(list)->last

#define DLL_next(T, node)  \
    (T*)((DLL_Node*)(node))->next

#define DLL_prev(T, node)  \
    (T*)((DLL_Node*)(node))->prev

/* Add single node to end of list */
#define DLL_append(list, node)                   \
{                                                \
    if ((list)->first == 0)                      \
        (list)->first = (DLL_Node*)(node);       \
    ((DLL_Node*)(node))->prev = (list)->last;    \
    ((DLL_Node*)(node))->next = 0;               \
    if ((list)->last)                            \
        (list)->last->next = (DLL_Node*)(node);  \
    (list)->last = (DLL_Node*)(node);            \
}

/* Remove node from list (doesn't free node) */
#define DLL_unlink(list, node)                                       \
{                                                                    \
    if ((list)->first == (DLL_Node*)(node))                          \
        (list)->first = ((DLL_Node*)(node))->next;                   \
    else                                                             \
        ((DLL_Node*)(node))->prev->next = ((DLL_Node*)(node))->next; \
    if ((list)->last == (DLL_Node*)(node))                           \
        (list)->last =((DLL_Node*)(node))->prev;                     \
    else                                                             \
        ((DLL_Node*)(node))->next->prev = ((DLL_Node*)(node))->prev; \
    ((DLL_Node*)(node))->prev = 0;                                   \
    ((DLL_Node*)(node))->next = 0;                                   \
}

/* Remove head node from list (doesn't free node)
 * Returns 0 for empty list
 */
void *DLL_decap (DL_List *list);

#endif


