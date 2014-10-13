/*
 (C) Petr Lastovicka

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License.
 */
#include <windows.h>
#pragma hdrstop
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <new>
#include "level.h"
//---------------------------------------------------------------------------
/*

You must add SOLVER to preprocessor definitions in the C++ project settings.
Solver is started by keyboard shortcuts Alt+Num0 - Alt+Num4.
Can be compiled as 32-bit or 64-bit.

Algorithms:
0) depth search (backtracking)
no optimization => very long solutions
needs large stack
simple heuristic - objects near stores are preferred
1) breadth search
finds solution which is optimized for pushes
slow
2) Dijkstra
finds solution which is optimized for moves+pushes
sometimes finds solution which is not optimal (because of duality)
very slow
3) gener
used in level editor to shuffle objects
4) optimizer
requires solution


Memory:
posTable:  (18+Nobj)*maxPos
hashTable: 8*maxPos
movedObj:  Nobj*48*maxPos
*/

//memory usage (MB)
#ifdef _DEBUG
const int	maxMem=100;
#elif defined(_WIN64)
const int	maxMem=3000;
#else
const int	maxMem=1400;
#endif

const unsigned maxPos0=2000000000;//max positions


int distId;   //each call to findObjects will use unique id to distinguish visited squares
Psquare *distBuf1, *distBuf2; //temporal buffers for functions findDist and findObjects
bool solving;

//-------------------------------------------------------------------
void nextDistId()
{
	distId++;
	if(distId<0){ //overflow
		for(Psquare p=board; p<boardk; p++) p->distId=0;
		distId=1;
	}
}
//-------------------------------------------------------------------
//find the shortest path from src to dest (without pushing any object),
//distance from src will be stored into Square.dist where Square.distId==distId
void findDist(Psquare src, Psquare dest)
{
	int i, d;
	Psquare *p1, *p2, pn;

	nextDistId();
	src->distId=distId;
	src->dist=0;
	distBuf1[0]=src;
	distBuf1[1]=0;
	for(d=1;; d++){
		if(d&1) p1=distBuf1, p2=distBuf2;
		else p1=distBuf2, p2=distBuf1;
		if(!*p1) break;
		for(; *p1; p1++){
			for(i=0; i<4; i++){
				pn= nxtP(*p1, i);
				if(pn->distId!=distId){
					if(pn->obj==BM_GROUND){
						pn->distId=distId;
						pn->dist=d;
						*p2++=pn;
					}
					else{
						pn->dist=MAXDIST;
					}
				}
			}
			if(*p1==dest) return;
		}
		*p2=0;
	}
}

//-------------------------------------------------------------------

#if SOLVER

struct Move {
	Psquare obj;  //object position before move
	Psquare next; //object position after move, next player position in dual Dijkstra
	short dist;   //distance from player to obj
	short direct; //pushing direction
};
typedef Move *PMove;

#ifdef _WIN64
typedef unsigned Ppos;
#define pos2ptr(p) (posTable0+(size_t)(p)*(size_t)posSize2)
#define ptr2pos(p) ((Ppos)((((Pchar)p)-posTable0)/posSize2))
#define pos2ptr0(p) ((p) ? pos2ptr(p) : 0)
#else
typedef Pchar Ppos;
#define pos2ptr(p) p
#define ptr2pos(p) p
#define pos2ptr0(p) p
#endif

int
DXY,      //2 if there are more than 255 squares, otherwise 1
	Nobj,     //number of objects
	Nstore,   //number of stores
	Nground,  //number of squares which are not wall
	posSize,  //position length without header (bytes)
	posSize1, //initially equal to posSize, then descreased in function testBlocked
	posSize2, //allocated length in posTable array, negative in function testBlocked
	DHDR,     //position header length
	maxPos,   //max. number of positions in posTable array
	DhashTable, //hash table length
	algorithm,  //0=depth, 1=breadth, 2=Dijkstra, 3=gener
	testing,    //1 in function testBlocked
	finalDone,  //count of objects on stores
	*finalDistA,//array for finalDists
	firstMoveEval; //eval of firstMove string

Ppos
*hashTable, //hash table for positions
	*hashTablek;//end of hashTable

Pchar
UfoundPos,  //pointer to final position if solution has been found
	UfoundPosD,
	posTable,   //all searched positions
	posTablek,
	posTable0,  //posTable+DHDR-posSize2
	posTable1,  //posTable-posSize2
	UposTable,  //current pointer in posTable
	UfoundSol,  //end of solution string
	firstMove,  //moves created by deleting blind lanes
	UfirstMove,
	finalPos;   //final position (without player)

PMove
movedObj;   //moves

Psquare
moverStart,//player start position after delBlind
	*i2p,      //conversion square index to pointer
	*fin2p;    //conversion store index to pointer (heuristic order)


//position header for depth search
struct Hdr2 {
	Pchar parent;
	int eval;
	PMove lastMove;
	short dist;
};
const int DHDR2= offsetof(Hdr2, dist)+sizeof(short);

//position header for breadth search
struct Hdr3 {
	Pchar parent;
	short mov, pus;
	PMove movesBeg, movesEnd;
	bool dual;
};
const int DHDR3= offsetof(Hdr3, dual)+sizeof(bool);

//position header for Dijkstra algorithm
struct Hdr1 {
	Pchar parent;
	int eval;
	int heapItem;
	PMove lastMove;
	bool dual;
};
const int DHDR1= offsetof(Hdr1, dual)+sizeof(bool);

//position header for optimizer algorithm
struct Hdr5 {
	Ppos parent;
	int eval;
	int heapItem;
	bool inSolution;
};
const int DHDR5= offsetof(Hdr5, inSolution)+sizeof(bool);

//position header for level generator
struct Hdr4 {
	short mov, pus;
	PMove movesBeg, movesEnd;
};
const int DHDR4= sizeof(Hdr4);

typedef Hdr1 *PHdr1;
typedef Hdr2 *PHdr2;
typedef Hdr3 *PHdr3;
typedef Hdr4 *PHdr4;
typedef Hdr5 *PHdr5;
#define HDR1(p) ((Hdr1*)(p-DHDR1))
#define HDR2(p) ((Hdr2*)(p-DHDR2))
#define HDR3(p) ((Hdr3*)(p-DHDR3))
#define HDR4(p) ((Hdr4*)(p-DHDR4))
#define HDR5(p) ((Hdr5*)(p-DHDR5))

#ifdef _WIN64
#define pos2hdr5(p) ((PHdr5)(posTable1+(size_t)(p)*(size_t)posSize2))
#else
#define pos2hdr5(p) HDR5(p)
#endif


Pchar backtrack(PMove Umoves, int movpus);

//-------------------------------------------------------------------
inline void wrXY0(Pchar p, int i)
{
	if(DXY==1) *((BYTE*)p) = (BYTE)i;
	else *((WORD*)p) = (WORD)i;
}

inline void wrXY(Pchar &p, int i)
{
	if(DXY==1) *((BYTE*)p) = (BYTE)i, p++;
	else *((WORD*)p) = (WORD)i, p+=2;
}

inline int rdXY(void *p)
{
	if(DXY==1) return *((BYTE*)p);
	else return *((WORD*)p);
}

void noMem()
{
	msg(lng(768, "Not enough memory"));
}
//-------------------------------------------------------------------
#ifndef NDEBUG
int testPos(Pchar prevPos)
{
	Pchar v;
	int i;           

	//objects must be placed on board
	for(v=prevPos+DXY; v<prevPos+posSize1; v+=DXY){
		i=rdXY(v);
		assert(i<Nground);
		if(i2p[i]->obj!=BM_OBJECT) return 0;
	}
	//objects must be sorted by index
	for(v=prevPos+2*DXY; v<prevPos+posSize1; v+=DXY){
		if(rdXY(v)<=rdXY(v-DXY)) return 0;
	}
	return 1;
}
#endif
//-------------------------------------------------------------------
#define finOrWall(p) ((p)->obj!=BM_WALL && !(p)->store)

