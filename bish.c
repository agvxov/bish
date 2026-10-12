#include <tcl.h>
#include "tcl-easy.h"

extern int Tcl_exec(TCL_ARGS);
extern int GetoptObjCmd(ClientData unused, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

int Bishc_Init(Tcl_Interp * interp) {
    if (Tcl_InitStubs(interp, "8.6", 0) == NULL) {
        return TCL_ERROR;
    }

    TCL_EASY_CREATE_COMMAND(exec)
    // XXX
    Tcl_CreateObjCommand(interp, "::bish::getopt", GetoptObjCmd, NULL, NULL);

    return Tcl_PkgProvide(interp, "bishc", "0.1");
}
