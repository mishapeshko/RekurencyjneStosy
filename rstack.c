// Library implementing recursive stacks.
// Uses reference counting and the Trial Deletion algorithm (Bacon) to detect and remove isolated cycles.

#include "rstack.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>

// Types of elements that can be stored on the stack
typedef enum {
    ITEM_IS_NUMBER,
    ITEM_IS_RSTACK
} node_kind_t;

// Structure of a single node in the list
typedef struct stack_node {
    struct stack_node *next;
    node_kind_t kind;
    union {
        struct rstack *rs_ptr;
        uint64_t num_val;
    };
} stack_node_t;

// Constants used by the Garbage Collector to track cycles
#define GC_SAFE   0
#define GC_CHECK  1
#define GC_TRASH  2

// Main structure representing the stack
struct rstack {
    stack_node_t *top_node;    // Pointer to the top of the stack.
    struct rstack *free_chain; // Helper pointer used by the GC to build the garbage list.
    size_t references;         // Reference counter.
    int gc_status;             // Node state in the cycle detection algorithm.
    bool dfs_flag;             // DFS flag: true when the node was visited during the current rstack_front call.
    bool is_visited;           // Re-entry lock flag during DFS.
};

// Helper function: reverses the direction of the links in the node list.
// Used during writing, to print elements starting from the bottom of the stack.
static stack_node_t *invert_list_order(stack_node_t *head) {
    stack_node_t *prev = nullptr, *curr = head;
    while (curr) {
        stack_node_t *next_node = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next_node;
    }
    return prev;
}

// Creates a new, empty stack and initializes its fields.
rstack_t *rstack_new(void) {
    rstack_t *rs = malloc(sizeof(rstack_t));
    if (!rs) {
        errno = ENOMEM;
        return nullptr;
    }
    
    rs->references = 1;
    rs->gc_status = GC_SAFE;
    rs->is_visited = false;
    rs->dfs_flag = false;
    rs->top_node = nullptr;
    rs->free_chain = nullptr;

    return rs;
}

// Phase 1 of the Trial Deletion algorithm: marks nodes for checking and temporarily subtracts internal references.
static void mark_suspects(rstack_t *st) {
    if (st->gc_status == GC_CHECK) return;
    
    st->gc_status = GC_CHECK;

    for (stack_node_t *node = st->top_node; node; node = node->next) {
        if (node->kind == ITEM_IS_RSTACK && node->rs_ptr) {
            node->rs_ptr->references--;
            mark_suspects(node->rs_ptr);
        }
    }
}

// Restores the GC_SAFE state and restores references for "alive" nodes.
static void restore_alive(rstack_t *st) {
    st->gc_status = GC_SAFE;

    for (stack_node_t *node = st->top_node; node; node = node->next) {
        if (node->kind == ITEM_IS_RSTACK && node->rs_ptr) {
            node->rs_ptr->references++;
            if (node->rs_ptr->gc_status != GC_SAFE) {
                restore_alive(node->rs_ptr);
            }
        }
    }
}

// Phase 2 of the Trial Deletion algorithm: verifies nodes and decides which ones form an isolated cycle.
static void verify_isolation(rstack_t *st) {
    if (st->gc_status != GC_CHECK) return;

    if (st->references == 0) {
        st->gc_status = GC_TRASH;
        for (stack_node_t *node = st->top_node; node; node = node->next) {
            if (node->kind == ITEM_IS_RSTACK && node->rs_ptr) {
                verify_isolation(node->rs_ptr);
            }
        }
    } else {
        restore_alive(st);
    }
}

// Phase 3 of the Trial Deletion algorithm: collects the nodes to be removed onto the trash_list.
static void gather_garbage(rstack_t *st, rstack_t **trash_list) {
    if (st->gc_status != GC_TRASH) return;

    st->gc_status = GC_SAFE;
    st->free_chain = *trash_list;
    *trash_list = st;

    stack_node_t *node = st->top_node;
    st->top_node = nullptr;

    while (node) {
        stack_node_t *next_node = node->next;
        if (node->kind == ITEM_IS_RSTACK && node->rs_ptr) {
            gather_garbage(node->rs_ptr, trash_list);
        }
        free(node);
        node = next_node;
    }
}

// Main function that removes a reference to the stack.
void rstack_delete(rstack_t *rs) {
    if (!rs) return;

    if (rs->references > 0) rs->references--;

    if (rs->references == 0) {
        // Standard freeing
        stack_node_t *node = rs->top_node;
        rs->top_node = nullptr;

        while (node) {
            stack_node_t *next = node->next;
            if (node->kind == ITEM_IS_RSTACK && node->rs_ptr) {
                rstack_delete(node->rs_ptr);
            }
            free(node);
            node = next;
        }
        free(rs);
    } else {
        // Potential cycle
        mark_suspects(rs);
        verify_isolation(rs);

        rstack_t *garbage_list = nullptr;
        gather_garbage(rs, &garbage_list);

        while (garbage_list) {
            rstack_t *next_garbage = garbage_list->free_chain;
            free(garbage_list);
            garbage_list = next_garbage;
        }
    }
}

