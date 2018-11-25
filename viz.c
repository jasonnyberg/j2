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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/freeglut.h>

#include <viz.h>

typedef struct { cvec offset[SYNAPSES]; } DendriteMap;

DendriteMap gDendriteMap[DENDRITE_CACHE];
#define DENDRITEMAP(i) (gDendriteMap[(i)%DENDRITE_CACHE])

typedef enum { ACTIVE=1,PREDICTED=2,IMAGINED=4 } HTM_STATE;

int cycles=0;
Seed gseed={{},NULL,NULL};

int show_cells=1;
int show_dendrites=1;
int show_map=0;
int show_scores=0;
int show_suppression=0;
int show_risers=0;
int show_predictions=1;
int show_tex=1;
int hide_input=0;
int do_generative=1;
int show_coords=0;


/********************************************************************************************
 *
 * Structures
 *
 */
typedef struct
{
    unsigned char permanence;
} Synapse;

typedef struct
{
    Synapse *synapse;
    unsigned char sensitivity;
    unsigned char score;
} Dendrite;

typedef struct
{
    Dendrite *dendrite;
    char bias;
} Dendrites;

typedef struct
{
    D3 size;
    fvec position;
    unsigned char *active;  // 50% decay per tick
    unsigned char *predicted;
    unsigned char *imagined;
    float *score;
    float *suppression;
} StateMap;

typedef struct
{
    StateMap *input;
    StateMap *output;
    int breadth,depth; // dendrites,synapses
    D3 insize,offset; // region of input to sample
    Dendrites *dendrites;
} Interface;

typedef enum { FEEDFWD,INTRA,FEEDBACK,INTERFACES } HTM_INPUT; // inter-region interfaces

typedef struct
{
    StateMap states;
    Interface interface[INTERFACES];
    int dendrites;
} Region;

typedef struct
{
    D3 size;
    fvec position;
    cvec breadth; // per interface
    cvec depth; // per interface
    int lowerlayer; // relative offset from this layer to it's lower-layer
    //D3 size,offset; // region of lower layer to sample
} RegionDesc;

typedef struct
{
    int regions;
    Region *region;
} Htm;



/********************************************************************************************
 *
 * Initialization
 *
 */
void DendriteMap_init()
{
    int i;

    void generate()
    {
        int synapse;
        unsigned long long r=NRAND(gseed);
        int v[]={1,(r&0x10000000)?1:-1};
        int xx=(r&0x2000000)?1:-1; // flip l/r
        int xy=(r&0x4000000)?1:0; // switch axes
        int mx[]={6,2};

        r<<=31;
        r|=NRAND(gseed);
        BZERO(DENDRITEMAP(i));

        LOOP(synapse,1,SYNAPSES)
        {
            if ((r&mx[v[0]]) && !(v[0]=!v[0]) && (r&1)) v[1]=-v[1];
            DENDRITEMAP(i).offset[synapse].v[xy] =v[0]*xx;
            DENDRITEMAP(i).offset[synapse].v[!xy]=v[0]?0:v[1];
            DENDRITEMAP(i).offset[synapse].v[2]  =(r&0xff000)>>12;
            r>>=1;
        }
    }

    ZLOOP(i,DENDRITE_CACHE) generate();
}


int StateMap_init(StateMap *map,D3 *size,fvec *position)
{
    int i;

    if (!map) return !0;
    VOL3D(size->v); // assigns to vol
    map->size=*size;
    map->position=*position;
    map->active=malloc(size->vol*sizeof(map->active[0]));
    map->predicted=malloc(size->vol*sizeof(map->predicted[0]));
    map->imagined=malloc(size->vol*sizeof(map->imagined[0]));
    map->score=malloc(size->vol*sizeof(map->score[0]));
    map->suppression=malloc(size->vol*sizeof(map->suppression[0]));
    bzero(map->active,size->vol*sizeof(map->active[0]));
    bzero(map->predicted,size->vol*sizeof(map->predicted[0]));
    bzero(map->imagined,size->vol*sizeof(map->imagined[0]));
    bzero(map->score,size->vol*sizeof(map->score[0]));
    bzero(map->suppression,size->vol*sizeof(map->suppression[0]));
    return 0;
}


