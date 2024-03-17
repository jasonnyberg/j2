(This doc is a work in progress…)

**NOTE: The “cpp” branch contains a “dumb” port of this project from C to C++, which I did for several reasons:**

*   **Obviously, it greases the skids towards expanding its runtime reflection capabilities into C++.**
*   **I wanted to eliminate the dependency on the GCC-only “nested functions” extension, using “standard” C++ lambdas instead…**
*   **...so it can be compiled with Clang/LLVM…**
*   **...so I can play with KLEE.**

-------------------------------------------------------------------------------------------------------------------------

## The Elevator Pitch...

J2 is a system that combines a minimalist programming language (“Edict”, for “executable dictionary”) with a C reflection/FFI system. It allows you to easily import shared libraries and use the datatypes, variables, and functions within it (at least the ones in global scope) _directly_, without needing to write any “glue” code.

For example, if I have a file “inc.c” containing:						

```
typedef struct mystruct { int i; float f; } mystruct;
extern mystruct increment(mystruct x) { x.i+=1; x.f+=1; return x; }
```


...and I make a shared library out of it:

```
gcc --shared -g inc.c -o inc.so
```

...then I can import that shared library into the edict interpreter and get right to using it:

```
e> loadlib([inc.so]) @mylib
Finished curating module
e> mylib<mystruct! <2@i 4@f> @x increment(x)>/ stack!
VMRES_STACK
 <null>
        structure_type "struct mystruct" 
    member "i" 
    base_type "int" 0x3 (0x7f361c0121a0)
    member "f" 
    base_type "float" 5 (0x7f361c0121a4)
```


It looks simple, but there’s a _lot_ going on here under the covers here...

## Language Overview

“Edict” is a minimalist programming language that makes up for its simplicity by having the built-in ability to understand and dynamically bind with C libraries, providing "native" access to C types, variables, and methods, of arbitrary complexity, without writing wrappers or glue code. Alternatively, you can look at it as a reflection library for C programs that allows them to expose dynamic access to their own internals at runtime.

The language is built upon a foundation of three elements:

*   Data Structure: All data (the entire state of the system) resides in a "Listree" structure, a recursively-defined hierarchical dictionary.
*   Reflection: C type, function, and variable definitions/declarations from C libraries are imported and curated via libraries’ DWARF (debugging) information.
*   Interpreter(s): A simple bytecode VM (with accompanying interpreters/jit-compilers) provides a unified method of accessing and exploiting instructions and data.

These three elements assemble themselves at runtime into a multi-purpose programmable environment. The system bootstraps itself by:

*   Importing its own library, so it can reflect upon itself
*   Compiling a kernel for the VM; by default, it compiles an Edict REPL (itself defined in Edict), using C building blocks imported from its own library
*   Running the kernel in the VM…
*   ...Which in turn (by default) evaluates an Edict file and/or interactive session.

The evolution of Edict has been influenced by Forth, Lisp, Joy, Factor, Tcl, Mathematica, and others.

## The Language

Key Points:

*   Edict is a “stack” language: Work is performed by popping data from the top of the data stack, operating on it, and pushing results back onto the stack.
*   Edict has a dictionary which contains… well, everything, including the data stack.
*   Edict 
*   “Code” is “data” (but not in the same way as Lisp).
*   Edict code is compiled into a simple core bytecode, which a VM executes.
*   Other kinds of structured/hierarchical data can be compiled into this bytecode.
*   This VM is “stackless”; It doesn’t recurse, and the VM inner bytecode dispatch loop is nearly condition-free:
    *   **<code>while (call=vm_dispatch(call)); // VM's inner loop</code></strong>
*   A VM context maintains its state within a single root “Listree” object.
*   Within a VM context, several stacks of items and frames, for:
    *   Bytecodes
    *   Data (Edict is a point-free postfix language)
    *   A hierarchical-dictionary
    *   Prefix-style “function call” operators 
    *   Exceptions

## Listree (“List Tree”)

