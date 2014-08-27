/*
 (C) Petr Lastovicka

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License.
 */
//---------------------------------------------------------------------------
#include <windows.h>
#pragma hdrstop
#include <commctrl.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "level.h"

#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"version.lib")
#pragma comment(lib,"htmlhelp.lib")

/*
//---------------------------------------------------------------------------
USERC("sokoban.rc");
USEUNIT("level.cpp");
USEUNIT("think.cpp");
USEUNIT("lang.cpp");
//---------------------------------------------------------------------------
*/
const int minWidth=395; //minimal window width
//---------------------------------------------------------------------------
int
level,         //current level index (from 0)
	moves, pushes, //current number of moves and pushes
	moverDirect,   //player direction, 0=left, 1=right, 2=up, 3=down
	playtime,      //game time in seconds
	width=1,       //level width (number of fields)
	height=1,      //level height (number of fields)
	x0, y0,        //top left level corner coordinates inside the main window
	bmH, bmW,      //field height and width in pixels on screen
	bfH, bfW,      //field height and width in BMP file
	statusH=18,    //status bar height
	toolBarH=28,   //tool bar height
	moveDelay=30,  //player delay while moving
	playTimer=250, //delay while playing recorded solution
	fastTimer=20,  //delay while fast forwarding recorded solution
	titleTimer=7000,//how long information in window title is visible
	toolBarVisible=1,//tool bar is visible
	statusBarVisible=1,//status bar is visible
	center=1,      //window is in screen center
	nowalls=0,     //walls are hidden
	noobjects=0,   //objects (boxes) are hidden
	noselected=0,  //selected objects are not highlited
	gratulOn=1,    //message is shown when better solution is found
	xtrans, ytrans,//level mirrorring

	Nlevels,       //count of all levels
	prevLevel,     //previous level
	replay,        //pressed button index on solution player toolbar
	edQsaveMoverX, edQsaveMoverY,//player coordinates inside remembered position
	edQsaveWidth, edQsaveHeight, //remembered position level dimension
	quickSaveLevel,//level which has remembered position
	quickLen,      //quickSave array length
	levDlgX=180, levDlgY=139,
	levDlgW=576, levDlgH=389,       //level dialog window size and position 
	movError,      //nonzero if solution is wrong
	clone,         //new level will be copied from the current level
	check,         //check level
	notResize,     //don't resize window
	notOptimize,   //don't delete unreachable fields
	notMsg,        //don't show message boxes
	notdraw,       //don't redraw window
	diroff[9],     //offsets to adjacent fields - L,R,U,D,LU,RU,LD,RD,0
	colWidth[]={50, 110, 20, 110, 58, 76, 270}, //level list columns
	colOrder[]={0, 1, 2, 3, 4, 5, 6},
	sortedCol=0,   //level list order
	descending=-1,
	nextAction=2;  //0=nothing, 1=random level, 2=next unsolved level

Pchar
logPos,        //current index in saved solution
	levels, levelsk,//file sokoban.dat in memory
	user, userk;    //user solutions

Psquare
mover,           //player position
	selected,      //object selected by mouse click
	hilited,       //highlited object
	editPos,       //last modified field during mouse movement
	board=0, boardk;//current level content

bool
delreg,          //registry settings have been deleted
	editing,       //0=game, 1=level editor
	rdonly,        //file sokoban.dat is read only
	ldown, rdown,  //mouse button is pressed
	bmWall,        //walls are connected (extended skin file)
	bmMover,       //player direction is visible (skin file contains 4 player bitmaps)
	undoing,       //true during undo, moves counter is descreasing
	justSelected,  //lineObj is disabled when true
	stopTime,      //time is paused
	modifData,     //some level changed, sokoban.dat must be saved
	modifUser;     //user solved some level, rec file must be saved

char
logbuf[Mlog],  //buffer for solution
	curAuthor[64]; //user name, used for new levels

UndoInfo rec[Mundo], *undoPos, *redoPos;//undo, redo
QuickInfo *quickSave;//remembered position
EdUndo edRec[1024], *edUndo, *edRedo; //undo in editor
Psquare1 edQsave;//quick saved position in editor
Level *levoff;   //level pointers array
static Level **A; //order in the listbox
//---------------------------------------------------------------------------
HBITMAP bm;
HDC dc, bmpdc;
HWND hWin, hStatus, toolbar, toolPlay;
HINSTANCE inst;
HACCEL haccel;

TfileName
fnsave,
	fnskin,
	fndata,
	fnuser,
	fnhelp,
	fnlevel;

char *title="SokobanP", *editTitle="SokobanP - Editor", *solverTitle="SokobanP - solver";
const char *subkey="Software\\Petr Lastovicka\\sokoban";
struct Treg { char *s; int *i; } regVal[]={
		{"level", &level},
		{"toolVis", &toolBarVisible},
		{"statusVis", &statusBarVisible},
		{"moveDelay", &moveDelay},
		{"fastTimer", &fastTimer},
		{"playTimer", &playTimer},
		{"center", &center},
		{"nowalls", &nowalls},
		{"noobjects", &noobjects},
		{"noselect", &noselected},
		{"gratul", &gratulOn},
		{"levDlgX", &levDlgX},
		{"levDlgY", &levDlgY},
		{"levDlgW", &levDlgW},
		{"levDlgH", &levDlgH},
		{"colLev", &colWidth[0]},
		{"colBest", &colWidth[1]},
		{"colUser", &colWidth[3]},
		{"colObj", &colWidth[4]},
		{"colDim", &colWidth[5]},
		{"colAuth", &colWidth[6]},
		{"nextAction", &nextAction},
};
struct Tregs { char *s; char *i; DWORD n; bool isPath; } regValS[]={
		{"skin", fnskin, sizeof(fnskin), 1},
		{"save", fnsave, sizeof(fnsave), 1},
		{"data", fndata, sizeof(fndata), 1},
		{"user", fnuser, sizeof(fnuser), 1},
		{"author", curAuthor, sizeof(curAuthor), 0},
		{"extLevel", fnlevel, sizeof(fnlevel), 1},
		{"language", lang, sizeof(lang), 0},
};

