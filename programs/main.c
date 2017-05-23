
/**
 * Copyright © 2016 - 2017 Tino Reichardt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You can contact the author at:
 * - zstdmt source repository: https://github.com/mcmilk/zstdmt
 */


/**
 * gzip compatible wrapper for the compression types of this lib
 */

#include <stdio.h>
#include <errno.h>

#include "platform.h"

static void perror_exit(const char *msg)
{
	printf("%s\n", msg);
	fflush(stdout);
	exit(1);
}

static void version(void)
{
	printf(PROGNAME " version " VERSION "\n");
	exit(0);
}

static void usage(void)
{
	printf("Usage: " PROGNAME " [options] INPUT > FILE\n");
	printf("or     " PROGNAME " [options] -o FILE INPUT\n");
	printf("or     cat INPUT | " PROGNAME " [options] -o FILE\n");
	printf("or     cat INPUT | " PROGNAME " [options] > FILE\n\n");

	printf("Options:\n");
	printf(" -c      force write to standard output\n");
	printf(" -d      use decompress mode\n");
	printf(" -f      force\n");
	printf(" -h      Display a help screen and quit.\n");

	printf(" -o FILE write result to a file named `FILE`\n");
	printf(" -#      set compression level to # (%d-%d, default:%d)\n",
		LEVEL_MIN, LEVEL_MAX, LEVEL_DEF);
	printf(" -T N    set number of (de)compression threads (def: #cores)\n");
	printf(" -i N    set number of iterations for testing (default: 1)\n");
	printf(" -b N    set input chunksize to N MiB (default: auto)\n");
	printf(" -z      use compress mode (this is the default)\n");
	printf(" -t      print timings and memory usage to stderr\n");
	printf(" -H      print headline for the timing values\n");
	printf(" -V      show version\n\n");

	printf("Ignored Options:\n");
	printf(" -a      Ascii text mode converting (not done)\n");

	exit(0);
}

static void headline(void)
{
	fprintf(stderr,
		"Level;Threads;InSize;OutSize;Frames;Real;User;Sys;MaxMem\n");
	exit(0);
}

int my_read_loop(void *arg, MT_Buffer * in)
{
	FILE *fd = (FILE *) arg;
	ssize_t done = fread(in->buf, 1, in->size, fd);
	in->size = done;

	return 0;
}

int my_write_loop(void *arg, MT_Buffer * out)
{
	FILE *fd = (FILE *) arg;
	ssize_t done = fwrite(out->buf, 1, out->size, fd);
	out->size = done;

	return 0;
}

static void do_compress(int threads, int level, int bufsize, FILE * fin, FILE * fout,
	    int opt_timings)
{
	static int first = 1;
	MT_RdWr_t rdwr;
	size_t ret;

	/* 1) setup read/write functions */
	rdwr.fn_read = my_read_loop;
	rdwr.fn_write = my_write_loop;
	rdwr.arg_read = (void *)fin;
	rdwr.arg_write = (void *)fout;

	/* 2) create compression context */
	MT_CCtx *ctx = MT_createCCtx(threads, level, bufsize);
	if (!ctx)
		perror_exit("Allocating ctx failed!");

	/* 3) compress */
	ret = MT_compressCCtx(ctx, &rdwr);
	if (MT_isError(ret))
		perror_exit(MT_getErrorString(ret));

	/* 4) get statistic */
	if (first && opt_timings) {
		fprintf(stderr, "%d;%d;%lu;%lu;%lu",
			level, threads,
			(unsigned long)MT_GetInsizeCCtx(ctx),
			(unsigned long)MT_GetOutsizeCCtx(ctx),
			(unsigned long)MT_GetFramesCCtx(ctx));
		first = 0;
	}

	/* 5) free resources */
	MT_freeCCtx(ctx);
}

static void do_decompress(int threads, int bufsize, FILE * fin, FILE *
			  fout, int opt_timings)
{
	static int first = 1;
	MT_RdWr_t rdwr;
	size_t ret;

	/* 1) setup read/write functions */
	rdwr.fn_read = my_read_loop;
	rdwr.fn_write = my_write_loop;
	rdwr.arg_read = (void *)fin;
	rdwr.arg_write = (void *)fout;

	/* 2) create compression context */
	MT_DCtx *ctx = MT_createDCtx(threads, bufsize);
	if (!ctx)
		perror_exit("Allocating ctx failed!");

	/* 3) compress */
	ret = MT_decompressDCtx(ctx, &rdwr);
	if (MT_isError(ret))
		perror_exit(MT_getErrorString(ret));

	/* 4) get statistic */
	if (first && opt_timings) {
		fprintf(stderr, "%d;%d;%lu;%lu;%lu",
			0, threads,
			(unsigned long)MT_GetInsizeDCtx(ctx),
			(unsigned long)MT_GetOutsizeDCtx(ctx),
			(unsigned long)MT_GetFramesDCtx(ctx));
		first = 0;
	}

	/* 5) free resources */
	MT_freeDCtx(ctx);
}

