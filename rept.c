#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
# include <windows.h>
#else
# include <unistd.h>
# include <errno.h>
# include <sys/types.h>
# include <sys/wait.h>
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
	if (!prefix)
		prefix = PACKAGE;
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
		fprintf(stderr, "%s: %lu - %s", prefix, error, message);
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
		if (skip_next)
			skip_next = false;
		else if (*cmdline == '\\')
			skip_next = true;
		else if (*cmdline == '"')
			in_quotes = !in_quotes;
	}

	while (isspace(*cmdline))
		cmdline += 1;

	return cmdline;
}

static bool pipe_error(DWORD err)
{
	switch (err) {
	case ERROR_NO_DATA:
	case ERROR_BROKEN_PIPE:
	case ERROR_PIPE_NOT_CONNECTED:
		return true;

	default:
		return false;
	}
}

/* Copy data from src to dest until EOF or broken pipe. Returns false if other
   read errors or any write errors occur. */
static bool drain(HANDLE src, HANDLE dest)
{
	char buf[4096];
	while (true) {
		DWORD num;
		if (!ReadFile(src, buf, sizeof(buf), &num, NULL)) {
			if (pipe_error(GetLastError()))
				break;

			pwinerror(PACKAGE ": ReadFile failed");
			exit(EXIT_FAILURE);
		}

		TCHAR *rest = buf;
		while (num > 0) {
			DWORD n;
			if (!WriteFile(dest, rest, num, &n, NULL)) {
				if (pipe_error(GetLastError()))
					return false;
				else {
					pwinerror(PACKAGE ": WriteFile "
						"failed");
					exit(EXIT_FAILURE);
				}
			}

			rest += n;
			num -= n;
		}
	}

	return true;
}

/* Returns 0 on success, -1 on broken pipe. Exits on error. */
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

	DWORD drain_err = drain(pipe_r, hout) ? 0 : GetLastError();

	CloseHandle(pi.hProcess);
	CloseHandle(pipe_r);

	if (drain_err == 0)
		return 0;
	else if (pipe_error(drain_err))
		return -1;
	else {
		pwinerror(PACKAGE);
		exit(EXIT_FAILURE);
	}
}

#else /* _WIN32 */

/* Copy data from src to dest until EOF or EPIPE. Returns false if other
   read errors on any write errors occur. */
static bool drain(int src, int dest)
{
	char buf[4096];
	while (true) {
		ssize_t num;
		while ((num = read(src, buf, sizeof(buf))) == -1
				&& errno == EINTR)
			;

		if (num == 0)
			break;
		else if (num == -1)
			return errno == EPIPE;

		char *rest = buf;
		while (num > 0) {
			ssize_t n;
			while ((n = write(dest, rest, num)) == -1
					&& errno == EINTR)
				;
			if (n == -1)
				return false;

			rest += n;
			num -= n;
		}
	}

	return true;
}

/* Returns 0 on success, -1 on broken pipe. Exits on error. */
static int run_child(char **argv)
{
	int fds[2];
	if (pipe(fds) == -1) {
		perror(PACKAGE ": failed to create pipe");
		exit(EXIT_FAILURE);
	}

	pid_t pid = fork();
	if (pid == -1) {
		perror(PACKAGE ": failed to fork()");
		exit(EXIT_FAILURE);
	}

	if (pid) {
		/* We're in the parent process.	We don't need the write end
		   of the pipe. */
		close(fds[1]);

		int drain_err = drain(fds[0], STDOUT_FILENO) ? 0 : false;
		wait(NULL);
		close(fds[0]);

		if (drain_err == 0)
			return 0;
		else if (drain_err == EPIPE)
			return -1;
		else {
			perror(PACKAGE);
			exit(EXIT_FAILURE);
		}
	} else {
		/* In the child process. Assign the write end of the
		   pipe we created to standard output */
		while (dup2(fds[1], STDOUT_FILENO) == -1) {
			if (errno == EINTR) {
				perror(PACKAGE ": failed to dup");
				exit(EXIT_FAILURE);
			}
		}

		/* We don't need the pipe handles any more */
		close(fds[0]);
		close(fds[1]);

		execvp(argv[0], argv);

		perror(PACKAGE ": failed to start child");
		exit(EXIT_FAILURE);
	}
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
		done = run_child(argv+1);
#endif
	}

	return EXIT_SUCCESS;
}
