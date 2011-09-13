#pragma once

#include <stdio.h>

/**

    Wrapper for FORTAN file io.
    
    open (id, file=path)
    write (id,*) "hello"
    close (id)
    
    becomes
    
    #include "$FTC/ftc_file_io.h"
    
    ftc__open_file(id,path);
    fprintf(ftc__get_file(id), "%s\n", "hello");
    ftc__close_file(id);
    
**/

#ifdef __cplusplus
extern "C" {
#endif

void ftc__open_file(int id, const char* path);
FILE* ftc__get_file(int id);
void ftc__close_file(int id);  

#ifdef __cplusplus
}
#endif