OPENFILENAME savOfn={
	sizeof(OPENFILENAME), 0, 0, 0, 0, 0, 1,
	fnsave, sizeof(fnsave),
	0, 0, 0, 0, 0, 0, 0, "SAV", 0, 0, 0
};
OPENFILENAME userOfn={
	sizeof(OPENFILENAME), 0, 0, 0, 0, 0, 1,
	fnuser, sizeof(fnuser),
	0, 0, 0, 0, 0, 0, 0, "REC", 0, 0, 0
};
OPENFILENAME skinOfn={
	sizeof(OPENFILENAME), 0, 0, 0, 0, 0, 1,
	fnskin, sizeof(fnskin),
	0, 0, 0, 0, 0, 0, 0, "BMP", 0, 0, 0
};
OPENFILENAME dataOfn={
	sizeof(OPENFILENAME), 0, 0, 0, 0, 0, 1,
	fndata, sizeof(fndata),
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
OPENFILENAME levelOfn={
	sizeof(OPENFILENAME), 0, 0, 0, 0, 0, 1,
	fnlevel, sizeof(fnlevel),
	0, 0, 0, 0, 0, 0, 0, "XSB", 0, 0, 0
};
//---------------------------------------------------------------------------
int vmsg(char *caption, char *text, int btn, va_list v)
{
	char buf[256];
	if(!text || notMsg) return IDCANCEL;
	_vsnprintf(buf, sizeof(buf), text, v);
	buf[sizeof(buf)-1]=0;
	return MessageBox(hWin, buf, caption, btn);
}

int msg3(int btn, char *caption, char *text, ...)
{
	va_list ap;
	va_start(ap, text);
	int result = vmsg(caption, text, btn, ap);
	va_end(ap);
	return result;
}

void msg2(char *caption, char *text, ...)
{
	va_list ap;
	va_start(ap, text);
	vmsg(caption, text, MB_OK|MB_ICONERROR, ap);
	va_end(ap);
}

int msg1(int btn, char *text, ...)
{
	va_list ap;
	va_start(ap, text);
	int result = vmsg(title, text, btn, ap);
	va_end(ap);
	return result;
}

void msg(char *text, ...)
{
	va_list ap;
	va_start(ap, text);
	vmsg(title, text, MB_OK|MB_ICONERROR, ap);
	va_end(ap);
}

void msgD(char *text, ...)
{
	va_list ap;
	va_start(ap, text);
#ifdef MSGD
	vmsg(title,text,MB_OK,ap);
#endif
	va_end(ap);
}

void Sleep2(int ms)
{
	static int d=0;

	if(ms<20){
		d+=ms;
		if(d>20){
			Sleep(20);
			d-=20;
		}
	}
	else{
		Sleep(ms);
	}
}

typedef WINUSERAPI BOOL(WINAPI *pIsHungAppWindow)(HWND hwnd);
pIsHungAppWindow isHungAppWindow;

//pause while during player movement
void sleep()
{
	MSG mesg;
	if(notdraw || isHungAppWindow && isHungAppWindow(hWin) &&
		(PeekMessage(&mesg, NULL, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_NOREMOVE)
		|| PeekMessage(&mesg, NULL, WM_NCLBUTTONDOWN, WM_NCLBUTTONDOWN, PM_NOREMOVE))) 
		return;
	if(!replay && PeekMessage(&mesg, NULL, WM_TIMER, WM_TIMER, PM_REMOVE)){
		DispatchMessage(&mesg);
	}
	aminmax(moveDelay, 0, 500);
	Sleep2(moveDelay);
}

//resize window
void setWindowXY(HWND hWnd, int x, int y)
{
	SetWindowPos(hWnd, 0, x, y, 0, 0, SWP_NOSIZE|SWP_NOZORDER);
}

int subBoard(Psquare p)
{
	if(!p) return -1;
	return int(p-board);
}

Psquare addBoard(int d)
{
	if(d<0) return 0;
	return board+d;
}

Psquare square(int x, int y)
{
	if(x<0 || y<0 || x>=width || y>=height) return 0;
	return board + y*width + x;
}

Psquare SquareXY(int x, int y)
{
	x= (x-x0)/bmW;
	y= (y-y0)/bmH;
	if(xtrans) x= width-1-x;
	if(ytrans) y= height-1-y;
	return square(x, y);
}
//---------------------------------------------------------------------------
int getRadioButton(HWND hWnd, int item1, int item2)
{
	for(int i=item1; i<=item2; i++){
		if(IsDlgButtonChecked(hWnd, i)){
			return i-item1;
		}
	}
	return 0;
}
//---------------------------------------------------------------------------
//set temporary window title
void setTitle(char *txt)
{
	char buf[256], *s;

	strcpy(buf, solving ? solverTitle : (editing ? editTitle : title));
	if(txt && *txt){
		strcat(buf, " - ");
		s=strchr(txt, 0);
		while(s>=txt && *s!='\\') s--;
		strcat(buf, s+1);
		s= strchr(buf, 0);
		while(s>=buf && *s!='.') s--;
		if(*s=='.') *s=0;
		SetTimer(hWin, 300, titleTimer, 0);
	}
	SetWindowText(hWin, buf);
}
//---------------------------------------------------------------------------
//set status bar text
void status(int i, char *txt, ...)
{
	char buf[256];

	if(notdraw) return;
	va_list ap;
	va_start(ap, txt);
	vsprintf(buf, txt, ap);
	SendMessage(hStatus, SB_SETTEXT, i, (LPARAM)buf);
	va_end(ap);
}
//---------------------------------------------------------------------------
void statusMoves()
{
	status(1, lng(550, "Moves:%d"), moves);
	status(2, lng(551, "Pushes:%d"), pushes);
}
//---------------------------------------------------------------------------
//redraw status bar
void status()
{
	statusMoves();
	status(0, "%d", level+1);
	Level *lev= &levoff[level];
	status(3, "%d - %d", lev->best.Mmoves, lev->best.Mpushes);
	status(6, lev->author);
}
//---------------------------------------------------------------------------
//draw field
void paintSquare(Psquare p)
{
	int i, x, y, dx, dy, k, k2;
	static int T[]={BM_WALL+1, BM_WALL+3, BM_WALL+4, BM_WALL+2,
		BM_WALL+1, BM_WALL+3, BM_WALL+4, BM_WALL};
	static int D[4][3]={{0, 2, 4}, {1, 2, 5}, {0, 3, 6}, {1, 3, 7}};

	if(notdraw || !p) return;
	x=p->x; y=p->y;
	if(xtrans) x= width-1-x;
	if(ytrans) y= height-1-y;
	x= x*bmW + x0;
	y= y*bmH + y0;
	if(p->obj==BM_WALL && bmWall && !nowalls && p!=mover){
		//connect wall with adjacent wall
		int bmW2= bmW>>1, bmH2= bmH>>1, bmW3= bmW-bmW2, bmH3= bmH-bmH2;
		for(k=0; k<4; k++){
			i=0;
			k2=k;
			if(xtrans) k2^=1;
			if(ytrans) k2^=2;
			if(nxtP(p, D[k2][0])->obj==BM_WALL) i+=1;
			if(nxtP(p, D[k2][1])->obj==BM_WALL) i+=2;
			if(nxtP(p, D[k2][2])->obj==BM_WALL) i+=4;
			dx=dy=0;
			if(k&1) dx=bmW2;
			if(k>1) dy=bmH2;
			BitBlt(dc, x+dx, y+dy, k&1 ? bmW3 : bmW2, k>1 ? bmH3 : bmH2,
				bmpdc, T[i]*bmW+dx, dy, SRCCOPY);
		}
	}
	else{
		if(p==mover){
			i=BM_MOVER;
			if(p->store) i=BM_MOVERSTORE;
			if(bmMover){
				i+= moverDirect;
				if(xtrans && moverDirect<2 || ytrans && moverDirect>=2) i^=1;
			}
		}
		else{
			i= p->obj;
			if(p->store){
				if(i==BM_GROUND) i=BM_STORE;
				if(i==BM_OBJECT) i=BM_OBJECTSTORE;
			}
			if(i==BM_WALL && nowalls && !editing) i=BM_BACKGROUND;
			if(i==BM_OBJECT && noobjects && !editing &&
				(abs(p->x - mover->x)>1 || abs(p->y - mover->y)>1)){
				i=BM_GROUND;
			}
			if(p==selected && !noselected){
				if(i==BM_OBJECT) i=BM_HILITE;
				if(i==BM_OBJECTSTORE) i=BM_HILITESTORE;
			}
		}
		BitBlt(dc, x, y, bmW, bmH, bmpdc, i*bmW, 0, SRCCOPY);
	}
}
//---------------------------------------------------------------------------
void paintSquare9(Psquare pos)
{
	for(int i=0; i<9; i++){
		paintSquare(nxtP(pos, i));
	}
}

void paintSquareO(Psquare pos)
{
	if(noobjects) paintSquare9(pos);
	else paintSquare(pos);
}
//---------------------------------------------------------------------------
//select and hilite object
void setSelected(Psquare p)
{
	if(p!=hilited && (!p || p->obj==BM_OBJECT)){
		Psquare old=hilited;
		hilited=selected=p;
		paintSquare(old);
		paintSquare(p);
	}
}
//---------------------------------------------------------------------------
//window rectangle without toolbar and statusbar
void getClient(RECT *rc)
{
	GetClientRect(hWin, rc);
	if(toolBarVisible || replay) rc->top+= toolBarH;
	if(statusBarVisible) rc->bottom-= statusH;
}
//---------------------------------------------------------------------------
//draw board
void repaint()
{
	RECT rc;

	getClient(&rc);
	HRGN hrgn=CreateRectRgnIndirect(&rc);
	SelectClipRgn(dc, hrgn);
	DeleteObject(hrgn);

	int xk= x0+width*bmW;
	int yk= y0+height*bmH;

	rc.top+= (y0-rc.top)%bmH -bmH;
	rc.left+= (x0-rc.left)%bmW -bmW;
	for(int y=rc.top; y<rc.bottom; y+=bmH){
		for(int x=rc.left; x<rc.right; x+=bmW){
			if(x<x0 || x+bmW>xk || y<y0 || y+bmH>yk)
				BitBlt(dc, x, y, bmW, bmH, bmpdc, bmW*BM_BACKGROUND, 0, SRCCOPY);
		}
	}
	for(Psquare p=board; p<boardk; p++) paintSquare(p);
	SelectClipRgn(dc, 0);
}
//---------------------------------------------------------------------------
//draw entire window
void update()
{
	repaint();
	status();
}
//---------------------------------------------------------------------------
//resize window according to skin size
void resize()
{
	int w, h, bw, bh, wc, hc, x, y, panelsH;
	RECT rc;

	if(notResize) return;
	notResize++;

	panelsH=0;
	if(toolBarVisible || replay) panelsH+=toolBarH;
	y0=panelsH;
	if(statusBarVisible) panelsH+= statusH;
	wc = 2*GetSystemMetrics(SM_CXFIXEDFRAME);

	/*GetClientRect(hWin,&rc);
	if(rc.bottom>0){ //client height is zero when program started and window is not yet visible
	hc = panelsH - rc.bottom + rc.top;
	GetWindowRect(hWin,&rc);
	hc += rc.bottom-rc.top;
	}else*/
	{
		hc = panelsH + 2*GetSystemMetrics(SM_CYFIXEDFRAME)
			+ GetSystemMetrics(SM_CYMENU) + GetSystemMetrics(SM_CYCAPTION);
	}

	//scale down skin so that the window is not larger than the screen
	SystemParametersInfo(SPI_GETWORKAREA, 0, &rc, 0);
	bw= (rc.right-rc.left-wc)/width;
	bh= (rc.bottom-rc.top-hc)/height;
	aminmax(bw, 4, bfW);
	aminmax(bh, 4, bfH);
	//keep ratio height to width
	if(bfW*bh > bfH*bw){
		bh=bw*bfH/bfW;
	}
	else{
		bw=bh*bfW/bfH;
	}
	//the first line contains smaller squares, the second line contains original bitmaps
	for(int i=0; i<BM_NUM; i++){
		StretchBlt(bmpdc, i*bw, 0, bw, bh, bmpdc, i*bfW, bfH, bfW, bfH, SRCCOPY);
	}
	bmW=bw;
	bmH=bh;
	//set window size and position
	w = wc + max(width*bmW, minWidth*GetDeviceCaps(dc, LOGPIXELSX)/96);
	h = hc + height*bmH;
	x= (rc.right + rc.left - w)/2;
	y= (rc.top + rc.bottom - h)/2;
	if(!IsZoomed(hWin)){
		SetWindowPos(hWin, 0, x, y, w, h,
			(center ? 0 : SWP_NOMOVE)|SWP_NOZORDER|SWP_NOCOPYBITS);
	}
	//calculate coordinates of top left board corner
	getClient(&rc);
	x0= (rc.right-rc.left-width*bmW)/2;
	y0+= (rc.bottom-rc.top-height*bmH)/2;
	//set panels size
	SendMessage(hStatus, WM_SIZE, 0, 0);
	SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
	SendMessage(toolPlay, TB_AUTOSIZE, 0, 0);
	//redraw window
	InvalidateRect(hWin, &rc, 0);
	notResize--;
}
//---------------------------------------------------------------------------
void squareCopy(HDC srcDC, int destI, int srcI)
{
	BitBlt(bmpdc, bfW*destI, bfH, bfW, bfH, srcDC, bfW*srcI, 0, SRCCOPY);
}
//---------------------------------------------------------------------------
//merge two bitmaps
void squareMask(HDC srcDC, int destI, int srcI, int srcG)
{
	int x, y;

	squareCopy(srcDC, destI, srcG);
	srcI *= bfW;
	destI *= bfW;
	COLORREF transp= GetPixel(srcDC, srcI, 0);

	for(x=0; x<bfW; x++)
		for(y=0; y<bfH; y++){
		COLORREF c= GetPixel(srcDC, x+srcI, y);
		if(c!=transp) SetPixel(bmpdc, x+destI, y+bfH, c);
		}
}
//---------------------------------------------------------------------------
//read skin from BITMAP
char *loadSkin(HBITMAP fbmp)
{
	static int D[]={F_NUM+9, F_NUM+7, F_NUM+6, F_NUM+5, F_NUM+4, F_NUM+3, F_NUM+2, F_NUM};
	static bool isWall[] ={1, 1, 1, 0, 1, 0, 0, 0};
	static bool isMover[] ={1, 1, 0, 1, 0, 1, 0, 0};
	static bool isStore[] ={1, 0, 1, 1, 0, 0, 1, 0};
	int i, w, h, typ, bmW1, offset;
	BITMAP bmp;
	char *errs= lng(700, "Cannot create bitmap for skin");

	if(fbmp){
		GetObject(fbmp, sizeof(BITMAP), &bmp);
		w= bmp.bmWidth;
		h= bmp.bmHeight;
		//find whether player has 1 or 4 bitmaps, wall has 1 or 5 bitmaps, object has 2 or 4 bitmaps
		for(typ=0; typ<sizeA(D); typ++){
			if(w%D[typ]==0 && w/D[typ]>=h) break;
		}
		if(typ==sizeA(D)){
			for(typ=0; typ<sizeA(D); typ++){
				if(w%D[typ]==0) break;
			}
		}
		if(typ==sizeA(D)){
			errs=lng(701, "Invalid bitmap width");
		}
		else{
			bmW1= w/D[typ];
			if(bmW1<9 || h<9){
				errs=lng(701, "Invalid bitmap width");
			}
			else{
				HDC fdc= CreateCompatibleDC(dc);
				if(fdc){
					HGDIOBJ oldB= SelectObject(fdc, fbmp);
					HBITMAP bm1= CreateCompatibleBitmap(dc, bmW1*BM_NUM, 2*h);
					if(bm1){
						DeleteObject(SelectObject(bmpdc, bm1));
						bmWall= isWall[typ];
						bmMover= isMover[typ];
						bfW=bmW1;
						bfH=h;
						offset=0;
						squareCopy(fdc, BM_GROUND, F_GROUND);
						squareMask(fdc, BM_OBJECT, F_OBJECT, F_GROUND);
						squareMask(fdc, BM_HILITE, F_HILITE, F_GROUND);
						squareCopy(fdc, BM_STORE, F_STORE);
						if(isStore[typ]){
							squareCopy(fdc, BM_OBJECTSTORE, F_STORE+1);
							squareCopy(fdc, BM_HILITESTORE, F_STORE+2);
							offset+=2;
						}
						else{
							squareMask(fdc, BM_OBJECTSTORE, F_OBJECT, F_STORE);
							squareMask(fdc, BM_HILITESTORE, F_HILITE, F_STORE);
						}
						squareCopy(fdc, BM_BACKGROUND, F_BACKGROUND+offset);
						for(i=0; i<4; i++){
							squareMask(fdc, i+BM_MOVER, i+offset+F_MOVER, F_GROUND);
							squareMask(fdc, i+BM_MOVERSTORE, i+offset+F_MOVER, F_STORE);
							if(!bmMover) break;
						}
						if(bmMover) offset+=3;
						for(i=0; i<5; i++){
							squareCopy(fdc, i+BM_WALL, i+offset+F_WALL);
							if(!bmWall) break;
						}
						bm=bm1;
						resize();
						errs=0;
					}
					DeleteObject(SelectObject(fdc, oldB));
					DeleteDC(fdc);
				}
			}
		}
	}
	return errs;
}
//---------------------------------------------------------------------------
//open skin file, file name is "fnskin"
char *loadSkin()
{
	BITMAPFILEHEADER hdr;
	HANDLE h;
	DWORD r, s;
	char *errs= lng(702, "Cannot load BMP file");

	h= CreateFile(fnskin, GENERIC_READ, FILE_SHARE_READ,
		0, OPEN_EXISTING, 0, 0);
	if(h!=INVALID_HANDLE_VALUE){
		ReadFile(h, &hdr, sizeof(BITMAPFILEHEADER), &r, 0);
		if(r==sizeof(BITMAPFILEHEADER) && ((char*)&hdr.bfType)[0]=='B' && ((char*)&hdr.bfType)[1]=='M'){
			s = GetFileSize(h, 0) - sizeof(BITMAPFILEHEADER);
			char *b= new char[s];
			if(b){
				ReadFile(h, b, s, &r, 0);
				if(r==s){
					BITMAPINFOHEADER* info = (BITMAPINFOHEADER*)b;
					HBITMAP fbmp = CreateDIBitmap(dc, info, CBM_INIT,
						b+hdr.bfOffBits-sizeof(BITMAPFILEHEADER),
						(BITMAPINFO*)b, DIB_RGB_COLORS);
					errs= loadSkin(fbmp);
					if(errs){
						DeleteObject(fbmp);
					}
					else{
						setTitle(fnskin);
					}
				}
				delete[] b;
			}
		}
		CloseHandle(h);
	}
	return errs;
}
//---------------------------------------------------------------------------
int openFileDlg(OPENFILENAME *o)
{
	for(;;){
		o->hwndOwner= hWin;
		o->Flags= OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_READONLY;
		if(GetOpenFileName(o)) return 1; //ok
		if(CommDlgExtendedError()!=FNERR_INVALIDFILENAME
			|| !*o->lpstrFile) return 0; //cancel
		*o->lpstrFile=0;
	}
}
//---------------------------------------------------------------------------
int saveFileDlg(OPENFILENAME *o, bool prompt)
{
	for(;;){
		o->hwndOwner= hWin;
		o->Flags= OFN_PATHMUSTEXIST|OFN_HIDEREADONLY;
		if(prompt) o->Flags|= OFN_OVERWRITEPROMPT;
		if(GetSaveFileName(o)) return 1; //ok
		if(CommDlgExtendedError()!=FNERR_INVALIDFILENAME
			|| !*o->lpstrFile) return 0; //cancel
		*o->lpstrFile=0;
	}
}
//---------------------------------------------------------------------------
void openSkin()
{
	if(openFileDlg(&skinOfn)){
		char *err= loadSkin();
		msg(err);
	}
}
//---------------------------------------------------------------------------
char *onlyExt(char *dest)
{
	char *s;

	if(!*fnskin) strcpy(fnskin, "skins\\");
	strcpy(dest, fnskin);
	for(s=strchr(dest, 0); s>=dest && *s!='\\'; s--);
	s++;
	strcpy(s, "*.bmp");
	return fnskin + (s-dest);
}
//---------------------------------------------------------------------------
void prevSkin()
{
	WIN32_FIND_DATA fd;
	TfileName buf, prev;
	HANDLE h;
	char *t;
	int pass=0;

	t= onlyExt(buf);
	h= FindFirstFile(buf, &fd);
	if(h==INVALID_HANDLE_VALUE){
		openSkin();
	}
	else{
		*prev=0;
		do{
			if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)){
				if(!_stricmp(t, fd.cFileName) || pass>10){
					strcpy(t, prev);
					if(!loadSkin()) break;
				}
				else{
					strcpy(prev, fd.cFileName);
				}
			}
			if(!FindNextFile(h, &fd)){
				FindClose(h);
				h = FindFirstFile(buf, &fd);
				pass++;
			}
		} while(pass<20);
		FindClose(h);
	}
}
//---------------------------------------------------------------------------
void nextSkin()
{
	WIN32_FIND_DATA fd;
	TfileName buf;
	HANDLE h;
	char *t;
	int pass=0;

	t= onlyExt(buf);
	h= FindFirstFile(buf, &fd);
	if(h==INVALID_HANDLE_VALUE){
		openSkin();
	}
	else{
		do{
			if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)){
				if(pass){
					strcpy(t, fd.cFileName);
					if(!loadSkin()) break;
				}
				else if(!_stricmp(t, fd.cFileName)) pass++;
			}
			if(!FindNextFile(h, &fd)){
				FindClose(h);
				h = FindFirstFile(buf, &fd);
				pass++;
			}
		} while(pass<3);
		FindClose(h);
	}
}
//---------------------------------------------------------------------------
//delete registry settings
void deleteini()
{
	HKEY key;
	DWORD i;

	delreg=true;
	if(RegDeleteKey(HKEY_CURRENT_USER, subkey)==ERROR_SUCCESS){
		if(RegOpenKey(HKEY_CURRENT_USER,
			"Software\\Petr Lastovicka", &key)==ERROR_SUCCESS){
			i=1;
			RegQueryInfoKey(key, 0, 0, 0, &i, 0, 0, 0, 0, 0, 0, 0);
			RegCloseKey(key);
			if(!i)
				RegDeleteKey(HKEY_CURRENT_USER, "Software\\Petr Lastovicka");
		}
	}
}

