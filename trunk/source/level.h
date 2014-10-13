/*
 (C) Petr Lastovicka

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License.
 */
//---------------------------------------------------------------------------
#ifndef levelH
#define levelH
#include "lang.h"
#pragma warning(disable:4701)
//---------------------------------------------------------------------------
enum{
	BM_GROUND, BM_OBJECT, BM_BACKGROUND, BM_STORE, BM_OBJECTSTORE,
	BM_HILITE, BM_HILITESTORE, BM_WALL, BM_MOVER=BM_WALL+5,
	BM_MOVERSTORE=BM_MOVER+4, BM_NUM=BM_MOVERSTORE+4
};
enum{
	F_GROUND, F_OBJECT, F_HILITE, F_STORE, F_BACKGROUND,
	F_MOVER, F_WALL, F_NUM
};

typedef char *Pchar;
typedef unsigned char uchar;
typedef uchar *Puchar;
typedef char TfileName[MAX_PATH];

struct Square1 {
	char obj; //BM_GROUND or BM_OBJECT or BM_WALL or BM_BACKGROUND
	bool store;
};
typedef Square1 *Psquare1;

struct Square : public Square1 {
	int x, y; //top-left corner has coordinates [0,0]
	int i;   //index of squares not occupied by wall
	int dist;
	int finalDist; //distance to the nearest store
	int distId;
	int direct;
	short cont[4];      //think:corridor, movePush:evaluation
	char pushDirect[4]; //result of movePush, player relative position from object before push
	int *finalDists;
	Square(){ store=false; obj=BM_GROUND; }
};
typedef Square *Psquare;

inline int eval(int mov, int pus){ return mov ? mov+pus : 9999999; }

struct Solution {
	int Mmoves, Mpushes;
	Pchar Mdata;
	void init(){ Mmoves=Mpushes=0; Mdata=0; }
	Solution(){ init(); }
	int eval(){ return ::eval(Mmoves, Mpushes); }
};

struct Level {
	Pchar offset, author;
	Solution best, user;
	int width, height;
	int i;
};

//quick save item
struct QuickInfo {
	int mover, pushBeg, pushEnd; //relative pointers
	short mov, pus; //copied from UndoInfo
};

//undo item
struct UndoInfo {
	Psquare mover, pushBeg, pushEnd;
	short mov, pus; //number of moves and pushes relative to previous position
};

//editor undo item
struct EdUndo {
	int x, y;
	char obj;
	bool store;
};
//---------------------------------------------------------------------------
void initUser();
void delUser();
void saveUser();
void saveData();
void saveLevel();
int initLevels();
int loadLevel(int which);
void loadNextLevel();
void clearBoard();
void resetLevel();
bool finish();
void openLevel();
void importLevels(char* dir);
void fillOuter();
void findDist(Psquare src, Psquare dest);
void movLevels(Level *first, Level *last, Level *dest);
int optimizeLevel();
bool wrLevel();
void openPos();
void savePos();
void openUser();
void newBoard(int w, int h, int copy);
void getDim(Pchar buf, int &widht, int &height);
int getNobj(Pchar buf);
Level *addLevel();
void loadSolution(int lev, Pchar p);
bool delLevel(int lev, HWND win);
void wrLog(char ch);
void findSolution(int alg);
void gener();
void solutionFromSok(char *fn);
void compressSolutions();

Psquare square(int x, int y);
void msg(char *text, ...);
int msg1(int btn, char *text, ...);
void msg2(char *caption, char *text, ...);
int msg3(int btn, char *caption, char *text, ...);
void msgD(char *text, ...);
void setTitle(char *txt);
void getClient(RECT *rc);
void paintSquare(Psquare p);
void repaint();
void update();
void resize();
void status();
void undoAll();
void redoAll();
bool fullUndo();
bool move(int direct, bool setUndo=false);
bool moveK(int direct);
int openFileDlg(OPENFILENAME *o);
int saveFileDlg(OPENFILENAME *o, bool prompt);
void writeini();
//---------------------------------------------------------------------------
extern int Nlevels, playtime, level, quickSaveLevel, width, height, bmW, bmH,
check, notOptimize, moverDirect, replay, moves, pushes, notMsg, notResize,
	notdraw, movError, gratulOn, diroff[9], distId, MNg, nextAction;
extern bool modifData, modifUser, editing, rdonly, stopTime, solving;
extern Psquare board, boardk, mover, selected, hilited, *distBuf1, *distBuf2;
extern Pchar user, userk, levels, levelsk, logPos;
extern Level *levoff;
extern QuickInfo *quickSave;

#ifdef SOLVE_ALL
#define Mundo 1500000
#else
#define Mundo 50000
#endif
extern UndoInfo rec[Mundo], *undoPos, *redoPos;
extern EdUndo edRec[1024], *edUndo, *edRedo;
#define Mlog (4*Mundo)
extern char logbuf[Mlog], *title;

extern TfileName fnuser, fndata, fnlevel, fnsave;
extern OPENFILENAME userOfn, levelOfn, dataOfn, savOfn;
extern HWND hWin;
//---------------------------------------------------------------------------
#define sizeA(a) (sizeof(a)/sizeof(*a))
#define endA(a) (a+(sizeof(a)/sizeof(*a)))
#define nxtP(p,s) ((Psquare)(((char*)p)+diroff[s]))
#define prvP(p,s) ((Psquare)(((char*)p)-diroff[s]))
#define isSep(c) ((c)==0 || (c)==';' || (c)=='\r' || (c)=='\n')
#define MAXDIST 15000

template <class T> inline void amin(T &x, int m)
{
	if(x<m) x=m;
}
template <class T> inline void amax(T &x, int m)
{
	if(x>m) x=m;
}
template <class T> inline void aminmax(T &x, int l, int h){
	if(x<l) x=l;
	if(x>h) x=h;
}

extern "C"{
	HWND WINAPI HtmlHelpA(HWND hwndCaller, LPCSTR pszFile, UINT uCommand, DWORD_PTR dwData);
	HWND WINAPI HtmlHelpW(HWND hwndCaller, LPCWSTR pszFile, UINT uCommand, DWORD_PTR dwData);
#ifdef UNICODE
#define HtmlHelp  HtmlHelpW
#else
#define HtmlHelp  HtmlHelpA
#endif
}
//---------------------------------------------------------------------------
#endif
