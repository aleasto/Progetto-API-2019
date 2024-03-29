#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "ht.h"
#include "bst.h"

#define LAMBDA(c_) ({ c_ _;})
#define LINE_LEN ID_LEN*4

void get_cmd(char**, char*);
void get_arg(char**, char*);
char* my_strdup(const char*);
connections_t* alloc_connections();
relation_t* alloc_relation();
void insertion_sort(char*[], int);
void my_itoa(int, char*);
void add_entity(ht_t, char*);
void del_entity(ht_t, bst_t*, char*);
void add_connection(bst_t*, char*, char*, char*);
void del_connection(bst_t*, char*, char*, char*);
void report(bst_t*);

int main(){
    char* line;
    char input[LINE_LEN];
    char cmd[ID_LEN+1];
    char arg1[ID_LEN+1];
    char arg2[ID_LEN+1];
    char arg3[ID_LEN+1];

    ht_t tracked = {0};
    bst_t* relations_tree = bst_create();

    while(1){
        if(!fgets(input, LINE_LEN, stdin))
            abort();

        line = input;
        get_cmd(&line, cmd);
        if (!strcmp(cmd, "addent")){
            get_arg(&line, arg1);
            add_entity(tracked, arg1);

        } else if(!strcmp(cmd, "delent")){
            get_arg(&line, arg1);
            if(ht_search(tracked, arg1))
                del_entity(tracked, relations_tree, arg1);

        } else if(!strcmp(cmd, "addrel")){
            get_arg(&line, arg1);
            get_arg(&line, arg2);
            get_arg(&line, arg3);
            ht_entry_t* entity1 = ht_search(tracked, arg1);
            ht_entry_t* entity2 = ht_search(tracked, arg2);
            if(entity1 && entity2)
                add_connection(relations_tree, arg3, entity1->key, entity2->key);

        } else if(!strcmp(cmd, "delrel")){
            get_arg(&line, arg1);
            get_arg(&line, arg2);
            get_arg(&line, arg3);
            ht_entry_t* entity1 = ht_search(tracked, arg1);
            ht_entry_t* entity2 = ht_search(tracked, arg2);
            if(entity1 && entity2)
                del_connection(relations_tree, arg3, arg1, arg2);

        } else if(!strcmp(cmd, "report")){
            report(relations_tree);
        } else if(!strcmp(cmd, "end")){
            fflush(stdout);
            break;
        }
    }

    return 0;
}

void get_cmd(char** from, char* to){
    char* line = *from;
    int i;
    for(i = 0; line[i] != '\0' && line[i] != '\n' && line[i] != ' '; i++)
        to[i] = line[i];
    to[i] = '\0';
    *from = *from + i + 1;
}

void get_arg(char** from, char* to){
    char* line = *from;
    int i;
    for(i = 1; line[i] != '\0' && line[i] != '\n' && line[i] != '"'; i++)
        to[i-1] = line[i];
    to[i-1] = '\0';
    *from = *from + i + 2;
}

void add_entity(ht_t tracked, char* id){
    // TODO: this can be optimized: combine search and insert
    if(ht_search(tracked, id) == 0)
        ht_insert(tracked, id);
}

void add_connection(bst_t* relations_tree, char* rel_id, char* from, char* to){
    // Get relation
    bst_node_t* relation_node;
    char* rel_key = my_strdup(rel_id);
    int new = bst_get_or_alloc_and_insert(&relation_node, relations_tree, rel_key);
    if(new)
        relation_node->object = alloc_relation();
    else
        free(rel_key);

    // Get recipient
    relation_t* relation = relation_node->object;
    bst_node_t* recipient;
    new = bst_get_or_alloc_and_insert(&recipient, relation->connections_tree, to);
    if(new)
        recipient->object = alloc_connections();

    bst_node_t* sender;
    new = bst_get_or_alloc_and_insert(&sender, ((connections_t*)recipient->object)->receiving, from);
    if(new){
        sender->object = NULL; // unused
        ((connections_t*)recipient->object)->receiving_count++;

        if(!relation->record.dirty && ((connections_t*)recipient->object)->receiving_count >= relation->record.max){
            if(((connections_t*)recipient->object)->receiving_count > relation->record.max){
                relation->record.max = ((connections_t*)recipient->object)->receiving_count;
                relation->record.len = 0;
            }
            relation->record.len++;
            relation->record.champions[relation->record.len-1] = recipient->key;
            insertion_sort(relation->record.champions, relation->record.len);
        }
    }
}

void free_connections(relation_t* relation, bst_node_t* target){
    free(((connections_t*)target->object)->receiving);
    free((connections_t*)target->object);
    free(bst_remove(relation->connections_tree, target));
}

static inline int has_connections(bst_node_t* target){
    return ((connections_t*)target->object)->receiving->root != NIL;
}

void del_connection(bst_t* relations_tree, char* rel_id, char* from, char* to){
    bst_node_t* relation_node = bst_get(relations_tree, rel_id);
    if(relation_node != NIL){
        relation_t* relation = relation_node->object;
        bst_node_t* recipient = bst_get(relation->connections_tree, to);
        if(recipient != NIL){
            bst_node_t* connection = bst_get(((connections_t*)recipient->object)->receiving, from);
            if(connection != NIL){
                free(bst_remove(((connections_t*)recipient->object)->receiving, connection));
                if(((connections_t*)recipient->object)->receiving_count == relation->record.max)
                    relation->record.dirty = 1; // TODO: We can do better!

                ((connections_t*)recipient->object)->receiving_count--;

                if(!has_connections(recipient))
                    free_connections(relation, recipient);
            }
        }
    }
}