// Pushes a new numeric value onto the given stack.
int rstack_push_value(rstack_t *rs, uint64_t val) {
    if (!rs) {
        errno = EINVAL;
        return -1;
    }

    stack_node_t *new_node = malloc(sizeof(stack_node_t));
    if (!new_node) {
        errno = ENOMEM;
        return -1;
    }

    new_node->kind = ITEM_IS_NUMBER;
    new_node->num_val = val;
    new_node->next = rs->top_node;
    rs->top_node = new_node;

    return 0;
}

// Pushes a reference to the second stack onto the first stack.
int rstack_push_rstack(rstack_t *rs1, rstack_t *rs2) {
    if (!rs1 || !rs2) {
        errno = EINVAL;
        return -1;
    }

    stack_node_t *new_node = malloc(sizeof(stack_node_t));
    if (!new_node) {
        errno = ENOMEM;
        return -1;
    }

    new_node->kind = ITEM_IS_RSTACK;
    new_node->rs_ptr = rs2;
    new_node->next = rs1->top_node;
    rs1->top_node = new_node;

    rs2->references++;
    return 0;
}

// Removes the top element from the stack.
void rstack_pop(rstack_t *rs) {
    if (!rs || !rs->top_node) return;

    stack_node_t *old_top = rs->top_node;
    rs->top_node = old_top->next;

    if (old_top->kind == ITEM_IS_RSTACK && old_top->rs_ptr) {
        rstack_delete(old_top->rs_ptr);
    }
    free(old_top);
}

// DFS searching for the nearest numeric value.
static result_t find_top_number(rstack_t *st) {
    if (!st || st->is_visited) return (result_t){false, 0};

    st->dfs_flag = true;
    st->is_visited = true;

    for (stack_node_t *node = st->top_node; node; node = node->next) {
        if (node->kind == ITEM_IS_NUMBER) {
            st->is_visited = false;
            return (result_t){true, node->num_val};
        }
        if (node->kind == ITEM_IS_RSTACK) {
            result_t res = find_top_number(node->rs_ptr);
            if (res.flag) {
                st->is_visited = false;
                return res;
            }
        }
    }
    return (result_t){false, 0};
}

// Clears the DFS flags in the visited nodes.
static void wipe_dfs_marks(rstack_t *st) {
    if (!st || !st->dfs_flag) return;

    st->is_visited = false;
    st->dfs_flag = false;

    for (stack_node_t *node = st->top_node; node; node = node->next) {
        if (node->kind == ITEM_IS_RSTACK && node->rs_ptr) {
            wipe_dfs_marks(node->rs_ptr);
        }
    }
}

// Interface for finding the nearest number under the top of the stack.
result_t rstack_front(rstack_t *rs) {
    result_t res = find_top_number(rs);
    wipe_dfs_marks(rs);
    return res;
}

// Checks whether the stack contains any numeric value.
bool rstack_empty(rstack_t *rs) {
    return !rstack_front(rs).flag;
}

// Creates a new stack from numeric values read from a file.
rstack_t *rstack_read(char const *path) {
    if (!path) {
        errno = EINVAL;
        return nullptr;
    }

    FILE *f = fopen(path, "r");
    if (!f) return nullptr;

    rstack_t *rs = rstack_new();
    if (!rs) {
        fclose(f);
        return nullptr;
    }

    int ch;
    while (true) {
        // Skip whitespace
        while ((ch = fgetc(f)) != EOF && isspace(ch));

        if (ch == EOF) break;

        if (ch == '-' || ch == '+') {
            rstack_delete(rs);
            fclose(f);
            errno = EINVAL;
            return nullptr;
        }

        ungetc(ch, f);
        uint64_t val;
        errno = 0;

        if (fscanf(f, "%" SCNu64, &val) != 1) break;

        if (errno == ERANGE) {
            rstack_delete(rs);
            fclose(f);
            errno = ERANGE;
            return nullptr;
        }

        if (rstack_push_value(rs, val) != 0) {
            rstack_delete(rs);
            fclose(f);
            return nullptr;
        }
    }

    if (!feof(f)) {
        rstack_delete(rs);
        fclose(f);
        errno = EINVAL;
        return nullptr;
    }

    fclose(f);
    return rs;
}

// Function that writes the stack's contents to a file.
static int dump_stack_content(rstack_t *st, FILE *out) {
    if (!st) return 0;
    if (st->is_visited) return 1;

    st->is_visited = true;
    st->top_node = invert_list_order(st->top_node);

    int status = 0;
    for (stack_node_t *node = st->top_node; node; node = node->next) {
        if (node->kind == ITEM_IS_NUMBER) {
            if (fprintf(out, "%" PRIu64 "\n", node->num_val) < 0) {
                status = -1;
                break;
            }
        } else {
            status = dump_stack_content(node->rs_ptr, out);
            if (status != 0) break;
        }
    }

    st->top_node = invert_list_order(st->top_node);
    st->is_visited = false;
    return status;
}

// Writes all encountered numeric values to a file.
int rstack_write(char const *path, rstack_t *rs) {
    if (!path || !rs) {
        errno = EINVAL;
        return -1;
    }

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    int res = dump_stack_content(rs, f);
    if (fclose(f) != 0) res = -1;

    return res == -1 ? -1 : 0;
}
