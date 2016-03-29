// Copyright 2009 Felix Domke <tmbinc@elitedvb.net>. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
//    1. Redistributions of source code must retain the above copyright notice, this list of
//       conditions and the following disclaimer.
//
//    2. Redistributions in binary form must reproduce the above copyright notice, this list
//       of conditions and the following disclaimer in the documentation and/or other materials
//       provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER ``AS IS'' AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
// FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// The views and conclusions contained in the software and documentation are those of the
// authors and should not be interpreted as representing official policies, either expressed
// or implied, of the copyright holder.
//

#define _LARGEFILE64_SOURCE
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <getopt.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/mman.h>

#define BGREP_VERSION "0.3"

// The Windows/DOS implementation of read(3) opens files in text mode by default,
// which means that an 0x1A byte is considered the end of the file unless a non-standard
// flag is used. Make sure it's defined even on real POSIX environments
#ifndef O_BINARY
#define O_BINARY 0
#endif

int bytes_before = 0, bytes_after = 0;
uint32_t block_size = 4096;
uint32_t count_blocks = 0;
uint32_t skip_blocks = 0;
int hex_context = 0;


void die(const char* msg, ...);

void print_char(unsigned char c)
{
    if (!hex_context) {
        if (isprint(c))
            putchar(c);
        else
            printf("\\x%02x", (int)c);
    } else {
        printf("%02x", (int)c);
    }
}

int ascii2hex(char c)
{
	if (c < '0')
		return -1;
	else if (c <= '9')
		return c - '0';
	else if (c < 'A')
		return -1;
	else if (c <= 'F')
		return c - 'A' + 10;
	else if (c < 'a')
		return -1;
	else if (c <= 'f')
		return c - 'a' + 10;
	else
		return -1;
}

void dump_context(unsigned char *mem, size_t size, unsigned long long pos, size_t len)
{
    int _bytes_before = (bytes_before <= pos) ? bytes_before : pos;
    int _bytes_after = (bytes_after <= (size - pos - len)) ? bytes_after : (size - pos - len);

    unsigned char *ptr = &mem[pos - _bytes_before];

    size_t i;
    for (i = 0; i < _bytes_before; i++) {
        print_char(*ptr);
        ptr++;
    }
    for (i = 0; i < len; i++) {
        print_char(*ptr);
        ptr++;
    }
    for (i = 0; i < _bytes_after; i++) {
        print_char(*ptr);
        ptr++;
    }

    putchar('\n');
}

void searchfile(const char *filename, int fd, const unsigned char *value, const unsigned char *mask, size_t len)
{
    off_t offset = 0;
    struct stat sb;
    size_t pos;
    off64_t mmap_off = 0;
    size_t length = 0;

    if (skip_blocks || count_blocks)
    {
        mmap_off = block_size * skip_blocks;
        length = block_size * count_blocks;
    } else
    {
        int ret = fstat(fd, &sb);
        if (ret) 
        {
            perror("fstat failed\n");
            exit(6);
        }
        length = sb.st_size;
    }
    unsigned char *memblock = mmap64(0, length, PROT_READ, MAP_SHARED, fd, mmap_off);
    if (!memblock) {
        perror("mmap64 failed\n");
        exit(5);
    }

    for (pos = 0; pos < length; pos++) 
    {
        size_t i = 0;
        for (; i < len; i++)
            if ((memblock[pos + i] & mask[i]) != value[i])
                break;

        if (i == len)
        {
            printf("%s: 0x%08lx\n", filename, pos);
            if (bytes_before || bytes_after)
                dump_context(memblock, length, pos, len);
        }
    }
	
	if (munmap(memblock, length)) 
    {
        perror("munmap failed\n");
        exit(4);
    }
}

void recurse(const char *path, const unsigned char *value, const unsigned char *mask, size_t len)
{
	struct stat s;
	if (stat(path, &s))
	{
		perror("stat");
		return;
	}
	if (!S_ISDIR(s.st_mode))
	{
		int fd = open(path, O_RDONLY | O_BINARY);
		if (fd < 0)
			perror(path);
		else
		{
			searchfile(path, fd, value, mask, len);
			close(fd);
		}
		return;
	} else if (count_blocks || skip_blocks) 
    {
        fprintf(stderr, "can't use --count or --skip recursively for directory\n");
    }

	DIR *dir = opendir(path);
	if (!dir)
	{
		perror(path);
		exit(3);
	}

	struct dirent *d;
	while ((d = readdir(dir)))
	{
		if (!(strcmp(d->d_name, ".") && strcmp(d->d_name, "..")))
			continue;
		char newpath[strlen(path) + strlen(d->d_name) + 1];
		strcpy(newpath, path);
		strcat(newpath, "/");
		strcat(newpath, d->d_name);
		recurse(newpath, value, mask, len);
	}

	closedir(dir);
}

