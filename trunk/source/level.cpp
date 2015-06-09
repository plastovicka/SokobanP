/*
 (C) Petr Lastovicka

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License.
 */
//---------------------------------------------------------------------------
#include <windows.h>
#pragma hdrstop
#include <stdio.h>
#include "level.h"
//---------------------------------------------------------------------------
//close file
int close(FILE *f, char *fn)
{
	if(!fclose(f)) return 0;
	if(msg1(MB_ICONEXCLAMATION|MB_RETRYCANCEL,
		lng(734, "Write error, file %s"), fn) ==IDRETRY) return 2;
	return 1;
}
//---------------------------------------------------------------------------
//get file size
int fsize(FILE *f)
{
	fseek(f, 0, SEEK_END);
	int len=ftell(f);
	rewind(f);
	if(len<=0){
		msg(lng(769, "Invalid file length"));
	}
	return len;
}
//---------------------------------------------------------------------------
//save file "sokoban.dat"
void saveData()
{
start:
	if(modifData){
		FILE *f=fopen(fndata, "wb");
		if(!f){
			msg(lng(733, "Cannot write to file %d"), fndata);
		}
		else{
			fputs("[Sokoban Database]\r\n", f);
			for(int i=0; i<Nlevels; i++){
				Level *lev= &levoff[i];
				if(!lev->offset) continue;
				fputs(lev->offset, f);
				fputc(';', f);
				fputs(lev->author, f);
				if(lev->best.Mdata){
					fputc(';', f);
					fputs(lev->best.Mdata, f);
				}
				fputc('\r', f);
				fputc('\n', f);
			}
			int cl= close(f, fndata);
			if(cl==2) goto start;
			if(!cl) modifData=false;
		}
	}
}
//---------------------------------------------------------------------------
//change user
void openUser()
{
	if(!*fnuser && modifUser){
		saveUser();
	}
	else{
		//save old user solutions
		saveUser();
		//open new user
		userOfn.hwndOwner= hWin;
		userOfn.Flags= OFN_PATHMUSTEXIST|OFN_HIDEREADONLY;
		for(;;){
			if(GetOpenFileName(&userOfn)){
				delUser();
				initUser();
				return; //ok
			}
			if(CommDlgExtendedError()!=FNERR_INVALIDFILENAME
				|| !*userOfn.lpstrFile) return; //cancel
			*userOfn.lpstrFile=0;
		}
	}
}
//---------------------------------------------------------------------------
//save user solutions to rec file 
void saveUser()
{
	if(modifUser){
		if(!*fnuser){
			if(!saveFileDlg(&userOfn, true)) return;
		}
	start:
		FILE *f=fopen(fnuser, "wt");
		if(!f){
			msg(lng(733, "Cannot write to file %s"), fnuser);
		}
		else{
			fputs("[Sokoban Solutions]\n", f);
			for(int i=0; i<Nlevels; i++){
				Level *lev= &levoff[i];
				if(lev->user.Mdata){
					fputs(lev->user.Mdata, f);
				}
				fputc('\n', f);
			}
			int cl=close(f, fnuser);
			if(cl==2) goto start;
			if(!cl) modifUser=false;
		}
	}
}
//---------------------------------------------------------------------------
//create new level
Level *addLevel()
{
	Level *l= new Level[Nlevels+1];
	if(!l){
		msg(lng(768, "Not enough memory"));
		return 0;
	}
	else{
		playtime=0;
		memcpy(l, levoff, Nlevels*sizeof(Level));
		level=Nlevels;
		Nlevels++;
		delete[] levoff;
		levoff=l;
		Level *lev= l+Nlevels-1;
		lev->author="";
		lev->offset=0;
		return lev;
	}
}
//---------------------------------------------------------------------------
//delete level
bool delLevel(int lev, HWND win)
{
	if(lev<Nlevels && lev>=0){
		if(MessageBox(win, lng(812, "Do you really want to remove this level from database ?"),
			title, MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2) ==IDYES){
			Level *l= new Level[Nlevels-1];
			if(!l){
				msg(lng(768, "Not enough memory"));
			}
			else{
				Nlevels--;
				memcpy(l, levoff, lev*sizeof(Level));
				memcpy(l+lev, levoff+lev+1, (Nlevels-lev)*sizeof(Level));
				delete[] levoff;
				levoff=l;
				modifData=modifUser=true;
				if(quickSaveLevel==lev){
					delete quickSave;
					quickSave=0;
				}
				if(quickSaveLevel>lev) quickSaveLevel--;
				playtime=0;
				if(level==lev){
					resetLevel();
					SetFocus(win);
				}
				if(level>lev) level--;
				return true;
			}
		}
	}
	return false;
}
//---------------------------------------------------------------------------
//delete all objects and walls and create border wall
void clearBoard()
{
	Psquare dest=board;
	for(int y=0; y<height; y++){
		for(int x=0; x<width; x++){
			dest->obj= BM_GROUND;
			dest->store=false;
			if(x==1 || y==1 || x==width-2 || y==height-2) dest->obj=BM_WALL;
			if(x==0 || y==0 || x==width-1 || y==height-1) dest->obj=BM_BACKGROUND;
			dest++;
		}
	}
}
//---------------------------------------------------------------------------
//change board dimension
void newBoard(int w, int h, int copy)
{
	int moverx, movery, len, x, y;

	if(!w){
		RECT rcS, rcW, rcC;
		SystemParametersInfo(SPI_GETWORKAREA, 0, &rcS, 0);
		GetWindowRect(hWin, &rcW);
		getClient(&rcC);
		w= (rcS.right-rcS.left - rcW.right+rcW.left +
			rcC.right-rcC.left)/bmW;
		amin(w, 26);
		h= (rcS.bottom-rcS.top - rcW.bottom+rcW.top +
			rcC.bottom-rcC.top)/bmH;
		amin(h, 20);
	}
	if(copy==1){
		amin(w, width);
		amin(h, height);
	}
	len= w*h;
	delete[] distBuf1;
	delete[] distBuf2;
	distBuf1= new Psquare[len];
	distBuf2= new Psquare[len];
	Psquare newp= new Square[len];
	diroff[8]=0;
	diroff[3]=sizeof(Square)* w; //down
	diroff[2]=-diroff[3];         //up
	diroff[1]=sizeof(Square);     //right
	diroff[0]=-diroff[1];         //left
	diroff[4]=diroff[0]+diroff[2];//left up
	diroff[5]=diroff[1]+diroff[2];//right up
	diroff[6]=diroff[0]+diroff[3];//left down
	diroff[7]=diroff[1]+diroff[3];//right down

	if(copy>=0){
		Psquare dest= newp;
		for(y=0; y<h; y++){
			for(x=0; x<w; x++){
				dest->x=x;
				dest->y=y;
				dest->distId=0;
				dest->obj= BM_GROUND;
				dest->store=false;
				if(x==0 || y==0 || x==w-1 || y==h-1) dest->obj=BM_BACKGROUND;
				dest++;
			}
		}
	}
	distId=0;

	if(copy==1){
		int dx= (w-width)>>1, dy= (h-height)>>1;
		for(y=1; y<height-1; y++){
			for(x=1; x<width-1; x++){
				Psquare src= square(x, y);
				Psquare dest= newp + x+dx + (y+dy)*w;
				if(dest->obj==BM_GROUND){
					dest->obj= src->obj;
					dest->store= src->store;
				}
			}
		}
		moverx= mover->x + dx;
		movery= mover->y + dy;
	}
	else{
		moverx= w>>1;
		movery= h>>1;
	}
	width=w;
	height=h;
	delete[] board;
	board= newp;
	boardk= newp+len;
	mover=square(moverx, movery);
	selected=hilited=0;
}
//---------------------------------------------------------------------------
//delete unreachable fields
int optimizeLevel()
{
	int x, y, i, d, Nobj, Nsto;
	Psquare p, pn;

	if(!check && notOptimize) return 0;

	for(p=board; p<boardk; p++) p->dist=MAXDIST;
	mover->obj= BM_GROUND;
	mover->dist=0;
	Nobj=Nsto=0;
	for(d=0;; d++){
		bool change=false;
		for(p=board; p<boardk; p++){
			if(p->dist==d){
				if(p->obj==BM_OBJECT) Nobj++;
				if(p->store && p->obj<2) Nsto++;
				for(i=0; i<4; i++){
					pn= nxtP(p, i);
					if(pn->dist > d+1 && pn->obj<2){
						pn->dist=d+1;
						change=true;
					}
				}
			}
		}
		if(!change) break;
	}
	if(check){
		if(!Nobj){
			repaint();
			msg(lng(813, "Man cannot get to objects"));
			return 4;
		}
		if(!Nsto){
			repaint();
			msg(lng(814, "Man cannot get to targets"));
			return 5;
		}
		if(Nobj!=Nsto){
			repaint();
			msg2(lng(815, "Invalid board"),
				lng(816, "Objects: %d\r\nTargets: %d\r\n"), Nobj, Nsto);
			return 6;
		}
	}
	if(!notOptimize){
		for(p=board; p<boardk; p++){
			if(p->obj<2 && p->dist==MAXDIST){
				if(p->obj==BM_OBJECT && p->store){
					p->obj=BM_STORE;
				}
				else{
					p->obj=BM_WALL;
					p->store=false;
				}
			}
		}
		for(y=1; y<height-1; y++){
			for(x=1; x<width-1; x++){
				Psquare dest= square(x, y);
				for(i=0; i<9; i++){
					if(nxtP(dest, i)->obj<2) break;
				}
				if(i==9 && dest->obj==BM_WALL) dest->obj= BM_BACKGROUND;
			}
		}
		for(p=board; p<boardk; p++){
			if(p->obj==BM_STORE) p->obj=BM_OBJECT;
		}
	}
	return 0;
}
//---------------------------------------------------------------------------
//create border wall
void fillOuter()
{
	int x, y;
	for(y=1; y<height-1; y++){
		for(x=1; x<width-1; x++){
			Psquare dest= square(x, y);
			if(x==1 || y==1 || y==height-2 || x==width-2) dest->obj=BM_WALL;
			if(dest->obj==BM_BACKGROUND) dest->obj=BM_GROUND;
		}
	}
}
//---------------------------------------------------------------------------
//count objects
int getNobj(Pchar buf)
{
	Pchar p;
	int i=0, n;

	for(p=buf; *p; p++){
		n=1;
		if(*p>='A' && *p<='Z') n= *p++ -'A'+1;
		if(*p=='*' || *p=='$') i+=n;
	}
	return i;
}
//---------------------------------------------------------------------------
//count dimension (including border wall)
void getDim(Pchar buf, int &width, int &height)
{
	int x, y, w, rep;
	Pchar p;

	x=y=w=0;
	for(p=buf; *p; p++){
		rep=1;
		if(*p>='A' && *p<='Z'){ rep+= *p-'A'; p++; }
		if(*p=='!'){
			amin(w, x);
			x=0;
			y++;
		}
		else{
			x+=rep;
		}
	}
	amin(w, x);
	if(p>buf && p[-1]!='!') y++;
	width=w+2; height=y+2;
}
//---------------------------------------------------------------------------
//read level from string
int rdLevel1(Pchar buf, int border, Level *lev)
{
	int x, y, w;
	Pchar p;

	if(!buf) return 12;
	if(lev){
		w=lev->width;
		y=lev->height;
	}
	else{
		getDim(buf, w, y);
	}
	if(w<3 || y<3){
		msg(lng(817, "Level is empty"));
		return 10;
	}
	if(w>127 || y>127){
		msg(lng(818, "Level is too big"));
		return 11;
	}
	border++;
	newBoard(w + 2*border-2, y + 2*border-2, (lev) ? -1 : 0);

	int Nmover=0;
	bool curstore;
	char curobj;
	int n=0;
	Psquare dest=board;
	p=buf;
	for(y=0; y<height; y++){
		for(x=0; x<width; x++){
			if(dest->obj!=BM_BACKGROUND){
				if(y<border || x<border || y>=height-border || x>=width-border){
					dest->obj=BM_WALL;
				}
				else{
					if(!n){
						n=1;
						if(*p>='A' && *p<='Z'){
							n= *p-'A'+1;
							p++;
						}
						curstore=false;
						switch(*p++){
							case '!': case '\0':
								n=width-border-x;
								curobj= (char)(border>1 ? BM_WALL : BM_GROUND);
								break;
							case '#':
								curobj=BM_WALL;
								break;
							case '+':
								curstore=true;
							case '@':
								mover=dest;
								Nmover++;
								curobj=BM_GROUND;
								break;
							case '*':
								curstore=true;
							case '$':
								curobj=BM_OBJECT;
								break;
							case '.':
								curstore=true;
							default:
							case ' ':
								curobj=BM_GROUND;
								break;
						}
					}
					dest->obj= curobj;
					dest->store= curstore;
					n--;
				}
			}
			dest++;
		}
		if(*p=='!') p++;
	}
	if(check && Nmover!=1){
		if(!Nmover){
			msg(lng(819, "There is no man in this level"));
			return 1;
		}
		msg(lng(820, "There are %d men in this level"), Nmover);
		return 2;
	}
	return 0;
}
//---------------------------------------------------------------------------
int rdLevel(Pchar buf, int border)
{
	int e=rdLevel1(buf, border, 0);
	if(e<10){
		if(!e) e=optimizeLevel();
		moverDirect=0;
		moves=pushes=0;
		undoPos=rec; redoPos=0;
		edUndo=edRec; edRedo=0;
		resize();
		status();
	}
	return e;
}
//---------------------------------------------------------------------------
//go to level with index "which"
int loadLevel(int which)
{
	aminmax(which, 0, Nlevels-1);
	if(replay) SendMessage(hWin, WM_COMMAND, 210, 0);
	if(level!=which){
		level=which;
		playtime=0;
	}
	Pchar p= levoff[which].offset;
	if(!p) return 3;
	return rdLevel(p, 1);
}