int testDead(Psquare p)
{
	Psquare pU, pD, pU2, pD2;

	pU=nxtP(p, 2); pU2=nxtP(pU, 2);
	pD=nxtP(p, 3); pD2=nxtP(pD, 3);
	if((p+1)->obj!=BM_GROUND){
		if(pD->obj!=BM_GROUND){
			if((pD+1)->obj!=BM_GROUND)
				if(!p->store || finOrWall(p+1) || finOrWall(pD) || finOrWall(pD+1)) return 1;
			if((pD+2)->obj!=BM_GROUND && (pD2+1)->obj!=BM_GROUND && (pD2+2)->obj!=BM_GROUND)
				if(!(pD+1)->store && !p->store) return 1;
		}
		else{
			if((pD2)->obj!=BM_GROUND){
				if((pD-1)->obj!=BM_GROUND &&
					(pD+1)->obj!=BM_GROUND && (pD2-1)->obj!=BM_GROUND
					|| (pD2+1)->obj!=BM_GROUND && (pD-1)->obj==BM_WALL &&
					(pD+2)->obj==BM_WALL && !(pD+1)->store)
					if(!(pD)->store && !p->store) return 1;
			}
		}
		if(pU->obj!=BM_GROUND){
			if((pU+1)->obj!=BM_GROUND)
				if(!p->store || finOrWall(p+1) || finOrWall(pU) || finOrWall(pU+1)) return 1;
			if((pU+2)->obj!=BM_GROUND && (pU2+1)->obj!=BM_GROUND && (pU2+2)->obj!=BM_GROUND)
				if(!(pU+1)->store && !p->store) return 1;
		}
		else{
			if((pU2)->obj!=BM_GROUND){
				if((pU+1)->obj!=BM_GROUND &&
					(pU-1)->obj!=BM_GROUND && (pU2-1)->obj!=BM_GROUND
					|| (pU2+1)->obj!=BM_GROUND && (pU-1)->obj==BM_WALL &&
					(pU+2)->obj==BM_WALL && !(pU+1)->store)
					if(!(pU)->store && !p->store) return 1;
			}
		}
	}
	else{
		if((p+2)->obj!=BM_GROUND){
			if(((pD)->obj!=BM_GROUND && (pD+2)->obj!=BM_GROUND &&
				(pU+1)->obj==BM_WALL && (pD2+1)->obj==BM_WALL && !(pD+1)->store
				|| (pU)->obj!=BM_GROUND && (pU+2)->obj!=BM_GROUND &&
				(pD+1)->obj==BM_WALL && (pU2+1)->obj==BM_WALL && !(pU+1)->store)
				|| (pU+1)->obj!=BM_GROUND && (pD+1)->obj!=BM_GROUND &&
				((pU)->obj!=BM_GROUND && (pD+2)->obj!=BM_GROUND ||
				(pD)->obj!=BM_GROUND && (pU+2)->obj!=BM_GROUND))
				if(!(p+1)->store && !p->store) return 1;
		}
	}
	if((p-1)->obj!=BM_GROUND){
		if(pD->obj!=BM_GROUND){
			if((pD-1)->obj!=BM_GROUND)
				if(!p->store || finOrWall(p-1) || finOrWall(pD) || finOrWall(pD-1)) return 1;
			if((pD-2)->obj!=BM_GROUND && (pD2-1)->obj!=BM_GROUND && (pD2-2)->obj!=BM_GROUND)
				if(!(pD-1)->store && !p->store) return 1;
		}
		else{
			if((pD2)->obj!=BM_GROUND){
				if((pD+1)->obj!=BM_GROUND &&
					(pD-1)->obj!=BM_GROUND && (pD2+1)->obj!=BM_GROUND
					|| (pD2-1)->obj!=BM_GROUND && (pD+1)->obj==BM_WALL &&
					(pD-2)->obj==BM_WALL && !(pD-1)->store)
					if(!(pD)->store && !p->store) return 1;
			}
		}
		if(pU->obj!=BM_GROUND){
			if((pU-1)->obj!=BM_GROUND)
				if(!p->store || finOrWall(p-1) || finOrWall(pU) || finOrWall(pU-1)) return 1;
			if((pU-2)->obj!=BM_GROUND && (pU2-1)->obj!=BM_GROUND && (pU2-2)->obj!=BM_GROUND)
				if(!(pU-1)->store && !p->store) return 1;
		}
		else{
			if((pU2)->obj!=BM_GROUND){
				if((pU+1)->obj!=BM_GROUND &&
					(pU-1)->obj!=BM_GROUND && (pU2+1)->obj!=BM_GROUND
					|| (pU2-1)->obj!=BM_GROUND && (pU+1)->obj==BM_WALL &&
					(pU-2)->obj==BM_WALL && !(pU-1)->store)
					if(!(pU)->store && !p->store) return 1;
			}
		}
	}
	else{
		if((p-2)->obj!=BM_GROUND){
			if(((pD)->obj!=BM_GROUND && (pD-2)->obj!=BM_GROUND &&
				(pU-1)->obj==BM_WALL && (pD2-1)->obj==BM_WALL && !(pD-1)->store
				|| (pU)->obj!=BM_GROUND && (pU-2)->obj!=BM_GROUND &&
				(pD-1)->obj==BM_WALL && (pU2-1)->obj==BM_WALL && !(pU-1)->store)
				|| (pU-1)->obj!=BM_GROUND && (pD-1)->obj!=BM_GROUND &&
				((pU)->obj!=BM_GROUND && (pD-2)->obj!=BM_GROUND ||
				(pD)->obj!=BM_GROUND && (pU-2)->obj!=BM_GROUND))
				if(!(p-1)->store && !p->store) return 1;
		}
	}
	return 0;
}
//-------------------------------------------------------------------
//find all objects reachable by player which can be pushed, write moves to array
PMove findObjects(PMove Umoves)
{
	Psquare pn, pnn, *p1, *p2;
	int i, d;

	nextDistId();
	mover->distId=distId;
	mover->dist=0;
	distBuf1[0]=mover;
	distBuf1[1]=0;
	for(d=1;; d++){
		if(d&1) p1=distBuf1, p2=distBuf2; //swap
		else p1=distBuf2, p2=distBuf1;
		if(!*p1) break; //empty buffer => exit
		for(; *p1; p1++){
			for(i=0; i<4; i++){
				pn= nxtP(*p1, i);
				if(pn->distId!=distId && pn->obj==BM_GROUND){
					//add new square to secondary buffer
					pn->distId=distId;
					pn->dist=d;
					*p2++=pn;
				}
				else if(pn->obj==BM_OBJECT){
					pnn= nxtP(pn, i);
					if(pnn->obj==BM_GROUND && pnn->finalDist<MAXDIST){
						Umoves->obj=pn;
						Umoves->next=pnn;
						Umoves->dist=(short)d;
						Umoves->direct=(short)i;
						Umoves++;
					}
				}
			}
		}
		*p2=0;
	}
	return Umoves;
}
//-------------------------------------------------------------------
//find all objects reachable by player which can be pulled, write moves to array
PMove findObjectsR(PMove Umoves)
{
	Psquare pn, pnn, *p1, *p2;
	int i, d;

	nextDistId();
	mover->distId=distId;
	mover->dist=0;
	distBuf1[0]=mover;
	distBuf1[1]=0;
	for(d=1;; d++){
		if(d&1) p1=distBuf1, p2=distBuf2;
		else p1=distBuf2, p2=distBuf1;
		if(!*p1) break;
		for(; *p1; p1++){
			for(i=0; i<4; i++){
				pn= nxtP(*p1, i);
				if(pn->distId!=distId && pn->obj==BM_GROUND){
					pn->distId=distId;
					pn->dist=d;
					*p2++=pn;
				}
				else if(pn->obj==BM_OBJECT){
					pnn= prvP(*p1, i);
					if(pnn->obj==BM_GROUND){
						Umoves->obj=pn;
						Umoves->next=*p1;
						Umoves->dist=(short)d;
						Umoves->direct=(short)i;
						Umoves++;
					}
				}
			}
		}
		*p2=0;
	}
	return Umoves;
}
//-------------------------------------------------------------------
//find all positions reachable by player where any object can be pulled, write moves to array
PMove findObjectsD(PMove Umoves, Psquare o, int dir)
{
	Psquare pn, pnn, *p1, *p2;
	int i, d;
	bool found;

	nextDistId();
	mover->distId=distId;
	mover->dist=0;
	distBuf1[0]=mover;
	distBuf1[1]=0;
	for(d=1;; d++){
		if(d&1) p1=distBuf1, p2=distBuf2;
		else p1=distBuf2, p2=distBuf1;
		if(!*p1) break;
		for(; *p1; p1++){
			found=false;
			for(i=0; i<4; i++){
				pn= nxtP(*p1, i);
				if(pn->distId!=distId && pn->obj==BM_GROUND){
					pn->distId=distId;
					pn->dist=d;
					*p2++=pn;
				}
				else if(pn->obj==BM_OBJECT){
					pnn= prvP(*p1, i);
					if(pnn->obj==BM_GROUND){
						found=true;
					}
				}
			}
			if(found){
				Umoves->next=*p1;
				Umoves->dist=(short)d;
				Umoves->obj=o;
				Umoves->direct=(short)dir;
				Umoves++;
			}
		}
		*p2=0;
	}
	return Umoves;
}
//-------------------------------------------------------------------
//write new position (without header)
void makeMove0(Pchar newPos, Pchar prevPos, Move &curMove)
{
	Pchar v, v1;

	memcpy(newPos, prevPos, posSize);
	//find last pushed object
	for(v=newPos+DXY; rdXY(v)!=curMove.obj->i; v+=DXY);
	//sort objects
	if(curMove.next->i < curMove.obj->i){ //push left or up
		for(v1=v-DXY; rdXY(v1)>curMove.next->i && v1>newPos; v1-=DXY){
			wrXY0(v1+DXY, rdXY(v1));
		}
		wrXY0(v1+DXY, curMove.next->i);
	}
	else{ //push right or down
		for(v1=v+DXY; v1<newPos+posSize1 && rdXY(v1)<curMove.next->i; v1+=DXY){
			wrXY0(v1-DXY, rdXY(v1));
		}
		wrXY0(v1-DXY, curMove.next->i);
	}
	//player position
	wrXY0(newPos, curMove.obj->i);
}

