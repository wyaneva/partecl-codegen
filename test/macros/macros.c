#include <stdio.h>
#include <stdlib.h>

int b = 2;
int c = 0;

#define addb(p,d) p+d+b

int f_addb(int a)
{
  return c+addb(a,c);
}
 
int main(int argc, char* argv[])
{
  if(argc < 2)
  {
    printf("Please, provide an integer.\n");
    return 0;
  }

  int a = atoi(argv[1]);
  int res = f_addb(a);
  //printf("%d + %d = %d\n", a, b, res);
}
