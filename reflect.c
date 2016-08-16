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


#include "edict.h"


LTV *reflection_member(LTV *val,char *name); // dereference member by name

int reflection_dump(LTv *val); // dump binary data, metadata to stdout

char *refelction_write(LTV *val); // to_string(s)
int reflection_read(LTV *val,char *value); // from_string(s)

int reflection_new(char *type); // expose through edict
int reflection_delete(LTV *val); // expose through edict

int reflection_pickle(); // TOS cvar to edict representation
int reflection_unpickle(); // TOS edict representation to cvar



LTV *reflection_member(LTV *val,char *name)
{
    return NULL;
}
