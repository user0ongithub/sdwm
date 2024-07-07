#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#include "util.h"

#define BUTTONMASK              (ButtonPressMask | ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~ (numlockmask | LockMask) & \
				(ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | \
				Mod4Mask | Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w), (m)->wx+(m)->ww) - MAX((x), (m)->wx)) * \
				MAX(0, MIN((y)+(h), (m)->wy+(m)->wh) - MAX((y), (m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->monitor->tagset[C->monitor->selected_tags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK | PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << ntags) - 1)

enum { CursorNormal, CursorResize, CursorMove, CursorLast };
enum { ColorNormal, ColorSelected };
enum { ClickClientWindow, ClickRootWindow, ClickLast };

union argument
{
	int i;
	unsigned int ui;
	float f;
	const void * v;
};

struct client
{
        int x, y, w, h;
        int oldx, oldy, oldw, oldh;
        int bw, oldbw;
        unsigned int tags;
        int isfloating, oldstate, isfullscreen;
        struct client * next;
        struct client * snext;
        struct monitor * monitor;
        Window window;
};

struct monitor
{
        float mfact;
        int nmasters;
        int num;
        int mx, my, mw, mh;
        int wx, wy, ww, wh;
        unsigned int tagset[2];
        unsigned int selected_tags;
        unsigned int selected_layout;
        struct client * clients;
        struct client * selected_client;
        struct client * stack;
        struct monitor * next;
        const void (** layouts[2]) (struct monitor *);
};

struct button
{
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (* function) (const union argument * argument);
	const union argument argument;
};

struct key
{
	unsigned int modifier;
	KeySym keysym;
	void (* function) (const union argument *);
	const union argument argument;
};

static int applysizehints(struct client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(struct monitor * monitor);
static void arrangemon(struct monitor * monitor);
static void attach(struct client *c);
static void attachstack(struct client *c);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(struct monitor *mon);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static struct monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(struct client *c);
static void detachstack(struct client *c);
static struct monitor *dirtomon(int dir);
static void enternotify(XEvent *e);
static void focus(struct client *c);
static void focusin(XEvent *e);
static void focusmon(const union argument *argument);
static void focusstack(const union argument *argument);
static int getrootptr(int *x, int *y);
static void grabbuttons(struct client *c, int focused);
static void grabkeys(void);
static void incnmaster(const union argument *argument);
static void keypress(XEvent *e);
static void killclient(const union argument *argument);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(struct monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const union argument *argument);
static struct client *nexttiled(struct client *c);
static void pop(struct client *c);
static void quit(const union argument *argument);
static struct monitor *recttomon(int x, int y, int w, int h);
static void resize(struct client *c, int x, int y, int w, int h, int interact);
static void resizeclient(struct client *c, int x, int y, int w, int h);
static void resizemouse(const union argument *argument);
static void restack(struct monitor *monitor);
static void run(void);
static void scan(void);
static void sendmon(struct client *c, struct monitor *m);
static void togglefullscreen(const union argument *argument);
static void setlayout(const union argument *argument);
static void setmfact(const union argument *argument);
static void setup(void);
static void showhide(struct client * client);
static void tag(const union argument *argument);
static void tagmon(const union argument *argument);
static void tile(struct monitor *m);
static void togglefloating(const union argument *argument);
static void toggletag(const union argument *argument);
static void toggleview(const union argument *argument);
static void unfocus(struct client *c, int setfocus);
static void unmanage(struct client *c, int destroyed);
static void unmapnotify(XEvent *e);
static int updategeom(void);
static void updatenumlockmask(void);
static void view(const union argument *argument);
static struct client *wintoclient(Window w);
static struct monitor *wintomon(Window w);
static int error_handler(Display *display, XErrorEvent *ee);
static int dummy_error_handler(Display *display, XErrorEvent *ee);
static int another_wm_error_handler(Display *display, XErrorEvent *ee);
static void zoom(const union argument *argument);

static Display * display;
static int screen_number;
static int screen_width, screen_height;
static Window root_window;
static int (* default_error_handler)(Display *, XErrorEvent *);
static void (* handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[UnmapNotify] = unmapnotify
};
static int running = 1;
static Cursor cursors[CursorLast];
static XColor * colors;
static struct monitor * monitors, * selected_monitor;
static unsigned int numlockmask = 0;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* Compile-time check if no. of tags fit into an unsigned bit array */
struct dummy { char dummy[sizeof ntags > 31 ? -1 : 1]; };

int applysizehints(struct client * c, int * x, int * y, int * w, int * h, int interact)
{
	struct monitor *m = c->monitor;
	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > screen_width)
			*x = screen_width - WIDTH(c);
		if (*y > screen_height)
			*y = screen_height - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void attach(struct client * c)
{
	c->next = c->monitor->clients;
	c->monitor->clients = c;
}

void attachstack(struct client * c)
{
	c->snext = c->monitor->stack;
	c->monitor->stack = c;
}

void buttonpress(XEvent * e)
{
	unsigned int i, click;
	struct client *c;
	struct monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;
	click = ClickRootWindow;
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selected_monitor) {
		unfocus(selected_monitor->selected_client, 1);
		selected_monitor = m;
		focus(NULL);
	}
	if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selected_monitor);
		XAllowEvents(display, ReplayPointer, CurrentTime);
		click = ClickClientWindow;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].function && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].function(&buttons[i].argument);
}