void del_entity_from_relation_recursive(ht_t tracked, bst_node_t* relation_node, char* id){
    if(relation_node != NIL){
        del_entity_from_relation_recursive(tracked, relation_node->left, id);
        del_entity_from_relation_recursive(tracked, relation_node->right, id);

        relation_t* relation = relation_node->object;
        bst_node_t* target = bst_get(relation->connections_tree, id);
        if(target != NIL){
            if(relation->record.max == ((connections_t*)target->object)->receiving_count)
                relation->record.dirty = 1;

            // Delete all my connections
            bst_t* receiving_tree = ((connections_t*)target->object)->receiving;
            while(receiving_tree->root != NIL){ //TODO: breadth-first-walk è più veloce
                free(bst_remove(receiving_tree, receiving_tree->root));
            }
            free_connections(relation, target);
        }

        // Find other entities that are receiving from me and delete that connections
        forall_in_ht(tracked, LAMBDA(void _(ht_entry_t* ht_entry){
            bst_node_t* other = bst_get(relation->connections_tree, ht_entry->key);
            if(other != NIL){
                connections_t* other_connections = other->object;
                bst_node_t* me = bst_get(other_connections->receiving, id);
                if(me != NIL){
                    free(bst_remove(other_connections->receiving, me));
                    if(relation->record.max == other_connections->receiving_count)
                        relation->record.dirty = 1;

                    other_connections->receiving_count--;

                    if(!has_connections(other))
                        free_connections(relation, other);
                }
            }
        }));
    }
}

void del_entity(ht_t tracked, bst_t* relations_tree, char* id){
    // forall relations in relations_tree => delete id's connections
    del_entity_from_relation_recursive(tracked, relations_tree->root, id);

    ht_delete(tracked, id);
}

void tree_walk_for_max_connections(relation_t* relation, bst_node_t* node){
    if(node != NIL){
        tree_walk_for_max_connections(relation, node->left);

        connections_t* connections = node->object;
        int count = connections->receiving_count;
        if(count > 0){
            if(count == relation->record.max){
                relation->record.len++;
                relation->record.champions[relation->record.len-1] = node->key;
            } else if(count > relation->record.max){
                relation->record.max = count;
                relation->record.len = 1;
                relation->record.champions[relation->record.len-1] = node->key;
            }
        }

        tree_walk_for_max_connections(relation, node->right);
    }
}

void compute_champions(relation_t* relation){
    relation->record.max = 0;
    relation->record.len = 0;
    tree_walk_for_max_connections(relation, relation->connections_tree->root);
}

// Helper for report()
int g_found_first = 0;
char buf[5];
int report_node_inorder(bst_t* tree, bst_node_t* relation_node){
    int any = 0;
    if(relation_node != NIL){
        any |= report_node_inorder(tree, relation_node->left);

        relation_t* relation = relation_node->object;
        if(relation->record.dirty){
            compute_champions(relation);
            relation->record.dirty = 0;
        }

        if(relation->record.len > 0){
            // Print
            if(g_found_first)
                fputs(" ", stdout);

            fputs("\"", stdout);
            fputs(relation_node->key, stdout);
            fputs("\" ", stdout);
            any = 1;
            for(int i = 0; i < relation->record.len; i++){
                fputs("\"", stdout);
                fputs(relation->record.champions[i], stdout);
                fputs("\" ", stdout);
            }
            my_itoa(relation->record.max, buf);
            fputs(buf, stdout);
            fputc(';', stdout);

            g_found_first = 1;
        }
        any |= report_node_inorder(tree, relation_node->right);
    }
    return any;
}

void report(bst_t* relations_tree){
    g_found_first = 0;
    int any = report_node_inorder(relations_tree, relations_tree->root);
    if(!any)
        printf("none");
    printf("\n");
}

relation_t* alloc_relation(){
    relation_t* rel = malloc(sizeof(relation_t));
    rel->record.max = 0;
    rel->record.len = 0;
    rel->record.dirty = 0;
    rel->connections_tree = bst_create();
    return rel;
}

connections_t* alloc_connections(){
    connections_t* conn = malloc(sizeof(connections_t));
    conn->receiving = bst_create();
    conn->receiving_count = 0;
    return conn;
}

// This will always be O(n) time, since the array is always almost-sorted except the last element
// By chosing the last element as the pivot, we can linearly sort the array.
void insertion_sort(char* arr[], int len){
    int i = len - 1;
    char* tmp;
    while(i > 0 && strcmp(arr[i-1], arr[i]) > 0){
        tmp = arr[i];
        arr[i] = arr[i-1];
        arr[i-1] = tmp;
        i--;
    }
}

char* my_strdup(const char* str){
    char* copy = malloc((ID_LEN+1) * sizeof(char));
    strcpy(copy, str);
    return copy;
}

// For inputs > 0 {strictly}
void my_itoa(int n, char* buf){
    int cpy = n;
    int len = 0;
    while(cpy>0){
        len++;
        cpy/=10;
    }
    buf[len] = '\0';
    while(n>0){
        buf[len-1] = '0' + n%10;
        len--;
        n/=10;
    }
}