void loadNextUnsolved(int current)
{
	for(int i=(current+1)%Nlevels; i!=current; i=(i+1)%Nlevels)
	{
		if(!levoff[i].user.Mdata){
			loadLevel(i);
			break;
		}
	}
}

void loadNextUnsolved()
{
	loadNextUnsolved(level);
}

void loadRandomLevel()
{
	int i=level;
	for(int j=0; j<Nlevels*4; j++)
	{
		i=(i+rand())%Nlevels;
		if(!levoff[i].user.Mdata){
			loadLevel(i);
			return;
		}
	}
	loadNextUnsolved(i);
}

void loadNextLevel()
{
	switch(nextAction){
		case 0:
			status();
			break;
		case 1:
			loadRandomLevel();
			break;
		case 2:
			loadNextUnsolved();
			break;
	}
}
//---------------------------------------------------------------------------
//reload level
void resetLevel()
{
	loadLevel(level);
}
//---------------------------------------------------------------------------
void dels(Pchar s)
{
	if(s && *s && (s<user || s>userk) && (s<levels || s>levelsk)){
		delete[] s;
	}
}
//---------------------------------------------------------------------------
//delete all user solutions from memory
void delUser()
{
	for(int i=0; i<Nlevels; i++){
		//duplicate the best solution which has been set from the user solution
		if(user){
			Solution *sol = &levoff[i].best;
			Pchar s = sol->Mdata;
			if(s>=user && s<=userk){
				sol->Mdata = new char[strlen(s)+1];
				strcpy(sol->Mdata, s);
			}
		}
		//delete user solution
		Solution *sol= &levoff[i].user;
		dels(sol->Mdata);
		sol->init();
	}
	delete[] user;
	user=userk=0;
}
//---------------------------------------------------------------------------
//save level to string
Pchar wrLevel(bool pack)
{
	int xlo, ylo, xhi, yhi, x, y, rep;
	size_t len;
	Psquare p;
	char ch, oldch;
	Pchar buf, result, s;

	xlo=ylo=999;
	xhi=yhi=0;
	for(p=board; p<boardk; p++){
		if(p->obj<2){
			amin(xhi, p->x);
			amin(yhi, p->y);
			amax(xlo, p->x);
			amax(ylo, p->y);
		}
	}
	if(!pack){
		xhi++; yhi++;
		xlo--; ylo--;
	}

	buf= new char[16384];
	if(!buf) return 0;
	s=buf;
	for(y=ylo; y<=yhi; y++){
		rep=0;
		oldch=0;
		for(x=xlo; x<=xhi; x++){
			p= square(x, y);
			if(p->store){
				if(p==mover) ch='+';
				else{
					switch(p->obj){
						case BM_GROUND:
							ch= '.';
							break;
						case BM_OBJECT:
							ch= '*';
							break;
						default:
							ch= '#';
							break;
					}
				}
			}
			else{
				if(p==mover) ch='@';
				else{
					switch(p->obj){
						case BM_GROUND:
							ch= ' ';
							break;
						case BM_OBJECT:
							ch= '$';
							break;
						case BM_BACKGROUND:
							if(!pack){ ch=' '; break; }
						default:
							ch= '#';
							break;
					}
				}
			}
			if(pack){
				if(ch==oldch) rep++;
				else{
					while(rep>25){
						rep-=26;
						*s++= 'Z';
						*s++= oldch;
					}
					if(rep){
						*s++= char(rep==1 ? oldch : 'A'+rep);
						rep=0;
					}
					if(oldch) *s++=oldch;
					oldch=ch;
				}
			}
			else{
				*s++= ch;
			}
		}
		if(pack){
			if(oldch!='#'){
				while(rep>25){
					rep-=26;
					*s++= 'Z';
					*s++= oldch;
				}
				if(rep){
					*s++= char(rep==1 ? oldch : 'A'+rep);
				}
				if(oldch) *s++=oldch;
			}
			*s++= '!';
		}
		else{
			s--;
			while(*s==' ') s--;
			s++;
			*s++='\n';
		}
	}
	len= s-buf;
	if(!pack) len++;
	result= new char[len];
	if(result){
		memcpy(result, buf, len);
		result[len-1]='\0';
	}
	delete[] buf;
	return result;
}
//---------------------------------------------------------------------------
bool wrLevel()
{
	Pchar result= wrLevel(true);
	if(!result) return false;
	Level *lev= &levoff[level];
	if(lev->offset && !strcmp(result, lev->offset)) return true;
	dels(lev->offset);
	lev->offset= result;
	modifData=true;
	dels(lev->best.Mdata);
	dels(lev->user.Mdata);
	lev->best.init();
	lev->user.init();
	if(quickSaveLevel==level){
		delete quickSave;
		quickSave=0;
	}
	return true;
}
//---------------------------------------------------------------------------
//check if all objects are on stores
bool isFinish()
{
	for(Psquare p=board; p<boardk; p++){
		if(p->store && p->obj==BM_GROUND) return false;
	}
	return true;
}
//---------------------------------------------------------------------------
//assing solution to "sol", but only if it is better
bool setSolution(Solution *sol, int mov, int pus, Pchar dat)
{
	if(mov && (!sol->Mmoves || eval(mov, pus) < sol->eval())){
		sol->Mmoves= mov;
		sol->Mpushes= pus;
		dels(sol->Mdata);
		sol->Mdata= dat;
		return true;
	}
	return false;
}
//---------------------------------------------------------------------------
//if "psol" solves level "lev" then assign it to "sol" and return 0
//level must have known dimension
int readSolution(Pchar psol, Level *lev, Solution *sol)
{
	int mov, pus, rep, direct, e;
	Pchar plev= lev->offset;

	if(!psol || isSep(*psol)) return 10;
	notMsg++;
	e=rdLevel1(plev, 1, lev);
	notMsg--;
	if(e) return 4;
	mov=pus=0;
	for(Pchar p=psol; !isSep(*p); p++){
		rep=1;
		if(*p>='A' && *p<='Z'){
			rep= *p-'A'+1;
			p++;
		}
		direct= *p-'0';
		if(direct<0 || direct>7) return 11;
		while(rep--){
			mov++;
			if(direct<4){
				mover= nxtP(mover, direct);
				if(mover->obj!=BM_GROUND) return 2;
			}
			else{
				mover= nxtP(mover, direct-4);
				Psquare next= nxtP(mover, direct-4);
				if(mover->obj!=BM_OBJECT || next->obj!=BM_GROUND) return 3;
				next->obj= BM_OBJECT;
				mover->obj= BM_GROUND;
				pus++;
			}
		}
	}
	if(!isFinish()) return 5; //solution is wrong
	setSolution(sol, mov, pus, psol);
	return 0;
}
//---------------------------------------------------------------------------
//count moves and pushes of "psol" and assign it to "sol"
int readSolutionFast(Pchar psol, Solution *sol)
{
	int mov, pus, rep, direct;

	if(!psol || isSep(*psol)) return 10;
	mov=pus=0;
	for(Pchar p=psol; !isSep(*p); p++){
		rep=1;
		if(*p>='A' && *p<='Z'){
			rep= *p-'A'+1;
			p++;
		}
		direct= *p-'0';
		if(direct<0 || direct>7) return 11;
		mov+=rep;
		if(direct>3) pus+=rep;
	}
	setSolution(sol, mov, pus, psol);
	return 0;
}
//---------------------------------------------------------------------------
//open XSB file
int openLevel(char* fileName, bool load)
{
	int err=-1;

	FILE *f= fopen(fileName, "r");
	if(!f){
		msg(lng(730, "Cannot open file %s"), fileName);
	}
	else{
		int len=fsize(f);
		if(len){
			Pchar buf= new char[len];
			if(!buf){
				msg(lng(768, "Not enough memory"));
			}
			else{
				if(!editing){
					addLevel();
				}

				//skip text at the beginning of file
				int ch;
				for(;;){
					ch=fgetc(f);
					if(!(ch=='\n' || ch>='A' && ch<='Z' || ch>='a' && ch<='z' || ch==':')) break;
					while(ch!='\n' && ch!=EOF){
						ch=fgetc(f);
					}
				}

				//copy level to buf and replace '\n' by '!'
				Pchar s=buf;
				while(!(ch=='\n' || ch==EOF || ch>='A' && ch<='Z' || ch>='a' && ch<='z')){
					do{
						*s++= char(ch);
						ch=fgetc(f);
					} while(ch!='\n' && ch!=EOF);
					*s++='!';
					ch=fgetc(f);
				}
				*s='\0';

				//read author
				if(ch!=EOF){
					ungetc(ch, f);
					char author[128];
					while(fgets(author, sizeA(author), f))
					{
						if(!_strnicmp(author, "author:", 7)){
							char *a= author+7;
							//trim spaces
							while(*a==' ') a++;
							char *e= strchr(a, 0);
							e--;
							while(e>=a && (*e==' ' || *e=='\r' || *e=='\n')) e--;
							e++;
							*e='\0';
							size_t l=e-a;
							if(l){
								char *&la= levoff[level].author;
								dels(la);
								la= new char[l+1];
								strcpy(la, a);
							}
						}
					}
				}

				notResize++;
				err= rdLevel(buf, 0);
				notResize--;
				if(!err || err>9){
					if(editing){
						newBoard(0, 0, 1);
						fillOuter();
						resize();
					}
					else{
						notMsg++;
						optimizeLevel();
						wrLevel();
						if(load) resetLevel();
						notMsg--;
					}
				}
				delete[] buf;
			}
		}
		fclose(f);
	}
	return err;
}