//save settings
void writeini()
{
	HKEY key;
	if(RegCreateKey(HKEY_CURRENT_USER, subkey, &key)!=ERROR_SUCCESS)
		msg(lng(735, "Cannot write to Windows registry"));
	else{
		for(Treg *u=regVal; u<endA(regVal); u++){
			RegSetValueEx(key, u->s, 0, REG_DWORD,
				(BYTE *)u->i, sizeof(int));
		}

		TfileName buf;
		getExeDir(buf, "");
		int len = (int)strlen(buf);
		for(Tregs *v=regValS; v<endA(regValS); v++){
			char *s = v->i;
			if(v->isPath && !_strnicmp(buf, v->i, len)) s += len; //convert absolute path to relative path
			RegSetValueEx(key, v->s, 0, REG_SZ, (BYTE *)s, (int)strlen(s)+1);
		}

		RegCloseKey(key);
	}
}

//read settings
void readini()
{
	HKEY key;
	DWORD d;
	if(RegOpenKey(HKEY_CURRENT_USER, subkey, &key)==ERROR_SUCCESS){
		for(Treg *u=regVal; u<endA(regVal); u++){
			d=sizeof(int);
			RegQueryValueEx(key, u->s, 0, 0, (BYTE *)u->i, &d);
		}

		char buf[192+sizeof(TfileName)];
		getExeDir(buf, "");
		int len = (int)strlen(buf);
		for(Tregs *v=regValS; v<endA(regValS); v++){
			d=v->n;
			RegQueryValueEx(key, v->s, 0, 0, (BYTE *)v->i, &d);
			if(v->isPath && v->i[0] && v->i[1]!=':' && v->i[0]!='\\'){
				//convert relative path to absolute path
				strcat(buf, v->i);
				lstrcpyn(v->i, buf, v->n);
				buf[len]=0;
			}
		}

		RegCloseKey(key);
	}
}

