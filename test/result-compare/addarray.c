#include <stdio.h>
#include <stdlib.h>

int add(int a, int b)
{
  return a + b;
}

int main(int argc, char* argv[])
{
  if(argc < 3)
  {
    printf("Please, enter integers you'd like to add.\n");
    return 0;
  }
  int a = atoi(argv[1]);
  int b = atoi(argv[2]);

  int sum[10];
  for(int i = 0; i < 10; i++)
  {
    sum[i] = add(a, b);
  }

  for(int i = 0; i < 10; i++)
  {
    printf("%d\n", sum[i]);
  }
}
