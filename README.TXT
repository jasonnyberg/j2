J2 is a minimalist programming language that makes up for its simplicity
by having the built-in ability to understand and dynamically bind with C
binaries/libraries via DWARF metadata...

I.e. Complex type/function signature awareness accessible via simple native
syntax, WITHOUT WRAPPERS.

The language is built upon the foundation of its data stack, its
eval stack, and its hierarchical dictionary. These are built
on top of a core "listree" structure (which can be thought of as a
hierarchical dictionary of double-ended queues ("deques"), which
is in turn built on top of what I think is an elegant implementation
of a bidirectional linked list (where all list modifications are
performed by the application of a single function, "splice").

The evolution of the language has been influenced by Forth, Lisp,
Joy, Factor, Tcl, Mathematica, and other languages.

This project is currently licensed under the GPLv3, but now that it uses
a homegrown AA BST implementation based on Arne Andersson's "simple" rbtree
variation (http://user.it.uu.se/~arnea/ps/simp.pdf) rather than the ancient
version of Linux's rbtree that I had been using, I plan on re-releasing it
under the LGPL v3.0.

#######################

Current external requirements: (Any GNU/Linux distro worth its salt
has these:

* gcc, cmake
* libdwarf, libdw, libelf (to read elf/dwarf debug section)
* libdl (for dlopen, dlsym...)
* libffi (foreign function calls to/from C libs)
* libpthread (multithreading)
* Optional: liblttng*/liburcu* (Linux Tracing Toolkit Next Gen)
* Optional: rlwrap/libreadline (REPL convenience)

To run: Assuming gcc, cmake, and libraries are not too ancient, simply
run "make" to get into the REPL.

#######################

A simple reflective edict program:

int! @a [3]@a a square! stack!

Breakdown:

int               [reference the "int" type within the interpreter's own reflection data, put it on stack]/
!                 [evaluate: evaluating types allocates a c variable ("cvar") of that type on the stack]/
@a                [name it "a"]/
[3]               [push the (string) literal "3" onto the stack]/
@a                [assign TOS to "a" again; since "a" is a cvar now, TOS is coerced into the C integer]/
a                 [put symbol "a"'s value onto the stack]/
square            [put the symbol "square" (a native C function!) onto the stack]/
!                 [evaluate the "code" on top of stack, whether it's edict or an ffi]/
stack!            [another native function reference/evaluation; you'll see int 0x9, i.e. 3 squared]/

Things have smoothed out over time; now you can do things like:

3 int! @          [assign number three to a C integer, all on the stack]/
3 square! stack!  [leverage some automatic argument coersions to skip some of the above steps]/

Note that "code" does not evaluate automatically; Evaluation is invoked explicitly via "!".
Code is just data until you decide to "run" it:

3 square          [data and "code" on stack, unevaluated]/
stack!            [show this, pre-evaluation]/
!                 [explicitly evaluate TOS]/
stack!            [show stack, post-evaluation]/

#######################

The interpreter component of this system has recently been changed
from an AST-traversing implementation to a bytecode compiler/VM implementation,
so I can use the same VM backend to support various front-end "hieraricical data"
interpreters (XML, JSON, etc.) and even other languages, theoretically (LISP).

TODO:

* resurrect read from file (DONE)
* external module (ultimately, compile-unit) loading (DONE)
* external debug info support (DONE) (debian-style /usr/lib/debug/.build-id/XX/YYYYY....)
* resurrect "map", "throw/catch" operators (throw/catch DONE, map etc. coming soon)
* basic cvar/ffi type coersion (DONE)
* symbolic ffi parameters
* array indexing (DONE)
* caller/callee hooks (insert shims between operators and operands i.e. "comma" operator)
* graph-preserving save/restore of listrees
* xml, json, yaml, swagger, lisp, mathematica-map bytecode compilers
* term-rewriting/symbolic-evaluation engine
* core libraries...