//save levels, solutions and settings
void saveAtExit()
{
	saveUser();
	saveData();
	if(!delreg) writeini();
}
//---------------------------------------------------------------------------
//initialize undo structure
void prepareUndo()
{
	undoPos->mover=mover;
	undoPos->pushBeg=0;
	undoPos->mov=1;
	undoPos->pus=0;
	redoPos=0;
	justSelected=false;
}
//---------------------------------------------------------------------------
//write one character to solution buffer
void wrLog(char ch)
{
	if(!logPos || logPos>=endA(logbuf)) return;
	if(logPos-2 >= logbuf && logPos[-1]==ch){
		if(logPos[-2]==ch && (logPos-3<logbuf || logPos[-3]!='Z')){
			logPos[-2]='C';
			return;
		}
		if(logPos[-2]>='A' && logPos[-2]<'Z'){
			logPos[-2]++;
			return;
		}
	}
	*logPos++ = ch;
}
//---------------------------------------------------------------------------
//move one square in direction "direct"
bool move(int direct, bool setUndo)
{
	moverDirect=direct;
	Psquare old=mover;
	mover= nxtP(mover, direct);
	Psquare next= nxtP(mover, direct);
	if(mover->obj==BM_WALL || mover->obj==BM_OBJECT && next->obj!=BM_GROUND){
		//wall collision
		mover=old;
		paintSquare(old); //change player direction
		movError++;
		return false;
	}
	//write undo
	if(setUndo){
		prepareUndo();
		undoPos->mover=old;
		if(mover->obj!=BM_GROUND){
			undoPos->pushBeg=mover;
			undoPos->pushEnd=next;
			undoPos->pus=1;
		}
		undoPos++;
	}
	paintSquareO(old);
	char ch= char(direct + '0');
	if(mover->obj!=BM_GROUND){
		//push object
		ch= char(direct + '4');
		next->obj= BM_OBJECT;
		mover->obj= BM_GROUND;
		paintSquare(next);
		pushes++;
	}
	if(undoing) moves--; else moves++;
	wrLog(ch);
	paintSquareO(mover);
	//update status bar
	statusMoves();
	return true;
}
//---------------------------------------------------------------------------
//go through path which was found by function findDist
void move1(Psquare dest)
{
	for(int i=0; i<4; i++){
		Psquare p= nxtP(dest, i);
		if(p->dist == dest->dist - 1){
			if(p->dist!=0){
				move1(p);
				sleep();
			}
			move(i^1);
			return;
		}
	}
}
//---------------------------------------------------------------------------
//similar to move1, but used by undo
void move1R()
{
	int i;
	Psquare p, dest;

	for(dest=mover;; dest=p){
		for(i=0; i<4; i++){
			p= nxtP(dest, i);
			if(p->dist == dest->dist - 1) break;
		}
		move(i);
		if(p->dist==0) break;
		sleep();
	}
}
//---------------------------------------------------------------------------
//find the shortest path to square "dest"
//return 0 if path does not exist
int move(Psquare dest)
{
	if(!dest || dest->obj!=BM_GROUND) return 0;
	if(dest==mover) return 1;
	findDist(mover, dest);
	if(dest->distId!=distId) return 0;
	move1(dest);
	return 2;
}
//---------------------------------------------------------------------------
//similar to move, but used by undo
int moveR(Psquare dest)
{
	if(!dest || dest->obj!=BM_GROUND) return 0;
	if(dest==mover) return 1;
	findDist(dest, mover);
	if(mover->distId!=distId) return 0;
	move1R();
	return 2;
}
//---------------------------------------------------------------------------
//push object to dest,
//path must have been found by movePush0
void push1(Psquare dest, int direct)
{
	Psquare pn, pnn;

	pn=nxtP(dest, direct);
	if(dest->obj==BM_OBJECT){
		if(move(pn)) return;
	}
	pnn=nxtP(pn, direct);
	push1(pn, dest->pushDirect[direct]);
	sleep();
	if(pnn!=mover){
		move(pnn);
		sleep();
	}
	move(direct^1);
}
//---------------------------------------------------------------------------
//pull object from "dest" to square beside mover
//path must have been found by movePush0
//"direct" is direction from "dest" to mover
void push1R(Psquare dest, int direct)
{
	Psquare pn;
	int im;

	for(;;){
		pn=nxtP(dest, direct);
		im=dest->pushDirect[direct];
		dest->obj=BM_GROUND;
		mover=nxtP(pn, direct);
		paintSquare(dest);
		pn->obj=BM_OBJECT;
		dest=pn;
		moverDirect=im;
		direct=im;
		paintSquareO(mover);
		paintSquareO(dest);
		moves--;
		pushes--;
		statusMoves();
		pn=nxtP(dest, im);
		if(pn!=mover){
			sleep();
			moveR(pn);
		}
		if(dest->pushDirect[direct]==9) break;
		sleep();
	}
}
//---------------------------------------------------------------------------
//find the shortest path to push object from pBegin to pEnd,
//result is in Square.pushDirect,
//returns direction or -1
int movePush0(Psquare pBegin, Psquare pEnd)
{
	int i, im, e;
	short m;
	Psquare mover0, p, pn, pp, pobj=0;
	const int MAXEVAL=MAXSHORT;

	if(!(pEnd && pBegin && pEnd->obj==BM_GROUND && pBegin->obj==BM_OBJECT)){
		return -1;
	}
	mover0=mover;
	for(p=board; p<boardk; p++){
		p->cont[0]=p->cont[1]=p->cont[2]=p->cont[3]=MAXEVAL;
		p->finalDist=1;
	}
	//find path from player to object
	for(i=0; i<4; i++){
		p= nxtP(pBegin, i);
		findDist(mover, p);
		if(p->distId==distId){
			pBegin->cont[i]= (short)p->dist;
			pBegin->pushDirect[i]=(char)9;
		}
	}
	pBegin->finalDist=0;
	pBegin->obj=BM_GROUND;
	for(;;){
		//find not final position which has minimal distance
		m=MAXEVAL;
		for(p=board; p<boardk; p++){
			if(!p->finalDist){
				for(i=0; i<4; i++){
					if(p->cont[i]<m && p->cont[i]>=0){
						m=p->cont[i];
						im=i;
						pobj=p;
					}
				}
			}
		}
		if(m==MAXEVAL){
			//path not found
			pBegin->obj=BM_OBJECT;
			mover=mover0;
			return -1;
		}
		pobj->cont[im] -= (short)MAXEVAL; //final evaluation
		for(i=0;; i++){
			if(pobj->cont[i]>=0 && pobj->cont[i]<MAXEVAL) break;
			if(i==3){ pobj->finalDist=1; break; }
		}
		if(pobj==pEnd) break; //path found
		//temporarily put object and player to current position
		mover=nxtP(pobj, im);
		pobj->obj=BM_OBJECT;
		//decrease distance of adjacent positions
		for(i=0; i<4; i++){
			pn=prvP(pobj, i);
			if(pn->obj==BM_GROUND){
				pp=nxtP(pobj, i);
				findDist(mover, pp);
				if(pp->distId==distId){
					e=m+2+pp->dist;
					if(pn->cont[i]>e){
						pn->cont[i]=(short)e;
						pn->pushDirect[i]=(char)im;
						pn->finalDist=0;
					}
				}
			}
		}
		pobj->obj=BM_GROUND;
	}
	pBegin->obj=BM_OBJECT;
	mover=mover0;
	return im;
}
//---------------------------------------------------------------------------
//push object from pBegin to pEnd (find the shortest path)
bool movePush(Psquare pBegin, Psquare pEnd, bool setUndo=false)
{
	int im, mov0, pus0;

	im= movePush0(pBegin, pEnd);
	if(im>=0){
		if(setUndo){
			prepareUndo();
			undoPos->pushBeg= pBegin;
			undoPos->pushEnd= pEnd;
			mov0=moves;
			pus0=pushes;
		}
		push1(pEnd, im);
		if(setUndo){
			undoPos->mov= short(moves-mov0);
			undoPos->pus= short(pushes-pus0);
			undoPos++;
		}
		return true;
	}
	return false;
}
//---------------------------------------------------------------------------
bool movePushR(Psquare pBegin, Psquare pEnd)
{
	pEnd->obj=BM_GROUND;
	pBegin->obj=BM_OBJECT;
	int im= movePush0(pBegin, pEnd);
	pBegin->obj=BM_GROUND;
	pEnd->obj=BM_OBJECT;
	if(im>=0){
		push1R(pEnd, im);
		return true;
	}
	return false;
}
//---------------------------------------------------------------------------
//find out diretion between pBegin and pEnd
//return -1 if they are not in the same line
int direction(Psquare pBegin, Psquare pEnd)
{
	int dir=-1;
	if(pEnd){
		if(pEnd->x==pBegin->x){
			if(pEnd->y > pBegin->y) dir=3;
			else dir=2;
		}
		else if(pEnd->y==pBegin->y){
			if(pEnd->x > pBegin->x) dir=1;
			else dir=0;
		}
	}
	return dir;
}
//---------------------------------------------------------------------------
//find an object between player and "pEnd"
Psquare lineObj(Psquare pEnd)
{
	Psquare p, pObj=0;

	if(pEnd && pEnd->obj==BM_GROUND){
		int dir=direction(mover, pEnd);
		if(dir>=0){
			int n=0;
			for(p=mover; p!=pEnd; p=nxtP(p, dir)){
				if(p->obj==BM_OBJECT){
					pObj=p;
					n++;
				}
				else if(p->obj!=BM_GROUND){
					return 0;
				}
			}
			if(n==1) return pObj;
		}
	}
	return 0;
}
//---------------------------------------------------------------------------
void setMover(Psquare pos)
{
	if(pos!=mover){
		Psquare old=mover;
		mover=pos;
		paintSquare(old);
	}
}
//---------------------------------------------------------------------------
void xchgEd()
{
	int x, y;
	char o;
	bool s;

	x= mover->x;
	y= mover->y;
	Psquare p= square(edUndo->x, edUndo->y);
	if(edUndo->obj==BM_MOVER){
		//set player position
		setMover(p);
		edUndo->x= x;
		edUndo->y= y;
	}
	else if(edUndo->obj==BM_MOVER+1){
		//push object
		Psquare next= nxtP(p, direction(mover, p));
		next->obj = BM_OBJECT;
		paintSquare(next);
		p->obj=BM_GROUND;
		setMover(p);
		edUndo->x= x;
		edUndo->y= y;
		edUndo->obj=BM_MOVER+2;
	}
	else if(edUndo->obj==BM_MOVER+2){
		//pull object
		Psquare next= nxtP(mover, direction(p, mover));
		next->obj = BM_GROUND;
		paintSquare(next);
		mover->obj=BM_OBJECT;
		setMover(p);
		edUndo->x= x;
		edUndo->y= y;
		edUndo->obj=BM_MOVER+1;
	}
	else{
		o= p->obj;
		s= p->store;
		p->obj= edUndo->obj;
		p->store= edUndo->store;
		edUndo->obj=o;
		edUndo->store=s;
	}
	paintSquare9(p);
}
//---------------------------------------------------------------------------
bool undo()
{
	if(editing){
		if(edUndo<=edRec) return false;
		if(!edRedo) edRedo=edUndo;
		edUndo--;
		xchgEd();
	}
	else{
		if(undoPos<=rec) return false;
		undoPos->mover=mover;
		if(!redoPos) redoPos=undoPos;
		undoPos--;
		Psquare pb=undoPos->pushBeg, pe=undoPos->pushEnd;
		undoing=true;
		if(notdraw){
			if(pb){
				pb->obj=BM_OBJECT;
				pe->obj=BM_GROUND;
			}
			mover= undoPos->mover;
			moves -= undoPos->mov;
			pushes -= undoPos->pus;
		}
		else{
			if(pb){
				bool isSel= pe==selected;
				mover=undoPos->mover;
				movePushR(pb, pe);
				if(isSel) setSelected(pb);
				if(mover!=undoPos->mover) sleep();
			}
			moveR(undoPos->mover);
		}
		undoing=false;
	}
	return true;
}
//---------------------------------------------------------------------------
void undoAll()
{
	notdraw++;
	while(undo());
	notdraw--;
}

void undoPus()
{
	if(editing){
		while(undo() && edUndo->obj==BM_MOVER) sleep();
	}
	else{
		do{
			undo();
			sleep();
		} while(undoPos>rec && !(undoPos-1)->pushBeg);
	}
}

void undo2()
{
	if(GetKeyState(VK_CONTROL)>=0){
		undoPus();
	}
	else{
		undo();
	}
}
//---------------------------------------------------------------------------
bool redo()
{
	if(editing){
		if(!edRedo || edRedo==edUndo) return false;
		xchgEd();
		edUndo++;
	}
	else{
		if(!redoPos || redoPos==undoPos) return false;
		Psquare pb=undoPos->pushBeg, pe=undoPos->pushEnd;
		if(notdraw && !logPos){
			if(pb){
				pb->obj=BM_GROUND;
				pe->obj=BM_OBJECT;
			}
			mover= undoPos[1].mover;
			moves += undoPos->mov;
			pushes += undoPos->pus;
		}
		else{
			if(pb){
				bool isSel= pb==selected;
				movePush(pb, pe);
				if(isSel) setSelected(pe);
			}
			else{
				move(undoPos[1].mover);
			}
		}
		undoPos++;
	}
	return true;
}
//---------------------------------------------------------------------------
void redoAll()
{
	notdraw++;
	while(redo());
	notdraw--;
}

void redoPus()
{
	if(editing){
		while(redo() && (edUndo-1)->obj==BM_MOVER) sleep();
	}
	else{
		int pus=pushes;
		while(redo() && pushes==pus) sleep();
	}
}

