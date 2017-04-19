
REPT                                 LOCAL                                REPT

NAME
     rept — repeat a command

SYNOPSIS
     rept program [argument ...]
     rept [options]

DESCRIPTION
     The rept utility calls the given command, waits for it to finish, then
     calls it again and so on until it is killed or standard output becomes
     unavailable.  The return value of the command is ignored.

     The Windows version of rept does no argument escaping of any kind; the
     command line following rept is passed to the system verbatim.  This means
     that the command is not executed within a cmd.exe context.  To use cmd
     built‐ins, invoke cmd as part of the command line as in the example
     below.

     Options:

     −h      Print a short help message and exit.

     −v      Print version and exit.

EXAMPLES
     The equivalent to the yes command is:

           rept echo yes

     On Windows, we need to go though cmd because echo is a built‐in:

           rept cmd /c echo yes

EXIT STATUS
     When standard output becomes unavailable the program terminates with exit
     status 0.  Should an error or abnormal termination occur, the exit status
     is nonzero.

AUTHORS
     iku@yokattana.com

Yokattana                       April 20, 2017                       Yokattana