void openLevel()
{
	if(openFileDlg(&levelOfn)) openLevel(fnlevel, true);
}

void importLevels(char* dir)
{
	HANDLE h;
	WIN32_FIND_DATA fd;
	char buf[MAX_PATH];

	GetCurrentDirectory(MAX_PATH, buf);
	SetCurrentDirectory(dir);
	h = FindFirstFile("*.xsb", &fd);
	if(h!=INVALID_HANDLE_VALUE){
		do{
			if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)){
				openLevel(fd.cFileName, false);
			}
		} while(FindNextFile(h, &fd));
		FindClose(h);
	}
	SetCurrentDirectory(buf);
}
//---------------------------------------------------------------------------
//open file "sokoban.dat"
int initLevels()
{
	int result=1;

start:
	rdonly=false;
	FILE *f=fopen(fndata, "r+b");
	if(!f){
		rdonly=true;
		f=fopen(fndata, "rb");
	}
	if(!f){
		result++;
		if(result==2){
			getExeDir(fndata, "sokoban.dat");
			goto start;
		}
		if(msg1(MB_YESNO,
			lng(821, "Cannot open levels database.\r\nDo you want to find it ?")
			) == IDYES){
			if(openFileDlg(&dataOfn)) goto start;
		}
	}
	else{
		//read whole file into memory
		int len=fsize(f);
		if(len>0){
			levels= new char[len+1];
			if(!levels){
				msg(lng(768, "Not enough memory"));
			}
			else{
				if(fread(levels, 1, len, f)!=unsigned(len)){
					msg(lng(754, "Read error in %s"), fndata);
				}
				else{
					levelsk= levels+len;
					*levelsk='\n';
					//total count 
					Nlevels=0;
					Pchar p= levels;
					while(p<levelsk){
						Nlevels++;
						while(*p!='\n') p++;
						p++;
					}
					//file header
					p=levels;
					if(*p=='['){
						while(*p!=']' && *p!='\n') p++;
						p++;
						if(*p=='\r') p++;
						if(*p=='\n') p++;
						Nlevels--;
					}
					if(Nlevels<=0){
						msg(lng(822, "Database is empty"));
					}
					else{
						//read all levels
						levoff= new Level[Nlevels];
						for(int i=0; i<Nlevels; i++){
							Level *lev= &levoff[i];
							lev->author="";
							lev->offset=p;
							while(!isSep(*p)) p++;
							
							/*//remove duplicates
							for(int j=0; j<i; j++){
								if(!_strnicmp(levoff[j].offset, lev->offset, p-lev->offset)){
									i--;
									Nlevels--;
									modifData=true;
									break;
								}
							}*/

							//author and solutions
							if(*p==';'){
								*p++='\0';
								lev->author=p;
								while(!isSep(*p)) p++;
								while(*p==';'){
									*p++='\0';
									readSolutionFast(p, &lev->best);
									while(!isSep(*p)) p++;
								}
							}
							*p++='\0';
							if(*p=='\n') p++;

							/*//verify solution
							if(lev->best.Mdata){
								getDim(lev->offset, lev->width, lev->height);
								if(readSolution(lev->best.Mdata, lev, &lev->best)) msg("Solution of level %d is wrong", i+1);
							}*/

							/*//remove levels which have only 1 object
							if(getNobj(lev->offset)<2){ 
								i--;
								Nlevels--;
								modifData=true;
							}*/
						}
						result=0; //success
					}
				}
			}
		}
		fclose(f);
	}
	return result;
}
//---------------------------------------------------------------------------
//open REC file
void initUser()
{
	int lastLev, i, Nerr=0;
	Level *lev;

	if(!*fnuser) return;

	FILE *f=fopen(fnuser, "rb");
	if(f){
		int len=fsize(f);
		if(len>0){
			user= new char[len+1];
			if(!user){
				msg(lng(768, "Not enough memory"));
			}
			else{
				userk= user+len;
				*userk='\n';
				if(fread(user, 1, len, f)!=unsigned(len)){
					msg(lng(754, "Read error in %s"), fnuser);
				}
				else{
					Pchar p=user;
					if(*p=='['){
						while(*p!=']'){
							if(*p=='\n') goto lclose;
							p++;
						}
						p++;
						if(*p=='\r') p++;
						if(*p=='\n') p++;
					}
					//count all levels dimensions
					for(i=0; i<Nlevels; i++){
						lev= &levoff[i];
						getDim(lev->offset, lev->width, lev->height);
					}
					//read solution
					lastLev=0;
					//int jj=0;
					while(p<userk){
						if(*p!='\r' && *p!='\n'){
							for(;;){
								if(lastLev>=Nlevels) lastLev=0;
								for(i=lastLev;;){
									lev= &levoff[i];
									if(!lev->user.Mmoves || i==lastLev){
										int e= readSolution(p, lev, &lev->user);
										if(!e){
											//set the best solution
											if(!rdonly &&
												setSolution(&lev->best, lev->user.Mmoves, lev->user.Mpushes, lev->user.Mdata)){
												modifData=true;
											}
											break;
										}
										if(e>9){ Nerr++; break; }
									}
									i++;
									if(i>=Nlevels) i=0;
									if(i==lastLev){
										Nerr++; break;
									}
								}
								lastLev=i;
								while(!isSep(*p)) p++;
								if(*p!=';') break;
								*p++='\0';
							}
						}
						lastLev++;
						//jj++;
						char cr=*p;
						*p++='\0';
						if(cr=='\r' && *p=='\n') p++;
					}
					resetLevel();
					setTitle(fnuser);
				}
			}
		}
	lclose:
		fclose(f);
		if(Nerr) msg(lng(823, "Failed to read %d solution%s"), Nerr, Nerr>1 ? "s" : "");
	}
}
//---------------------------------------------------------------------------
//save XSB file
void saveLevel()
{
	char *s = strchr(fnlevel, 0);
	while(s>=fnlevel && *s!='\\') s--;
	s++;
	sprintf(s, "L%d.xsb", level+1);
	if(saveFileDlg(&levelOfn, false)){
	start:
		FILE *f= fopen(fnlevel, "wt");
		if(!f){
			msg(lng(733, "Cannot create file %s"), fnlevel);
		}
		else{
			if(!editing) optimizeLevel();
			Pchar buf= wrLevel(false);
			if(buf){
				fputs(buf, f);
				delete[] buf;
			}
			if(*levoff[level].author){
				fprintf(f, "Author: %s\n", levoff[level].author);
			}
			if(close(f, fnlevel)==2) goto start;
		}
	}
}
//---------------------------------------------------------------------------
//execute solution without redrawing, set movError if the solution is wrong
void loadSolution(int lev, Pchar p)
{
	notdraw++;
	loadLevel(lev);
	movError=0;
	int rep=1;
	while(*p && !movError){
		if(*p>='A' && *p<='Z'){
			rep= *p-'A'+1;
			p++;
		}
		if(*p>='0' && *p<='3'){
			while(rep--){
				int o=pushes, m=moves;
				if(!fullUndo()) move(*p-'0', true);
				if(pushes!=o || moves!=m+1) movError++;
			}
			rep=1;
		}
		if(*p>='4' && *p<='7'){
			while(rep--){
				int o=pushes;
				if(!fullUndo()) move(*p-'4', true);
				if(pushes!=o+1) movError++;
			}
			rep=1;
		}
		p++;
	}
	notdraw--;
}
//---------------------------------------------------------------------------
//open SAV file
void openPos()
{
	int l;

	if(openFileDlg(&savOfn)){
		FILE *f= fopen(fnsave, "r");
		if(!f){
			msg(lng(730, "Cannot open file %s"), fnsave);
		}
		else{
			size_t len= fsize(f);
			if(len>0){
				Pchar buf= new char[len];
				if(fscanf(f, " %d ", &l)!=1){
					msg(lng(824, "There is no level number"));
				}
				else{
					aminmax(l, 1, Nlevels);
					l--;
					len= fread(buf, 1, len, f);
					buf[len]='\0';
					if(!len){
						msg(lng(754, "Read error, file %s"), fnsave);
					}
					else{
						for(int i=l;;){
							notResize++;
							loadSolution(i, buf);
							notResize--;
							if(!movError){
								resize();
								update();
								break;
							}
							i--;
							if(i<0) i=Nlevels-1;
							if(i==l){
								loadLevel(l);
								msg(lng(825, "Position is wrong"));
								break;
							}
						}
					}
				}
				delete[] buf;
			}
			fclose(f);
		}
	}
}
//---------------------------------------------------------------------------
//save solution to string
Pchar wrSolution()
{
	redoPos=0;
	undoAll();
	logPos=logbuf;
	redoAll();
	size_t len=logPos-logbuf;
	logPos=0;
	char *r = new char[len+1];
	memcpy(r, logbuf, len);
	r[len]='\0';
	return r;
}
//---------------------------------------------------------------------------
//save SAV file
void savePos()
{
	if(undoPos==rec){
		msg(lng(809, "Nothing to save"));
	}
	else{
		if(saveFileDlg(&savOfn, false)){
		start:
			FILE *f= fopen(fnsave, "wt");
			if(!f){
				msg(lng(733, "Cannot create file %s"), fnsave);
			}
			else{
				fprintf(f, "%d\n", level+1);
				Pchar buf= wrSolution();
				fputs(buf, f);
				delete[] buf;
				if(close(f, fnsave)==2) goto start;
			}
		}
	}
}
//---------------------------------------------------------------------------
//check if current game is finished and then open next level
bool finish()
{
	Pchar solution, solution2;
	Level *lev;
	Solution *sol;
	int oldM, oldP;

	stopTime= isFinish();
	if(!stopTime) return false;
	lev= &levoff[level];
	oldM=0;
	bool hiscore=false;
	solution=0;
	if(!rdonly){
		sol= &lev->best;
		if(!sol->Mmoves || eval(moves, pushes) < sol->eval()){
			//this is the best solution
			if(sol->Mmoves){ oldM=sol->Mmoves; oldP=sol->Mpushes; }
			sol->Mmoves= moves;
			sol->Mpushes= pushes;
			dels(sol->Mdata);
			sol->Mdata= solution= wrSolution();
			modifData=true;
			hiscore=true;
		}
	}
	sol= &lev->user;
	if(!sol->Mmoves || eval(moves, pushes) < sol->eval()){
		//this is the best solution of the current user
		if(sol->Mmoves){ oldM=sol->Mmoves; oldP=sol->Mpushes; }
		sol->Mmoves= moves;
		sol->Mpushes= pushes;
		dels(sol->Mdata);
		if(solution){
			solution2 = new char[strlen(solution)+1];
			strcpy(solution2, solution);
			sol->Mdata= solution2;
		}
		else{
			sol->Mdata= wrSolution();
		}
		modifUser=true;
	}
	if(oldM && gratulOn){
		msg3(MB_OK, hiscore ? lng(826, "Hiscore") : lng(827, "Congratulations !"),
			lng(828, "Previous solution: %d - %d\r\nNew solution:       %d - %d\r\n"),
			oldM, oldP, moves, pushes);
	}
	return true;
}
//---------------------------------------------------------------------------
//move selected levels to new index
void movLevels(Level *first, Level *last, Level *dest)
{
	if(!first || !last || !dest) return;
	if(last<first) first=last;
	last++;
	dest++;
	size_t n= last-first;
	Level *l= new Level[n];
	memcpy(l, first, n*sizeof(Level));
	if(dest>last){
		memmove(first, last, (dest-last)*sizeof(Level));
		dest -= n;
	}
	else if(dest<first){
		memmove(dest+n, dest, (first-dest)*sizeof(Level));
	}
	memcpy(dest, l, n*sizeof(Level));
	delete[] l;

	delete quickSave;
	quickSave=0;
	modifData=true;
}
//---------------------------------------------------------------------------
void solutionFromSok(char *fn)
{
	TfileName fnrec;

	FILE *f=fopen(fn, "rb");
	if(!f){
		msg(lng(730, "Cannot open file %s"), fn);
	}
	else{
		int len=fsize(f);
		if(len>0){
			Pchar content = new char[len+1];
			if(!content){
				msg(lng(768, "Not enough memory"));
			}
			else{
				if(fread(content, 1, len, f)!=unsigned(len)){
					msg(lng(754, "Read error in %s"), fn);
				}
				else{
					content[len]=0;
					if(strlen(fn)<5){
						msg("Invalid file name %s", fn);
					}
					else{
						strcpy(fnrec, fn);
						strcpy(fnrec+strlen(fn)-3, "rec");
						FILE *fo=fopen(fnrec, "wt");
						if(!fo){
							msg(lng(733, "Cannot create file %s"), fnrec);
						}
						else{
							fputs("[Sokoban Solutions]\n", fo);
							for(Pchar p=content;;){
								p=strstr(p, "\nSolution");
								if(!p) break;
								p=strchr(p+1, '\n');
								if(!p) break;
								for(;; p++){
									char c=0;
									if(*p=='\r') p++;
									if(*p=='\n') p++;
									switch(*p){
										case 'l': c='0'; break;
										case 'r': c='1'; break;
										case 'u': c='2'; break;
										case 'd': c='3'; break;
										case 'L': c='4'; break;
										case 'R': c='5'; break;
										case 'U': c='6'; break;
										case 'D': c='7'; break;
									}
									if(c==0) break;
									fputc(c, fo);
								}
								fputc('\n', fo);
							}
							close(fo, fnrec);
						}
					}
				}
				delete[] content;
			}
		}
		fclose(f);
	}
}
//---------------------------------------------------------------------------
void compressSolutions()
{
	for(int i=0; i<Nlevels; i++){
		Level *lev= &levoff[i];
		if(lev->best.Mdata){
			logPos=logbuf;
			int n=0;
			for(char *c=lev->best.Mdata; *c; c++){
				wrLog(*c);
				n++;
			}
			if(int(logPos-logbuf) < n){
				*logPos=0;
				strcpy(lev->best.Mdata, logbuf);
				modifData=true;
			}
		}
	}
}
//---------------------------------------------------------------------------
