#include <tcl.h>
#include "tcl-easy.h"
#include <errno.h>
#include <signal.h>
#include <unistd.h>

int
Tcl_exec(TCL_ARGS) {
    char **args;
    struct sigaction dfl, old_pipe;
    sigset_t none, old_mask;

    (void)clientData;

    if (argc < 2) {
        return TCL_OK;
    }

    // Flush channels
    Tcl_EvalEx(interp, "foreach c [chan names] {catch {chan flush $c}}", -1, 0);
    Tcl_ResetResult(interp);

    args = (char **)ckalloc(argc * sizeof(char *));
    for (int i = 1; i < argc; i++) {
        args[i - 1] = (char *) argv[i];
    }
    args[argc - 1] = NULL;

    /* Tcl ignores SIGPIPE and may block signals
     *  the new program must not inherit that
     */
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    dfl.sa_flags = 0;
    sigaction(SIGPIPE, &dfl, &old_pipe);
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, &old_mask);

    execvp(args[0], args);

    // Fail branch only
    int e = errno;
    sigaction(SIGPIPE, &old_pipe, NULL);
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
    ckfree(args);

    Tcl_SetErrno(e);
    Tcl_AppendResult(
        interp,
        "exec: ",
        argv[1],
        ": ",
        Tcl_PosixError(interp),
        (char*)NULL
    );
    return TCL_ERROR;
}
