#define MODKEY Mod1Mask

static const unsigned int border_pixel  = 1;        /* border pixel of windows */
static const unsigned int snap      = 32;       /* snap pixel */

static const char *color_scheme[]    = {
	[ColorNormal] = "#444444",
	[ColorSelected]  = "#009900",
};

static const unsigned int ntags = 9;

static const float mfact     = 0.55; /* factor of master area size [0.05..0.95] */
static const int nmasters     = 1;    /* number of clients in master area */

static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */

static const void (* layouts[]) (struct monitor *) = { tile, monocle, 0 };

#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                         KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY | ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY | ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY | ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

static const struct key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },

	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_d,      incnmaster,     {.i = -1 } },

	{ MODKEY,                       XK_h,      setmfact,       {.f = -0.05} },
	{ MODKEY,                       XK_l,      setmfact,       {.f = +0.05} },

	{ MODKEY,                       XK_Return, zoom,           {0} },

	{ MODKEY,                       XK_Tab,    view,           {0} },

	{ MODKEY | ShiftMask,             XK_c,      killclient,     {0} },

	{ MODKEY,                       XK_t,      setlayout,      {.v = &layouts[0]} },
	{ MODKEY,                       XK_m,      setlayout,      {.v = &layouts[1]} },
	{ MODKEY,                       XK_f,      setlayout,      {.v = &layouts[2]} },
	{ MODKEY,                       XK_space,  setlayout,      {0} },

	{ MODKEY | ShiftMask,             XK_space,  togglefloating, {0} },
	{ MODKEY | ShiftMask,             XK_f,      togglefullscreen,  {0} },

	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } },
	{ MODKEY | ShiftMask,             XK_0,      tag,            {.ui = ~0 } },

	{ MODKEY,                       XK_comma,  focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_period, focusmon,       {.i = +1 } },

	{ MODKEY | ShiftMask,             XK_comma,  tagmon,         {.i = -1 } },
	{ MODKEY | ShiftMask,             XK_period, tagmon,         {.i = +1 } },

	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)

	{ MODKEY|ShiftMask,             XK_q,      quit,           {0} },
};

/* button definitions */
/* click can be ClickClientWin, or ClickRootWin */
static const struct button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClickClientWindow,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClickClientWindow,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClickClientWindow,         MODKEY,         Button3,        resizemouse,    {0} },
};