int Interface_init(Interface *interface,StateMap *input,StateMap *output,int breadth,int depth)
{
    int i,d,s,r;
    unsigned char r2;

    int dendrites;

    if (!interface || !output || !input) return !0;
    interface->input=input;
    interface->output=output;
    interface->breadth=breadth;
    interface->depth=depth;

    interface->dendrites=malloc(output->size.vol * sizeof(Dendrites));
    ZLOOP(i,output->size.vol)
    {
        interface->dendrites[i].bias=0;
        interface->dendrites[i].dendrite=malloc(breadth*sizeof(Dendrite));
        ZLOOP(d,breadth)
        {
            r=NRAND(gseed);
            r2=r>>12;
            interface->dendrites[i].dendrite[d].sensitivity=r2;
            interface->dendrites[i].dendrite[d].synapse=malloc(depth*sizeof(Synapse));
            ZLOOP(s,depth)
                interface->dendrites[i].dendrite[d].synapse[s].permanence=PTHRESH;
        }
    }
    return 0;
}


int Region_init(Region *region,D3 *size,fvec *position)
{
    D3 s;
    if (!region || !size) return !0;
    BZERO(*region);
    StateMap_init(&region->states,size,position);
    return 0;
}


int Htm_init(Htm *htm,RegionDesc *rd,int regions)
{
    int r,ll,i;
    if (!htm || !regions || !rd) return !0;

    DendriteMap_init();

    htm->regions=regions;
    htm->region=malloc(sizeof(Region)*regions);
    ZLOOP(r,regions) Region_init(&htm->region[r],&rd[r].size,&rd[r].position);

    ZLOOP(r,regions)
    {
        ZLOOP(i,INTERFACES) htm->region[r].dendrites+=rd[r].breadth.v[i];

        Interface_init(&htm->region[r].interface[INTRA],
                       &htm->region[r].states,
                       &htm->region[r].states,
                       rd[r].breadth.v[INTRA],
                       rd[r].depth.v[INTRA]);

        if ((ll=rd[r].lowerlayer))
        {
            Interface_init(&htm->region[r].interface[FEEDFWD],
                           &htm->region[r-ll].states,
                           &htm->region[r].states,
                           rd[r].breadth.v[FEEDFWD],
                           rd[r].depth.v[FEEDFWD]);

            Interface_init(&htm->region[r-ll].interface[FEEDBACK],
                           &htm->region[r].states,
                           &htm->region[r-ll].states,
                           rd[r].breadth.v[FEEDBACK],
                           rd[r].depth.v[FEEDBACK]);
        }
    }
}



/********************************************************************************************
 *
 * Processing
 *
 */
typedef int (*Synapse_op)(D3 *ipos,D3 *opos,int dendrite,int synapse,DendriteMap *map);

int Interface_traverse(Interface *interface,Synapse_op op)
{
    int status=0;
    Seed seed;
    D3 ipos,opos;
    fvec delta;
    ivec fanout;
    int d,s,axis;
    DendriteMap map;
    Synapse *syn;
    int i;
    int random;

    if (!interface || !interface->input || !interface->output) return !0;

    delta.x=(float) interface->input->size.x/(float) interface->output->size.x;
    delta.y=(float) interface->input->size.y/(float) interface->output->size.y;
    fanout.x=MAX((int) delta.x,1);
    fanout.y=MAX((int) delta.y,1);

    RESEED(seed,interface); // reseed when traversing an interface's dendrites

    ZLOOPD3(opos.v,interface->output->size.v)
    {
        (void) DIM3D(opos.v,interface->output->size.v); // populate opos.vol
        ZLOOP(d,interface->breadth)
        {
            random=NRAND(seed); random>>=4;
            ipos.x=(int) (opos.x*delta.x)+(random%fanout.x); random>>=4;
            ipos.y=(int) (opos.y*delta.y)+(random%fanout.y); random>>=4;
            map=DENDRITEMAP(random);
            ZLOOP(s,interface->depth)
            {
                ipos.x+=map.offset[s].x;
                ipos.y+=map.offset[s].y;
                ipos.z=map.offset[s].z%interface->input->size.z;
                if ((status=op(&ipos,&opos,d,s,&map)))
                    goto done;
            }
        }
    }
 done:
    return status;
}