void redo2()
{
	if(GetKeyState(VK_CONTROL)>=0){
		redoPus();
	}
	else{
		redo();
	}
}
//---------------------------------------------------------------------------
//check undo buffer overflow
bool fullUndo()
{
	bool result= undoPos>=endA(rec)-1;
	if(result){
		msg(lng(800, "Too many moves"));
		movError++;
	}
	return result;
}
//---------------------------------------------------------------------------
//move player one square
bool moveK(int direct)
{
	if(fullUndo()) return false;
	if(undoPos>rec && undoPos[-1].mover==nxtP(mover, direct) &&
		!undoPos[-1].pushBeg){
		undo();
		return true;
	}
	if(redoPos && redoPos>undoPos && undoPos[1].mover==nxtP(mover, direct)
		&& (!undoPos->pushBeg || undoPos->pushBeg==undoPos[1].mover &&
		undoPos->pushEnd==nxtP(undoPos->pushBeg, direct))){
		redo();
		return true;
	}
	return move(direct, true);
}
//---------------------------------------------------------------------------
//move player by mouse to position "p"
void moveM(Psquare p)
{
	if(fullUndo()) return; //buffer overflow
	prepareUndo();
	int mov0=moves;
	if(move(p) > 1){
		undoPos->mov= short(moves-mov0);
		undoPos++;
	}
}
//---------------------------------------------------------------------------
//move player by keyboard arrows
void moveK2(int direct)
{
	Psquare p;
	int d1, d2;
	static int Y[]={0, 1, 3, 2, 6, 7, 4, 5};
	static int X[]={1, 0, 2, 3, 5, 4, 7, 6};

	if(fullUndo()) return;
	if(xtrans) direct=X[direct];
	if(ytrans) direct=Y[direct];
	if(direct>3){
		//move diagonally
		d1= direct & 1;
		d2= direct >> 1;
		if(nxtP(mover, d1)->obj==BM_GROUND || nxtP(mover, d2)->obj==BM_WALL){
			moveK(d1);
			moveK(d2);
		}
		else{
			moveK(d2);
			moveK(d1);
		}
	}
	else if(GetKeyState(VK_CONTROL)<0){
		//go to wall or object if CTRL is pressed
		for(p=mover; p->obj==BM_GROUND; p=nxtP(p, direct));
		moveM(prvP(p, direct));
	}
	else if(GetKeyState(VK_SHIFT)<0){
		//push object to wall if SHIFT is pressed
		int Nobj=0;
		Psquare o=0;
		for(p=mover;; p=nxtP(p, direct)){
			if(p->obj==BM_OBJECT){
				Nobj++;
				if(Nobj>1) break;
				o=p;
			}
			else if(p->obj!=BM_GROUND) break;
		}
		p=prvP(p, direct);
		if(o){
			if(o!=p) movePush(o, p, true); //push object
			else moveM(prvP(p, direct));  //go to object which is at wall
		}
		else{
			//there is no object, go to wall
			moveM(p);
		}
	}
	else{
		//one move or push
		moveK(direct);
	}
}
//---------------------------------------------------------------------------
//check editor buffer overflow
void fullEdUndo()
{
	if(edUndo==endA(edRec)-1){
		//buffer for undo is full
		memmove(edRec, edRec+1, sizeof(edRec)-sizeof(EdUndo));
		edUndo--;
	}
}
//---------------------------------------------------------------------------
bool moveE(int direct)
{
	moverDirect=direct;
	Psquare old=mover;
	mover= nxtP(mover, direct);
	Psquare prev= prvP(old, direct);
	if(mover->obj!=BM_GROUND){
		mover=old;
		return false;
	}
	//write undo
	fullEdUndo();
	edUndo->x= old->x;
	edUndo->y= old->y;
	edUndo->obj= BM_MOVER;
	if(prev->obj==BM_OBJECT && GetKeyState(VK_CONTROL)<0){
		//push
		old->obj= BM_OBJECT;
		prev->obj= BM_GROUND;
		paintSquare(prev);
		edUndo->obj++;
	}
	paintSquare(old);
	paintSquare(mover);
	edUndo++;
	edRedo=0;
	return true;
}
//---------------------------------------------------------------------------
//press solutions player button
void replayBtn(int cmd)
{
	if(replay==cmd) return;
	if(!replay){
		//show toolbar
		ShowWindow(toolbar, SW_HIDE);
		ShowWindow(toolPlay, SW_SHOW);
		replay=cmd;
		resize();
	}
	//press toolbar button
	SendMessage(toolPlay, TB_CHECKBUTTON, replay, MAKELONG(FALSE, 0));
	SendMessage(toolPlay, TB_CHECKBUTTON, cmd, MAKELONG(TRUE, 0));
	replay=cmd;
}
//---------------------------------------------------------------------------
//mouse button in editor
void editMouse(Psquare pos, WPARAM wP)
{
	fullEdUndo();
	edUndo->x= pos->x;
	edUndo->y= pos->y;
	edUndo->obj= pos->obj;
	edUndo->store= pos->store;
	if(wP & MK_CONTROL){
		//set player position
		edUndo->x= mover->x;
		edUndo->y= mover->y;
		edUndo->obj= BM_MOVER;
		setMover(pos);
	}
	else if(wP & MK_SHIFT){
		//create or delete store
		pos->store= !pos->store;
	}
	else{
		if(ldown)
			//left button -> construct or destroy wall
				pos->obj= char(pos->obj==BM_WALL ? BM_GROUND : BM_WALL);
		else
			//right button -> create or remove object
			pos->obj= char(pos->obj==BM_OBJECT ? BM_GROUND : BM_OBJECT);
	}
	for(int i=0; i<9; i++){
		paintSquare(nxtP(pos, i));
	}
	edUndo++;
	edRedo=0;
}
//---------------------------------------------------------------------------
//move mouse in editor
void editMouse(WPARAM wP, LPARAM lP)
{
	if(!ldown && !rdown) return; //mouse buttons not pressed
	Psquare pos= SquareXY(LOWORD(lP), HIWORD(lP));
	if(!pos || pos->x<2 || pos->y<2 || pos->x>width-3 || pos->y>height-3 ||
		pos==editPos) return;
	//make changes to squares between last and new mouse position
	int dir=direction(pos, editPos);
	for(Psquare p=pos; p!=editPos; p=nxtP(p, dir)){
		editMouse(p, wP);
		if(dir<0) break;
	}
	editPos= pos;
}
//---------------------------------------------------------------------------
int editDisabled[]= //menu items which are disabled in level editor
{112, 115, 204, 206, 217, 401, 402, 403, 405};

void setMenuName(int cmd, char *s)
{
	MENUITEMINFO mii;
	mii.cbSize=sizeof(MENUITEMINFO);
	mii.fMask=MIIM_TYPE;
	mii.fType=MFT_STRING;
	mii.dwTypeData= s;
	SetMenuItemInfo(GetMenu(hWin), cmd, FALSE, &mii);
}

void editMenu()
{
	setMenuName(114, editing ? lng(555, "Don&e\tEnter") :
		lng(556, "&Edit\tCtrl+E"));
	setMenuName(404, editing ? lng(557, "&Cancel edit\tEsc") :
		lng(558, "&Reset\tB"));
	for(int *u=editDisabled; u<endA(editDisabled); u++){
		EnableMenuItem(GetMenu(hWin), *u, MF_BYCOMMAND |
			(editing ? MF_GRAYED : MF_ENABLED));
	}
}

//exit editor
void cancelEdit()
{
	playtime=0;
	resetLevel();
	editing=false;
	setTitle(0);
	editMenu();
}

//enter editor
void startEdit()
{
	fillOuter();
	resize();
	editing=true;
	setTitle(0);
	editMenu();
}

void stop()
{
	if(replay){
		KillTimer(hWin, 200);
		replayBtn(205);
	}
}
//---------------------------------------------------------------------------
//advance time by 1 second
void clock()
{
	if(IsIconic(hWin)) return; //window is minimalized
	if(editing || replay){
		//don't display time in editor or solutions player
		status(4, "");
	}
	else{
		playtime++;
		if(!stopTime){
			//show time on status bar
			int t=playtime;
			status(4, "%02d:%02d:%02d", t/3600, (t/60)%60, t%60);
		}
	}
}
//---------------------------------------------------------------------------
void langChanged()
{
	static int subId[]={606, 605, 604, 603, 602, 600, 601};
	loadMenu(hWin, "MENU1", subId);
	editMenu();
	DrawMenuBar(hWin);
	//update status bar
	status();
	//change texts in open/save dialogs
	savOfn.lpstrFilter=lng(801, "Saved positions (*.sav)\0*.sav\0All files\0*.*\0");
	userOfn.lpstrFilter=lng(802, "Player's solutions (*.rec)\0*.rec\0All files\0*.*\0");
	skinOfn.lpstrFilter=lng(803, "Skins (*.bmp)\0*.bmp\0");
	dataOfn.lpstrFilter=lng(804, "Levels database\0*.*\0");
	levelOfn.lpstrFilter=lng(805, "Sokoban levels (*.xsb)\0*.xsb\0Text files (*.txt)\0*.txt\0All files\0*.*\0");
}
//---------------------------------------------------------------------------
//dialog Options
BOOL CALLBACK OptionsProc(HWND hWnd, UINT mesg, WPARAM wP, LPARAM)
{
	static struct{ int *prom, id; } D[]={
			{&moveDelay, 101},
			{&playTimer, 102},
			{&fastTimer, 103},
			{&toolBarVisible, 511},
			{&statusBarVisible, 512},
			{&center, 513},
			{&nowalls, 514},
			{&noobjects, 515},
	};
	int i;

	switch(mesg){
		case WM_INITDIALOG:
			setDlgTexts(hWnd, 510);
			for(i=0; i<sizeof(D)/sizeof(*D); i++){
				if(D[i].id>=300){
					CheckDlgButton(hWnd, D[i].id, *D[i].prom ? BST_CHECKED : BST_UNCHECKED);
				}
				else{
					SetDlgItemInt(hWnd, D[i].id, *D[i].prom, FALSE);
				}
			}
			CheckRadioButton(hWnd, 560, 562, 560+nextAction);
			return TRUE;

		case WM_COMMAND:
			wP=LOWORD(wP);
			switch(wP){
				case IDOK:
				{
					for(i=0; i<sizeof(D)/sizeof(*D); i++){
						if(D[i].id>=300){
							*D[i].prom= IsDlgButtonChecked(hWnd, D[i].id);
						}
						else{
							*D[i].prom= GetDlgItemInt(hWnd, D[i].id, NULL, FALSE);
						}
					}
					nextAction= getRadioButton(hWnd, 560, 562);
					ShowWindow(toolbar, toolBarVisible && !replay ? SW_SHOW : SW_HIDE);
					ShowWindow(hStatus, statusBarVisible ? SW_SHOW : SW_HIDE);
					resize();
				}
				case IDCANCEL:
					EndDialog(hWnd, wP);
					return TRUE;
			}
			break;
	}
	return FALSE;
}
//---------------------------------------------------------------------------
//dialog Go to level
BOOL CALLBACK GotoProc(HWND hWnd, UINT mesg, WPARAM wP, LPARAM)
{
	switch(mesg){
		case WM_INITDIALOG:
			setDlgTexts(hWnd, 519);
			SetDlgItemInt(hWnd, 101, prevLevel+1, FALSE);
			return TRUE;
		case WM_COMMAND:
			wP=LOWORD(wP);
			switch(wP){
				case IDOK:
					prevLevel=level;
					loadLevel(GetDlgItemInt(hWnd, 101, NULL, FALSE)-1);
				case IDCANCEL:
					EndDialog(hWnd, wP);
					return TRUE;
			}
			break;
	}
	return FALSE;
}
//---------------------------------------------------------------------------
//dialog Go to move
BOOL CALLBACK MovProc(HWND hWnd, UINT mesg, WPARAM wP, LPARAM)
{
	switch(mesg){
		case WM_INITDIALOG:
			setDlgTexts(hWnd, 521);
			SetDlgItemInt(hWnd, 101, moves, FALSE);
			return TRUE;
		case WM_COMMAND:
			wP=LOWORD(wP);
			switch(wP){
				case IDOK:
				{
					int mov= GetDlgItemInt(hWnd, 101, NULL, FALSE);
					notdraw++;
					undoAll();
					while(moves<mov && redo());
					notdraw--;
					update();
				}
				case IDCANCEL:
					EndDialog(hWnd, wP);
					return TRUE;
			}
			break;
	}
	return FALSE;
}
//---------------------------------------------------------------------------
DWORD getVer()
{
	HRSRC r;
	HGLOBAL h;
	void *s;
	VS_FIXEDFILEINFO *v;
	UINT i;

	r=FindResource(0, (char*)VS_VERSION_INFO, RT_VERSION);
	h=LoadResource(0, r);
	s=LockResource(h);
	if(!s || !VerQueryValue(s, "\\", (void**)&v, &i)) return 0;
	return v->dwFileVersionMS;
}