void checkotherwm(void)
{
	default_error_handler = XSetErrorHandler(another_wm_error_handler);
	/* this causes an error if some other window manager is running */
	XSelectInput(display, DefaultRootWindow(display), SubstructureRedirectMask);
	XSync(display, False);
	XSetErrorHandler(error_handler);
	XSync(display, False);
}

void cleanup(void)
{
	union argument a = {.ui = ~0};
	const void (* layout) (struct monitor *) = 0;
	struct monitor *m;
	size_t i;
	view(&a);
	selected_monitor->layouts[selected_monitor->selected_layout] = &layout;
	for (m = monitors; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(display, AnyKey, AnyModifier, root_window);
	while (monitors)
		cleanupmon(monitors);
	for (i = 0; i < CursorLast; i++)
		XFreeCursor(display, cursors[i]);
	free(colors);
	XSync(display, False);
	XSetInputFocus(display, PointerRoot, RevertToPointerRoot, CurrentTime);
}

void cleanupmon(struct monitor * mon)
{
	struct monitor * m;
	if (mon == monitors)
		monitors = monitors->next;
	else {
		for (m = monitors; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	free(mon);
}

void configurenotify(XEvent * e)
{
	struct monitor * m;
	struct client * c;
	XConfigureEvent * ev = &e->xconfigure;
	int dirty;
	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root_window) {
		dirty = (screen_width != ev->width || screen_height != ev->height);
		screen_width = ev->width;
		screen_height = ev->height;
		if (updategeom() || dirty) {
			for (m = monitors; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void configurerequest(XEvent * e)
{
	struct client * c;
	struct monitor * m;
	XConfigureRequestEvent * ev = &e->xconfigurerequest;
	XWindowChanges wc;
	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if (c->isfloating || !selected_monitor->layouts[selected_monitor->selected_layout]) {
			m = c->monitor;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if (ISVISIBLE(c))
				XMoveResizeWindow(display, c->window, c->x, c->y, c->w, c->h);
		}
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(display, ev->window, ev->value_mask, &wc);
	}
	XSync(display, False);
}

struct monitor * createmon(void)
{
	struct monitor * m;
	m = ecalloc(1, sizeof(struct monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmasters = nmasters;
	m->layouts[0] = &layouts[0];
	m->layouts[1] = &layouts[LENGTH(layouts) - 1];
	return m;
}

void destroynotify(XEvent *e)
{
	struct client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;
	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
}

void detach(struct client *c)
{
	struct client **tc;
	for (tc = &c->monitor->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void detachstack(struct client *c)
{
	struct client **tc, *t;
	for (tc = &c->monitor->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;
	if (c == c->monitor->selected_client) {
		for (t = c->monitor->stack; t && !ISVISIBLE(t); t = t->snext);
		c->monitor->selected_client = t;
	}
}

struct monitor * dirtomon(int dir)
{
	struct monitor *m = NULL;
	if (dir > 0) {
		if (!(m = selected_monitor->next))
			m = monitors;
	} else if (selected_monitor == monitors)
		for (m = monitors; m->next; m = m->next);
	else
		for (m = monitors; m->next != selected_monitor; m = m->next);
	return m;
}

void enternotify(XEvent *e)
{
	struct client *c;
	struct monitor *m;
	XCrossingEvent *ev = &e->xcrossing;
	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root_window)
		return;
	c = wintoclient(ev->window);
	m = c ? c->monitor : wintomon(ev->window);
	if (m != selected_monitor) {
		unfocus(selected_monitor->selected_client, 1);
		selected_monitor = m;
	} else if (!c || c == selected_monitor->selected_client)
		return;
	focus(c);
}

void focus(struct client *c)
{
	if (!c || !ISVISIBLE(c))
		for (c = selected_monitor->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selected_monitor->selected_client && selected_monitor->selected_client != c)
		unfocus(selected_monitor->selected_client, 0);
	if (c) {
		if (c->monitor != selected_monitor)
			selected_monitor = c->monitor;
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		XSetWindowBorder(display, c->window, colors[ColorSelected].pixel);
		XSetInputFocus(display, c->window, RevertToPointerRoot, CurrentTime);
	} else 
		XSetInputFocus(display, root_window, RevertToPointerRoot, CurrentTime);
	selected_monitor->selected_client = c;
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;
	if (selected_monitor->selected_client && ev->window != selected_monitor->selected_client->window)
		XSetInputFocus(display, selected_monitor->selected_client->window, RevertToPointerRoot, CurrentTime);
}

void focusmon(const union argument *argument)
{
	struct monitor *m;
	if (!monitors->next)
		return;
	if ((m = dirtomon(argument->i)) == selected_monitor)
		return;
	unfocus(selected_monitor->selected_client, 0);
	selected_monitor = m;
	focus(NULL);
}

void focusstack(const union argument *argument)
{
	struct client *c = NULL, *i;
	if (!selected_monitor->selected_client ||
		(selected_monitor->selected_client->isfullscreen && lockfullscreen))
		return;
	if (argument->i > 0) {
		for (c = selected_monitor->selected_client->next; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			for (c = selected_monitor->clients; c && !ISVISIBLE(c); c = c->next);
	} else {
		for (i = selected_monitor->clients; i != selected_monitor->selected_client; i = i->next)
			if (ISVISIBLE(i)) c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i)) c = i;
	}
	if (c) {
		focus(c);
		restack(selected_monitor);
	}
}

int getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;
	return XQueryPointer(display, root_window, &dummy, &dummy, x, y, &di, &di, &dui);
}

void grabbuttons(struct client *c, int focused)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(display, AnyButton, AnyModifier, c->window);
		if (!focused)
			XGrabButton(display, AnyButton, AnyModifier, c->window, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClickClientWindow)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(display, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->window, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j, k;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;
		XUngrabKey(display, AnyKey, AnyModifier, root_window);
		XDisplayKeycodes(display, &start, &end);
		syms = XGetKeyboardMapping(display, start, end - start + 1, &skip);
		if (!syms)
			return;
		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				/* skip modifier codes, we do that ourselves */
				if (keys[i].keysym == syms[(k - start) * skip])
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabKey(display, k,
							 keys[i].modifier | modifiers[j],
							 root_window, True,
							 GrabModeAsync, GrabModeAsync);
		XFree(syms);
	}
}

void incnmaster(const union argument *argument)
{
	selected_monitor->nmasters = MAX(selected_monitor->nmasters + argument->i, 0);
	arrange(selected_monitor);
}

#ifdef XINERAMA
static int isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;
	ev = &e->xkey;
	keysym = XKeycodeToKeysym(display, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].modifier) == CLEANMASK(ev->state)
		&& keys[i].function)
			keys[i].function(&(keys[i].argument));
}

void killclient(const union argument *argument)
{
	if (!selected_monitor->selected_client)
		return;
	XGrabServer(display);
	XSetErrorHandler(dummy_error_handler);
	XSetCloseDownMode(display, DestroyAll);
	XKillClient(display, selected_monitor->selected_client->window);
	XSync(display, False);
	XSetErrorHandler(error_handler);
	XUngrabServer(display);
}

void manage(Window w, XWindowAttributes *wa)
{
	struct client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;
	c = ecalloc(1, sizeof(struct client));
	c->window = w;
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;
	if (XGetTransientForHint(display, w, &trans) && (t = wintoclient(trans))) {
		c->monitor = t->monitor;
		c->tags = t->tags;
	} else {
		c->monitor = selected_monitor;
		c->tags = c->monitor->tagset[c->monitor->selected_tags];
	}
	if (c->x + WIDTH(c) > c->monitor->wx + c->monitor->ww)
		c->x = c->monitor->wx + c->monitor->ww - WIDTH(c);
	if (c->y + HEIGHT(c) > c->monitor->wy + c->monitor->wh)
		c->y = c->monitor->wy + c->monitor->wh - HEIGHT(c);
	c->x = MAX(c->x, c->monitor->wx);
	c->y = MAX(c->y, c->monitor->wy);
	c->bw = border_pixel;
	wc.border_width = c->bw;
	XConfigureWindow(display, w, CWBorderWidth, &wc);
	XSetWindowBorder(display, w, colors[ColorNormal].pixel);
	XSelectInput(display, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None;
	if (c->isfloating)
		XRaiseWindow(display, c->window);
	attach(c);
	attachstack(c);
	/* some windows require this
	XMoveResizeWindow(display, c->window, c->x + 2 * screen_width, c->y, c->w, c->h); */
	if (c->monitor == selected_monitor)
		unfocus(selected_monitor->selected_client, 0);
	c->monitor->selected_client = c;
	arrange(c->monitor);
	XMapWindow(display, c->window);
	focus(NULL);
}

void mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;
	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;
	if (!XGetWindowAttributes(display, ev->window, &wa) || wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void monocle(struct monitor *m)
{
	unsigned int n = 0;
	struct client *c;
	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c))
			n++;
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

void motionnotify(XEvent *e)
{
	static struct monitor *mon = NULL;
	struct monitor *m;
	XMotionEvent *ev = &e->xmotion;
	if (ev->window != root_window)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selected_monitor->selected_client, 1);
		selected_monitor = m;
		focus(NULL);
	}
	mon = m;
}

void movemouse(const union argument *argument)
{
	int x, y, ocx, ocy, nx, ny;
	struct client *c;
	struct monitor *m;
	XEvent ev;
	Time lasttime = 0;
	if (!(c = selected_monitor->selected_client))
		return;
	if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selected_monitor);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(display, root_window, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursors[CursorMove], CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
	do {
		XMaskEvent(display, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;
			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selected_monitor->wx - nx) < snap)
				nx = selected_monitor->wx;
			else if (abs((selected_monitor->wx + selected_monitor->ww) - (nx + WIDTH(c))) < snap)
				nx = selected_monitor->wx + selected_monitor->ww - WIDTH(c);
			if (abs(selected_monitor->wy - ny) < snap)
				ny = selected_monitor->wy;
			else if (abs((selected_monitor->wy + selected_monitor->wh) - (ny + HEIGHT(c))) < snap)
				ny = selected_monitor->wy + selected_monitor->wh - HEIGHT(c);
			if (!c->isfloating &&
				selected_monitor->layouts[selected_monitor->selected_layout] &&
				(abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL);
			if (!selected_monitor->layouts[selected_monitor->selected_layout] ||
				c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(display, CurrentTime);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selected_monitor) {
		sendmon(c, m);
		selected_monitor = m;
		focus(NULL);
	}
}

struct client * nexttiled(struct client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

void pop(struct client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange(c->monitor);
}

void quit(const union argument *argument) { running = 0; }

struct monitor * recttomon(int x, int y, int w, int h)
{
	struct monitor *m, *r = selected_monitor;
	int a, area = 0;
	for (m = monitors; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void resize(struct client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void resizeclient(struct client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;
	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(display, c->window, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	XSync(display, False);
}

void resizemouse(const union argument *argument)
{
	int ocx, ocy, nw, nh;
	struct client *c;
	struct monitor *m;
	XEvent ev;
	Time lasttime = 0;
	if (!(c = selected_monitor->selected_client))
		return;
	if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selected_monitor);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(display, root_window, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursors[CursorResize], CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(display, None, c->window, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	do {
		XMaskEvent(display, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;
			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
			if (c->monitor->wx + nw >= selected_monitor->wx &&
				c->monitor->wx + nw <= selected_monitor->wx + selected_monitor->ww &&
				c->monitor->wy + nh >= selected_monitor->wy &&
				c->monitor->wy + nh <= selected_monitor->wy + selected_monitor->wh)
			{
				if (!c->isfloating &&
					selected_monitor->layouts[selected_monitor->selected_layout]
					&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}
			if (!selected_monitor->layouts[selected_monitor->selected_layout] ||
				c->isfloating)
				resize(c, c->x, c->y, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(display, None, c->window, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(display, CurrentTime);
	while (XCheckMaskEvent(display, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selected_monitor) {
		sendmon(c, m);
		selected_monitor = m;
		focus(NULL);
	}
}

void run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(display, False);
	while (running && !XNextEvent(display, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;
	if (XQueryTree(display, root_window, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(display, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(display, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(display, wins[i], &wa))
				continue;
			if (XGetTransientForHint(display, wins[i], &d1)
			&& wa.map_state == IsViewable)
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void sendmon(struct client *c, struct monitor *m)
{
	if (c->monitor == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->monitor = m;
	c->tags = m->tagset[m->selected_tags]; /* assign tags of target monitor */
	attach(c);
	attachstack(c);
	focus(NULL);
	arrange(NULL);
}

void togglefullscreen(const union argument *argument)
{
	struct client * c;
        if (!selected_monitor->selected_client)
                return;
        c = selected_monitor->selected_client;
        c->isfullscreen = !c->isfullscreen;
	if (c->isfullscreen)
	{
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->monitor->mx, c->monitor->my, c->monitor->mw, c->monitor->mh);
		XRaiseWindow(display, c->window);
	}
	else
	{
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange(c->monitor);
	}
}

void setlayout(const union argument * argument)
{
	if (!argument || !argument->v || argument->v !=
		selected_monitor->layouts[selected_monitor->selected_layout])
		selected_monitor->selected_layout ^= 1;
	if (argument && argument->v)
		selected_monitor->layouts[selected_monitor->selected_layout] =
			(void (**) (struct monitor *)) argument->v;
	if (selected_monitor->selected_client)
		arrange(selected_monitor);
}

/* argument > 1.0 will set mfact absolutely */
void setmfact(const union argument *argument)
{
	float f;
	if (!argument || !selected_monitor->layouts[selected_monitor->selected_layout])
		return;
	f = argument->f < 1.0 ? argument->f + selected_monitor->mfact : argument->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;
	selected_monitor->mfact = f;
	arrange(selected_monitor);
}

void setup(void)
{
	int i;
	XSetWindowAttributes wa;
	/* init screen */
	screen_number = DefaultScreen(display);
	screen_width = DisplayWidth(display, screen_number);
	screen_height = DisplayHeight(display, screen_number);
	root_window = RootWindow(display, screen_number);
	updategeom();
	/* init cursors */
	cursors[CursorNormal] = XCreateFontCursor(display, XC_left_ptr);
	cursors[CursorResize] = XCreateFontCursor(display, XC_sizing);
	cursors[CursorMove] = XCreateFontCursor(display, XC_fleur);
	/* init appearance */
	colors = ecalloc(LENGTH(color_scheme), sizeof (XColor));
	for (i = 0; i < LENGTH(color_scheme); i++)
	{
		if (!XAllocNamedColor(display,
			DefaultColormap(display, screen_number),
			color_scheme[i],
			&colors[i],
			&colors[i]))
			die("error, cannot allocate color '%s'", color_scheme[i]);
		colors[i].pixel |= 0xff << 24;
	}
	/* select events */
	wa.cursor = cursors[CursorNormal];
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(display, root_window, CWEventMask|CWCursor, &wa);
	XSelectInput(display, root_window, wa.event_mask);
	grabkeys();
	focus(NULL);
}

void tag(const union argument *argument)
{
	if (selected_monitor->selected_client && argument->ui & TAGMASK) {
		selected_monitor->selected_client->tags = argument->ui & TAGMASK;
		focus(NULL);
		arrange(selected_monitor);
	}
}

void tagmon(const union argument *argument)
{
	if (!selected_monitor->selected_client || !monitors->next)
		return;
	sendmon(selected_monitor->selected_client, dirtomon(argument->i));
}

void tile(struct monitor *m)
{
	unsigned int i, n, h, mw, my, ty;
	struct client *c;
	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;
	if (n > m->nmasters)
		mw = m->nmasters ? m->ww * m->mfact : 0;
	else
		mw = m->ww;
	for (i = my = ty = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmasters) {
			h = (m->wh - my) / (MIN(n, m->nmasters) - i);
			resize(c, m->wx, m->wy + my, mw - (2*c->bw), h - (2*c->bw), 0);
			if (my + HEIGHT(c) < m->wh)
				my += HEIGHT(c);
		} else {
			h = (m->wh - ty) / (n - i);
			resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2*c->bw), h - (2*c->bw), 0);
			if (ty + HEIGHT(c) < m->wh)
				ty += HEIGHT(c);
		}
}

void togglefloating(const union argument *argument)
{
	if (!selected_monitor->selected_client)
		return;
	if (selected_monitor->selected_client->isfullscreen) /* no support for fullscreen windows */
		return;
	selected_monitor->selected_client->isfloating = !selected_monitor->selected_client->isfloating;
	if (selected_monitor->selected_client->isfloating)
		resize(selected_monitor->selected_client,
			selected_monitor->selected_client->oldx,
			selected_monitor->selected_client->oldy,
			selected_monitor->selected_client->oldw,
			selected_monitor->selected_client->oldh, 0);
	arrange(selected_monitor);
}

void toggletag(const union argument *argument)
{
	unsigned int newtags;
	if (!selected_monitor->selected_client)
		return;
	newtags = selected_monitor->selected_client->tags ^ (argument->ui & TAGMASK);
	if (newtags) {
		selected_monitor->selected_client->tags = newtags;
		focus(NULL);
		arrange(selected_monitor);
	}
}

void toggleview(const union argument *argument)
{
	unsigned int newtagset =
		selected_monitor->tagset[selected_monitor->selected_tags] ^ (argument->ui & TAGMASK);
	if (newtagset) {
		selected_monitor->tagset[selected_monitor->selected_tags] = newtagset;
		focus(NULL);
		arrange(selected_monitor);
	}
}

void unfocus(struct client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(display, c->window, colors[ColorNormal].pixel);
	if (setfocus)
		XSetInputFocus(display, root_window, RevertToPointerRoot, CurrentTime);
}

void unmanage(struct client *c, int destroyed)
{
	struct monitor *m = c->monitor;
	XWindowChanges wc;
	detach(c);
	detachstack(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(display); /* avoid race conditions */
		XSetErrorHandler(dummy_error_handler);
		XSelectInput(display, c->window, NoEventMask);
		XConfigureWindow(display, c->window, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(display, AnyButton, AnyModifier, c->window);
		XSync(display, False);
		XSetErrorHandler(error_handler);
		XUngrabServer(display);
	}
	free(c);
	focus(NULL);
	arrange(m);
}

void unmapnotify(XEvent *e)
{
	struct client *c;
	XUnmapEvent *ev = &e->xunmap;
	if ((c = wintoclient(ev->window))) {
		if (!ev->send_event)
			unmanage(c, 0);
	}
}

int updategeom(void)
{
	int dirty = 0;
#ifdef XINERAMA
	if (XineramaIsActive(display))
	{
		int i, j, n, nn;
		struct client *c;
		struct monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(display, &nn);
		XineramaScreenInfo *unique = NULL;
		for (n = 0, m = monitors; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;
		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = monitors; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				monitors = createmon();
		}
		for (i = 0, m = monitors; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh)
			{
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = monitors; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				detachstack(c);
				c->monitor = monitors;
				attach(c);
				attachstack(c);
			}
			if (m == selected_monitor)
				selected_monitor = monitors;
			cleanupmon(m);
		}
		free(unique);
	}
	else
	{
#endif /* XINERAMA */
		/* default monitor setup */
		if (!monitors)
			monitors = createmon();
		if (monitors->mw != screen_width || monitors->mh != screen_height) {
			dirty = 1;
			monitors->mw = monitors->ww = screen_width;
			monitors->mh = monitors->wh = screen_height;
		}
#ifdef XINERAMA
	}
#endif /* XINERAMA */
	if (dirty)
	{
		selected_monitor = monitors;
		selected_monitor = wintomon(root_window);
	}
	return dirty;
}

void updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;
	numlockmask = 0;
	modmap = XGetModifierMapping(display);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(display, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void view(const union argument *argument)
{
	if ((argument->ui & TAGMASK) == selected_monitor->tagset[selected_monitor->selected_tags])
		return;
	selected_monitor->selected_tags ^= 1; /* toggle sel tagset */
	if (argument->ui & TAGMASK)
		selected_monitor->tagset[selected_monitor->selected_tags] = argument->ui & TAGMASK;
	focus(NULL);
	arrange(selected_monitor);
}

struct client * wintoclient(Window w)
{
	struct client *c;
	struct monitor *m;
	for (m = monitors; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->window == w)
				return c;
	return NULL;
}

struct monitor * wintomon(Window w)
{
	int x, y;
	struct client *c;
	if (w == root_window && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	if ((c = wintoclient(w)))
		return c->monitor;
	return selected_monitor;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int error_handler(Display *display, XErrorEvent *event)
{
	if (event->error_code == BadWindow
	|| (event->request_code == X_SetInputFocus && event->error_code == BadMatch)
	|| (event->request_code == X_PolyText8 && event->error_code == BadDrawable)
	|| (event->request_code == X_PolyFillRectangle && event->error_code == BadDrawable)
	|| (event->request_code == X_PolySegment && event->error_code == BadDrawable)
	|| (event->request_code == X_ConfigureWindow && event->error_code == BadMatch)
	|| (event->request_code == X_GrabButton && event->error_code == BadAccess)
	|| (event->request_code == X_GrabKey && event->error_code == BadAccess)
	|| (event->request_code == X_CopyArea && event->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "sdwm: fatal error, request code=%d, error code=%d\n",
		event->request_code, event->error_code);
	return default_error_handler(display, event); /* may call exit */
}

int dummy_error_handler(Display *display, XErrorEvent * event) { return 0; }

int another_wm_error_handler(Display *display, XErrorEvent * event)
{
	die("sdwm: another window manager is already running");
	return -1;
}

void zoom(const union argument * argument)
{
	struct client * c = selected_monitor->selected_client;
	if (!selected_monitor->layouts[selected_monitor->selected_layout] || !c || c->isfloating)
		return;
	if (c == nexttiled(selected_monitor->clients) && !(c = nexttiled(c->next)))
		return;
	pop(c);
}

void arrangemon(struct monitor * monitor)
{
        if (monitor->layouts[monitor->selected_layout] &&
                * monitor->layouts[monitor->selected_layout])
                (* monitor->layouts[monitor->selected_layout])(monitor);
}

void restack(struct monitor * monitor)
{
        struct client * client;
        XWindowChanges changes;
        XEvent event;
        if (!monitor->selected_client)
                return;
        if (monitor->selected_client->isfloating ||
                !monitor->layouts[monitor->selected_layout])
                XRaiseWindow(display, monitor->selected_client->window);
        if (monitor->layouts[monitor->selected_layout]) {
                changes.stack_mode = Below;
                changes.sibling = monitor->stack->window;
                for (client = monitor->stack->snext; client; client = client->snext)
                        if (!client->isfloating && ISVISIBLE(client)) {
                                XConfigureWindow(display,
                                        client->window,
                                        CWSibling | CWStackMode,
                                        &changes);
                                changes.sibling = client->window;
                        }
        }
        XSync(display, False);
        while (XCheckMaskEvent(display, EnterWindowMask, &event));
}

void arrange(struct monitor * monitor)
{
        if (monitor) showhide(monitor->stack);
        else for (monitor = monitors; monitor; monitor = monitor->next)
                showhide(monitor->stack);
        if (monitor) {
                arrangemon(monitor);
                restack(monitor);
        } else for (monitor = monitors; monitor; monitor = monitor->next)
                arrangemon(monitor);
}

void showhide(struct client * client)
{
        if (!client)
                return;
        if (ISVISIBLE(client)) {
                XMoveWindow(display, client->window, client->x, client->y);
                if ((!client->monitor->layouts[client->monitor->selected_layout] ||
                        client->isfloating) &&
                        !client->isfullscreen)
                        resize(client, client->x, client->y, client->w, client->h, 0);
                showhide(client->snext);
        } else {
                showhide(client->snext);
                XMoveWindow(display, client->window, WIDTH(client) * -2, client->y);
        }
}

int main(int argc, char * argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
	{
		puts(VERSION);
		return 0;
	}
	else if (argc != 1)
		die("usage: %s [-v]", argv[0]);
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(display = XOpenDisplay(NULL)))
		die("sdwm: cannot open display");
	checkotherwm();
	setup();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	run();
	cleanup();
	XCloseDisplay(display);
	return EXIT_SUCCESS;
}
