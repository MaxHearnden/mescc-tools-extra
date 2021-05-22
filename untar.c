/*
 * This file is in the public domain.  Use it as you see fit.
 */

/*
 * "untar" is an extremely simple tar extractor:
 *  * A single C source file, so it should be easy to compile
 *    and run on any system with a C compiler.
 *  * Extremely portable standard C.  The only non-ANSI function
 *    used is mkdir().
 *  * Reads basic ustar tar archives.
 *  * Does not require libarchive or any other special library.
 *
 * To compile: cc -o untar untar.c
 *
 * Usage:  untar <archive>
 *
 * In particular, this program should be sufficient to extract the
 * distribution for libarchive, allowing people to bootstrap
 * libarchive on systems that do not already have a tar program.
 *
 * To unpack libarchive-x.y.z.tar.gz:
 *    * gunzip libarchive-x.y.z.tar.gz
 *    * untar libarchive-x.y.z.tar
 *
 * Written by Tim Kientzle, March 2009.
 *
 * Released into the public domain.
 */

/* These are all highly standard and portable headers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This is for mkdir(); this may need to be changed for some platforms. */
#include <sys/stat.h>  /* For mkdir() */
#include "M2libc/bootstrappable.h"

/* Parse an octal number, ignoring leading and trailing nonsense. */
int parseoct(char const* p, size_t n)
{
	int i = 0;
	int h;

	while(((p[0] < '0') || (p[0] > '7')) && (n > 0))
	{
		p = p + 1;
		n = n - 1;
	}

	while((p[0] >= '0') && (p[0] <= '7') && (n > 0))
	{
		i = i << 3;
		h = p[0];
		i = i + h - 48;
		p = p + 1;
		n = n - 1;
	}

	return i;
}

/* Returns true if this is 512 zero bytes. */
int is_end_of_archive(char const* p)
{
	int n;

	for(n = 511; n >= 0; n = n - 1)
	{
		if(p[n] != 0)
		{
			return FALSE;
		}
	}

	return TRUE;
}

/* Create a directory, including parent directories as necessary. */
void create_dir(char *pathname, int mode)
{
	char *p;
	int r;

	/* Strip trailing '/' */
	if(pathname[strlen(pathname) - 1] == '/')
	{
		pathname[strlen(pathname) - 1] = '\0';
	}

	/* Try creating the directory. */
	r = mkdir(pathname, mode);

	if(r != 0)
	{
		/* On failure, try creating parent directory. */
		p = strrchr(pathname, '/');

		if(p != NULL)
		{
			p[0] = '\0';
			create_dir(pathname, 0755);
			p[0] = '/';
			r = mkdir(pathname, mode);
		}
	}

	if(r != 0)
	{
		fputs("Could not create directory ", stderr);
		fputs(pathname, stderr);
		fputc('\n', stderr);
	}
}

/* Create a file, including parent directory as necessary. */
FILE* create_file(char *pathname, int mode)
{
	FILE* f;
	f = fopen(pathname, "w");

	if(f == NULL)
	{
		/* Try creating parent dir and then creating file. */
		char *p = strrchr(pathname, '/');

		if(p != NULL)
		{
			p[0] = '\0';
			create_dir(pathname, 0755);
			p[0] = '/';
			f = fopen(pathname, "w");
		}
	}

	return f;
}

/* Verify the tar checksum. */
int verify_checksum(char const* p)
{
	int n;
	int u = 0;
	unsigned h;

	for(n = 0; n < 512; n = n + 1)
	{
		/* Standard tar checksum adds unsigned bytes. */
		if((n < 148) || (n > 155))
		{
			h = p[n];
			u = u + h;
		}
		else
		{
			u = u + 0x20;
		}
	}

	int r = parseoct(p + 148, 8);

	return (u == r);
}

/* Extract a tar archive. */
void untar(FILE *a, char const* path)
{
	char* buff = calloc(514, sizeof(char));
	FILE* f = NULL;
	size_t bytes_read;
	int filesize;
	int op;
	fputs("Extracting from ", stdout);
	puts(path);

	while(TRUE)
	{
		memset(buff, 0, 514);
		bytes_read = fread(buff, sizeof(char), 512, a);

		if(bytes_read < 512)
		{
			fputs("Short read on ", stderr);
			fputs(path, stderr);
			fputs(": expected 512, got ", stderr);
			fputs(int2str(bytes_read, 10, TRUE), stderr);
			fputc('\n', stderr);
			return;
		}

		if(is_end_of_archive(buff))
		{
			fputs("End of ", stdout);
			puts(path);
			return;
		}

		if(!verify_checksum(buff))
		{
			fputs("Checksum failure\n", stderr);
			return;
		}

		filesize = parseoct(buff + 124, 12);

		op = buff[156];
		if('1' == op)
		{
			fputs(" Ignoring hardlink ", stdout);
			puts(buff);
		}
		else if('2' == op)
		{
			fputs(" Ignoring symlink ", stdout);
			puts(buff);
		}
		else if('3' == op)
		{
			fputs(" Ignoring character device ", stdout);
			puts(buff);
		}
		else if('4' == op)
		{
			fputs(" Ignoring block device ", stdout);
			puts(buff);
		}
		else if('5' == op)
		{
			fputs(" Extracting dir ", stdout);
			puts(buff);
			create_dir(buff, parseoct(buff + 100, 8));
			filesize = 0;
		}
		else if('6' == op)
		{
			fputs(" Ignoring FIFO ", stdout);
			puts(buff);
		}
		else
		{
			fputs(" Extracting file ", stdout);
			puts(buff);
			f = create_file(buff, parseoct(buff + 100, 8));
		}

		while(filesize > 0)
		{
			bytes_read = fread(buff, 1, 512, a);

			if(bytes_read < 512)
			{
				fputs("Short read on ", stderr);
				fputs(path, stderr);
				fputs(": Expected 512, got ", stderr);
				puts(int2str(bytes_read, 10, TRUE));
				return;
			}

			if(filesize < 512)
			{
				bytes_read = filesize;
			}

			if(f != NULL)
			{
				op = fwrite(buff, 1, bytes_read, f);
				if(op != bytes_read)
				{
					fputs("Failed write\n", stderr);
					fclose(f);
					f = NULL;
				}
			}

			filesize = filesize - bytes_read;
		}

		if(f != NULL)
		{
			fclose(f);
			f = NULL;
		}
	}
}

int main(int argc, char **argv)
{
	FILE *a;
	int i;
	for(i = 1; argc > i; i = i + 1)
	{
		a = fopen(argv[i], "r");

		if(a == NULL)
		{
			fputs("Unable to open ", stderr);
			fputs(argv[i], stderr);
			fputc('\n', stderr);
		}
		else
		{
			untar(a, argv[i]);
			fclose(a);
		}
	}

	return 0;
}