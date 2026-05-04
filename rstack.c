// Biblioteka obsługująca stosy rekurencyjne.
// Wykorzystuje zliczanie referencji oraz algorytm Trial Deletion (Bacon) do wykrywania i usuwania izolowanych cykli.

#include "rstack.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>

// Typy elementów, jakie mogą znajdować się na stosie
typedef enum {
    ITEM_IS_NUMBER,
    ITEM_IS_RSTACK
} node_kind_t;

// Struktura pojedynczego węzła na liście
typedef struct stack_node {
    struct stack_node *next;
    node_kind_t kind;
    union {
        struct rstack *rs_ptr;
        uint64_t num_val;
    };
} stack_node_t;

// Stałe używane przez Garbage Collector do śledzenia cykli
#define GC_SAFE   0
#define GC_CHECK  1
#define GC_TRASH  2

// Główna struktura reprezentująca stos
struct rstack {
    stack_node_t *top_node;    // Wskaźnik na wierzchołek stosu.
    struct rstack *free_chain; // Wskaźnik pomocniczy używany przez GC do budowania listy śmieci.
    size_t references;         // Licznik referencji.
    int gc_status;             // Stan węzła w algorytmie detekcji cykli.
    bool dfs_flag;             // Flaga DFS: true gdy węzeł był odwiedzony w bieżącym wywołaniu rstack_front.
    bool is_visited;           // Flaga blokady ponownego wejścia podczas DFS.
};

// Funkcja pomocnicza: odwraca kierunek powiązań na liście węzłów.
// Wykorzystywana przy zapisie, by wypisywać elementy od dna stosu.
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

// Tworzy nowy, pusty stos i inicjalizuje jego składowe.
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

// Faza 1 algorytmu Trial Deletion: oznacza węzły do sprawdzenia i tymczasowo odejmuje referencje wewnętrzne.
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

// Przywraca stan GC_SAFE i odtwarza referencje dla węzłów "żywych".
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

// Faza 2 algorytmu Trial Deletion: weryfikuje węzły i decyduje, które tworzą izolowany cykl.
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

// Faza 3 algorytmu Trial Deletion: agreguje węzły do usunięcia na liście trash_list.
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

// Główna funkcja usuwająca referencję do stosu.
void rstack_delete(rstack_t *rs) {
    if (!rs) return;

    if (rs->references > 0) rs->references--;

    if (rs->references == 0) {
        // Standardowe zwalnianie
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
        // Potencjalny cykl
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

// Odkłada na wskazany stos nową wartość liczbową.
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

// Odkłada referencję do drugiego stosu na pierwszy stos.
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

// Zdejmuje najwyższy element ze stosu.
void rstack_pop(rstack_t *rs) {
    if (!rs || !rs->top_node) return;

    stack_node_t *old_top = rs->top_node;
    rs->top_node = old_top->next;

    if (old_top->kind == ITEM_IS_RSTACK && old_top->rs_ptr) {
        rstack_delete(old_top->rs_ptr);
    }
    free(old_top);
}

// DFS szukający najbliższej wartości liczbowej.
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

// Czyści flagi DFS w odwiedzonych węzłach.
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

// Interfejs do znajdowania najbliższej liczby pod wierzchołkiem stosu.
result_t rstack_front(rstack_t *rs) {
    result_t res = find_top_number(rs);
    wipe_dfs_marks(rs);
    return res;
}

// Sprawdza czy stos zawiera jakąkolwiek wartość liczbową.
bool rstack_empty(rstack_t *rs) {
    return !rstack_front(rs).flag;
}

// Tworzy nowy stos na podstawie wartości liczbowych odczytanych z pliku.
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

// Funkcja robocza zapisująca zawartość stosu do pliku.
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

// Zapisuje wszystkie napotkane wartości liczbowe do pliku.
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
