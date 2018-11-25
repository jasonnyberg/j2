/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 * Copyright (C) 2018 Jason Nyberg <jasonnyberg@gmail.com> (dual-licensed)
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

#ifndef VIZ_H
#define VIZ_H

#define VIZ

//////////////////////////////////////////////////
// Constructs used in graphical visualization
//////////////////////////////////////////////////

typedef struct { unsigned short s[0]; void *d1; void *d2; } Seed;
#define RESEED(seed,ptr) (seed.d1=seed.d2=ptr,seed.s)
#define NRAND(seed) (nrand48(seed.s)) // 0 through 2^31
#define JRAND(seed) (jrand48(seed.s)) // -2^31 through 2^31
#define ERAND(seed) (erand48(seed.s)) // 0.0 through 1.0
#define DRAND(seed) (ERAND(seed)*2.0-1.0) // -1.0 through 1.0


typedef struct { int v[0]; int x,y,z,vol; } D3;
#define VOL3D(i) ((i)[3]=(i)[0]*(i)[1]*(i)[2])

#define DIM3(x,y,z,d) ((x)*(d)[1]*(d)[2] + (y)*(d)[2] + (z))
#define DIM3D(i,d) (((i)[3]=DIM3((i)[0],(i)[1],(i)[2],(d))),((i)[3]<0?-1:((i)[3]>(d)[3]?-1:(i)[3])))
#define CLIP3D(i,d) (DIM3D((i),(d)),(i)[0]>=0 && (i)[0]<(d)[0] && (i)[1]>=0 && (i)[1]<(d)[1] && (i)[2]>=0 && (i)[2]<(d)[2])

#define WRAP3(x,y,z,d) (((x)%=(d)[0])*(d)[1]*(d)[2] + ((y)%=(d)[1])*(d)[2] + ((z%=(d)[2])))
#define WRAP3D(i,d) ((i)[3]=WRAP3((i)[0],(i)[1],(i)[2],(d)))

#define LOOP(i,from,to) for ((i)=(from);(i)<(to);(i)++)
#define ZLOOP(i,lim) LOOP((i),0,lim)
#define RLOOP(i,from,to) for ((i)=(from-1);(i)>=(to);(i)--)
#define ZRLOOP(i,lim) RLOOP((i),lim,0)
#define LOOPD3(iv,fromv,tov) LOOP((iv)[0],(fromv)[0],(tov)[0]) LOOP((iv)[1],(fromv)[1],(tov)[1]) LOOP((iv)[2],(fromv)[2],(tov)[2])
#define ZLOOPD3(iv,limv) LOOP((iv)[0],0,(limv)[0]) LOOP((iv)[1],0,(limv)[1]) LOOP((iv)[2],0,(limv)[2])

typedef struct { int   v[0]; int   x,y,z; } ivec;
typedef struct { char  v[0]; char  x,y,z; } cvec;
typedef struct { float v[0]; float x,y,z; } fvec;


#endif // VIZ_H
