#include <math.h> 
#include <routines.h> 

double add(double x,int y)
{
  double __retv;
  __retv = x + y;
  return __retv;
}

void set_x(double *x,int y)
{
   *x = 10.0000F + y;
}
