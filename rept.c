#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
# include <windows.h>
#else
# include <unistd.h>
# include <errno.h>
#endif

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

#ifdef _WIN32
static void pwinerror(const char *prefix)
{
	if (!prefix) prefix = PACKAGE;
	DWORD error = GetLastError();

	char *message;
	DWORD len = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_FROM_SYSTEM, /* dwFlags */
		NULL,            /* lpSource */
		error,           /* dwMessageId */
		0,               /* dwLanguageId */
		(LPSTR)&message, /* lpBuffer */
		0,               /* nSize */
		NULL);           /* Arguments */

	if (len) {
		/* No trailing newline since FormatMessage adds it */
		fprintf(stderr, "%s: %s", prefix, message);
		HeapFree(GetProcessHeap(), 0, message);
	} else {
		fprintf(stderr, "%s: error %lu\n", prefix, error);
		fprintf(stderr, PACKAGE ": GetLastError failed: %lu\n",
			GetLastError());
	}
}

/* Extract the command line for the child command.

   Internally Windows doesn't use argv but a single command line string, which
   is what CreateProcess expects also. Programs differ in how they parse the
   command line into arguments. Most use CommandLineToArgvW but not all, in
   particular cmd.exe, so we shouldn't meddle there.

   Instead, just take our own command line (e.g. "rept.exe foo bar") and strip
   the first value, our program name. Also account for quoting and escape
   characters. */
static LPSTR get_child_cmdline()
{
	LPSTR cmdline = GetCommandLineA();
	bool in_quotes = false;
	bool skip_next = false;
	for (; *cmdline && (in_quotes || !isspace(*cmdline)); cmdline++) {
		if (skip_next) skip_next = false;
		else if (*cmdline == '\\') skip_next = true;
		else if (*cmdline == '"') in_quotes = !in_quotes;
	}
	while (isspace(*cmdline)) cmdline += 1;

	return cmdline;
}

/* Returns: 0 success
           -1 broken pipe
   Exits on error. */
static int run_child(TCHAR *cmdline)
{
	SECURITY_ATTRIBUTES sa = {0};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE pipe_r, pipe_w;
	if (!CreatePipe(&pipe_r, &pipe_w, &sa, 0)) {
		pwinerror(PACKAGE ": CreatePipe failed");
		exit(EXIT_FAILURE);
	}

	if (!SetHandleInformation(pipe_r, HANDLE_FLAG_INHERIT, 0)) {
		pwinerror(PACKAGE ": SetHandleInformation failed");
		exit(EXIT_FAILURE);
	}

	STARTUPINFO si = {0};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = pipe_w;
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	PROCESS_INFORMATION pi;
	BOOL created = CreateProcess(
		NULL,     /* lpApplicationName */
		cmdline,  /* lpCommandLine */
		NULL,     /* lpProcessAttributes */
		NULL,     /* lpThreadAttributes */
		TRUE,     /* bInheritHandles */
		0,        /* dwCreationFlags */
		NULL,     /* lpEnvironment */
		NULL,     /* lpCurrentDirectory */
		&si,      /* lpStatupInfo */
		&pi);     /* lpProcessInformation */

	if (!created) {
		pwinerror(PACKAGE ": CreateProcess failed");
		exit(EXIT_FAILURE);
	}

	CloseHandle(pi.hThread);
	CloseHandle(pipe_w); /* or we'll block waiting for ourselves */

	HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hout == INVALID_HANDLE_VALUE) {
		pwinerror(PACKAGE ": GetStdHandle failed");
		exit(EXIT_FAILURE);
	}

	char buf[4096];
	while (1) {
		DWORD len;
		if (!ReadFile(pipe_r, &buf, sizeof(buf), &len, NULL)) {
			if (GetLastError() == ERROR_BROKEN_PIPE) {
				/* Input pipe closed so child terminated.
				   Now we'll need to rerun the command */
				return 0;
			}

			pwinerror(PACKAGE ": ReadFile failed");
			exit(EXIT_FAILURE);
		}

		TCHAR *rest = buf;
		do {
			DWORD nwritten;
			if (WriteFile(hout, rest, len, &nwritten, NULL)) {
				len -= nwritten;
				rest += nwritten;
				continue;
			}

			switch (GetLastError()) {
			case ERROR_BROKEN_PIPE:
			case ERROR_NO_DATA: /* also happens on broken pipe */
				/* Output pipe closed, we're done */
				return 1;

			default:
				pwinerror(PACKAGE ": WriteFile failed");
				exit(EXIT_FAILURE);
			}
		} while (len > 0);
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pipe_r);
}

#else /* _WIN32 */

/* Returns: 0 success
           -1 broken pipe
   Exits on error. */
static int run_child(int argc, char **argv)
{
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
				if (errno == EPIPE) return -1;
				goto err;
			}
			len -= n;
			rest += n;
		} while (len > 0);
	}

	return 0;

err:	perror(PACKAGE);
	exit(EXIT_FAILURE);
}
#endif /* _WIN32 */

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

#if _WIN32
	LPSTR cmdline = get_child_cmdline();
#endif

	int done = 0;
	while (!done) {
		/* We don't actually want to run forever, but only as long as
		   our output is read, e.g. in:

		   $ rept echo hello | head -n5

		   we should terminate after `head` has read its 5 lines and
		   terminates. When this happens our standard output file will
		   be closed.

		   To detect this situation we pipe the output from the child
		   process through our own. When our write to stdout fails,
		   we know we're done. */
#ifdef _WIN32
		done = run_child(cmdline);
#else
		done = run_child(argc+1, argv-1);
#endif
	}

	return EXIT_SUCCESS;
}
