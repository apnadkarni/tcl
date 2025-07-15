---
CommandName: Tcl_Eval
ManualSection: 3
Version: 8.1
TclPart: Tcl
TclDescription: Tcl Library Procedures
Keywords:
 - execute
 - file
 - global
 - result
 - script
 - value
Copyright:
 - Copyright (c) 1989-1993 The Regents of the University of California.
 - Copyright (c) 1994-1997 Sun Microsystems, Inc.
 - Copyright (c) 2000 Scriptics Corporation.
---

# Name

Tcl_EvalObjEx, Tcl_EvalFile, Tcl_EvalObjv, Tcl_Eval, Tcl_EvalEx, Tcl_GlobalEval, Tcl_GlobalEvalObj, Tcl_VarEval - execute Tcl scripts

# Synopsis

::: {.synopsis} :::
**#include <tcl.h>**
[int]{.ret} [Tcl_EvalObjEx]{.lit} [interp, objPtr, flags]{.arg}
[int]{.ret} [Tcl_EvalFile]{.lit} [interp, fileName]{.arg}
[int]{.ret} [Tcl_EvalObjv]{.lit} [interp, objc, objv, flags]{.arg}
[int]{.ret} [Tcl_Eval]{.lit} [interp, script]{.arg}
[int]{.ret} [Tcl_EvalEx]{.lit} [interp, script, numBytes, flags]{.arg}
[int]{.ret} [Tcl_GlobalEval]{.lit} [interp, script]{.arg}
[int]{.ret} [Tcl_GlobalEvalObj]{.lit} [interp, objPtr]{.arg}
[int]{.ret} [Tcl_VarEval]{.lit} [interp, part, part, ...= §(char *)NULL]{.arg}
:::

# Arguments

.AP Tcl_Interp *interp in Interpreter in which to execute the script.  The interpreter's result is modified to hold the result or error message from the script. .AP Tcl_Obj *objPtr in A Tcl value containing the script to execute. .AP int flags in OR'ed combination of flag bits that specify additional options. \fBTCL_EVAL_GLOBAL\fR and \fBTCL_EVAL_DIRECT\fR are currently supported. .AP "const char" *fileName in Name of a file containing a Tcl script. .AP Tcl_Size objc in The number of values in the array pointed to by \fIobjv\fR; this is also the number of words in the command. .AP Tcl_Obj **objv in Points to an array of pointers to values; each value holds the value of a single word in the command to execute. .AP int numBytes in The number of bytes in \fIscript\fR, not including any null terminating character.  If -1, then all characters up to the first null byte are used. .AP "const char" *script in Points to first byte of script to execute (null-terminated and UTF-8). .AP "const char" *part in String forming part of a Tcl script. 

# Description

The procedures described here are invoked to execute Tcl scripts in various forms. \fBTcl_EvalObjEx\fR is the core procedure and is used by many of the others. It executes the commands in the script stored in \fIobjPtr\fR until either an error occurs or the end of the script is reached. If this is the first time \fIobjPtr\fR has been executed, its commands are compiled into bytecode instructions which are then executed.  The bytecodes are saved in \fIobjPtr\fR so that the compilation step can be skipped if the value is evaluated again in the future.

The return value from \fBTcl_EvalObjEx\fR (and all the other procedures described here) is a Tcl completion code with one of the values \fBTCL_OK\fR, \fBTCL_ERROR\fR, \fBTCL_RETURN\fR, \fBTCL_BREAK\fR, or \fBTCL_CONTINUE\fR, or possibly some other integer value originating in an extension. In addition, a result value or error message is left in \fIinterp\fR's result; it can be retrieved using \fBTcl_GetObjResult\fR.

**Tcl_EvalFile** reads the file given by \fIfileName\fR and evaluates its contents as a Tcl script.  It returns the same information as \fBTcl_EvalObjEx\fR. If the file could not be read then a Tcl error is returned to describe why the file could not be read. The eofchar for files is "\x1A" (^Z) for all platforms. If you require a "^Z" in code for string comparison, you can use "\x1A", which will be safely substituted by the Tcl interpreter into "^Z".

**Tcl_EvalObjv** executes a single preparsed command instead of a script.  The \fIobjc\fR and \fIobjv\fR arguments contain the values of the words for the Tcl command, one word in each value in \fIobjv\fR.  \fBTcl_EvalObjv\fR evaluates the command and returns a completion code and result just like \fBTcl_EvalObjEx\fR. The caller of \fBTcl_EvalObjv\fR has to manage the reference count of the elements of \fIobjv\fR, insuring that the values are valid until \fBTcl_EvalObjv\fR returns.

