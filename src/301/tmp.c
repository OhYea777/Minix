#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
 
int main(int argc, char **argv)
{
    char buf[2];
    char file[] = "/tmp/httpXXXXXX";
    int filedesc = mkstemp(file);
    printf("%s\n", file);
    /*int filedesc = open(file, O_RDWR);*/

    if(filedesc < 0)
	{
        	return 1;
	}
    write(filedesc,"This will be output to testfile.txt\n", 36);
    close(filedesc);
    filedesc = open(file, O_RDWR);

    while(read(filedesc, buf, sizeof(buf))) {
        printf("%s", buf);
    }
    return 0;
}