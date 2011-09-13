#include <stdio.h>
#include <stdlib.h>

typedef struct file_id {
    FILE* file;
    int id;
    struct file_id* next;
} file_id;

file_id* ftc__files = NULL;

FILE* ftc__get_file(int id) {
    file_id* cf = ftc__files;
    while(cf!=NULL && cf->id != id) cf = cf->next;
        
    if(cf!=NULL) return cf->file;
    else return NULL;
}

void ftc__open_file(int id, const char* path) {
    file_id* cf = ftc__files;
    while(cf!=NULL && cf->id != id) cf = cf->next;
    
    if(cf!=NULL) {
        fclose(cf->file);
        cf->file = fopen(path,"rw");
    }else {
        cf = (file_id*)malloc(sizeof(file_id));
        cf->file = fopen(path,"rw");
        cf->id = id;
        cf->next = ftc__files;
        ftc__files = cf;
    }
}

void ftc__close_file(int id) {
    file_id* cf = ftc__files;
    file_id* pre = NULL;
    while(cf!=NULL && cf->id != id) { pre = cf; cf = cf->next; }
    if(cf!=NULL) {
        if(pre==NULL) ftc__files = cf->next;
        else pre->next = cf->next;
        
        fclose(cf->file);
        free(cf);
    }
}