BOOL CALLBACK AboutProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM)
{
	char buf[64];
	DWORD d;

	switch(msg){
		case WM_INITDIALOG:
			setDlgTexts(hWnd, 11);
			d=getVer();
			sprintf(buf, "%d.%d", HIWORD(d), LOWORD(d));
			SetDlgItemText(hWnd, 101, buf);
			return TRUE;

		case WM_COMMAND:
			int cmd=LOWORD(wP);
			switch(cmd){
				case 123:
					GetDlgItemTextA(hWnd, cmd, buf, sizeA(buf)-13);
					if(!strcmp(lang, "English")) strcat(buf, "/indexEN.html");
					ShellExecuteA(0, 0, buf, 0, 0, SW_SHOWNORMAL);
				case IDOK:
				case IDCANCEL:
					EndDialog(hWnd, wP);
					return TRUE;
			}
			break;
	}
	return 0;
}
//---------------------------------------------------------------------------
//dialog Author name
BOOL CALLBACK AuthorProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM)
{
	switch(msg){
		case WM_INITDIALOG:
			setDlgTexts(hWnd, 523);
			SetDlgItemText(hWnd, 101, curAuthor);
			CheckRadioButton(hWnd, 525, 526, 525+clone);
			return TRUE;

		case WM_COMMAND:
			wP=LOWORD(wP);
			switch(wP){
				case IDOK:
					GetDlgItemText(hWnd, 101, curAuthor, sizeof(curAuthor));
					clone= getRadioButton(hWnd, 525, 526);
				case IDCANCEL:
					EndDialog(hWnd, wP);
					return TRUE;
			}
			break;
	}
	return 0;
}
//---------------------------------------------------------------------------
int countSolved()
{
	int solved=0;
	for(int i=0; i<Nlevels; i++){
		Level *lev= &levoff[i];
		if(lev->user.Mmoves) solved++;
	}
	return solved;
}
//---------------------------------------------------------------------------
int sortLevel(const void *a, const void *b)
{
	return descending*int((*(Level**)b)-(*(Level**)a));
}
int sortBest(const void *a, const void *b)
{
	return descending*((*(Level**)a)->best.eval() - (*(Level**)b)->best.eval());
}
int sortUser(const void *a, const void *b)
{
	return descending*((*(Level**)a)->user.eval() - (*(Level**)b)->user.eval());
}
int sortEq(const void *a, const void *b)
{
	return descending*((*(Level**)b)->user.eval()-(*(Level**)b)->best.eval()
		-(*(Level**)a)->user.eval()+(*(Level**)a)->best.eval());
}
int sortDim(const void *a, const void *b)
{
	return descending*((*(Level**)b)->i-(*(Level**)a)->i);
}
int sortAuthor(const void *a, const void *b)
{
	return descending*_stricmp((*(Level**)b)->author, (*(Level**)a)->author);
}
int sortObj(const void *a, const void *b)
{
	return descending*((*(Level**)b)->i-(*(Level**)a)->i);
}

void sortList()
{
	Level *lev;
	int i;
	static int(*F[])(const void*, const void*)={sortLevel, sortBest, sortEq, sortUser, sortObj, sortDim, sortAuthor};

	for(i=0; i<Nlevels; i++){
		lev= &levoff[i];
		A[i]=lev;
		getDim(lev->offset, lev->width, lev->height);
		if(F[sortedCol]==sortDim){
			lev->i = lev->width*lev->height;
		}
		if(F[sortedCol]==sortObj){
			lev->i = getNobj(lev->offset);
		}
	}
	qsort(A, Nlevels, sizeof(Level*), F[sortedCol]);
}

//---------------------------------------------------------------------------
static bool helpVisible;

void showHelp()
{
	char buf[MAX_PATH], buf2[MAX_PATH+24];

	getExeDir(buf, lng(13, "sokoban.chm"));
	//if ZIP file has been extracted by Explorer, CHM has internet zone identifier which must be deleted before displaying help
	sprintf(buf2, "%s:Zone.Identifier:$DATA", buf);
	DeleteFile(buf2); //delete only alternate data stream
	if(HtmlHelp(hWin, buf, 0, 0)) helpVisible=true;
}

