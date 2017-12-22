J2 is a minimalist programming language that makes up for its simplicity
by having the built-in ability to understand and dynamically bind with C
binaries, transparently.

I.e. no wrappers necessary.

The language is built upon a foundation of its data stack, its
eval stack, and its hierarchical dictionary. These in turn are built
on top of what I think is an elegant implementation of a circular
linked list (where all list operations are performed by the
uniform application of a single function, "splice") and my "listree"
structure (which can be thought of as a hierarchical dictionary of
double-ended queues ("deques").

While the project has evolved organically on its own from earlier,
less "language like" roots, how it's evolved has been influenced by
Forth, Lisp, Joy, Factor, Tcl, Mathematica, and other languages.

This project is licensed under the GPLv3, including the conveniently
included linux rbtree sources. dwarf.c is a completely mangled version
of libdwarf's "simplereader.c"

#######################

Current external requirements: (Any GNU/Linux distro worth its salt
has these:

* gcc, cmake
* libdwarf, libelf (to read elf/dwarf debug section)
* libdl (for dlopen, dlsym...)
* libffi (to make dynamic function calls into C libraries)
* libpthread (the VM will support multithreading)
* rlwrap (REPL conveniences; not strictly required)

To run: Assuming gcc, cmake, and libraries are not too ancient, simply
run "make" to get into the REPL.

#######################

A simple reflective edict program:

self.int #new @a [3]@a a self.square! #dump

Breaddown:

self.int          [reference the "int" type within the interpreter's own reflection data, put it on stack]/
#new              [a builtin: allocate a new instance of whatever type is sitting on top of the stack]/
@a                [name it "a"]/
[3]               [push the (string) literal "3" onto the stack]/
@a                [assign the symbol "a" (created above) with the value at the top of the stack]/
a                 [put symbol "a"'s value onto the stack]/
self.square       [put the symbol "self.square" (a native C function!) onto the stack]/
!                 [evaluate the "code" on top of stack, whether it's edict or an ffi]/
#dump             [a builtin: show internal structures; On the stack you'll see int 0x9, i.e. 3 squared]/

#######################

The interpreter component of this system has recently been changed
from an AST-traversing implementation to a decoupled JIT bytecode
compiler/VM implementation, so I can use the same VM backend to
support various front-end "hieraricical data" interpreters (XML, JSON,
etc.).

TODO:

* resurrect read from file
* external module (ultimately, compile-unit) loading
* resurrect "map", "throw/catch" operators
* basic cvar/ffi type coersion
* symbolic ffi parameters
* caller/callee hooks (insert shims between operators and operands)
* xml, json, yaml, swagger, lisp, mathematica-map JIT compilers
* core libraries...