#define MODE_COMPRESS    1
#define MODE_DECOMPRESS  2

/* for the -i option */
#define MAX_ITERATIONS   1000

int main(int argc, char **argv)
{
	/* default options: */
	int opt, opt_threads = getcpucount(), opt_level = 3;
	int opt_mode = MODE_COMPRESS;
	int opt_iterations = 1, opt_bufsize = 0, opt_timings = 0;
	int opt_numbers = 0;
	char *ofilename = NULL;
	struct rusage ru;
	struct timeval tms, tme, tm;
	FILE *fin = NULL, *fout = NULL;

	while ((opt = getopt(argc, argv, "VhHT:i:dczb:o:t0123456789")) != -1) {
		switch (opt) {
		case 'V':	/* version */
			version();
		case 'h':	/* help */
			usage();
		case 'H':	/* headline */
			headline();
		case 'T':	/* threads */
			opt_threads = atoi(optarg);
			break;
		case 'i':	/* iterations */
			opt_iterations = atoi(optarg);
			break;
		case 'd':	/* mode = decompress */
			opt_mode = MODE_DECOMPRESS;
			break;
		case 'c':	/* to stdout, ignored */
			break;
		case 'z':	/* mode = compress */
			opt_mode = MODE_COMPRESS;
			break;
		case 'b':	/* input buffer in MB */
			opt_bufsize = atoi(optarg);
			break;
		case 'o':	/* output filename */
			ofilename = optarg;
			break;
		case 't':	/* print timings */
			opt_timings = 1;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (opt_numbers == 0)
				opt_level = 0;
			else
				opt_level *= 10;
			opt_level += ((int)opt - 48);
			opt_numbers++;
			break;
		default:
			usage();
		}
	}

	/**
	 * check parameters
	 */

	/* make opt_level valid */
	if (opt_level < LEVEL_MIN)
		opt_level = LEVEL_MIN;
	else if (opt_level > LEVEL_MAX)
		opt_level = LEVEL_MAX;

	/* opt_threads = 1..THREAD_MAX */
	if (opt_threads < 1)
		opt_threads = 1;
	else if (opt_threads > THREAD_MAX)
		opt_threads = THREAD_MAX;

	/* opt_iterations = 1..MAX_ITERATIONS */
	if (opt_iterations < 1)
		opt_iterations = 1;
	else if (opt_iterations > MAX_ITERATIONS)
		opt_iterations = MAX_ITERATIONS;

	/* opt_bufsize is in MiB */
	if (opt_bufsize > 0)
		opt_bufsize *= 1024 * 1024;

	/* File IO */
	if (argc < optind + 1) {
		if (IS_CONSOLE(stdin)) {
			usage();
		} else {
			fin = stdin;
		}
	} else
		fin = fopen(argv[optind], "rb");

	if (fin == NULL)
		perror_exit("Opening infile failed");

	if (ofilename == NULL) {
		if (IS_CONSOLE(stdout)) {
			usage();
		} else {
			fout = stdout;
		}
	} else
		fout = fopen(ofilename, "wb");

	if (fout == NULL)
		perror_exit("Opening outfile failed");

	/* begin timing */
	if (opt_timings)
		gettimeofday(&tms, NULL);

	for (;;) {
		if (opt_mode == MODE_COMPRESS) {
			do_compress(opt_threads, opt_level, opt_bufsize, fin,
				    fout, opt_timings);
		} else {
			do_decompress(opt_threads, opt_bufsize, fin, fout,
				      opt_timings);
		}

		opt_iterations--;
		if (opt_iterations == 0)
			break;

		fseek(fin, 0, SEEK_SET);
		fseek(fout, 0, SEEK_SET);
	}

	/* show timings */
	if (opt_timings) {
		gettimeofday(&tme, NULL);
		timersub(&tme, &tms, &tm);
		getrusage(RUSAGE_SELF, &ru);
		fprintf(stderr, ";%ld.%ld;%ld.%ld;%ld.%ld;%ld\n",
			tm.tv_sec, tm.tv_usec / 1000,
			ru.ru_utime.tv_sec, ru.ru_utime.tv_usec / 1000,
			ru.ru_stime.tv_sec, ru.ru_stime.tv_usec / 1000,
			(long unsigned)ru.ru_maxrss);
	}

	/* exit should flush stdout */
	exit(0);
}