void makeMove(Pchar newPos, Pchar prevPos, Move &curMove)
{
	makeMove0(newPos, prevPos, curMove);
	assert(testPos(newPos));
}
//-------------------------------------------------------------------
//calculate hash value of position
Ppos *hash(Pchar pos)
{
	DWORD h;
	Pchar posEnd;

	h=0;
	posEnd= pos+posSize1;
	for(; pos<posEnd; pos++){
		h= (h + (DWORD)*(Puchar)pos)*1234567;
		//   h= h = (h << 5) + h + *(Puchar)pos;
	}
	return &hashTable[h%DhashTable];
}
//-------------------------------------------------------------------
//findObjects must be called before this function
int testBlocked(Pchar prevPos, PMove lastMove, PMove Umoves)
{
	Psquare p, p0, pn, *p1, *p2;
	Pchar curPos, v1, v2, found, w;
	int i, d, posSize0, result, finalDone0;

	if(!lastMove) return 0;
	//look around last move
	p0=lastMove->next;
	p1=distBuf1;
	for(i=0; i<8; i++){
		p=nxtP(p0, i);
		if(p->obj==BM_GROUND && p->distId!=distId){
			*p1++=p;
			p->distId=distId+1;
		}
	}
	if(p1-distBuf1==0) return 0;
	if(UposTable==posTablek) return 0;
	*p1=0;
	//find objects adjacent to unreachable region
	nextDistId();
	for(d=1;; d++){
		if(d&1) p1=distBuf1, p2=distBuf2;
		else p1=distBuf2, p2=distBuf1;
		if(!*p1) break;
		for(; *p1; p1++){
			for(i=0; i<8; i++){
				pn= nxtP(*p1, i);
				if(pn->distId!=distId){
					if(pn->obj==BM_OBJECT){
						pn->distId=distId;
					}
					else if(pn->obj==BM_GROUND && i<4){
						pn->distId=distId;
						*p2++=pn;
					}
				}
			}
		}
		*p2=0;
	}
	//tested positions will be written at end of posTable array
	if(!testing){
		w=posTablek;
		posTablek=UposTable-posSize2;
		UposTable= w-posSize2;
		posSize2= -posSize2;
	}
	//create new position which has less objects
	curPos= UposTable;
	v2=curPos+DXY;
	for(v1=prevPos+DXY; v1<prevPos+posSize1; v1+=DXY){
		p=i2p[rdXY(v1)];
		if(p->distId==distId){
			wrXY(v2, p->i);
		}
		else{
			p->obj=BM_GROUND;
#ifdef DEBUG
			paintSquare(p);
#endif
		}
	}
	//decrease posSize1
	posSize0=posSize1;
	posSize1=(int)(v2-curPos);
	result=0;
	if(posSize1<posSize0){
		if(posSize1>2*DXY){
			while(v2!=curPos+posSize){
				wrXY(v2, -1);
			}
			//backtracking
			HDR2(curPos)->parent=0;
			HDR2(curPos)->eval=0;
			HDR2(curPos)->lastMove=0;
			finalDone0=finalDone;
			testing++;
#ifdef DEBUG
			repaint();
			Sleep(10);
#endif
			found= backtrack(Umoves, 0);
			if(found){
				HDR2(found)->parent=curPos;
			}
			else{
				//position is unsolvable or too difficult
				result=1;
			}
			testing--;
			finalDone=finalDone0;
		}
		//put back all objects
		posSize1=posSize0;
		for(v1=prevPos+DXY; v1<prevPos+posSize1; v1+=DXY){
			assert(rdXY(v1)>=0 && rdXY(v1)<width*height);
			i2p[rdXY(v1)]->obj=BM_OBJECT;
#ifdef DEBUG
			paintSquare(i2p[rdXY(v1)]);
#endif
		}
	}
	if(!testing){
		posSize2= -posSize2;
		w=posTablek;
		posTablek=UposTable+posSize2;
		UposTable= w+posSize2;
	}
	return result;
}
//-------------------------------------------------------------------
/*

example of cont when direct==1 (push right)
. X  X  X  X  X  X  X  X
-1 0  1  1  2  0  1  1  X
. X  X  X  .  X  X  X  X

example of reverse cont when direct==1 (pull left)
. X  X  X  X  X  X  X  X
-1 0  1  0 -1  0  1  ?  X
. X  X  X  .  X  X  X  X

*/

//push object through corridor
PMove testLastMove(PMove lastMove, PMove Umoves)
{
	if(lastMove && lastMove->next->cont[lastMove->direct]>0){
		Psquare p=nxtP(lastMove->next, lastMove->direct);
		if(p->obj==BM_GROUND && p->finalDist<MAXDIST){
			//pushing direction is same as previous move
			Umoves->obj= lastMove->next;
			Umoves->next= p;
			Umoves->dist=1;
			Umoves->direct= lastMove->direct;
			Umoves++;
		}
		return Umoves;
	}
	return 0;
}

//pull object through corridor
PMove testLastMoveR(PMove lastMove, PMove Umoves)
{
	if(lastMove && lastMove->next->cont[lastMove->direct^1]==1){
		Psquare p=prvP(lastMove->next, lastMove->direct);
		Psquare pn=prvP(p, lastMove->direct);
		assert(p->obj==BM_GROUND);
		assert(lastMove->obj->obj==BM_GROUND);
		assert(lastMove->next->obj==BM_OBJECT);
		if(pn->obj==BM_GROUND){
			//pull object, direction is same as previous move
			Umoves->obj= lastMove->next;
			Umoves->next= p;
			Umoves->dist=1;
			Umoves->direct= lastMove->direct;
			Umoves++;
		}
		return Umoves;
	}
	return 0;
}

