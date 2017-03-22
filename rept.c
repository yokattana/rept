#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#ifndef PACKAGE
#define PACKAGE "rept"
#endif

#ifndef VERSION
#define VERSION "(git)"
#endif

static const char * const usage =
"usage: " PACKAGE " program [argument...]  run rept until pipe closed\n"
"       " PACKAGE " -h|--help              print this\n"
"       " PACKAGE " -v|--version           print name and version";

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "%s\n", usage);
		return EXIT_FAILURE;
	} else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		printf("%s\n", usage);
		return EXIT_SUCCESS;
	} else if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
		printf("%s %s\n", PACKAGE, VERSION);
		return EXIT_SUCCESS;
	} else if (argv[1][0] == '-') {
		fprintf(stderr, "%s: unknown option: %s\n", PACKAGE, argv[1]);
		return EXIT_FAILURE;
	}

	while (1) {
		/* We don't actually want to run forever, but only as long as
		   our output is read, e.g. in:

		   $ rept echo hello | head -n5

		   we should terminate after `head` has read its 5 lines and
		   terminates. When this happens our standard output file will
		   be closed.

		   To detect this situation we pipe the output from the child
		   process through our own. When our write to stdout fails,
		   we know we're done. */
		int fds[2];
		if (pipe(fds) == -1) goto err;

		pid_t pid = fork();
		if (pid == -1) goto err;

		if (pid == 0) {
			/* In the child process. Assign the write end of the
			   pipe we created to standard output */
			while (dup2(fds[1], STDOUT_FILENO) == -1) {
				if (errno != EINTR) goto err;
			}

			/* We don't need the pipe handles any more */
			close(fds[0]);
			close(fds[1]);

			execvp(argv[1], &argv[1]);

			/* execvp has failed if we get here */
			goto err;
		}

		/* We're in the parent process.	We don't need the write end
		   of the pipe. */
		close(fds[1]);

		char buf[4096];
		while (1) {
			ssize_t len;
			do { len = read(fds[0], buf, sizeof(buf));
			} while (len == -1 && errno == EINTR);

			if (len == -1) goto err;
			if (len == 0) {
				/* EOF, child has terminated */
				close(fds[0]);	
				break;
			}

			char *rest = buf;
			do {
				size_t n = write(STDOUT_FILENO, rest, len);
				if (n == -1) {
					if (errno == EINTR) continue;
					if (errno == EPIPE) goto done;
					goto err;
				}
				len -= n;
				rest += n;
			} while (len > 0);
		}
	}

done:	return EXIT_SUCCESS;

err:	perror(PACKAGE);
	return EXIT_FAILURE;
}