void die(const char* msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

void usage(char** argv)
{
	fprintf(stderr, "bgrep version: %s\n", BGREP_VERSION);
	fprintf(stderr, "usage: %s [-B bytes] [-A bytes] [-C bytes] <hex> [<path> [...]] [--bs <block_size>] [--count <count_blocks>] [--skip <skip_blocks>] [-x]\n", *argv);
	exit(1);
}

void parse_opts(int argc, char** argv)
{
	int c;

    static struct option long_options[] =
    {
        {"after-context",  required_argument,       0, 'A'},
        {"before-context", required_argument,     0, 'B'},
        {"context",    required_argument,     0, 'C'},
        {"bs",    required_argument,     0, 0},
        {"count",    required_argument,     0, 1},
        {"skip",    required_argument,     0, 2},
        {"hex-context",    no_argument,     0, 'x'},

        {0, 0, 0, 0}
    };

    int option_index = 0;

    while ((c = getopt_long(argc, argv, "xA:B:C:", long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'A':
				bytes_after = atoi(optarg);
				break;
			case 'B':
				bytes_before = atoi(optarg);
				break;
			case 'C':
				bytes_before = bytes_after = atoi(optarg);
				break;
            case 0:
                block_size = atoi(optarg);
                break;
            case 1:
                count_blocks = atoi(optarg);
                break;
            case 2:
                skip_blocks = atoi(optarg);
                break;
            case 'x':
                hex_context = 1;
                break;
			default:
				usage(argv);
		}
	}

	if (bytes_before < 0)
		die("Invalid value %d for bytes before", bytes_before);
	if (bytes_after < 0)
		die("Invalid value %d for bytes after", bytes_after);
}

int main(int argc, char **argv)
{
	unsigned char value[0x100], mask[0x100];
	size_t len = 0;

	if (argc < 2)
	{
		usage(argv);
		return 1;
	}

	parse_opts(argc, argv);
	argv += optind - 1; /* advance the pointer to the first non-opt arg */
	argc -= optind - 1;

	char *h = argv[1];
	enum {MODE_HEX,MODE_TXT,MODE_TXT_ESC} parse_mode = MODE_HEX;

	while (*h && (parse_mode != MODE_HEX || h[1]) && len < 0x100)
	{
		int on_quote = (h[0] == '"');
		int on_esc = (h[0] == '\\');

		switch (parse_mode)
		{
			case MODE_HEX:
				if (on_quote)
				{
					parse_mode = MODE_TXT;
					h++;
					continue; /* works under switch - will continue the loop*/
				}
				break; /* this one is for switch */
			case MODE_TXT:
				if (on_quote)
				{
					parse_mode = MODE_HEX;
					h++;
					continue;
				}

				if (on_esc)
				{
					parse_mode = MODE_TXT_ESC;
					h++;
					continue;
				}

				value[len] = h[0];
				mask[len++] = 0xff;
				h++;
				continue;

			case MODE_TXT_ESC:
				value[len] = h[0];
				mask[len++] = 0xff;
				parse_mode = MODE_TXT;
				h++;
				continue;
		}
		//
		if (h[0] == '?' && h[1] == '?')
		{
			value[len] = mask[len] = 0;
			len++;
			h += 2;
		} else if (h[0] == ' ')
		{
			h++;
		} else
		{
			int v0 = ascii2hex(*h++);
			int v1 = ascii2hex(*h++);

			if ((v0 == -1) || (v1 == -1))
			{
				fprintf(stderr, "invalid hex string!\n");
				return 2;
			}
			value[len] = (v0 << 4) | v1; mask[len++] = 0xFF;
		}
	}

	if (!len || *h)
	{
		fprintf(stderr, "invalid/empty search string\n");
		return 2;
	}

	if (argc < 3)
		searchfile("stdin", 0, value, mask, len);
	else
	{
		int c = 2;
		while (c < argc)
			recurse(argv[c++], value, mask, len);
	}
	return 0;
}