//pull object through corridor (used in Dijkstra)
void testLastMoveD(PMove lastMove, PMove Umoves, PMove UmovesEnd)
{
	if(!lastMove) return;
	Psquare pm= prvP(lastMove->obj, lastMove->direct);
	if(pm->cont[lastMove->direct^1]==1){
#ifndef NDEBUG
		Psquare p=prvP(pm, lastMove->direct);
#endif
		assert(p->obj==BM_GROUND);
		assert(lastMove->obj->obj==BM_GROUND);
		assert(pm->obj==BM_OBJECT);
		for(PMove m=Umoves; m<UmovesEnd; m++){
			if(m->obj!=pm || m->direct!=lastMove->direct) m->direct=-1;
		}
	}
}
//-------------------------------------------------------------------
Pchar backtrack(PMove Umoves, int movpus)
{
	Pchar v, newPos, prevPos, found, result=0;
	Ppos *u;
	int m, o, i, f1, f2, movpus1, finalDone0, direct0;
	PMove UmovesEnd, um, bm;
	Psquare mover0, *newMover, p, pn;
	Move curMove;

	prevPos=UposTable;
	mover0=mover;
	finalDone0=finalDone;
	//find reachable objects
	UmovesEnd= findObjects(Umoves);
	//place player into top left corner
	for(newMover=i2p; (*newMover)->distId!=distId; newMover++);
	//write player position
	wrXY0(prevPos, (*newMover)->i);
	//exit testBlocked when all objects are reachable from all directions
	if(testing){
		for(v=prevPos+posSize1-DXY; v>prevPos; v-=DXY){
			p=i2p[rdXY(v)];
			if(!p->store){
				for(i=0; i<4; i++){
					pn=nxtP(p, i);
					if(pn->obj==BM_GROUND && pn->distId!=distId) goto l1;
				}
			}
		}
		return prevPos;
	}
l1:
	//calculate hash function
	u= hash(prevPos);
	//write new position into posTable array
	for(; *u;){
		newPos=pos2ptr(*u);
		if(!memcmp(newPos, prevPos, posSize)){
			if(testing && HDR2(newPos)->parent){
				//position is solvable
				return newPos;
			}
			//position already exists
			return 0;
		}
		u++;
		if(u==hashTablek) u=hashTable;
	}
	*u=ptr2pos(prevPos);
	UposTable+=posSize2;
	//push object through corridor
	um=testLastMove(HDR2(prevPos)->lastMove, Umoves);
	if(um) UmovesEnd=um;
	//test whether last move blocked other objects
	if(testBlocked(prevPos, HDR2(prevPos)->lastMove, UmovesEnd)) return 0;
	//try all possible moves
	for(; Umoves<UmovesEnd; Umoves++){
		newPos=UposTable;
		if(newPos==posTablek) break; //memory is full
		bm=Umoves;
		if(0+testing){
			//choose object which can be pushed towards blocked region
			m=-1;
			for(um=Umoves; um<UmovesEnd; um++){
				for(i=0; i<4; i++){
					p=nxtP(um->obj, i);
					if(p->obj==BM_GROUND && distId - p->distId > m){
						m = distId - p->distId;
						bm=um;
					}
				}
			}
		}
		else{
			//choose object which is near store
			m=0x7fffffff;
			for(um=Umoves; um<UmovesEnd; um++){
				f1= um->obj->finalDists[finalDone];
				f2= um->next->finalDists[finalDone];
				o= (f2<<2) + ((f2-f1)<<5) + um->dist;
				if(um->obj->direct == (um->direct^1)) o+=600;
				if(o<m){
					m=o;
					bm=um;
				}
			}
		}
		curMove=*bm;
		*bm=*Umoves;
		movpus1= movpus+curMove.dist+1;
		//make one push
		curMove.next->obj= BM_OBJECT;
		curMove.obj->obj= BM_GROUND;
		if(testDead(curMove.next)){
			curMove.next->obj= BM_GROUND;
			curMove.obj->obj= BM_OBJECT;
		}
		else{
#ifdef DEBUG
			mover=0;
			paintSquare(curMove.next);
			paintSquare(curMove.obj);
			Sleep(20);
#endif
			mover=curMove.obj;
			direct0= curMove.next->direct;
			curMove.next->direct= curMove.direct;
			//create new position
			HDR2(newPos)->parent=0;
			HDR2(newPos)->eval= movpus1;
			HDR2(newPos)->lastMove= &curMove;
			makeMove(newPos, prevPos, curMove);
			//compare new position and final position
			amax(finalDone, curMove.obj->finalDist);
			if(curMove.next->finalDist==finalDone){
				do{
					finalDone++;
				} while(fin2p[finalDone]->obj==BM_OBJECT);
			}
			if(!testing && finalDone==Nstore){
				UfoundPos= newPos;
				HDR2(newPos)->eval=movpus1;
				HDR2(newPos)->parent=prevPos;
				UposTable+=posSize2;
				result=prevPos;
			}
			else{
				//backtracking
				found=backtrack(UmovesEnd, movpus1);
				if(found){
					HDR2(found)->parent= prevPos;
					assert(testing || HDR2(found)->eval==movpus1);
					HDR2(found)->dist= (short)(curMove.dist+1);
					result=prevPos;
				}
			}
			//revert all changes
			curMove.next->obj= BM_GROUND;
			curMove.obj->obj= BM_OBJECT;
#ifdef DEBUG
			mover=0;
			paintSquare(curMove.next);
			paintSquare(curMove.obj);
#endif
			curMove.next->direct= direct0;
			mover=mover0;
			finalDone=finalDone0;
			if(result) break;
		}
	}
	return result;
}
//-------------------------------------------------------------------
void depthSearch()
{
	int m=mover->i;
	//prepare start position
	PHdr2 hdr= HDR2(UposTable);
	hdr->parent=0;
	hdr->eval=0;
	hdr->lastMove=0;
	while(fin2p[finalDone]->obj==BM_OBJECT)  finalDone++;
	//run backtracking
	backtrack(movedObj, 0);
	//restore initial position
	wrXY0(posTable+DHDR2, m);
}
//-------------------------------------------------------------------
void breadthSearch()
{
	Pchar v, curPos, newPos, prevPos;
	Ppos *u;
	char *movedObjBeg;
	PHdr3 curPosHdr, newPosHdr, found;
	PMove Umoves, Umoves1, um;
	Psquare p, pn, *pp;
	int i, j;
	bool isDual;

	//initial position
	curPos=UposTable;
	curPosHdr=HDR3(curPos);
	curPosHdr->parent=0;
	curPosHdr->mov=curPosHdr->pus=0;
	curPosHdr->movesBeg= movedObj;
	curPosHdr->movesEnd= Umoves= findObjects(movedObj);
	curPosHdr->dual= false;
	UposTable+=posSize2;


	///
	/*
	memcpy(posTable+posSize2,posTable,posSize2);
	findDist(i2p[rdXY(posTable+DHDR)],0);
	for(pp=i2p; (*pp)->distId!=distId; pp++) ;
	wrXY0(UposTable,(*pp)->i);
	newPosHdr=HDR3(UposTable);
	newPosHdr->mov=(short)(*pp)->dist;
	u=hash(UposTable);
	assert(!*u);
	*u=UposTable;
	UposTable+=posSize2;
	*/


	//remove all objects
	for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
		assert(i2p[rdXY(v)]->obj==BM_OBJECT);
		i2p[rdXY(v)]->obj=BM_GROUND;
	}
	//final position
	for(i=0; i<Nstore; i++){
		assert(fin2p[i]->obj==BM_GROUND);
		fin2p[i]->obj=BM_OBJECT;
	}

	///curPos=UposTable;


	//multiple final positions according to player position
	for(p=board; p<boardk; p++){
		p->distId=0;
	}
	for(j=0; j<Nstore; j++){
		p=fin2p[j];
		for(i=0; i<4; i++){
			pn=nxtP(p, i);
			if(!pn->distId && pn->obj==BM_GROUND){
				findDist(pn, 0);
				for(pp=i2p; (*pp)->distId!=distId; pp++);
				memcpy(UposTable+DXY, finalPos, posSize1-DXY);
				wrXY0(UposTable, (*pp)->i);
				UposTable+=posSize2;
			}
		}
	}
	for(v=curPos+posSize2; v<UposTable; v+=posSize2){
		newPosHdr=HDR3(v);
		newPosHdr->parent=0;
		newPosHdr->mov=curPosHdr->pus=0;
		newPosHdr->dual= true;
		newPosHdr->movesBeg= Umoves;
		mover=i2p[rdXY(v)];
		newPosHdr->movesEnd= findObjectsR(Umoves);
		///
		for(um=Umoves; um<newPosHdr->movesEnd; um++)  um->dist=1;

		Umoves= newPosHdr->movesEnd;
	}
	//remove all objects
	for(i=0; i<Nstore; i++){
		assert(fin2p[i]->obj==BM_OBJECT);
		fin2p[i]->obj=BM_GROUND;
	}
	movedObjBeg=(char*)movedObj+65536;

	//main loop for searched positions
	for(; curPos<UposTable; curPos+=posSize2){
		curPosHdr= HDR3(curPos);
		isDual= curPosHdr->dual;
		//create current position
		for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
			assert(i2p[rdXY(v)]->obj==BM_GROUND);
			i2p[rdXY(v)]->obj=BM_OBJECT;
		}

		//release memory of already processed moves
		char *m= (char*)((INT_PTR)curPosHdr->movesBeg & (INT_PTR)-65536);
		INT_PTR i= m-movedObjBeg;
		if(i>5000000){
			VirtualFree(movedObjBeg, i, MEM_DECOMMIT);
			movedObjBeg=m;
		}

		//try all possible pushes from current position
		for(Umoves1=curPosHdr->movesBeg; Umoves1<curPosHdr->movesEnd && Umoves1->direct>=0; Umoves1++){
			Move &curMove= *Umoves1;
			newPos=UposTable;
			if(newPos==posTablek) break;
			newPosHdr=HDR3(newPos);
			//make move
			assert(testPos(curPos));
			assert(curMove.next->obj==BM_GROUND);
			assert(curMove.obj->obj==BM_OBJECT);
			curMove.next->obj= BM_OBJECT;
			curMove.obj->obj= BM_GROUND;
			if(isDual || !testDead(curMove.next)){
				//create new position
				mover= isDual ? prvP(curMove.next, curMove.direct) : curMove.obj;
				makeMove(newPos, curPos, curMove);
				newPosHdr->mov= (short)(curPosHdr->mov + curMove.dist);
				newPosHdr->pus= (short)(curPosHdr->pus + 1);
				newPosHdr->dual= isDual;
				//find all possible pushes from new position
				newPosHdr->movesEnd= isDual ? findObjectsR(Umoves) : findObjects(Umoves);
				//place player into top left corner
				for(pp=i2p; (*pp)->distId!=distId; pp++);
				wrXY0(newPos, (*pp)->i);
				//calculate hash function
				u=hash(newPos);
				for(;;){
					if(!*u){
						//write new position to posTable array
						*u=ptr2pos(newPos);
						UposTable+=posSize2;
						newPosHdr->parent= curPos;
						newPosHdr->movesBeg= Umoves;
						if(!isDual){ ///
							um= (isDual ? testLastMoveR : testLastMove)(&curMove, Umoves);
							if(um) um->direct=-1;
						}
						if(!isDual && testBlocked(newPos, &curMove, newPosHdr->movesEnd)){///
							//position does not have any moves
							newPosHdr->movesEnd= Umoves;
						}
						else{
							Umoves= newPosHdr->movesEnd;
						}
#ifdef DEBUG
						repaint();
						Sleep(1);
#endif
						break;
					}
					prevPos=pos2ptr(*u);
					if(!memcmp(prevPos, newPos, posSize)){
						//position already exists
						found=HDR3(prevPos);
						if(found->dual!=isDual){
							newPosHdr->parent= curPos;
							if(isDual){
								UfoundPos=prevPos;
								UfoundPosD=newPos;
							}
							else{
								UfoundPosD=prevPos;
								UfoundPos=newPos;
							}
							goto lend;
						}
						if(found->pus == newPosHdr->pus &&
							found->mov > newPosHdr->mov){
							//found better solution
							found->mov= newPosHdr->mov;
							found->parent= curPos;
							if(found->movesEnd > found->movesBeg){
								assert(newPosHdr->movesEnd-Umoves==found->movesEnd-found->movesBeg);
								memcpy(found->movesBeg, Umoves, (char*)newPosHdr->movesEnd-(char*)Umoves);
							}
						}
						break;
					}
					u++;
					if(u==hashTablek) u=hashTable;
				}
			}
			//revert move
			curMove.next->obj= BM_GROUND;
			curMove.obj->obj= BM_OBJECT;
		}
		//remove all objects
		for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
			assert(i2p[rdXY(v)]->obj==BM_OBJECT);
			i2p[rdXY(v)]->obj=BM_GROUND;
		}
		if(UposTable==posTablek) break; //memory is full
	}

lend:;
#if !defined(NDEBUG)
	VirtualAlloc(movedObj,(char*)movedObjBeg-(char*)movedObj,MEM_COMMIT,PAGE_READWRITE);
