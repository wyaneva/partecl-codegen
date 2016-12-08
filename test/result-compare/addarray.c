#include <stdio.h>
#include <stdlib.h>

int a[3] = {1, 2, 3};

int add(int a, int b)
{
  return a + b;
}

int main(int argc, char* argv[])
{
  if(argc < 2)
  {
    printf("Please, enter integers you'd like to add.\n");
    return 0;
  }

  int b = atoi(argv[1]);

  int sum[3];
  for(int i = 0; i < 3; i++)
  {
    sum[i] = add(a[i], b);
  }

  for(int i = 0; i < 3; i++)
  {
    printf("%d\n", sum[i]);
  }
}
