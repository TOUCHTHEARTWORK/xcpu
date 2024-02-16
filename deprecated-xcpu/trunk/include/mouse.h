#ifndef _MOUSE_H_
#define _MOUSE_H_ 1
#if defined(__cplusplus)
extern "C" { 
#endif
typedef struct	Menu Menu;
typedef struct 	Mousectl Mousectl;

struct	Mouse
{
	int	buttons;	/* bit array: LMR=124 */
	Point	xy;
	ulong	msec;
};

struct Mousectl
{
	Mouse	m;
	struct Channel	*c;	/* chan(Mouse) */
	struct Channel	*resizec;	/* chan(int)[2] */
			/* buffered in case client is waiting for a mouse action before handling resize */

	char		*file;
	int		mfd;		/* to mouse file */
	int		cfd;		/* to cursor file */
	int		pid;		/* of slave proc */
	Display		*display;
	/*Image*	image;	/ * of associated window/display */
	
	/* clumsy hack for X11 */
	struct Channel *ccursor;
	struct Channel *ccursorwait;
	QLock cursorlock;
};

struct Menu
{
	char	**item;
	char	*(*gen)(int);
	int	lasthit;
};

/*
 * Mouse
 */
extern Mousectl*	initmouse(char*, Image*);
extern void		moveto(Mousectl*, Point);
extern int			readmouse(Mousectl*);
extern void		closemouse(Mousectl*);
struct Cursor;
extern void		setcursor(Mousectl*, struct Cursor*);
extern void		drawgetrect(Rectangle, int);
extern Rectangle	getrect(int, Mousectl*);
extern int	 		menuhit(int, Mousectl*, Menu*, Screen*);

extern void		bouncemouse(Mouse*);
extern int			_windowhasfocus;	/* XXX do better */
extern int			_wantfocuschanges;

#if defined(__cplusplus)
}
#endif
#endif