#endif
}
//-------------------------------------------------------------------
void addToHash(Pchar pos)
{
	Ppos *u;

	u=hash(pos);
	while(*u){
		u++;
		if(u==hashTablek) u=hashTable;
	}
	*u=ptr2pos(pos);
}
//-------------------------------------------------------------------
void dijkstra()
{
	int i, j, heapSize, foundEval, movpus1, x;
	Pchar v, curPos, newPos, prevPos;
	Ppos *u;
	PHdr1 curPosHdr, newPosHdr, *heap, h;
	PMove Umoves, UmovesEnd, um;
	Psquare p, pn, pr, pnn;
	bool isDual;

	foundEval=0x7fffffff;
	//allocate array for heap, heap is a tree, root is at index 1
	//root is pointer to position which has minimal evaluation
	heap= new PHdr1[maxPos];
	//write start position header
	curPos=UposTable;
	curPosHdr= HDR1(curPos);
	heap[curPosHdr->heapItem=heapSize=1]= curPosHdr;
	curPosHdr->parent=0;
	curPosHdr->eval=0;
	curPosHdr->lastMove=0;
	curPosHdr->dual=false;
	addToHash(UposTable);
	UposTable+=posSize2;
	//remove all objects
	for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
		assert(i2p[rdXY(v)]->obj==BM_OBJECT);
		i2p[rdXY(v)]->obj=BM_GROUND;
	}
	//multiple final positions according to player position
	for(j=0; j<Nstore; j++){
		p=fin2p[j];
		for(i=0; i<4; i++){
			pn=nxtP(p, i);
			if(pn->obj==BM_GROUND && !pn->store){
				pnn=nxtP(pn, i);
				if(pnn->obj==BM_GROUND && !pnn->store){
					memcpy(UposTable+DXY, finalPos, posSize1-DXY);
					wrXY0(UposTable, pn->i);
					h= HDR1(UposTable);
					heap[h->heapItem=++heapSize]=h;
					h->parent=0;
					h->eval=0;
					h->lastMove=0;
					h->dual=true;
					addToHash(UposTable);
					UposTable+=posSize2;
				}
			}
		}
	}

	Umoves=movedObj;
	//main loop for searched positions
	for(; heapSize;){
		//take top item from heap - position with lowest evaluation
		curPosHdr= heap[1];
		curPosHdr->heapItem=0;
		curPos= ((Pchar)curPosHdr)+DHDR1;
		isDual= curPosHdr->dual;
		//remove top of heap and sort descendants
		x=heap[heapSize]->eval;
		j=1;
		i=2;
		while(i<heapSize){
			if(i+1 < heapSize && heap[i]->eval > heap[i+1]->eval) i++;
			if(x <= heap[i]->eval) break;
			heap[j]=heap[i];
			heap[j]->heapItem=j;
			j=i;
			i*=2;
		}
		heap[j]=heap[heapSize];
		heap[j]->heapItem=j;
		heapSize--;
		//create current position
		mover= i2p[rdXY(curPos)];
		for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
			assert(i2p[rdXY(v)]->obj==BM_GROUND);
			i2p[rdXY(v)]->obj=BM_OBJECT;
		}
		if(curPos==UfoundPos || curPos==UfoundPosD) goto end;
		//find all possible moves from current position and write them to Umoves
		if(!isDual){
			UmovesEnd=findObjects(Umoves);
		}
		else{
			//Can't call findObjectsR here because it finds positions where player is BEFORE push.
			//We need to find positions where player is AFTER push.
			UmovesEnd=Umoves;
			p=mover;
			for(i=0; i<4; i++){
				pn=nxtP(p, i);
				if(pn->obj==BM_OBJECT){
					pr=prvP(p, i);
					if(pr->obj==BM_GROUND){
						mover=pr;
						p->obj=BM_OBJECT;
						pn->obj=BM_GROUND;
						UmovesEnd=findObjectsD(UmovesEnd, pn, i);
						pn->obj=BM_OBJECT;
						p->obj=BM_GROUND;
					}
				}
			}
			mover=p;
		}
		if(!isDual){
			um= testLastMove(curPosHdr->lastMove, Umoves);
			if(um) UmovesEnd=um;
		}
		else{
			testLastMoveD(curPosHdr->lastMove, Umoves, UmovesEnd);
		}
		if(isDual || !testBlocked(curPos, curPosHdr->lastMove, UmovesEnd)){///
			//try all possible moves from current position
			for(; Umoves<UmovesEnd; Umoves++){
				newPos=UposTable;
				if(newPos==posTablek) break; //memory full
				newPosHdr=HDR1(newPos);
				assert(testPos(curPos));
				//make move
				Move &curMove= *Umoves;
				if(curMove.direct<0) continue;
				assert(curMove.obj->obj==BM_OBJECT);
				curMove.obj->obj= BM_GROUND;
				mover= curMove.obj;
				if(isDual){
					mover= curMove.next;
					curMove.next= prvP(curMove.obj, curMove.direct);
				}
				assert(curMove.next->obj==BM_GROUND);
				curMove.next->obj= BM_OBJECT;

				if(isDual || !testDead(curMove.next)){
					//create new position in posTable array
					makeMove(newPos, curPos, curMove);
					wrXY0(newPos, mover->i);
					movpus1= curPosHdr->eval + curMove.dist + 1;
					//calculate hash function
					u=hash(newPos);
					//find position in hash table
					for(;;){
						if(!*u){
							*u=ptr2pos(newPos); //add new position to hash table
							newPosHdr->dual=isDual;
							UposTable+=posSize2; //add new position to posTable array
							newPosHdr->heapItem= ++heapSize; //add new position to heap
#ifdef DEBUG
							repaint();
							Sleep(1);
#endif
							break;
						}
						prevPos=pos2ptr(*u);
						if(rdXY(prevPos)==rdXY(newPos) && !memcmp(prevPos, newPos, posSize)){
							//position already exists
							h=HDR1(prevPos);
							//test if forward and backward positions met
							//TODO - this will not find the shortest solution
							if(h->dual!=isDual){
								if(!h->heapItem && movpus1+h->eval < foundEval){///
									foundEval= movpus1+h->eval;
									if(isDual){
										UfoundPos=prevPos;
										UfoundPosD=curPos;
									}
									else{
										UfoundPosD=prevPos;
										UfoundPos=curPos;
									}
								}
								goto nxt;
							}
							newPos=prevPos;
							newPosHdr= HDR1(newPos);
							assert(!newPosHdr->parent || newPosHdr->parent>=posTable && newPosHdr->parent<posTablek);
							if(newPosHdr->eval <= movpus1) goto nxt;
							//found better solution
							break;
						}
						u++;
						if(u==hashTablek) u=hashTable;
					}
					//write header
					newPosHdr->parent= curPos;
					newPosHdr->eval= movpus1;
					newPosHdr->lastMove= &curMove;
					//correct heap sort order
					j= newPosHdr->heapItem;
					i=j/2;
					while(i>0 && heap[i]->eval > movpus1){
						heap[j]=heap[i];
						heap[j]->heapItem=j;
						j=i;
						i/=2;
					}
					heap[j]= newPosHdr;
					newPosHdr->heapItem=j;
				}
			nxt:
				//revert move
				curMove.next->obj= BM_GROUND;
				curMove.obj->obj= BM_OBJECT;
			}
		}
		//remove all objects
		for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
			assert(i2p[rdXY(v)]->obj==BM_OBJECT);
			i2p[rdXY(v)]->obj=BM_GROUND;
		}
		if(UposTable==posTablek) break; //memory is full
	}