int Interface_suppress(Interface *interface)
{
    float synapses=interface->depth;
    int i;

#define SUPPRESSION ((synapses-synapse)/synapses)

    int synapse_op(D3 *ipos,D3 *opos,int dendrite,int synapse,DendriteMap *map)
    {
        if (synapse>0 && CLIP3D(ipos->v,interface->input->size.v))
            interface->input->suppression[ipos->vol] += interface->output->score[opos->vol] * SUPPRESSION;
        return 0;
    }

    if (!interface->input || !interface->output || interface->output->size.vol==0)
        return 0;

    Interface_traverse(interface,synapse_op);

    ZLOOP(i,interface->output->size.vol)
    {
        interface->output->score[i]*=synapses;
        interface->output->score[i]-=interface->output->suppression[i];
    }

    return 0;
}


/********************************************************************************************/
/********************************************************************************************/

int Interface_score(Interface *interface,HTM_STATE state,int mask)
{
    int synapse_op(D3 *ipos,D3 *opos,int dendrite,int synapse,DendriteMap *map)
    {
        Dendrites *dens=&interface->dendrites[opos->vol];
        Dendrite *den=&dens->dendrite[dendrite];
        Synapse *syn=&den->synapse[synapse];

        int firing(int input) { return mask? input&mask : input>den->sensitivity+dens->bias; }

        if (synapse==0)
        {
            den->score=0;
            if (interface->input==interface->output) return 0; // don't let cell use itself as input
        }

        if (CLIP3D(ipos->v,interface->input->size.v))
            if (syn->permanence+(DRAND(gseed)*NOISE_FACTOR) > PTHRESH)
            {
                if (state&ACTIVE)    den->score+=firing(interface->input->active[ipos->vol]);
                if (state&PREDICTED) den->score+=firing(interface->input->predicted[ipos->vol]);
                if (state&IMAGINED)  den->score+=firing(interface->input->imagined[ipos->vol]);
            }

        if (synapse==interface->depth-1)
            if (den->score >= DTHRESH)
                interface->output->score[opos->vol] += den->score;

        return 0;
    }

    return Interface_traverse(interface,synapse_op);
}


int Interface_adjust(Interface *interface,HTM_INPUT input,int mask)
{
#define INC(x,y) ((x)=MAX((x),(typeof(x)) ((x)+(y))))
#define DEC(x,y) ((x)=MIN((x),(typeof(x)) ((x)-(y))))

    int synapse_op(D3 *ipos,D3 *opos,int dendrite,int synapse,DendriteMap *map)
    {
        Dendrites *dens=&interface->dendrites[opos->vol];
        Dendrite *den=&dens->dendrite[dendrite];
        Synapse *syn=&den->synapse[synapse];

        int firing(int input) { return mask? input&mask : input>den->sensitivity+dens->bias; }

        if (interface->input==interface->output && synapse==0) return 0; // don't let cell use itself as input

        if (CLIP3D(ipos->v,interface->input->size.v))
        {
            if (interface->output->active[opos->vol] & IS_ACTIVE)
            {
                if (firing(interface->input->active[ipos->vol]))
                    INC(syn->permanence,STRONGER);
                else
                    DEC(syn->permanence,WEAKER);
            }
        }

        return 0;
    }

    return Interface_traverse(interface,synapse_op);
}

