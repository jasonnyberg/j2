J2 is a system that combines its three main components into something that can be viewed as



*   a programming language ("Edict", for "executable dictionary")
*   a C reflection system
*   a C library wrapper/"auto-cli"/"glue" system
*   a structured-data reader/manipulator/writer
*   all of these at once

Edict is a minimalist programming language that makes up for its simplicity by having the built-in ability to understand and dynamically bind with C libraries, providing "native" access to C types and methods of arbitrary complexity without writing wrappers. Alternatively, you can look at it as a reflection library for C programs that allows them to expose dynamic access to its own internals at runtime.

The language is built upon a foundation of three elements:



*   INFORMATION: All data (the entire state of the system) resides in a "Listree" structure, which is a recursively-defined hierarchical dictionary.
*   CONTEXT: J2 can curate type, function, and variable descriptions from C libraries via DWARF, including its own library.
*   INTENTION: A simple VM (with accompanying interpreters/jit-compilers) provides a unified method of accessing and exploiting instructions and data in the domain it's applied to.

These three elements assemble themselves at runtime into a multi-purpose programmable environment. The system bootstraps itself by:



*   importing its own library
*   compiling its VM kernel (by default, it defines and evaluates a REPL using imported C methods)
*   running the compiled kernel in the VM…
*   ...which in turn starts the REPL


## The Language

<Basics>

The evolution of the language has been influenced by Forth, Lisp, Joy, Factor, Tcl, Mathematica, and others.

Literals

Names/DictionaryAPI


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
</table>



## Examples

A simple reflective program:


```
int! [3]@ square! stack!
```


Breakdown:


<table>
  <tr>
   <td><code>int</code>
   </td>
   <td>Lookup and push value of "int" (a native C type) onto the stack
   </td>
  </tr>
  <tr>
   <td><code>!</code>
   </td>
   <td>Evaluate top-of-stack, in this case allocate an instance of an "int"
   </td>
  </tr>
  <tr>
   <td><code>[3]</code>
   </td>
   <td>Push the literal "3" onto the stack
   </td>
  </tr>
  <tr>
   <td><code>@</code>
   </td>
   <td>Assignment; in this case, string "3" is automatically coerced into a C "int"
   </td>
  </tr>
  <tr>
   <td><code>square</code>
   </td>
   <td>Lookup and push the value of "square" (a native C method) onto the stack
   </td>
  </tr>
  <tr>
   <td><code>!</code>
   </td>
   <td>Evaluate top-of-stack, in this case an FFI call to native C method "square"
   </td>
  </tr>
  <tr>
   <td><code>stack</code>
   </td>
   <td>Lookup and push the value of "stack" (a native C method) onto the stack
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


```
[3] square! stack!
```


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



## Factorial

[@x x 1 x equal! | x decrement! multiply!]@factorial


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
   <td>Fuse top to layers of data stack, pop head of dictionary stack and push it to data stack
   </td>
  </tr>
  <tr>
   <td>FUN_PUSH
   </td>
   <td>Pop top of data stack and push onto func stack, add void layer to dict, add layer to data stack
   </td>
  </tr>
  <tr>
   <td>FUN_POP
   </td>
   <td>Push {RESET,CTX_POP,REMOVE} to code stack, pop top of func stack and do "EVAL"
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

(Any GNU/Linux distro worth its salt has these:



*   gcc, cmake
*   libdwarf, libdw, libelf (to read elf/dwarf debug section)
*   libdl (for dlopen, dlsym...)
*   libffi (foreign function calls to/from C libs)
*   libpthread (multithreading)
*   Optional: liblttng*/liburcu* (Linux Tracing Toolkit Next Gen)
*   Optional: rlwrap/libreadline (REPL convenience)

To run: Assuming gcc, cmake, and libraries are not too ancient, simply run "make" to build and get into the REPL.


## TODO



*   Symbolic ffi parameters
*   Do something with "comma" operator
*   Graph-preserving save/restore of listrees
*   XML, JSON, YAML, Swagger, lisp, mathematica-map bytecode compilers
*   Term-rewriting/symbolic-evaluation engine
*   Core libraries...

<!-- Docs to Markdown version 1.0β16 -->