end:
	delete[] heap;
	if(UfoundPos){

	}
}
//-------------------------------------------------------------------
int optimizer()
{
	int i, j, rep, heapSize, movpus1, x, prevStepEval, step;
	Pchar v, Ufirst, curPos=0, newPos, prevPos, startPos, startPosEnd=0;
	Ppos *u;
	PHdr5 curPosHdr, newPosHdr, h, *heap;
	PMove Umoves, UmovesEnd;
	Psquare p, pn, pnn;

	Solution &sol=levoff[level].best;
	if(!sol.Mdata) return 0;
	step=0;
	movpus1= sol.Mmoves+sol.Mpushes;
	//allocate array for heap, heap is a tree, root is at index 1
	//root is pointer to position which has minimal evaluation
	heap= new PHdr5[maxPos];
	heapSize=0;
	//multiple start positions according to player position
	startPos=UposTable;
	UmovesEnd=findObjects(movedObj);
	for(Umoves=movedObj; Umoves<UmovesEnd; Umoves++){
		Move &curMove= *Umoves;
		curPos=UposTable;
		wrXY0(curPos, prvP(curMove.obj, curMove.direct)->i);
		h= HDR5(curPos);
		h->parent=0;
		h->eval=movpus1-curMove.dist+1-firstMoveEval;
		h->heapItem=0;
		h->inSolution=false;
		addToHash(curPos);
		UposTable+=posSize2;
		memcpy(UposTable, curPos, posSize); //copy to next position
		startPosEnd=curPos;
	}
	assert(startPosEnd);
	//create positions from solution
	Ufirst=firstMove;
	curPosHdr=0;
	Umoves=movedObj;
	rep=1;
	for(v=sol.Mdata; *v; v++){
		if(*v>='A' && *v<='Z'){
			rep= *v-'A'+1;
			v++;
		}
		if(*v>='0' && *v<='3'){
			while(rep--){
				if(Ufirst<UfirstMove){
					assert(*Ufirst==*v);
					if(*Ufirst++!=*v) goto end;
				}
				else{
					mover=nxtP(mover, *v-'0');
				}
				movpus1--;
			}
			rep=1;
		}
		if(*v>='4' && *v<='7'){
			while(rep--){
				if(Ufirst<UfirstMove){
					assert(*Ufirst==*v);
					if(*Ufirst++!=*v) goto end;
				}
				else{
					if(!curPosHdr){ //the first push
						//find start position which is in solution
						for(; startPos<=startPosEnd; startPos+=posSize2){
							if(rdXY(startPos)==mover->i){
								curPos=startPos;
								curPosHdr=HDR5(curPos);
								break;
							}
						}
						assert(curPosHdr);
						//some solutions don't have optimal path from player start position to the first object
						assert(curPosHdr->eval>=movpus1);
					}
					else{
						curPos=UposTable;
						curPosHdr->parent= ptr2pos(curPos); //write parent to previous position
						curPosHdr= HDR5(curPos);
						curPosHdr->heapItem=0;
						wrXY0(curPos, mover->i);
						addToHash(curPos);
						UposTable+=posSize2;
					}
					curPosHdr->eval=movpus1;
					curPosHdr->inSolution=true;
					//push
					Umoves->direct=*v-'4';
					mover=nxtP(mover, Umoves->direct);
					Umoves->obj=mover;
					Umoves->next=nxtP(mover, Umoves->direct);
					makeMove0(UposTable, curPos, *Umoves);
				}
				movpus1-=2;
			}
			rep=1;
		}//(*v>='4' && *v<='7')
	}//for(v=sol.Mdata
	assert(movpus1==0);
	assert(HDR5(startPos)->inSolution);
	//remove all objects
	for(v=startPos+DXY; v<startPos+posSize1; v+=DXY){
		assert(i2p[rdXY(v)]->obj==BM_OBJECT);
		i2p[rdXY(v)]->obj=BM_GROUND;
	}
	//multiple final positions according to player position
	nextDistId();
	for(j=0; j<Nstore; j++){
		p=fin2p[j];
		for(i=0; i<4; i++){
			pn=nxtP(p, i);
			if(pn->obj==BM_GROUND && !pn->store && pn->distId!=distId){
				pnn=nxtP(pn, i);
				if(pnn->obj==BM_GROUND && !pnn->store){
					curPos=UposTable;
					wrXY0(curPos, pn->i);
					h= HDR5(curPos);
					h->parent=0;
					h->eval=0;
					heap[h->heapItem=++heapSize]=h;
					h->inSolution= pn==mover;
					if(h->inSolution && curPosHdr){
						curPosHdr->parent= ptr2pos(curPos);
						curPosHdr=0;
					}
					addToHash(curPos);
					UposTable+=posSize2;
					memcpy(UposTable, curPos, posSize); //copy to next position
					pn->distId=distId;
				}
			}
		}
	}
	assert(!curPosHdr);
	prevStepEval=0;
	step=1;

	//main loop of Dijkstra
	while(heapSize){
		//take top item from heap - position with lowest evaluation
		curPosHdr= heap[1];
		curPos= ((Pchar)curPosHdr)+DHDR5;
		//remove top of heap and sort descendants
		curPosHdr->heapItem=0;
		x=heap[heapSize]->eval;
		j=1;
		i=2;
		while(i<heapSize){
			if(i+1 < heapSize && heap[i]->eval > heap[i+1]->eval) i++;
			if(x <= heap[i]->eval) break;
			heap[j]=heap[i];
			heap[j]->heapItem=j;
			j=i;
			i*=2;
		}
		heap[j]=heap[heapSize];
		heap[j]->heapItem=j;
		heapSize--;
		//create current position
		mover= i2p[rdXY(curPos)];
		for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
			assert(i2p[rdXY(v)]->obj==BM_GROUND);
			i2p[rdXY(v)]->obj=BM_OBJECT;
		}
		if(curPos<=startPosEnd){
			//start position reached
			mover=moverStart;
			findObjects(movedObj);
			if(curPosHdr->eval + i2p[rdXY(curPos)]->dist < HDR5(startPos)->eval + i2p[rdXY(startPos)]->dist){
				//better solution found
				UfoundPos=curPos;
			}
			goto end;
		}
		//find all possible moves from current position and write them to Umoves
		UmovesEnd=findObjectsR(movedObj);
		assert(distId>0);
		//try all possible moves from current position
		for(Umoves=movedObj; Umoves<UmovesEnd; Umoves++){
			newPos=UposTable;
			if(newPos==posTablek) break; //memory full
			newPosHdr=HDR5(newPos);
			assert(testPos(curPos));
			Move &curMove= *Umoves;
			if(curMove.direct<0) continue;
			movpus1= curPosHdr->eval + curMove.dist + 1;
			assert(curMove.next->obj==BM_GROUND);
			//pull object through corridor
			for(p=curMove.next; p->cont[curMove.direct]==1;){
				p=prvP(p, curMove.direct);
				movpus1+=2;
				if(p->obj!=BM_GROUND) break;
			}
			//make move
			mover= prvP(p, curMove.direct);
			if(p->obj!=BM_GROUND || mover->obj!=BM_GROUND){
				assert(p!=curMove.next);
				continue;
			}
			p->obj= BM_OBJECT;
			curMove.next=p;
			assert(curMove.obj->obj==BM_OBJECT);
			curMove.obj->obj= BM_GROUND;

			//create new position in posTable array
			makeMove(newPos, curPos, curMove);
			wrXY0(newPos, mover->i);
			//calculate hash function
			u=hash(newPos);
			//find position in hash table
			for(;;){
				if(!*u){
					*u=ptr2pos(newPos); //add new position to hash table
					newPosHdr->inSolution=false;
					UposTable+=posSize2; //add new position to posTable array
					newPosHdr->heapItem= ++heapSize; //add new position to heap
#ifdef DEBUG
					repaint();
					Sleep(1);
#endif
					break;
				}
				prevPos=pos2ptr(*u);
				if(rdXY(prevPos)==rdXY(newPos) && !memcmp(prevPos, newPos, posSize)){
					//position already exists
					newPos=prevPos;
					newPosHdr=HDR5(newPos);
					assert(!newPosHdr->parent || pos2ptr(newPosHdr->parent)>=posTable && pos2ptr(newPosHdr->parent)<posTablek);
					//compare evaluation
					x = newPosHdr->eval - movpus1;
					if(x<=0 && (newPosHdr->heapItem || x<0)) goto nxt; //new path is longer
					if(!newPosHdr->heapItem){
						//add position from solution to heap
						newPosHdr->heapItem = ++heapSize;
						if(x==0 && newPosHdr->parent) goto sortheap; //don't change parent if new path is not better
					}
					if(newPosHdr->inSolution){
						//better solution found
						UfoundPos=startPos;
						for(h=HDR5(UfoundPos); h!=newPosHdr; h=pos2hdr5(h->parent)){
							assert(h->parent);
							h->eval -= x;
						}
						//remove old path from solution
						for(;; h=pos2hdr5(h->parent)){
							h->inSolution=false;
							if(!h->parent) break;
						}
						//add new path to solution
						newPosHdr->parent= ptr2pos(curPos);
						for(h=newPosHdr;; h=pos2hdr5(h->parent)){
							h->inSolution=true;
							if(!h->parent) break;
						}
					}
					break;
				}
				u++;
				if(u==hashTablek) u=hashTable;
			}//for

			//write header
			newPosHdr->parent= ptr2pos(curPos);
			newPosHdr->eval= movpus1;
		sortheap:
			//correct heap order
			j= newPosHdr->heapItem;
			assert(j);
			i=j/2;
			while(i>0 && heap[i]->eval > movpus1){
				heap[j]=heap[i];
				heap[j]->heapItem=j;
				j=i;
				i/=2;
			}
			heap[j]= newPosHdr;
			newPosHdr->heapItem=j;
		nxt:
			//revert move
			curMove.next->obj= BM_GROUND;
			curMove.obj->obj= BM_OBJECT;
		}//for(Umoves

		//remove all objects
		for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
			assert(i2p[rdXY(v)]->obj==BM_OBJECT);
			i2p[rdXY(v)]->obj=BM_GROUND;
		}

		if(UposTable==posTablek){ //memory is full
			//next step evaluation
			x= (curPosHdr->eval + prevStepEval)/2;
			assert(x>=prevStepEval);
			h=0;
			//delete heap
			while(heapSize>0) heap[heapSize--]->heapItem=0;
			//clear parent from start positions
			for(newPosHdr=(PHdr5)posTable; (Pchar)newPosHdr<startPosEnd; newPosHdr=(PHdr5)((Pchar)newPosHdr+posSize2)){
				if(!newPosHdr->inSolution) newPosHdr->parent=0;
			}
			assert(newPosHdr==HDR5(startPosEnd+posSize2));
			//delete all positions except solution and start positions
			u=0;
			for(curPosHdr=HDR5(startPos);; curPosHdr=pos2hdr5(*u)){
				assert(curPosHdr->inSolution);
				//find empty slot
				while(newPosHdr->inSolution){
					newPosHdr=(PHdr5)((Pchar)newPosHdr+posSize2);
				}
				//move position to empty slot
				if(curPosHdr>newPosHdr){
					memcpy(newPosHdr, curPosHdr, posSize2);
					curPosHdr->inSolution=false;
					curPosHdr=newPosHdr;
					if(u) *u=ptr2pos((Pchar)curPosHdr+DHDR5);
				}
				if(curPosHdr->eval>x) h=curPosHdr;
				u= &curPosHdr->parent;
				if(!*u) break;
			}
			//add new final position to heap
			assert(h);
			if(!h) goto end;
			heap[h->heapItem=++heapSize]=h;
			assert(h->eval>prevStepEval);
			prevStepEval=h->eval;
			//set UposTable
			while(newPosHdr->inSolution){
				newPosHdr=(PHdr5)((Pchar)newPosHdr+posSize2);
			}
			UposTable=(Pchar)newPosHdr+DHDR5;
			//add all positions to hash table
			memset(hashTable, 0, DhashTable*sizeof(*hashTable));
			for(curPos=posTable+DHDR; curPos<UposTable; curPos+=posSize2){
				addToHash(curPos);
			}
			step++;
		}//if(UposTable==posTablek)
	}//while(heapSize)
	if(!heapSize) msg("Unsolvable"); //should not happen