int Interface_rscore(Interface *interface,HTM_STATE state)
{
    int synapse_op(D3 *ipos,D3 *opos,int dendrite,int synapse,DendriteMap *map)
    {
        Dendrites *dens=&interface->dendrites[opos->vol];
        Dendrite *den=&dens->dendrite[dendrite];
        Synapse *syn=&den->synapse[synapse];

        if (interface->input==interface->output && synapse==0) return 0; // don't let cell use itself as input

        if (CLIP3D(ipos->v,interface->input->size.v))
            if (syn->permanence > PTHRESH)
            {
                if (state&ACTIVE && interface->output->active[opos->vol]&IS_ACTIVE)
                    interface->input->score[ipos->vol]++;
                if (state&PREDICTED && interface->output->predicted[opos->vol]&IS_ACTIVE)
                    interface->input->score[ipos->vol]++;
                if (state&IMAGINED && interface->output->imagined[opos->vol]&IS_ACTIVE)
                    interface->input->score[ipos->vol]++;
            }

        return 0;
    }

    Interface_traverse(interface,synapse_op);

    return 0;
}

int Htm_update(Htm *htm)
{
    int r,i,j;

    int zactive()      { ZLOOP(r,htm->regions) ZLOOP(i,htm->region[r].states.size.vol) htm->region[r].states.active[i]>>=DECAY; }
    int zpredicted()   { ZLOOP(r,htm->regions) ZLOOP(i,htm->region[r].states.size.vol) htm->region[r].states.predicted[i]>>=DECAY; }
    int zimagined()    { ZLOOP(r,htm->regions) ZLOOP(i,htm->region[r].states.size.vol) htm->region[r].states.imagined[i]>>=DECAY; }
    int zscore()       { ZLOOP(r,htm->regions) ZLOOP(i,htm->region[r].states.size.vol) htm->region[r].states.score[i]=0; }
    int zsuppression() { ZLOOP(r,htm->regions) ZLOOP(i,htm->region[r].states.size.vol) htm->region[r].states.suppression[i]=0; }

    int sense(Region *region)
    {
        if (!region) return !0;
        if (region->interface[FEEDFWD].input)
        {
            Interface_score(&region->interface[FEEDFWD],ACTIVE,0);
            Interface_suppress(&region->interface[INTRA]);
            ZLOOP(i,region->states.size.vol) if (region->states.score[i]>0) region->states.active[i]|=IS_ACTIVE;
        }
        else
        {
            //ZLOOP(i,region->states.size.vol) region->states.active[i]=getchar();
            D3 p,a={{},1,1,0,0},b={{},6,6,1,0},c={{},7,7,1,0};
            static int offset=0;

            ZLOOP(i,region->states.size.vol) region->states.active[i]=0x00;
            if (!hide_input)
            {
                ZLOOPD3(p.v,c.v) region->states.active[(DIM3D(p.v,region->states.size.v)+offset)%region->states.size.vol]=0xff;
                LOOPD3(p.v,a.v,b.v) region->states.active[(DIM3D(p.v,region->states.size.v)+offset)%region->states.size.vol]=0x00;
            }
            offset++;
        }
    }

    int adjust(Region *region)
    {
        if (!region) return !0;
        Interface_adjust(&region->interface[FEEDFWD],FEEDFWD,0);
        Interface_adjust(&region->interface[INTRA],INTRA,IS_ACTIVE>>DECAY);
        Interface_adjust(&region->interface[FEEDBACK],FEEDBACK,IS_ACTIVE>>DECAY);
    }

    int predict(Region *region)
    {
        Interface_score(&region->interface[INTRA],ACTIVE,IS_ACTIVE);
        //Interface_score(&region->interface[FEEDBACK],ACTIVE,IS_ACTIVE);
        ZLOOP(i,region->states.size.vol) if (region->states.score[i]>0) region->states.predicted[i]|=IS_ACTIVE;
    }

    int imagine1(Region *region)
    {
        Interface_rscore(&region->interface[FEEDFWD],PREDICTED | IMAGINED);
        //Interface_score(&region->interface[INTRA],PREDICTED | IMAGINED);
        ZLOOP(i,region->states.size.vol) if (region->states.score[i]>0) region->states.imagined[i]|=IS_ACTIVE;
    }

    int imagine2(Region *region)
    {
        Interface_score(&region->interface[FEEDFWD], PREDICTED | IMAGINED,IS_ACTIVE);
        Interface_score(&region->interface[INTRA],   PREDICTED | IMAGINED,IS_ACTIVE);
        Interface_score(&region->interface[FEEDBACK],PREDICTED | IMAGINED,IS_ACTIVE);
        ZLOOP(i,region->states.size.vol) if (region->states.score[i]>0) region->states.imagined[i]|=IS_ACTIVE;
    }

    if (!htm) return !0;
    zactive();
    zscore();
    zsuppression();

    ZLOOP(r,htm->regions) sense(&htm->region[r]);
    ZLOOP(r,htm->regions) adjust(&htm->region[r]);

    zpredicted();
    zscore();
    ZRLOOP(r,htm->regions) predict(&htm->region[r]);

    zimagined();
    if (do_generative)
    {
        zscore();
        ZRLOOP(r,htm->regions) imagine1(&htm->region[r]);
        //ZLOOP(r,htm->regions) imagine2(&htm->region[r]);
    }
    cycles++;

    return 0;
}


