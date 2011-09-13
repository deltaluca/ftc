#include <ftc_file_io.h> 
#include <stdio.h> 
#include <math.h> 
#include <main.h> 

int main(int *argc,char* *argv)
{
  int __retv;
  int value;
  ftc__open_file(3,argv[1]);
  fscanf(ftc__get_file(3),"%d\n",&value);
  ftc__close_file(3);
  printf("%s%d\n","value=",value);
  __retv = 0;
  return __retv;
}