(NOTE: I found this "ZigZag" structure that is _very_ similar (and predates) my Listree: https://www.nongnu.org/gzz/gi/gi.html)

The “Listree” is the core data structure of Edict and the VM which implements it. An instance of a Listree consists simply of a Listree Value, which contains:

*   Zero or one buffers(s) of “arbitrary” data, and
*   Zero or more _labeled_ _references_ to other Listree Values*.

The possibility of a Listree Value to contain (labeled) references to other Listree Values is what makes the Listree a “hierarchical” or “recursive” data structure. The Listree is at both the core of Edict and the system which implements it.

*It’s not explicit in this simple explanation (and that’s on purpose), but each label actually refers to a _list_ of references to other Listree Values.

As mentioned, the VM maintains all of its state within a Listree Value, including “Data Stack” and “Dictionary” sub-Listrees, among several others.

In Edict, there are two types of data values, but values of both types are stored using Listree Values which exist either on the VM’s Data Stack or within its Dictionary.

## Literals

The simplest type of value is a "literal". Literals are just text, delineated by square brackets:

```
[This is a literal]
```

If you come from a LISP background, you might think that the interpreter breaks literals down into s-expressions, but this is not the case. Everything between the brackets is represented “literally” in a Listree Value’s “data buffer”.

The Edict interpreter keeps track of nested square brackets, so:

```
[This is a [nested] literal]
```

is interpreted as a single literal with the value "This is a [nested] literal".

A "\" character appearing in the definition of a literal "escapes" the next character, allowing the interpreter to create literals containing (for instance) unbalanced square brackets:

```
[This is a literal containing an unbalanced \[ bracket]
```

When the interpreter comes across one, literal values (or rather, _references_ to the value -- an important distinction) are simply placed on the data stack.

OTOH, the interpreter does a little more processing on anything that’s _not_ a literal. (There are other kinds of values that are _not_ literals, which I’ll discuss in a later section…)

## The (Hierarchical) Dictionary

The Edict programmer can assign names to values on the stack, and subsequently refer to those values by those names. Assignment looks simply like this:

```
@mylabel
```

Assignment of a name to a value actually does several things:

*   It adds a “Listree Item” to the top frame* of the dictionary with that name, if it doesn’t already exist, and
*   adds to it a reference to the value, and
*   removes the stack’s reference to that value.

When the interpreter sees a reference to a label, that reference is replaced by a reference to the value associated with that label… In other words:

*   You can name things
*   You can then reference those things by name
*   (It’s not rocket science)

## Evaluation

Edict is different from other languages in one important way. Many languages are “homoiconic”, i.e. code and data are represented using the same underlying structures. (LISP is a traditional example of a homoiconic language.) Edict is neither homoiconic nor _non_-homoiconic: It doesn’t have “functions” at all. _It simply has an evaluation operator which can be applied to values._

A basic “function-like” thing in Edict might look like:

```
[1@x]
```

(Assign the label “x” to the value “1”.)

Note that _it is just a literal_.

To _invoke_ it, the _evaluation operator_ is used:

```
[1@x]!
```

The outcome of this is exactly the same as if the interpreter had just directly read the following:

```
1@x
```

All the evaluation operator does is feed the contents of the top of the top of the stack to the interpreter.

Now: recall that labels can be assigned to values, and that labels can be invoked to recall their values, and those values are pushed to the stack, and that the evaluation operator feeds the contents of the stack back into the interpreter:

```
[1@x]@f
f!
```

This little sequence does the following:

*   Pushes the literal value “[1@x]” to the stack
*   Applies the label “f” to the TOS value (removing it from the stack in the process)
*   Recalls the value labeled “f” to the stack
*   Evaluates it, which:
    *   Pushes the literal value “1” to the stack
    *   Assigns the label “x” to the TOS value (removing it in the process)

## Library Import/Reflection

Edict can import C library types and global variable/function information via the debugging section (DWARF) of a library, if it’s available. The DWARF information is processed and stored in the dictionary, and the VM can “understand” this information and present it within the Edict interpreter using the same simple “native” syntax that operates on literal values.

*   Instances of a type can be allocated (and placed on the stack, of course) by simply “evaluating the name of the type
    *   `int! @x`
*   Global variables can be “referenced” by their name (and their value will be placed on the stack) just like any other labeled data
    *   (assuming “int MyCGlobalInt=0;”)
    *   `MyCGlobalInt @y`
*   Global functions can be “evaluated” just like a literal value, using the “!” operator; arguments will be pulled from the stack, and return values will be pushed to the stack.
    *   (Assuming “int Multiply(int x, int y) { return x*y; }”)
    *   `x y Multiply!`

(WIP BELOW…)

<table>
  <tr>
   <td>REF
   </td>
   <td>Reference
   </td>
  </tr>
  <tr>
   <td>-REF
   </td>
   <td>Reference (tail)
   </td>
  </tr>
  <tr>
   <td>@
   </td>
   <td>Assignment to TOS
   </td>
  </tr>
  <tr>
   <td>@REF
   </td>
   <td>Assignment to Reference
   </td>
  </tr>
  <tr>
   <td>/
   </td>
   <td>Release TOS
   </td>
  </tr>
  <tr>
   <td>/REF
   </td>
   <td>Release REF
   </td>
  </tr>
  <tr>
   <td>^REF
   </td>
   <td>MetaReference
   </td>
  </tr>
  <tr>
   <td>REF^
   </td>
   <td>Merge top stack layer to REF
   </td>
  </tr>
  <tr>
   <td>^REF^ (^REF + ^)
   </td>
   <td>MetaReference and Merge Top Stack Layer to REF
   </td>
  </tr>
  <tr>
   <td>!
   </td>
   <td>Evaluate TOS
   </td>
  </tr>
  <tr>
   <td>TOS&lt;...>
   </td>
   <td>“Evaluate In Dict-Context” :Push TOS to dict, evaluate contents of &lt;...>, pop top of dict stack to TOS.
   </td>
  </tr>
  <tr>
   <td>TOS(...)
   </td>
   <td>“Evaluate in Code-Context”: Push null stack/dict layers, Push TOS to code stack, Evaluate contents of parens, evaluate top of code stack, discard top of Dict, concatenate top of stack to previous layer.
   </td>
  </tr>
</table>

## Examples

A simple reflective program:

    int! [3]@ square! stack!

Breakdown:

<table>
  <tr>
   <td><code>int</code>
   </td>
   <td>Lookup and push value of “int” (a native C type) onto the stack
   </td>
  </tr>
  <tr>
   <td><code>!</code>
   </td>
   <td>Evaluate top-of-stack, in this case allocate an instance of an “int”
   </td>
  </tr>
  <tr>
   <td><code>[3]</code>
   </td>
   <td>Push the literal “3” onto the stack
   </td>
  </tr>
  <tr>
   <td><code>@</code>
   </td>
   <td>Assignment; in this case, string “3” is automatically coerced into a C “int”
   </td>
  </tr>
  <tr>
   <td><code>square</code>
   </td>
   <td>Lookup and push the value of “square” (a native C method) onto the stack
   </td>
  </tr>
  <tr>
   <td><code>!</code>
   </td>
   <td>Evaluate top-of-stack, in this case an FFI call to native C method “square”
   </td>
  </tr>
  <tr>
   <td><code>stack</code>
   </td>
   <td>Lookup and push the value of “stack” (a native C method) onto the stack
   </td>
  </tr>
  <tr>
   <td><code>!</code>
   </td>
   <td>Evaluate top of stack...
   </td>
  </tr>
</table>

You'll see int 0x9, i.e. 3 squared.

The explicit creation and assignment of a C integer is shown only for the example; a simpler version would be:

    [3] square! stack!

The same coercion would have been performed automatically during the marshalling of FFI arguments.

Note that "code" does not evaluate automatically; Evaluation is invoked _explicitly_ via "!". Code is just data until you decide to "run" it:

<table>
  <tr>
   <td><code>[3] square stack!</code>
   </td>
   <td>data  and "code" on stack, unevaluated
   </td>
  </tr>
  <tr>
   <td><code>! stack!</code>
   </td>
   <td>Evaluate TOS and observe results
   </td>
  </tr>
</table>

Factorial:

    [@n int_iszero(n) 1 | int_mul(fact(int_dec(n)) n)]@fact

## Listree

In a Listree, a VALUE contains a dictionary of KEY/CLL pairs, where each CLL ("circularly-linked-list", implementing a double-ended queue) contains one or more references to VALUEs, each of which contains a dictionary of... And so on. The KEY/DEQ component of a listree is a BST implementation based on Arne Andersson's "simple" rbtree variation. (http://user.it.uu.se/~arnea/ps/simp.pdf)

## The VM

The VM is very simple; It evaluates bytecodes, each of which is an index into an array of C methods that implements the bytecode.

<table>
  <tr>
   <td>RESET
   </td>
   <td>Clear LIT
   </td>
  </tr>
  <tr>
   <td>EXT
   </td>
   <td>Decode character sequence and set LIT ("[one two 3]", "a.b.c")
   </td>
  </tr>
  <tr>
   <td>EXT_PUSH
   </td>
   <td>Push LIT onto data stack
   </td>
  </tr>
  <tr>
   <td>REF
   </td>
   <td>Create dictionary reference REF from LIT
   </td>
  </tr>
  <tr>
   <td>DEREF
   </td>
   <td>Resolve REF in dictionary
   </td>
  </tr>
  <tr>
   <td>ASSIGN
   </td>
   <td>Pop top of data stack and insert into the dictionary at location REF ("@")
   </td>
  </tr>
  <tr>
   <td>REMOVE
   </td>
   <td>Release value at REF
   </td>
  </tr>
  <tr>
   <td>EVAL
   </td>
   <td>pop top of data stack and A) call FFI, or B) push onto CODE stack and YIELD VM
   </td>
  </tr>
  <tr>
   <td>CTX_PUSH
   </td>
   <td>Pop top of data stack and push it to head of dictionary stack, push new layer to data stack
   </td>
  </tr>
  <tr>
   <td>CTX_POP
   </td>
   <td>Fuse top two layers of data stack, pop head of dictionary stack and push it to data stack
   </td>
  </tr>
  <tr>
   <td>FUN_PUSH
   </td>
   <td>Pop top of data stack and push onto func stack, add layer to data stack, add a null layer to dictionary
   </td>
  </tr>
  <tr>
   <td>FUN_EVAL
   </td>
   <td>Push {FUN_POP} to code stack, pop top of func stack and do "EVAL"
   </td>
  </tr>
  <tr>
   <td>FUN_POP
   </td>
   <td>Fuse top two stack layers, discard null dictionary layer
   </td>
  </tr>
  <tr>
   <td>THROW
   </td>
   <td>Throw an exception*
   </td>
  </tr>
  <tr>
   <td>CATCH
   </td>
   <td>Catch an exception*
   </td>
  </tr>