end:
	delete[] heap;
	return step;
}
//-------------------------------------------------------------------
void delBlind()
{
	Psquare p, pn, pn1=0;
	int i, j, i1;

	//delete blind lanes
	UfirstMove= firstMove= new char[width*height/2];
	firstMoveEval=0;
	for(p=board; p<boardk; p++){
		if(p->obj==BM_GROUND && !p->store){
			j=0;
			for(i=0; i<4; i++){
				pn=nxtP(p, i);
				if(pn->obj!=BM_WALL){
					j++;
					pn1=pn;
					i1=i;
				}
			}
			if(j==1){
				if(p==mover){
					if(pn1->obj==BM_OBJECT){
						if(nxtP(pn1, i1)->obj!=BM_GROUND || pn1->store){
							continue;
						}
						pn1->obj=BM_GROUND;
						nxtP(pn1, i1)->obj=BM_OBJECT;
						*UfirstMove++ = char('4'+i1);
						firstMoveEval++;
					}
					else{
						*UfirstMove++ = char('0'+i1);
					}
					firstMoveEval++;
					mover=pn1;
				}
				p->obj=BM_WALL;
				p=board;
			}
		}
	}
}
//-------------------------------------------------------------------
int thinkInit()
{
	Psquare p, pn, pnn, *p1, *p2, *pf, *ps, po[4];
	int i, j, jm, m, d, k, w, *UfinA, *finRadius;
	Pchar UfinalPos, UstartPos;

	try{

		//init Nobj, Nstore, Square.i, Square.cont, Square.distId
		Nobj=Nstore=i=0;
		for(p=board; p<boardk; p++){
			if(p->obj==BM_GROUND || p->obj==BM_OBJECT){
				p->i=i++;
				for(j=0; j<4; j++){
					po[j]=nxtP(p, j);
					p->cont[j]=-1;
				}
				if(po[0]->obj!=BM_WALL && po[1]->obj!=BM_WALL &&
					po[2]->obj==BM_WALL && po[3]->obj==BM_WALL){
					p->cont[0]=p->cont[1]=1; //corridor left-right
				}
				else if(po[0]->obj==BM_WALL && po[1]->obj==BM_WALL &&
					po[2]->obj!=BM_WALL && po[3]->obj!=BM_WALL){
					p->cont[2]=p->cont[3]=1; //corridor up-down
				}
			}
			else{
				p->i=-1;
			}
			if(p->obj==BM_OBJECT) Nobj++;
			if(p->store) Nstore++;
			p->distId=0;
		}

		Nground=i;
		UfinA= finalDistA= new int[i*Nstore];
		i2p= new Psquare[i];
		fin2p= new Psquare[Nstore+1];
		DXY=1;
		if(i>255) DXY=sizeof(WORD);
		const int D[]={DHDR2, DHDR3, DHDR1, DHDR4, DHDR5};
		DHDR= D[algorithm];
		distId=0;
		posSize1= posSize= (Nobj+1)*DXY;
		posSize2= posSize1+DHDR;
		if(Nobj){
			size_t n;
			if(algorithm!=4) n=(size_t)maxMem*(1000000/4)/sizeof(Move)/Nstore;
			else n=(size_t)maxMem*1000000/(posSize2+16);
			if(n>maxPos0) maxPos=maxPos0;
			else maxPos=(unsigned)n;
		}
		aminmax(maxPos, 100, maxPos0);
		DhashTable= maxPos*2+17;
		hashTable= new Ppos[DhashTable];
		hashTablek= hashTable+DhashTable;
		posTable= new char[(size_t)maxPos*posSize2];
		UposTable= posTable+DHDR;
		posTable0= UposTable-posSize2;
		posTable1= posTable-posSize2;
		posTablek= UposTable + (size_t)maxPos*posSize2;
		UfoundPos=UfoundPosD=0;
		testing=0;
		finalDone=0;
		if(!posTable){
			noMem();
			return 1;
		}
		memset(hashTable, 0, DhashTable*sizeof(*hashTable));

		//start and final position
		UfinalPos= finalPos= new char[Nstore*DXY];
		p1=distBuf1;
		p2=distBuf2;
		UstartPos= UposTable;
		wrXY(UstartPos, mover->i);
		pf=fin2p;
		for(p=board; p<boardk; p++){
			p->finalDist=MAXDIST;
			if(p->store){
				p->finalDist=0;
				*p1++=p;
				*p2++=p;
				*pf++=p;
				wrXY(UfinalPos, p->i);
			}
			if(p->obj==BM_OBJECT){
				wrXY(UstartPos, p->i);
			}
			if(p->i!=-1){
				i2p[p->i]=p;
				p->finalDists= UfinA;
				UfinA += Nstore;
				p->direct=-1;
				for(j=0; j<4; j++){
					if(algorithm==4){
						//pull
						if(p->cont[j]==1 && (p->obj==BM_OBJECT
							|| prvP(p, j)->cont[j]<0 || nxtP(p, j)->cont[j]<0)){
							p->cont[j]=0;
						}
					}
					else{
						//push
						if((prvP(p, j)->cont[j]&-2)==0 &&  //previous is 1 or 0
							p->cont[j]==-1 && !p->store &&
							(nxtP(p, j^2)->obj==BM_WALL || nxtP(p, j^3)->obj==BM_WALL)){
							p->cont[j]=2;
						}
						if(p->cont[j]==1 &&
							(p->store || (prvP(p, j)->cont[j]&-2))){ //previous is -1 or 2
							p->cont[j]=0;
						}
					}
				}
			}
		}
		assert(UfinA==finalDistA+Nground*Nstore);

		//order of store squares
		*p1=*p2=0;
		ps=distBuf2;
		for(d=Nstore; *ps && d>=0; d--){
			for(p1=ps; *p1; p1++){
				for(i=0; i<4; i++){
					pn= nxtP(*p1, i);
					pnn= nxtP(pn, i);
					if(pn->obj!=BM_WALL && pnn->obj!=BM_WALL &&
						pn->finalDist>d && pnn->finalDist>d){
						(*p1)->finalDist=d;
						*p1=*ps++;
						break;
					}
				}
			}
		}
		//init finalDist
		for(d=(Nstore+1)|1;; d++){
			if(d&1) p1=distBuf1, p2=distBuf2;
			else p1=distBuf2, p2=distBuf1;
			if(!*p1) break;
			for(; *p1; p1++){
				for(i=0; i<4; i++){
					pn= nxtP(*p1, i);
					pnn= nxtP(pn, i);
					if(pn->finalDist>=MAXDIST && pn->obj!=BM_WALL && pnn->obj!=BM_WALL
						&& pn->obj!=BM_BACKGROUND){
						pn->finalDist=d;
						*p2++=pn;
					}
				}
			}
			*p2=0;
		}
		//init finalDists[j]
		for(p1=fin2p; p1<fin2p+Nstore; p1++){
			(*p1)->finalDist=0;
		}
		finRadius= new int[Nstore];
		for(k=Nstore; k;){
			for(j=0; j<k; j++){
				for(p1=i2p; p1<i2p+Nground; p1++){
					(*p1)->finalDists[j]=MAXDIST;
				}
				p=fin2p[j];
				distBuf1[0]=p;
				distBuf1[1]=0;
				p->finalDists[j]=0;
				for(d=(Nstore+1)|1;; d++){
					if(d&1) p1=distBuf1, p2=distBuf2;
					else p1=distBuf2, p2=distBuf1;
					if(!*p1) break;
					for(; *p1; p1++){
						for(i=0; i<4; i++){
							pn= nxtP(*p1, i);
							pnn= nxtP(pn, i);
							if(pn->obj!=BM_WALL  && pn->obj!=BM_BACKGROUND &&
								pn->finalDists[j]>=MAXDIST &&
								pnn->obj!=BM_WALL &&
								pn->finalDist>=k && pnn->finalDist>=k){
								pn->finalDists[j]=d;
								*p2++=pn;
							}
						}
					}
					*p2=0;
				}
				finRadius[j]=d;
			}
			m=MAXDIST;
			jm=0;
			for(j=0; j<k; j++){
				if(finRadius[j]<=m && finRadius[j] > Nstore+5 &&
					(finRadius[j]<m || k==Nstore ||
					fin2p[k]->finalDists[j] < fin2p[k]->finalDists[jm])){
					m=finRadius[j];
					jm=j;
				}
			}
			k--;
			p=fin2p[jm];
			fin2p[jm]=fin2p[k];
			fin2p[k]=p;
			p->finalDist=k;
			for(p1=i2p; p1<i2p+Nground; p1++){
				p=*p1;
				w= p->finalDists[k];
				p->finalDists[k]= p->finalDists[jm];
				p->finalDists[jm]= w;
				if(p->finalDist<k) p->finalDists[k]=0;
			}
		}
		fin2p[Nstore]= board;
		delete[] finRadius;
		assert(testPos(UposTable));
		if(!memcmp(finalPos, UposTable+DXY, posSize1-DXY)){
			UfoundPos=UfoundPosD=UposTable;
		}
		size_t len= Nobj*4*(size_t)maxPos;
		if(algorithm==4) len=Nobj*4+1;
		movedObj= new Move[len];
		if(!movedObj){
			noMem();
			return 1;
		}
	} catch(std::bad_alloc)
	{
		noMem();
		return 1;
	}
	return 0;
}
//-------------------------------------------------------------------
void thinkDestroy()
{
	delete[] movedObj; movedObj=0;
	delete[] finalPos; finalPos=0;
	delete[] posTable; posTable=0;
	delete[] hashTable; hashTable=0;
	delete[] fin2p; fin2p=0;
	delete[] i2p; i2p=0;
	delete[] finalDistA; finalDistA=0;
	delete[] firstMove; firstMove=0;
}
//-------------------------------------------------------------------
void gener()
{
	Pchar v, curPos, newPos;
	Ppos *u;
	PHdr4 curPosHdr, newPosHdr;
	PMove Umoves, Umoves1;
	Psquare *pp;

	algorithm=3;
	if(thinkInit()){
		thinkDestroy();
		return;
	}
	curPos= UposTable;
	curPosHdr=HDR4(curPos);
	curPosHdr->mov=curPosHdr->pus=0;
	curPosHdr->movesBeg= movedObj;
	curPosHdr->movesEnd= Umoves= findObjectsR(movedObj);
	for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
		assert(i2p[rdXY(v)]->obj==BM_OBJECT);
		i2p[rdXY(v)]->obj=BM_GROUND;
	}
	UposTable+=posSize2;
	for(; curPos<UposTable; curPos+=posSize2){
		curPosHdr= HDR4(curPos);
		//create current position
		for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
			assert(i2p[rdXY(v)]->obj==BM_GROUND);
			i2p[rdXY(v)]->obj=BM_OBJECT;
		}
		for(Umoves1=curPosHdr->movesBeg; Umoves1<curPosHdr->movesEnd; Umoves1++){
			Move &curMove= *Umoves1;
			newPos=UposTable;
			if(newPos==posTablek) break;
			newPosHdr=HDR4(newPos);
			//make move
			assert(testPos(curPos));
			assert(curMove.next->obj==BM_GROUND);
			assert(curMove.obj->obj==BM_OBJECT);
			curMove.next->obj= BM_OBJECT;
			curMove.obj->obj= BM_GROUND;
			//create new position
			mover= prvP(curMove.next, curMove.direct);
			//find all possible pushes from new position
			newPosHdr->movesEnd= findObjectsR(Umoves);
			if(newPosHdr->movesEnd>Umoves){
				makeMove(newPos, curPos, curMove);
				newPosHdr->mov= (short)(curPosHdr->mov + curMove.dist);
				newPosHdr->pus= (short)(curPosHdr->pus + 1);
				//place player into top left corner
				for(pp=i2p; (*pp)->distId!=distId; pp++);
				wrXY0(newPos, (*pp)->i);
				//calculate hash function
				u=hash(newPos);
				for(;;){
					if(!*u){
						//write new position to posTable array
						*u=ptr2pos(newPos);
						UposTable+=posSize2;
						newPosHdr->movesBeg= Umoves;
						Umoves= newPosHdr->movesEnd;
#ifdef DEBUG
						repaint();
#endif
						break;
					}
					if(!memcmp(pos2ptr(*u), newPos, posSize1)){
						//position already exists
						break;
					}
					u++;
					if(u==hashTablek) u=hashTable;
				}
			}
			//revert move
			curMove.next->obj= BM_GROUND;
			curMove.obj->obj= BM_OBJECT;
		}
		//remove all objects
		for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
			assert(i2p[rdXY(v)]->obj==BM_OBJECT);
			i2p[rdXY(v)]->obj=BM_GROUND;
		}
	}
	//set last founded position
	curPos=UposTable-posSize2;
	curPosHdr=HDR4(curPos);
	mover= i2p[rdXY(curPos)];
	for(v=curPos+DXY; v<curPos+posSize1; v+=DXY){
		i2p[rdXY(v)]->obj=BM_OBJECT;
	}
	repaint();
	msg("%d-%d\n%d", curPosHdr->mov, curPosHdr->pus,
		(UposTable-posTable)/posSize2);
	thinkDestroy();
	edUndo=edRec; edRedo=0;
}
//-------------------------------------------------------------------
//write solution while moving from src to dest
void wrSolPath(Psquare src, Psquare dest)
{
	Psquare p, pn;
	int i;

	if(!src) return;
	findDist(dest, src);
	assert(src->distId==distId);
	for(p=src; p!=dest; p=pn){
		for(i=0;; i++){
			assert(i<4);
			pn=nxtP(p, i);
			if(pn->dist==p->dist-1) break;
		}
		*UfoundSol++= (char)(i+'0');
	}
}
//-------------------------------------------------------------------
Pchar getParent(Pchar pos)
{
	switch(algorithm){
		case 0:
			return HDR2(pos)->parent;
		case 1:
			return HDR3(pos)->parent;
		case 2:
			return HDR1(pos)->parent;
		case 4:
			return pos2ptr0(HDR5(pos)->parent);
	}
	return 0;
}
//-------------------------------------------------------------------
void wrSol()
{
	Pchar u, prv, v, v1, v2, foundSol;
	Psquare p, pn, p2, pn2, moverS=0;
	int d, direct, dual;

	//write moves which were created by deleting blind corridors
	logPos=logbuf;
	for(u=firstMove; u<UfirstMove; u++){
		wrLog(*u);
	}
	//allocate buffer for solution
	switch(algorithm){
		case 0:
			d=HDR2(UfoundPos)->eval;
			break;
		case 1:
			d=HDR3(UfoundPos)->mov+HDR3(UfoundPosD)->mov;
			break;
		case 2:
			d=HDR1(UfoundPos)->eval+HDR1(UfoundPosD)->eval;
			break;
		case 4:
			d=HDR5(UfoundPos)->eval;
			break;
	}
	foundSol= new char[d+Nground+1];

	for(dual=0; dual<2; dual++){
		UfoundSol=foundSol;
		//remove objects
		for(p=board; p<boardk; p++){
			if(p->obj==BM_OBJECT) p->obj=BM_GROUND;
		}
		//set position
		u= dual ? UfoundPosD : UfoundPos;
		if(!u) break;
		for(v=u+DXY; v<u+posSize1; v+=DXY){
			i2p[rdXY(v)]->obj=BM_OBJECT;
		}
		mover=0;
		if(dual) mover=moverS;
		if(algorithm==4){
			dual=1;
			mover=moverStart;
		}
		assert(posSize==posSize1);
		for(; (prv=getParent(u))!=0; u=prv){
			//find direction from position u to position prv
			//from pn to p
			for(v1=prv+DXY, v2=u+DXY; rdXY(v1)==rdXY(v2); v1+=DXY, v2+=DXY);
			assert(v1<prv+posSize && v2<u+posSize);
			p=i2p[rdXY(v1)];
			pn=i2p[rdXY(v2)];
			for(v1=prv+posSize-DXY, v2=u+posSize-DXY; rdXY(v1)==rdXY(v2); v1-=DXY, v2-=DXY);
			assert(v1>prv && v2>u);
			p2=i2p[rdXY(v1)];
			pn2=i2p[rdXY(v2)];
			if(pn->i > p->i){
				if(p!=p2 || p->y!=pn->y){
					direct=2; //up
				}
				else{
					direct=0; //left
				}
				pn=pn2;
			}
			else{
				if(p!=p2 || p->y!=pn->y){
					direct=3; //down
				}
				else{
					direct=1; //right
				}
				p=p2;
			}
			assert(direct<2 ? p->y==pn->y : p->x==pn->x);
			//move player to object
			if(dual) direct^=1;
			wrSolPath(mover, nxtP(pn, direct));
			if(u==UfoundPos) moverS=p;
			//push object from pn to p (pull if dual==0)
			assert(pn->obj==BM_OBJECT);
			assert(p->obj==BM_GROUND);
			pn->obj=BM_GROUND;
			p->obj=BM_OBJECT;
			mover= nxtP(p, direct);
			if(dual) direct^=1;
			do{
				*UfoundSol++= (char)(direct+'4');
				pn=nxtP(pn, direct);
			} while(pn!=p);
		}
		if(!dual){
			wrSolPath(mover, i2p[rdXY(posTable+DHDR)]);
			//the first part of solution - reverse
			for(u=UfoundSol-1; u>=foundSol; u--){
				wrLog(char(*u^1));
			}
			if(algorithm==2){
				HDR1(UfoundPos)->parent=UfoundPosD;
				UfoundPosD=UfoundPos;
			}
		}
		else{
			//the second part of solution
			for(u=foundSol; u<UfoundSol; u++){
				wrLog(*u);
			}
		}
	}
	assert(UfoundSol-foundSol<=d);
	delete[] foundSol;
	*logPos=0;
	logPos=0;
}
//-------------------------------------------------------------------
int findSolutionThread()
{
	int k, step=0;
	DWORD time= GetTickCount();
	playtime=0;
	stopTime=false;

	delBlind();
	moverStart=mover;
	if(thinkInit()){
		thinkDestroy();
		return -4;
	}
	if(Nobj!=Nstore || !Nobj) return -3;
	switch(algorithm){
		case 0:
			depthSearch();
			break;
		case 1:
			breadthSearch();
			break;
		case 2:
			dijkstra();
			break;
		case 4:
			step=optimizer();
			break;
	}
	assert(UposTable<=posTablek);
	if(UfoundPos){
		//write and compress solution
		wrSol();

		///assert(algorithm!=2 || mov+pus==HDR1(UfoundPos)->eval);
		//if(algorithm==1) msg("%d-%d, %d-%d",HDR3(UfoundPos)->mov,HDR3(UfoundPos)->pus,HDR3(UfoundPosD)->mov,HDR3(UfoundPosD)->pus);
	}
	k= int(((char*)UposTable-(char*)posTable)/posSize2);
	double NpositionM = k/1000000.0;
	thinkDestroy();
	time=GetTickCount()-time;
	double timeS=time/1000.0;
	if(UfoundPos){
		loadSolution(level, logbuf);
		assert(!movError);
		assert(algorithm!=4 || eval(moves, pushes) <= levoff[level].best.eval());
		//write solution to dat file
		finish();
		saveUser();
		saveData();
		status();
		if(!stopTime)
			msg("Solution is wrong !");
		else if(gratulOn){
			msg("Solution: %d-%d\nTime: %f s\n\nPositions: %f", moves, pushes, timeS, NpositionM);
		}
		return moves+pushes;
	}
	else{
		resetLevel();
		stopTime=true;
	}
	if(UposTable==posTablek){
		if(gratulOn){
			//assert(k==maxPos);
			msg("Timeout: %f s\n\nPositions: %f", timeS, NpositionM);
		}
		return -1;
	}
	if(!step) msg("Unsolvable\n\nPositions: %f", NpositionM);
	else if(gratulOn){
		msg("Time: %f s\n\nSteps: %d\n\nPositions: %f", timeS, step,
			step==1 ? NpositionM : maxPos/1000000.0);
	}
	return -2;
}