//---------------------------------------------------------------------------
//levels list
BOOL CALLBACK LevelsProc(HWND hWnd, UINT mesg, WPARAM wP, LPARAM lP)
{
	static char *colNames[]=
	{"Level", "Best solution", "", "Your solution", "Objects", "Dimension", "Author"};

	static Level *first, *last;
	int item, notif, i, y;
	HWND listBox = GetDlgItem(hWnd, 101);
	HWND header = GetDlgItem(hWnd, 102);
	RECT rc;
	HD_LAYOUT hdl;
	WINDOWPOS wp;
	TEXTMETRIC tm;
	HD_ITEM hdi;
	DRAWITEMSTRUCT *lpdis;
	MEASUREITEMSTRUCT *lpmis;
	Level *lev;
	char buf[128];

	switch(mesg){
		case WM_INITDIALOG:
			setDlgTexts(hWnd, 527);
			A=new Level*[Nlevels];
			sortList();
			//create header control
			header= CreateWindowEx(0, WC_HEADER, 0,
				WS_CHILD | WS_BORDER | HDS_BUTTONS | HDS_HORZ,
				0, 0, 0, 0, hWnd, (HMENU)102, inst, 0);
			SendMessage(header, WM_SETFONT, SendMessage(hWnd, WM_GETFONT, 0, 0), 0);
			//columns names
			for(i=0; i<sizeA(colNames); i++){
				hdi.mask = HDI_TEXT | HDI_FORMAT | HDI_WIDTH;
				hdi.pszText = lng(530+colOrder[i], colNames[colOrder[i]]);
				hdi.cchTextMax = (int)strlen(hdi.pszText);
				hdi.cxy = colWidth[i];
				hdi.fmt = HDF_LEFT | HDF_STRING;
				Header_InsertItem(header, i, &hdi);
			}
			//count solved levels
			SetDlgItemInt(hWnd, 104, countSolved(), FALSE);
			//set window size
			MoveWindow(hWnd, levDlgX, levDlgY, 10, 10, FALSE); //needed to call WM_SIZE
			MoveWindow(hWnd, levDlgX, levDlgY, levDlgW, levDlgH, FALSE);
			//cursor in the middle 
			SendMessage(listBox, LB_SETCOUNT, Nlevels, 0);
			for(i=0; i<Nlevels; i++){
				if(A[i]==&levoff[level]){
					SendMessage(listBox, LB_SETCURSEL, i+6, 0);
					SendMessage(listBox, LB_SETCURSEL, i, 0);
					break;
				}
			}
			first=last=0;
			return TRUE;

		case WM_GETMINMAXINFO:
			((MINMAXINFO FAR*) lP)->ptMinTrackSize.x = 430;
			((MINMAXINFO FAR*) lP)->ptMinTrackSize.y = 160;
			break;

		case WM_MEASUREITEM:
			lpmis = (LPMEASUREITEMSTRUCT)lP;
			lpmis->itemHeight = HIWORD(GetDialogBaseUnits())+1;
			return TRUE;

		case WM_DRAWITEM:
			lpdis = (LPDRAWITEMSTRUCT)lP;
			if(lpdis->itemID == -1) break;
			if(lpdis->itemAction==ODA_DRAWENTIRE || lpdis->itemAction==ODA_SELECT){
				SelectObject(lpdis->hDC, GetStockObject(NULL_BRUSH));
				SelectObject(lpdis->hDC, GetStockObject(WHITE_PEN));
				Rectangle(lpdis->hDC, lpdis->rcItem.left, lpdis->rcItem.top,
					lpdis->rcItem.right, lpdis->rcItem.bottom);
			}
			switch(lpdis->itemAction){
				case ODA_DRAWENTIRE:
					lev= A[lpdis->itemID];
					GetTextMetrics(lpdis->hDC, &tm);
					rc.top= lpdis->rcItem.top;
					rc.bottom= lpdis->rcItem.bottom;
					rc.right=4;
					for(i=0; i<sizeA(colWidth); i++){
						switch(colOrder[i]){
							case 0:
								sprintf(buf, "%d", lev-levoff+1);
								break;
							case 1:
								sprintf(buf, "%d - %d", lev->best.Mmoves, lev->best.Mpushes);
								break;
							case 2:
								buf[0]=0;
								if(lev->best.Mmoves && lev->user.Mmoves){
									if(lev->best.eval()==lev->user.eval()) buf[0]= '=';
								}
								buf[1]=0;
								break;
							case 3:
								sprintf(buf, "%d - %d", lev->user.Mmoves, lev->user.Mpushes);
								break;
							case 4:
								sprintf(buf, "%d", getNobj(lev->offset));
								break;
							case 5:
								sprintf(buf, "%d x %d", lev->width, lev->height);
								break;
							case 6:
								lstrcpyn(buf, lev->author, sizeA(buf));
								break;
						}
						rc.left=rc.right;
						rc.right+=colWidth[i];
						DrawText(lpdis->hDC, buf, (int)strlen(buf), &rc, DT_END_ELLIPSIS|DT_NOPREFIX);
					}
				case ODA_SELECT:
					if(lpdis->itemState & ODS_SELECTED){
						DrawFocusRect(lpdis->hDC, &lpdis->rcItem);
					}
			}
			return TRUE;

		case WM_SIZE:
			//adjust controls positions inside window
			GetClientRect(hWnd, &rc);
			rc.bottom -= 3*HIWORD(GetDialogBaseUnits())+10;
			hdl.prc = &rc;
			hdl.pwpos = &wp;
			Header_Layout(header, &hdl);
			SetWindowPos(listBox, 0, 0, wp.cy, rc.right, rc.bottom, SWP_NOZORDER);
			SetWindowPos(header, wp.hwndInsertAfter, 0, 0,
				wp.cx, wp.cy+1, wp.flags|SWP_SHOWWINDOW);
			y= HIWORD(lP)-2*HIWORD(GetDialogBaseUnits());
			setWindowXY(GetDlgItem(hWnd, IDOK), int(LOWORD(lP)*0.33), y);
			setWindowXY(GetDlgItem(hWnd, IDCANCEL), int(LOWORD(lP)*0.61), y);
			setWindowXY(GetDlgItem(hWnd, 528), 4, y+2);
			setWindowXY(GetDlgItem(hWnd, 104), 9*LOWORD(GetDialogBaseUnits()), y+2);
			break;

		case WM_NOTIFY:
		{
			HD_NOTIFY *nmhdr = (HD_NOTIFY*)lP;
			switch(nmhdr->hdr.code){
				case HDN_TRACK:
					colWidth[nmhdr->iItem]= nmhdr->pitem->cxy;
					Header_SetItem(header, nmhdr->iItem, nmhdr->pitem);
					InvalidateRect(listBox, 0, TRUE);
					break;
				case HDN_ITEMCLICK:
					if(sortedCol==nmhdr->iItem){
						descending= -descending;
					}
					else{
						sortedCol=nmhdr->iItem;
						descending= -1;
					}
					sortList();
					InvalidateRect(listBox, 0, TRUE);
					break;
			}
			break;
		}
		case WM_VKEYTOITEM:
			item= (int)SendMessage(listBox, LB_GETCURSEL, 0, 0);
			lev=0;
			if(item>=0 && item<Nlevels) lev=A[item];
			switch(LOWORD(wP)){
				case 'P':
					item-=2;
				case 'N':
					item++;
				case ' ':
					if(item>=0 && item<Nlevels){
						loadLevel(int(A[item]-levoff));
						SetFocus(listBox);
					}
					break;
				case 'B':
					first=lev;
					break;
				case 'E':
					last=lev;
					break;
				case 'V':
					if(sortedCol==0){
						movLevels(first, last, lev);
						InvalidateRect(listBox, 0, TRUE);
					}
					break;
				case VK_DELETE:
					if(delLevel(int(lev-levoff), hWnd)){
						if(item==Nlevels) item--;
						SetDlgItemInt(hWnd, 104, countSolved(), FALSE);
						SendMessage(listBox, LB_SETCOUNT, Nlevels, 0);
						sortList();
						InvalidateRect(listBox, 0, TRUE);
					}
					break;
				default:
					return -1;
			}
			PostMessage(listBox, LB_SETCURSEL, item+6, 0);
			PostMessage(listBox, LB_SETCURSEL, item, 0);
			return -2;

		case WM_COMMAND:
			notif = HIWORD(wP);
			wP=LOWORD(wP);
			switch(wP){
				case 101: //listBox
					if(notif!=LBN_DBLCLK) return FALSE;
				case IDOK:
					prevLevel=level;
					item= (int)SendMessage(listBox, LB_GETCURSEL, 0, 0);
					if(item>=0 && item<Nlevels){
						i=int(A[item]-levoff);
						if(i!=level) loadLevel(i);
					}
				case IDCANCEL:
					GetWindowRect(hWnd, &rc);
					levDlgX=rc.left;
					levDlgY=rc.top;
					levDlgW=rc.right-rc.left;
					levDlgH=rc.bottom-rc.top;
					delete[] A;
					EndDialog(hWnd, wP);
					return TRUE;
			}
			break;
	}
	return FALSE;
}
//---------------------------------------------------------------------------
//choose user solution or the best solution
BOOL CALLBACK SolutionProc(HWND hWnd, UINT mesg, WPARAM wP, LPARAM)
{
	switch(mesg){
		case WM_INITDIALOG:
		{
			setDlgTexts(hWnd, 540);
			CheckDlgButton(hWnd, 541, BST_CHECKED);
			Level *lev= &levoff[level];
			Pchar dat=0;
			int n=0;
			Pchar s[2]={lev->best.Mdata, lev->user.Mdata};
			for(int i=0; i<2; i++){
				if(s[i]){
					dat=s[i];
					n++;
				}
			}
			if(s[0] && s[1] && !strcmp(s[0], s[1])) n--;
			if(!n) msg(lng(807, "This level has not been solved"));
			if(n<2) EndDialog(hWnd, (int)dat);
			return TRUE;
		}

		case WM_COMMAND:
			wP=LOWORD(wP);
			switch(wP){
				case IDOK:
				{
					Pchar dat=0;
					Level *lev= &levoff[level];
					if(IsDlgButtonChecked(hWnd, 541)) dat=lev->best.Mdata;
					if(IsDlgButtonChecked(hWnd, 542)) dat=lev->user.Mdata;
					EndDialog(hWnd, (int)dat);
				}
					return TRUE;
				case IDCANCEL:
					EndDialog(hWnd, 0);
					return TRUE;
			}
			break;
	}
	return FALSE;
}
//---------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT mesg, WPARAM wP, LPARAM lP)
{
	WORD cmd;

	switch(mesg) {
		case WM_COMMAND:
			cmd=LOWORD(wP);
			if(cmd<211 || cmd>216) setSelected(0);
			if(setLang(cmd)) break;
			if(solving) break;
			if(editing){
				switch(cmd){
					case 114: //editor
					{
						check++;
						int e=optimizeLevel();
						check--;
						if(!e){
							if(wrLevel()) cancelEdit();
							else fillOuter();
							repaint();
						}
					}
						break;
					case 101: //escape
					case 404: //reset level
						if(level==Nlevels-1 && !levoff[level].offset){
							Nlevels--;
							level--;
						}
						cancelEdit();
						break;
					case 106: //open position
					case 407: //open level
						notOptimize++;
						openLevel();
						notOptimize--;
						break;
					case 107: //save position
						saveLevel();
						break;
					case 110: //quick save
					{
						delete[] edQsave;
						edQsave= new Square1[width*height];
						Psquare1 dest;
						Psquare src;
						for(src=board, dest=edQsave; src<boardk; src++, dest++){
							dest->obj= src->obj;
							dest->store= src->store;
						}
						edQsaveMoverX= mover->x;
						edQsaveMoverY= mover->y;
						edQsaveWidth= width;
						edQsaveHeight= height;
					}
						break;
					case 111: //quick load
						if(!edQsave || width!=edQsaveWidth || height!=edQsaveHeight){
							msg(lng(808, "Nothing to load"));
						}
						else{
							Psquare dest;
							Psquare1 src;
							for(dest=board, src=edQsave; dest<boardk; src++, dest++){
								dest->obj= src->obj;
								dest->store= src->store;
							}
							mover=square(edQsaveMoverX, edQsaveMoverY);
							edUndo=edRec;
							edRedo=0;
							repaint();
						}
						break;
					case 408: //delete level
					{
						clearBoard();
						repaint();
					}
						break;
					case 450:
					case 451:
						if(!mover->store){
							for(Psquare p=board; p<boardk; p++)
								if(p->obj==BM_GROUND || p->obj==BM_OBJECT)
									p->obj= (char)(p->store ? BM_OBJECT : BM_GROUND);
						}
#if SOLVER
					case 452:
						gener();
						break;
#endif
				}
			}
			else{ //not editing
				switch(cmd){
					case 101: //escape
						if(replay) SendMessage(hWnd, WM_COMMAND, 210, 0);
						else ShowWindow(hWnd, SW_MINIMIZE);
						break;
					case 106: //open position
						openPos();
						break;
					case 107: //save position
						savePos();
						break;
					case 110: //quick save
						if(undoPos==rec){
							msg(lng(809, "Nothing to save"));
						}
						else{
							delete[] quickSave;
							quickSaveLevel=level;
							undoPos->mover=mover;
							quickSave = new QuickInfo[quickLen = int(undoPos-rec)+1];
							for(int i=0; i<quickLen; i++){
								QuickInfo *q= &quickSave[i];
								q->mover= subBoard(rec[i].mover);
								q->pushBeg= subBoard(rec[i].pushBeg);
								q->pushEnd= subBoard(rec[i].pushEnd);
								q->mov= rec[i].mov;
								q->pus= rec[i].pus;
							}
						}
						break;
					case 111: //quick load
						if(!quickSave){
							msg(lng(808, "Nothing to load"));
						}
						else{
							if(level!=quickSaveLevel){
								if(msg1(MB_YESNO|MB_ICONQUESTION,
									lng(810, "Position has been saved in level %d.\r\nDo you want to restore it ?"),
									quickSaveLevel+1) == IDNO) break;
								loadLevel(quickSaveLevel);
							}
							undoAll();
							for(int i=0; i<quickLen; i++){
								QuickInfo *q= &quickSave[i];
								rec[i].mover= addBoard(q->mover);
								rec[i].pushBeg= addBoard(q->pushBeg);
								rec[i].pushEnd= addBoard(q->pushEnd);
								rec[i].mov= q->mov;
								rec[i].pus= q->pus;
							}
							redoPos= rec+quickLen-1;
							undoPos= rec;
							redoAll();
							update();
						}
						break;
					case 114: //editor
						if(replay) SendMessage(hWnd, WM_COMMAND, 210, 0);
						notMsg++;
						undoAll();
						notMsg--;
						newBoard(0, 0, 1);
						startEdit();
						break;
					case 115: //new level
						if(replay) SendMessage(hWnd, WM_COMMAND, 210, 0);
						if(DialogBox(inst, "AUTHOR", hWnd, (DLGPROC)AuthorProc)==IDOK){
							if(clone) prevLevel=level;
							Level *lev= addLevel();
							if(lev){
								int len=(int)strlen(curAuthor);
								if(len){
									lev->author= new char[len+1];
									strcpy(lev->author, curAuthor);
								}
								newBoard(0, 0, clone);
								status();
								startEdit();
							}
						}
						break;
					case 202: //rewind
					case 203: //back
					case 207: //play
					case 208: //fast forward
						if(replay){
							if(replay==cmd){
								if(cmd==207 || cmd==202) cmd++;
								else if(cmd==208 || cmd==203) cmd--;
							}
							replayBtn(cmd);
							aminmax(playTimer, 10, 9000);
							aminmax(fastTimer, 0, playTimer);
							SetTimer(hWin, 200, cmd==202 || cmd==208 ? fastTimer : playTimer, 0);
						}
						break;
					case 204: //undo
						stop();
						undo();
						break;
					case 205: //pause
						stop();
						break;
					case 206: //redo
						stop();
						redo();
						break;
					case 112: //play solution
					case 210: //stop
						if(replay){
							stop();
							replay=0;
							ShowWindow(toolPlay, SW_HIDE);
							if(toolBarVisible) ShowWindow(toolbar, SW_SHOW);
							resize();
							SetFocus(hWin);
						}
						else{
							Pchar p= (Pchar)DialogBox(inst, "SOLUTION",
								hWnd, (DLGPROC)SolutionProc);
							if(p){
								loadSolution(level, p);
								if(movError){
									msg(lng(811, "Solution is wrong !"));
								}
								undoAll();
								update();
								replayBtn(205);
							}
						}
						break;
					case 217: //goto move
						DialogBox(inst, "MOV", hWnd, (DLGPROC)MovProc);
						break;
					case 401: //next level
						loadLevel(level+1);
						UpdateWindow(hWin);
						break;
					case 402: //previous level
						loadLevel(level-1);
						UpdateWindow(hWin);
						break;
					case 403: //list levels
						DialogBox(inst, "LEVEL", hWnd, (DLGPROC)LevelsProc);
						break;
					case 405: //go to level
						DialogBox(inst, "GOTO", hWnd, (DLGPROC)GotoProc);
						break;
					case 407: //open level
						check++;
						openLevel();
						check--;
						break;
					case 408: //delete level
						delLevel(level, hWin);
						break;
#if SOLVER
					case 450:
					case 451:
					case 452:
					case 454:
						undoAll();
						update();
						findSolution(cmd-450);
						break;
#endif
				}
			}
			switch(cmd){
				case 100: //about
					DialogBox(inst, "ABOUT", hWnd, (DLGPROC)AboutProc);
					break;
				case 350: //help
					showHelp();
					break;
				case 103: //exit
					SendMessage(hWin, WM_CLOSE, 0, 0);
					break;
				case 104: //options
					DialogBox(inst, "OPTIONS", hWnd, (DLGPROC)OptionsProc);
					break;
				case 105: //delete settings
					if(MessageBox(hWnd, lng(736, "Do you want to delete your settings ?"),
						title, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) ==IDYES){
						deleteini();
					}
					break;
				case 108: //save all
					modifUser=true;
					saveUser();
					saveData();
					break;
				case 109: //change player
					openUser();
					break;
				case 116: //mirror X
					xtrans=!xtrans;
					repaint();
					CheckMenuItem(GetMenu(hWin), cmd,
						MF_BYCOMMAND|(xtrans ? MF_CHECKED : MF_UNCHECKED));
					break;
				case 117: //mirror Y
					ytrans=!ytrans;
					repaint();
					CheckMenuItem(GetMenu(hWin), cmd,
						MF_BYCOMMAND|(ytrans ? MF_CHECKED : MF_UNCHECKED));
					break;
				case 201: //begin
					undoAll();
					update();
					break;
				case 209: //end
					redoAll();
					update();
					break;
				case 211: //undo push
					undoPus();
					break;
				case 212: //redo push
					redoPus();
					break;
				case 213: //undo move
					undo();
					break;
				case 214: //redo move
					redo();
					break;
				case 215: //undo
					undo2();
					break;
				case 216: //redo
					redo2();
					break;
				case 301: //open skin
					openSkin();
					break;
				case 302: //next skin
					nextSkin();
					break;
				case 303: //previous skin
					prevSkin();
					break;
				case 304: //reload skin
					loadSkin();
					break;
				case 404: //reset level
					if(replay) SendMessage(hWnd, WM_COMMAND, 201, 0);
					else resetLevel();
					break;
				case 406: //save level
					saveLevel();
					break;
			}
			break;

		case WM_TIMER:
			switch(wP){
				case 128:
					clock();
					break;
				case 200:
					if(replay>205) redo();
					else undo();
					break;
				case 300:
					KillTimer(hWin, wP);
					setTitle(0);
					break;
			}
			break;

		case WM_MOUSEMOVE:
		{
			Psquare mouse= SquareXY(LOWORD(lP), HIWORD(lP));
			if(mouse){
				static Psquare last;
				if(mouse!=last){
					status(5, "%d:%d", mouse->x, mouse->y);
					last=mouse;
				}
			}
			else{
				status(5, "");
			}
			if(solving) break;
			if(editing){
				editMouse(wP, lP);
			}
			else{
				setSelected(selected);
				if(mouse && !justSelected){
					Psquare pObj= lineObj(mouse);
					if(pObj){
						Psquare oldSel=selected;
						setSelected(pObj);
						selected=oldSel;
					}
				}
			}
		}
			break;

		case WM_LBUTTONUP:
			if(ldown){
				ReleaseCapture();
				ldown=false;
				editPos=0;
			}
			break;

		case WM_RBUTTONUP:
			if(rdown){
				ReleaseCapture();
				rdown=false;
				editPos=0;
			}
			break;

		case WM_RBUTTONDOWN:
		{
			if(solving) break;
			Psquare mouse= SquareXY(LOWORD(lP), HIWORD(lP));
			if(!mouse) break;
			if(editing){
				if(!ldown){
					rdown=true;
					SetCapture(hWin);
				}
				editMouse(wP, lP);
			}
			else{
				if(replay) break;
				if(mouse==mover) undo();
				else{
					setSelected(0);
					moveM(mouse);
				}
			}
		}
			break;

		case WM_LBUTTONDOWN:
		{
			if(solving) break;
			Psquare mouse= SquareXY(LOWORD(lP), HIWORD(lP));
			if(!mouse) break;
			if(editing){
				if(!rdown){
					ldown=true;
					SetCapture(hWin);
				}
				editMouse(wP, lP);
			}
			else{
				if(replay) break;
				if(fullUndo()) break;
				Psquare pObj=0;
				if(!justSelected) pObj=lineObj(mouse);
				if(mouse->obj==BM_OBJECT) justSelected=true;
				if(pObj){
					movePush(pObj, mouse, true);
				}
				else{
					movePush(selected, mouse, true);
				}
				setSelected(mouse);
				if(finish()) loadNextLevel();
			}
		}
			break;

		case WM_KEYDOWN:
			if(solving) break;
			if(editing){
				switch(wP){
					case VK_RETURN:
						SendMessage(hWnd, WM_COMMAND, 114, 0);
						break;
					case VK_LEFT:
					case VK_NUMPAD4:
						moveE(0);
						break;
					case VK_RIGHT:
					case VK_NUMPAD6:
						moveE(1);
						break;
					case VK_UP:
					case VK_NUMPAD8:
						moveE(2);
						break;
					case VK_DOWN:
					case VK_NUMPAD2:
						moveE(3);
						break;
					default:
						return 0;
				}
			}
			else{
				if(replay) break;
				setSelected(0);
				switch(wP){
					case VK_LEFT:
					case VK_NUMPAD4:
						moveK2(0);
						break;
					case VK_RIGHT:
					case VK_NUMPAD6:
						moveK2(1);
						break;
					case VK_UP:
					case VK_NUMPAD8:
						moveK2(2);
						break;
					case VK_DOWN:
					case VK_NUMPAD2:
						moveK2(3);
						break;
					case VK_NUMPAD7:
						moveK2(4);
						break;
					case VK_NUMPAD9:
						moveK2(5);
						break;
					case VK_NUMPAD1:
						moveK2(6);
						break;
					case VK_NUMPAD3:
						moveK2(7);
						break;
					default:
						return 0;
				}
				if(finish()) loadNextLevel();
			}
			break;

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			BeginPaint(hWnd, &ps);
			repaint();
			EndPaint(hWnd, &ps);
		}
			break;

		case WM_SIZE:
			resize();
			break;

		case WM_NOTIFY:
		{
			LPNMHDR nmhdr = (LPNMHDR)lP;
			switch(nmhdr->code){
				case TTN_NEEDTEXT:
				{
					TOOLTIPTEXT *ttt = (LPTOOLTIPTEXT)lP;
					int id= (int)ttt->hdr.idFrom+1000;
					char *s= lng(id, 0);
					if(s){
						ttt->hinst= NULL;
						ttt->lpszText= s;
					}
					else{
						//English tooltip is in resource
						ttt->hinst= inst;
						ttt->lpszText= MAKEINTRESOURCE(id);
					}
				}
					break;

#ifndef UNICODE
					//Linux
				case TTN_NEEDTEXTW:
				{
					TOOLTIPTEXTW *ttt = (LPTOOLTIPTEXTW)lP;
					int id= (int)ttt->hdr.idFrom+1000;
					char *s= lng(id, 0);
					if(s){
						ttt->hinst= NULL;
						MultiByteToWideChar(CP_ACP, 0, s, -1, ttt->szText, sizeA(ttt->szText));
						ttt->szText[sizeA(ttt->szText)-1]=0;
					}
					else{
						//English tooltip is in resource
						ttt->hinst= inst;
						ttt->lpszText= MAKEINTRESOURCEW(id);
					}
				}
					break;
#endif

			}
		}
			break;

		case WM_QUERYENDSESSION:
			saveAtExit();
			return TRUE;

		case WM_CLOSE:
			DestroyWindow(hWnd);
			saveAtExit();
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		default:
			return DefWindowProc(hWnd, mesg, wP, lP);
	}
	return 0;
}
//---------------------------------------------------------------------------
int pascal WinMain(HINSTANCE hInstance, HINSTANCE hPrevInst, LPSTR, int cmdShow)
{
	msgD("start");
	int i;
	WNDCLASS wc;
	RECT rc;
	MSG mesg;
	static int parts[]={35, 90, 90, 75, 60, 40, -1};
	static TBBUTTON tbb[]={
			{0, 106, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{1, 107, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{10, 104, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{12, 109, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{0, 0, 0, TBSTYLE_SEP, {0}, 0},
			{5, 215, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{4, 216, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{13, 112, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{0, 0, 0, TBSTYLE_SEP, {0}, 0},
			{2, 402, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{3, 401, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{11, 403, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{7, 404, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{0, 0, 0, TBSTYLE_SEP, {0}, 0},
			{9, 302, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{8, 301, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{6, 304, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
	};
	static TBBUTTON tbbP[]={
			{0, 201, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{1, 202, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{2, 203, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{3, 204, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{4, 205, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{5, 206, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{6, 207, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{7, 208, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{8, 209, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
			{9, 210, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0},
	};

	inst=hInstance;
#if _WIN32_IE >= 0x0300
	INITCOMMONCONTROLSEX iccs;
	iccs.dwSize= sizeof(INITCOMMONCONTROLSEX);
	iccs.dwICC= ICC_BAR_CLASSES|ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&iccs);
#else
	InitCommonControls();
#endif
	readini();
	msgD("config");
	initLang();
	msgD("language");
	//open sokoban.dat
	if(initLevels()) return 3;
	msgD("levels");

	wc.style= CS_OWNDC;
	wc.lpfnWndProc= MainWndProc;
	wc.cbClsExtra= 0;
	wc.cbWndExtra= 0;
	wc.hInstance= hInstance;
	wc.hIcon= LoadIcon(hInstance, MAKEINTRESOURCE(1));
	wc.hCursor= LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground= NULL;
	wc.lpszMenuName= 0;
	wc.lpszClassName= "SokobanWCLS";
	if(!hPrevInst && !RegisterClass(&wc)) return 1;
	msgD("class");
	isHungAppWindow = (pIsHungAppWindow)GetProcAddress(GetModuleHandle("user32.dll"), "IsHungAppWindow");

	hWin = CreateWindow("SokobanWCLS", title,
		WS_OVERLAPPEDWINDOW-WS_THICKFRAME,
		CW_USEDEFAULT, CW_USEDEFAULT,
		50, 50, NULL, NULL, hInstance, NULL);
	if(!hWin) return 2;
	msgD("window");
	dc= GetDC(hWin);
	hStatus= CreateStatusWindow(WS_CHILD, 0, hWin, 1);
	for(i=1; i<sizeA(parts)-1; i++){
		parts[i]+=parts[i-1];
	}
	for(i=0; i<sizeA(parts)-1; i++){
		parts[i]=parts[i]*GetDeviceCaps(dc, LOGPIXELSX)/96;
	}
	SendMessage(hStatus, SB_SETPARTS, sizeA(parts), (LPARAM)parts);
	GetClientRect(hStatus, &rc);
	statusH= rc.bottom;
	if(statusBarVisible) ShowWindow(hStatus, SW_SHOW);

	int n=sizeA(tbb);
	for(TBBUTTON *u=tbb; u<endA(tbb); u++){
		if(u->fsStyle==TBSTYLE_SEP) n--;
	}
	toolbar = CreateToolbarEx(hWin,
		WS_CHILD|TBSTYLE_TOOLTIPS|0x800, 2, n,
		inst, 10, tbb, sizeA(tbb),
		16, 16, 16, 15, sizeof(TBBUTTON));
	if(toolBarVisible) ShowWindow(toolbar, SW_SHOW);
	toolPlay = CreateToolbarEx(hWin,
		WS_CHILD|TBSTYLE_TOOLTIPS|0x800, 3, sizeA(tbbP),
		inst, 11, tbbP, sizeA(tbbP),
		16, 16, 16, 15, sizeof(TBBUTTON));
	bmpdc= CreateCompatibleDC(dc);
	SetStretchBltMode(bmpdc, HALFTONE);

	notResize++;
	loadSkin(LoadBitmap(hInstance, "SKIN"));
	if(!*fnskin){
		getExeDir(fnskin, "skins\\");
	}
	else{
		loadSkin();
	}
	if(!bm) return 7;
	msgD("skin");
	aminmax(level, 0, Nlevels-1);
	prevLevel=level;
	resetLevel();
	langChanged();
	notResize--;
	ShowWindow(hWin, cmdShow);
	UpdateWindow(hWin);
	msgD("show");
	initUser();
	msgD("user");
	SetTimer(hWin, 128, 1000, 0);
	haccel=LoadAccelerators(hInstance, MAKEINTRESOURCE(3));
	msgD("accelerators");
	srand(GetTickCount());

	while(GetMessage(&mesg, NULL, 0, 0)==TRUE){
		if(!TranslateAccelerator(hWin, haccel, &mesg)){
			TranslateMessage(&mesg);
			DispatchMessage(&mesg);
		}
	}
	hWin=0;
	msgD("exit");
	DeleteDC(bmpdc);
	DeleteObject(bm);
	delete[] board;
	delete[] distBuf1;
	delete[] distBuf2;
	delete[] levoff;
	delete[] levels;
	delete[] user;

	//HtmlHelp bug workaround, process freezes in itss.dll CITUnknown::CloseActiveObjects() if help is visible and user closes both windows from the taskbar
	if(helpVisible) Sleep(600);
	return 0;
}
//---------------------------------------------------------------------------
