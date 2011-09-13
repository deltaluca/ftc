#include <stdio.h> 
#include <math.h> 
#include <main.h> 

int main(int *argc,char* *argv)
{
  int __retv;
  int xs[11];
  int i;
  int sum;
  sum = 0;
  i = 0;
  int __fbound0 = 10;
  for (; i <= __fbound0; ++i) {
    xs[i] = i;
    printf("%d\n",xs[i]);
    sum = sum + i;
  }
  printf("%s%d\n","and their sum is: ",sum);
  __retv = 0;
  return __retv;
}
