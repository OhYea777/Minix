#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main(int argc, char **argv)
{
  char buf[32];
  int filedesc = open(argv[1], O_RDWR);

  printf("Trying to open file: %s\n", argv[1]);

  if (filedesc < 0) {
    printf("Failed to open file\n");

    return 1;
  }

  printf("Opened file\n");
  write(filedesc,"This will be output to testfile.txt\n", 36);

  while (read(filedesc, buf, sizeof(buf))) {
    printf("%s", buf);
  }

  return 0;
}