**Tcl_Eval** is similar to \fBTcl_EvalObjEx\fR except that the script to be executed is supplied as a string instead of a value and no compilation occurs.  The string should be a proper UTF-8 string as converted by \fBTcl_ExternalToUtfDString\fR or \fBTcl_ExternalToUtf\fR when it is known to possibly contain upper ASCII characters whose possible combinations might be a UTF-8 special code.  The string is parsed and executed directly (using \fBTcl_EvalObjv\fR) instead of compiling it and executing the bytecodes.  In situations where it is known that the script will never be executed again, \fBTcl_Eval\fR may be faster than \fBTcl_EvalObjEx\fR.  \fBTcl_Eval\fR returns a completion code and result just like \fBTcl_EvalObjEx\fR.

**Tcl_EvalEx** is an extended version of \fBTcl_Eval\fR that takes additional arguments \fInumBytes\fR and \fIflags\fR.

**Tcl_GlobalEval** and \fBTcl_GlobalEvalObj\fR are older procedures that are now deprecated.  They are similar to \fBTcl_EvalEx\fR and \fBTcl_EvalObjEx\fR except that the script is evaluated in the global namespace and its variable context consists of global variables only (it ignores any Tcl procedures that are active).  These functions are equivalent to using the \fBTCL_EVAL_GLOBAL\fR flag (see below).

**Tcl_VarEval** takes any number of string arguments of any length, concatenates them into a single string, then calls \fBTcl_Eval\fR to execute that string as a Tcl command. It returns the result of the command and also modifies the interpreter result in the same way as \fBTcl_Eval\fR. The last argument to \fBTcl_VarEval\fR must be (char *)NULL to indicate the end of arguments. 

# Flag bits

Any OR'ed combination of the following values may be used for the \fIflags\fR argument to procedures such as \fBTcl_EvalObjEx\fR:

**TCL_EVAL_DIRECT**
: This flag is only used by \fBTcl_EvalObjEx\fR; it is ignored by other procedures.  If this flag bit is set, the script is not compiled to bytecodes; instead it is executed directly as is done by \fBTcl_EvalEx\fR.  The \fBTCL_EVAL_DIRECT\fR flag is useful in situations where the contents of a value are going to change immediately, so the bytecodes will not be reused in a future execution.  In this case, it is faster to execute the script directly.

**TCL_EVAL_GLOBAL**
: If this flag is set, the script is evaluated in the global namespace instead of the current namespace and its variable context consists of global variables only (it ignores any Tcl procedures that are active). 


# Miscellaneous details

During the processing of a Tcl command it is legal to make nested calls to evaluate other commands (this is how procedures and some control structures are implemented). If a code other than \fBTCL_OK\fR is returned from a nested \fBTcl_EvalObjEx\fR invocation, then the caller should normally return immediately, passing that same return code back to its caller, and so on until the top-level application is reached. A few commands, like \fBfor\fR, will check for certain return codes, like \fBTCL_BREAK\fR and \fBTCL_CONTINUE\fR, and process them specially without returning.

**Tcl_EvalObjEx** keeps track of how many nested \fBTcl_EvalObjEx\fR invocations are in progress for \fIinterp\fR. If a code of \fBTCL_RETURN\fR, \fBTCL_BREAK\fR, or \fBTCL_CONTINUE\fR is about to be returned from the topmost \fBTcl_EvalObjEx\fR invocation for \fIinterp\fR, it converts the return code to \fBTCL_ERROR\fR and sets \fIinterp\fR's result to an error message indicating that the \fBreturn\fR, \fBbreak\fR, or \fBcontinue\fR command was invoked in an inappropriate place. This means that top-level applications should never see a return code from \fBTcl_EvalObjEx\fR other than \fBTCL_OK\fR or \fBTCL_ERROR\fR.

# Reference count management

**Tcl_EvalObjEx** and \fBTcl_GlobalEvalObj\fR both increment and decrement the reference count of their \fIobjPtr\fR argument; you must not pass them any value with a reference count of zero. They also manipulate the interpreter result; you must not count on the interpreter result to hold the reference count of any value over these calls.

**Tcl_EvalObjv** may increment and decrement the reference count of any value passed via its \fIobjv\fR argument; you must not pass any value with a reference count of zero. This function also manipulates the interpreter result; you must not count on the interpreter result to hold the reference count of any value over this call. 