</table>

*Exceptions are how both VM errors and conditionals are implemented, and they fiddle with the state of the VM a little more than the average operation, deserving their own paragraph.

This simple suite of operations is sufficient to interact with the dictionary, recursively evaluate language constructs (the VM is a stackless implementation), and most importantly, exploit C types, functions, and data. It is a bare-bones framework that relies on its tight coupling to C in order to extend its capabilities effortlessly. 

## Dependencies

Any GNU/Linux distro worth its salt has these: (May need to use xxx-dev library packages)

*   gcc, cmake
*   libdwarf, libdw, libelf (to read elf/dwarf debug section)
*   libdl (for dlopen, dlsym...)
*   libffi (foreign function calls to/from C libs)
*   libpthread (multithreading)
*   Optional: liblttng*/liburcu* (Linux Tracing Toolkit Next Gen)
*   Optional: rlwrap/libreadline (REPL convenience, "make run" uses rlwrap)

To run: Assuming gcc, cmake, and libraries are not too ancient, simply run "make run" to build and get into the REPL.

To test some reflection and multithreading capabilities, do "make run" and then enter "test.async7!" at the prompt after you see "Finished curating <...>/libreflect.so". (That edict method is found in bootstrap.edict)

Exit the repl with ^D (EOF).

## TODO

*   Symbolic ffi parameters
*   Do something with “comma” operator
*   Graph-preserving save/restore of listrees
*   XML, JSON, YAML, Swagger, lisp, mathematica-map bytecode compilers
*   Term-rewriting/symbolic-evaluation engine
*   Core libraries...
