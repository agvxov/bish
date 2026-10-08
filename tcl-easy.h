#ifndef TCL_EASY_H
#define TCL_EASY_H

#define TCL_ARGS ClientData clientData, Tcl_Interp *interp, int argc, const char **argv
#define TCL_EASY_CREATE_COMMAND(c) \
    Tcl_CreateCommand(interp, #c, Tcl_ ## c, (ClientData)NULL, (void (*)(void*))NULL);

#endif
