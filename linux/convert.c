/* Simple program to strip all __FreeBSD__ code from kernel modules and 
 * remove __linux__ ifdef's that's been added during FreeBSD inclusion */

/* Must create a directory called 'new' underneath current directory */

/* Using 'bash' do this to convert !
 *
 *	for i in *; do; ./convert $i; done
 */

main(int argc, char **argv)
{
	int fd;
	int wfd;
	int i;
	int skip = 0;
	char s[1000];
	char wfile[50];

	fd = fopen(argv[1], "r");
	sprintf(wfile, "new/%s", argv[1]);
	wfd = fopen(wfile, "w");

	if (fd <= 0) {
		printf("source file - error\n");
		exit(1);
	}

	if (wfd <= 0) {
		printf("destination file - error\n");
		exit(1);
	}

	while (fgets(s, 1000, fd)) {
		if (strstr(s, "__FreeBSD__")) {
			if (skip == 0)
				skip = 1;
			else
				skip = 0;
		} else
		if (strstr(s, "__linux__")) {
			/* do nothing */
		} else
			if (skip == 0)
				fprintf(wfd, "%s",s);
	}
}