/********************************************************************************************
 *
 * OpenGL
 *
 */

extern int htm_main(int argc, char **argv)
{
    int gwidth=400,gheight=400;

    float camera[] = { 0,0,0 };
    float center[] = { 0,1,0 };
    float viewup[] = { 0,0,1 };

    int mousestate[6]={0,0,0,0,0,0};
    ivec mousepos={{},0,0,0};
    fvec rot={{},0,60,0};
    fvec trans={{},0,0,-30};
    float zoom=.75;

    Htm htm;
    RegionDesc rd[]= {
        //   size             pos         breadth        depth  ll
        {{{},16,16,1,0}, {{}, -8, -8, -8}, {{}, 0, 4, 8}, {{}, 0, 4, 8}, 0},
        {{{},32,32,1,0}, {{},-16,-16,  8}, {{},16, 8, 0}, {{}, 2, 8, 0}, 1},
        //{{{},16,16,4,0}, {{}, -8, -8, 8}, {{},16, 8, 8}, {{}, 2, 8, 8}, 1},
        //{{{}, 8, 8,4,0}, {{}, -4, -4,16}, {{}, 8, 8, 0}, {{}, 8, 8, 0}, 1}
    };

    Htm_init(&htm,rd,2);


    void display()
    {
        int r,i;

        int Region_display(Region *region)
        {
            Seed seed;
            D3 opos;
            fvec vertex;
            int axis;
            int state;
            float score,suppression;

            void draw_cell(float scale)
            {
                glVertex3f(vertex.v[0]-scale,vertex.v[1]-scale,vertex.v[2]);
                glVertex3f(vertex.v[0]-scale,vertex.v[1]+scale,vertex.v[2]);
                glVertex3f(vertex.v[0]+scale,vertex.v[1]+scale,vertex.v[2]);
                glVertex3f(vertex.v[0]+scale,vertex.v[1]-scale,vertex.v[2]);
            }

            if (!region) return !0;

            glBegin(GL_QUADS);
            ZLOOPD3(opos.v,region->states.size.v)
            {
                (void) DIM3D(opos.v,region->states.size.v);
                ZLOOP(axis,3) vertex.v[axis]=opos.v[axis]+region->states.position.v[axis];
                if (show_cells)
                {
                    int active=region->states.active[opos.vol];
                    int predicted=region->states.predicted[opos.vol];
                    int imagined=region->states.imagined[opos.vol];
                    if (active||predicted||imagined)
                    {
                        glColor4f(active/255.0,predicted/255.0,imagined/255.0,1);
                        draw_cell(.4);
                    }
                }
            }
            glEnd();
        }

        int Region_texture(Region *region,GLuint tid)
        {
            int i;
            Interface *interface;

            if (region) ZLOOP(i,INTERFACES) if (i==INTRA)
            {
                interface=&htm.region[r].interface[i];

                glColor4f(1,1,1,1);
                if (interface &&
                    interface->output && interface->output->size.vol &&
                    interface->input && interface->input->size.vol)
                {
                    int size=interface->output->size.x*interface->depth*interface->output->size.y*interface->depth;
                    GLubyte tex[interface->output->size.x][interface->depth][interface->output->size.y][interface->depth][4];
                    int active,predicted,imagined,perm,score;
                    GLubyte r,g,b,a;
                    float x,y,z,w,h;

                    int synapse_op(D3 *ipos,D3 *opos,int dendrite,int synapse,DendriteMap *map)
                    {
                        Dendrites *dens=&interface->dendrites[opos->vol];
                        Dendrite *den=&dens->dendrite[dendrite];
                        Synapse *syn=&den->synapse[synapse];
                        int ix,iy,offset;

                        if (synapse==0) ix=iy=interface->depth/2;

                        if (CLIP3D(ipos->v,interface->input->size.v))
                        {
                            ix+=map->offset[synapse].x;
                            iy+=map->offset[synapse].y;
                            active=interface->output->active[opos->vol];
                            predicted=interface->output->predicted[opos->vol];
                            imagined=interface->output->imagined[opos->vol];
                            perm=syn->permanence;
                            score=interface->output->score[opos->vol];
                            r=perm<PTHRESH?(PTHRESH-perm-1)*2:0;
                            g=perm>PTHRESH?(perm-PTHRESH)*2:0;
                            b=synapse==0?0xff:0;
                            a=r|g|b?0xff:0;
                            offset=(opos->x+ix)*(opos->y+iy);
                            if (offset>=0 && offset<size)
                            {
                                tex[opos->x][ix][opos->y][iy][0]|=r;
                                tex[opos->x][ix][opos->y][iy][1]|=g;
                                tex[opos->x][ix][opos->y][iy][2]|=b;
                                tex[opos->x][ix][opos->y][iy][3]|=a;
                            }
                        }
                        return 0;
                    }

                    BZERO(tex);
                    Interface_traverse(interface,synapse_op);

                    glPixelStorei(GL_UNPACK_ALIGNMENT, 0);
                    glBindTexture(GL_TEXTURE_2D,tid);

                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,GL_NEAREST_MIPMAP_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_LINEAR);
                    gluBuild2DMipmaps(GL_TEXTURE_2D,
                                      GL_RGBA,
                                      interface->output->size.y*interface->depth,
                                      interface->output->size.x*interface->depth,
                                      GL_RGBA,
                                      GL_UNSIGNED_BYTE,
                                      tex);

                    x=interface->output->position.x-.5;
                    y=interface->output->position.y-.5;
                    z=interface->output->position.z-.1;
                    w=interface->output->size.x;
                    h=interface->output->size.y;

                    glEnable(GL_TEXTURE_2D);
                    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                    glBindTexture(GL_TEXTURE_2D, tid);
                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0, 0.0); glVertex3f(x,y,z);
                    glTexCoord2f(0.0, 1.0); glVertex3f(x+w,y,z);
                    glTexCoord2f(1.0, 1.0); glVertex3f(x+w,y+h,z);
                    glTexCoord2f(1.0, 0.0); glVertex3f(x,y+h,z);
                    glEnd();
                    glDisable(GL_TEXTURE_2D);
                }
            }
        }

        int Interface_display(Interface *interface)
        {
            fvec jitter={{},0,0,0};
            fvec scale={{},0,0,0.05};
            fvec vertex;
            int axis;
            static int show=1;

            int synapse_op(D3 *ipos,D3 *opos,int dendrite,int synapse,DendriteMap *map)
            {
                int active=interface->output->active[opos->vol];
                int predict=interface->output->predicted[opos->vol];
                if (active || predict)
                {
                    int perm=interface->dendrites[opos->vol].dendrite[dendrite].synapse[synapse].permanence;

                    if (synapse==0)
                    {
                        show=(i==FEEDFWD || active || predict);

                        glBegin(GL_LINE_STRIP);
                        ZLOOP(axis,3) vertex.v[axis]=opos->v[axis]+interface->output->position.v[axis];
                        glColor4f(active/255.0,predict/255.0,0,1);
                        if (show && show_risers)
                            glVertex3fv(vertex.v);
                    }

                    if (CLIP3D(ipos->v,interface->input->size.v))
                    {
                        glColor4f(active/255.0,predict/255.0,perm>PTHRESH?((perm-PTHRESH)/(float) PTHRESH):0,perm/255.0);
                        ZLOOP(axis,3) vertex.v[axis]=ipos->v[axis]+(opos->v[axis]+jitter.v[axis])*scale.v[axis]+interface->input->position.v[axis];
                        if (show)
                            glVertex3fv(vertex.v);
                    }

                    if (synapse==interface->depth-1)
                        glEnd();
                }

                return 0;
            }

            if (interface->output && interface->output->size.vol)
            {
                jitter.x = interface->output->position.x;
                scale.x = .5/interface->output->size.x;
                jitter.y = interface->output->position.y;
                scale.y = .5/interface->output->size.y;
                return Interface_traverse(interface,synapse_op);
            }
            else return 0;
        }

        int Scoring_display(Region *region)
        {
            D3 pos;
            float z;

            void vertex()
            {
                (void) DIM3D(pos.v,region->states.size.v);
                z=pos.z+region->states.position.z;
                if (show_scores) z+=region->states.score[pos.vol];
                if (show_suppression) z-=region->states.suppression[pos.vol];
                glVertex3f(region->states.position.x+pos.x,region->states.position.x+pos.y,z);
            }

            ZLOOP(pos.z,region->states.size.z)
            {
                switch(pos.z)
                {
                    default:
                    case 0: glColor4f(.5,.5,.5,1); break;
                    case 1: glColor4f(1,0,0,1); break;
                    case 2: glColor4f(0,1,0,1); break;
                    case 3: glColor4f(0,0,1,1); break;
                }

                ZLOOP(pos.x,region->states.size.x)
                {
                    glBegin(GL_LINE_STRIP);
                    ZLOOP(pos.y,region->states.size.y) vertex();
                    glEnd();
                }
                ZLOOP(pos.y,region->states.size.y)
                {
                    glBegin(GL_LINE_STRIP);
                    ZLOOP(pos.x,region->states.size.x) vertex();
                    glEnd();
                }
            }

            glFlush();
        }

        int DendriteMap_display()
        {
            int d,s,axis;
            ivec p,z={{},0,0,0};
            DendriteMap map;
            int dendrites=DENDRITE_CACHE;

            glColor4f(1,1,1,.01);
            ZLOOP(d,dendrites)
            {
                map=dendrites==DENDRITE_CACHE?DENDRITEMAP(d):DENDRITEMAP(NRAND(gseed)>>12);
                p=z;
                glBegin(GL_LINE_STRIP);
                glVertex3iv(p.v);
                ZLOOP(s,SYNAPSES)
                {
                    ZLOOP(axis,2) p.v[axis]+=map.offset[s].v[axis]; // x/y!
                    p.v[2]=map.offset[s].v[2]%4; // z!
                    glVertex3iv(p.v);
                }
                glEnd();
                glFlush();

            }
        }

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(trans.x,-trans.y,trans.z);
        glRotatef(-rot.y, 1.0, 0.0, 0.0);
        glRotatef(rot.x, 0.0, 0.0, 1.0);
        glScalef(zoom,zoom,zoom);

        if (show_cells) ZLOOP(r,htm.regions) Region_display(&htm.region[r]);
        if (show_scores || show_suppression) ZLOOP(r,htm.regions) Scoring_display(&htm.region[r]);
        if (show_tex) ZLOOP(r,htm.regions) Region_texture(&htm.region[r],1);
        glDepthMask(GL_FALSE);
        if (show_dendrites) ZLOOP(r,htm.regions) ZLOOP(i,INTERFACES) Interface_display(&htm.region[r].interface[i]);
        glDepthMask(GL_TRUE);
        if (show_map) DendriteMap_display();

        glutSwapBuffers();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void reshape(int w,int h)
    {
        float r=((float) w/(float) h);

        gwidth=w;
        gheight=h;
        glViewport(0,0,(GLsizei) w,(GLsizei) h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        if (w>h) glFrustum (zoom*-r,zoom*r,-zoom,zoom,0.5,500.0);
        else     glFrustum (-zoom,zoom,zoom/-r,zoom/r,0.5,500.0);


        glMatrixMode(GL_MODELVIEW);
    }

    void keyboard(unsigned char key,int x,int y)
    {
        switch (key)
        {
            case 'q': case 27: exit(0); // esc
            case 'u': Htm_update(&htm);
            case 'c': show_cells=!show_cells; break;
            case 'd': show_dendrites=!show_dendrites; break;
            case 'm': show_map=!show_map; break;
            case 's': show_scores=!show_scores; break;
            case 'S': show_suppression=!show_suppression; break;
            case 'r': show_risers=!show_risers; break;
            case 'p': show_predictions=!show_predictions; break;
            case 't': show_tex=!show_tex; break;
            case 'h': hide_input=!hide_input; break;
            case 'g': do_generative=!do_generative; break;
            case 'C': show_coords=!show_coords; break;
        }
    }

    void mouse(int button,int state,int x,int y)
    {
        mousestate[button]=!state;
        mousepos.x=x;
        mousepos.y=y;
        if (state==0) switch (button)
        {
            case 3: zoom*=0.9; break;
            case 4: zoom*=1.1; break;
        }
        glutPostRedisplay();
        if (show_coords) printf("t(%f,%f,%f) r(%f,%f,%f) z(%f)\n",trans.x,trans.y,trans.z,rot.x,rot.y,rot.z,zoom);
    }

    void motion(int x,int y)
    {
        if (mousestate[1])
        {
            trans.x+=(x-mousepos.x)*.1;
            trans.y+=(y-mousepos.y)*.1;
        }
        else
        {
            rot.x+=(x-mousepos.x)*.1;
            rot.y-=(y-mousepos.y)*.1;
        }
        mousepos.x=x;
        mousepos.y=y;
        glutPostRedisplay();
    }

    void idle()
    {
        Htm_update(&htm);
        glutPostRedisplay();
    }

    void menuselect(int id)
    {
        switch (id)
        {
            case 0: exit(0); break;
        }
    }

    int menu(void)
    {
        int menu=glutCreateMenu(menuselect);
        glutAddMenuEntry("Exit demo\tEsc",0);
        return menu;

    }

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_ACCUM | GLUT_ALPHA | GLUT_RGBA | GLUT_STENCIL);

    glutInitWindowPosition(100,100);
    glutInitWindowSize(gwidth,gheight);
    glutCreateWindow("HTM");

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    menu();
    glutAttachMenu(GLUT_RIGHT_BUTTON);
    glutIdleFunc(idle);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_POINT_SMOOTH);
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_POLYGON_SMOOTH);
    glShadeModel(GL_SMOOTH);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); // transparency
    //glBlendFunc(GL_SRC_ALPHA_SATURATE,GL_ONE); // back-to-front compositing

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glutSwapBuffers();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glutSwapBuffers();

    reshape(gwidth,gheight);
    glLoadIdentity();
    gluLookAt(camera[0],camera[1],camera[2],
              center[0],center[1],center[2],
              viewup[0],viewup[1],viewup[2]);
    glMatrixMode(GL_MODELVIEW);

    glutMainLoop();

    return 0;
}
