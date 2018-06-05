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

#ifndef VM_H
#define VM_H

#include "listree.h"

extern int vm_bootstrap(char *bootstrap);

#define THROW(expression,ltv) do { if (expression) { vm_throw(ltv); goto done; } } while(0)

extern void vm_throw(LTV *ltv);
extern LTV *vm_stack_enq(LTV *ltv);
extern LTV *vm_stack_deq(int pop);
extern LTV *encaps_ltv(LTV *ltv);
extern LTV *decaps_ltv(LTV *ltv);
extern LTV *vm_resolve(LTV *ref);
extern void vm_eval_ltv(LTV *ltv);

#endif // VM_H