DWORD WINAPI findSolutionThread(LPVOID alg)
{
	algorithm=(int)alg;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);

#ifdef SOLVE_ALL
	char f[16];
	strcpy(f,"comp0.rec");
	f[4]=(char)(algorithm+'0');
	getExeDir(fnuser,f);
	delUser();
	initUser();
	gratulOn=0;
	for(int i=0; i<Nlevels; i++){
		loadLevel(i);
		if((algorithm==4 ? levoff[i].best.Mmoves>0 : getNobj(levoff[i].offset)<20) && !levoff[i].user.Mmoves){
			update();
			findSolutionThread();
			writeini();
		}
	}
	gratulOn=1;
#else
#ifndef NDEBUG
	gratulOn=1;
#endif
	findSolutionThread();
#endif

	InvalidateRect(hWin, 0, TRUE);
	solving=false;
	setTitle("");
	return 0;
}

void findSolution(int alg)
{
#ifdef _WIN64
	const size_t stackSize = 1000000000;
#else
	const size_t stackSize = 150994944;
#endif
	solving=true;
	char num[20];
	_itoa(alg, num, 10);
	setTitle(num);
	DWORD threadId;
	HANDLE thread= CreateThread(0, (alg!=4) ? stackSize : 0,
		(LPTHREAD_START_ROUTINE)findSolutionThread, (LPVOID)alg, 0, &threadId);
	CloseHandle(thread);
}

//-------------------------------------------------------------------

#endif
