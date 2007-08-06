struct list {
    struct list *next, *prev;
};

static inline void list_init(struct list *head) {
    head->next = head->prev = head;
}

static inline int list_empty(struct list *head) {
    return head->next == head;
}

static inline struct list *list_head(struct list *head) {
    return head->next;
}

static inline struct list *list_tail(struct list *head) {
    return head->prev;
}

static inline void list_insert_between(struct list *a, struct list *list, struct list *b) {

    list->next = a->next;
    list->prev = b->prev;
    a->next = b->prev = list;
}

static inline void list_push(struct list *head, struct list *list) {
    list_insert_between(head->prev, list, head);
}

static inline void list_push_head(struct list *head, struct list *list) {
    list_insert_between(head, list, head->next);
}

static inline void list_remove(struct list *list) {
    list->next->prev = list->prev;
    list->prev->next = list->next;
}

static inline struct list *list_pop(struct list *head) {
    struct list *list = head->prev;
    list_remove(list);
    return list;
}

static inline struct list *list_pop_head(struct list *head) {
    struct list *list = head->next;
    list_remove(list);
    return list;
}

static inline void list_move(struct list *newhead, struct list *oldhead) {
    struct list *first = oldhead->next;
    struct list *last = oldhead->prev;
    assert(!list_empty(oldhead));
    newhead->next = first;
    newhead->prev = last;
    last->next = first->prev = newhead;
    list_init(oldhead);
}

#define list_struct(p, t, f) (t*)((char*)(p) - __builtin_offsetof(t, f))




