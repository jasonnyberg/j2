/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 * Copyright (C) 2018 Jason Nyberg <jasonnyberg@gmail.com> (dual-licensed)
 * (C) Copyright 2019 Hewlett Packard Enterprise Development LP.
 *
 * This file is part of j2.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either
 *
 *   * the GNU Lesser General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at
 *     your option) any later version
 *
 * or
 *
 *   * the GNU General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at
 *     your option) any later version
 *
 * or both in parallel, as here.
 *
 * j2 is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and
 * the GNU Lesser General Public License along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 */

/*
#include "vm.h"

const char *bootstrap=
    "[@input_stream [brl(input_stream) ! lambda!]@lambda lambda! |]@repl\n" // define repl
    "ROOT<repl(get_stdin())> [RETURN] ARG0 @";                              // read from stdin

int main(int argc, char *argv[]) { return vm_bootstrap(argc>1?argv[1]:(char *) bootstrap); }
*/

#include <stdio.h>
#include <dlfcn.h>  // dlopen/dlsym/dlclose

const char *bootstrap =
    "[@input_stream [brl(input_stream) ! lambda!]@lambda lambda! |]@repl\n"  // define repl
    "ROOT<repl([bootstrap.edict] [r] file_open!)> [RETURN] ARG0 @";

int main(int argc, char *argv[]) {
    int   status   = 0;
    void *dlhandle = NULL;

    dlerror();  // reset
    if (dlhandle = dlopen("libreflect.so", RTLD_LAZY | RTLD_GLOBAL | RTLD_NODELETE | RTLD_DEEPBIND)) {
        dlerror();  // reset
        int *(*vm_bootstrap)(char *) = dlsym(dlhandle, "vm_bootstrap");
        if (vm_bootstrap) vm_bootstrap(argc > 1 ? argv[1] : (char *) bootstrap);
        dlclose(dlhandle);
    } else {
        printf("dlopen libreflect.so failed, error %s\n", dlerror());
    }

    return status;
}
