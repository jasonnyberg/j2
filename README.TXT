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

The interpreter component of this system has recently been changed
from an AST-traversing implementation to a decoupled JIT bytecode
compiler/VM implementation, so I can use the same VM backend to
support various front-end "hieraricical data" interpreters (XML, JSON,
etc.).

TODO:

* resurrect "map", "cond", "throw/catch" operators
* resurrect read from file
* sub-module loading
* xml, json, yaml, swagger, mathematica-map JIT compilers
* core libraries...
