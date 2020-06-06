#include <stdio.h>
#include <stdlib.h>

int c;

int addc(int a, int b)
{
  return a + b + c;
}

int main(int argc, char* argv[])
{
  if(argc < 3)
  {
    printf("Please, enter two integers.\n");
    return 0;
  }

  int a = atoi(argv[1]);
  int b = atoi(argv[2]);
  c = 3;

  int sum = addc(a, b);

  printf("%d + %d + %d = %d\n", a, b, c, sum);
}
