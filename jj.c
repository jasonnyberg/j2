/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "vm.h"

char *bootstrap=
    "[@input_stream [brl(input_stream) ! lambda!]@lambda lambda! |]@repl\n"
    "ROOT<repl([bootstrap.edict] [r] file_open!) ARG0 decaps! <> encaps! RETURN @>";

int main(int argc, char *argv[]) { return vm_bootstrap(argc>1?argv[1]:bootstrap); }
