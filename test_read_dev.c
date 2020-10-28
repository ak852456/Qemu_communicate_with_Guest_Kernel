#include <stdio.h>
#include <stdlib.h>

int main()
{
	FILE *pFile;
	char buffer[1024];
    
    pFile = fopen("/dev/ivshmem", "r+");
    if(pFile == NULL)
    {
        printf("Error: Fail to open the device\n");
        exit(1);
    }

    else
    {
        fread(buffer, 1, sizeof(buffer), pFile);
    }

    printf("%s", buffer);
    fclose(pFile);
    return 0;
}