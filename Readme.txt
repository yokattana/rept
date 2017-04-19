
REPT                                 LOCAL                                REPT

NNAAMMEE
     rreepptt -- repeat a command

SSYYNNOOPPSSIISS
     rreepptt _p_r_o_g_r_a_m [_a_r_g_u_m_e_n_t _._._.]
     rreepptt [options]

DDEESSCCRRIIPPTTIIOONN
     The rreepptt utility calls the given command, waits for it to finish, then
     calls it again and so on until it is killed or standard output becomes
     unavailable.  The return value of the command is ignored.

     The Windows version of rreepptt does no argument escaping of any kind; the
     command line following rreepptt is passed to the system verbatim.  This means
     that the command is not executed within a _c_m_d_._e_x_e context.  To use _c_m_d
     built-ins, invoke _c_m_d as part of the command line as in the example
     below.

     Options:

     --hh      Print a short help message and exit.

     --vv      Print version and exit.

EEXXAAMMPPLLEESS
     The equivalent to the _y_e_s command is:

           rept echo yes

     On Windows, we need to go though _c_m_d because _e_c_h_o is a built-in:

           rept cmd /c echo yes

EEXXIITT SSTTAATTUUSS
     When standard output becomes unavailable the program terminates with exit
     status 0.  Should an error or abnormal termination occur, the exit status
     is nonzero.

AAUUTTHHOORRSS
     _i_k_u_@_y_o_k_a_t_t_a_n_a_._c_o_m

Yokattana                       April 19, 2017                       Yokattana
