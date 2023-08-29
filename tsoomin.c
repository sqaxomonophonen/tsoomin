// $ cc -Wall -std=c11 $(pkg-config --cflags x11 xext gl) tsoomin.c -o tsoomin $(pkg-config --libs x11 xext gl)

#ifndef ZOOM_SPEED
#define ZOOM_SPEED (0.06) // mouse wheel -> zoom factor multiplier
#endif

#ifndef N_SNAP_BACK_FRAMES
#define N_SNAP_BACK_FRAMES 5 // number of frame spent "returning to normal":
#endif

#ifndef PAN_MOUSE_BUTTON
#define PAN_MOUSE_BUTTON 1 // LMB=1, MMB=2, RMB=3
#endif

#ifndef MODIFIER
#define MODIFIER     Mod4Mask // OS-key; you can change it with -D options or whatever
#endif

#ifndef MOVE_FRICTION
#define MOVE_FRICTION (0.85f) // cursor movement: friction
#endif

#ifndef MOVE_ACCELERATION
#define MOVE_ACCELERATION (2.5f) // cursor movement: acceleration
#endif

#ifndef MOVE_BRAKE
#define MOVE_BRAKE (0.7f) // cursor movement: brake (when releaseing keys)
#endif

#ifndef TRACKING_SPEED
#define TRACKING_SPEED (0.6f) // how fast it moves to the target rectangle (which is "discrete" due to mouse wheel events)
#endif

#ifndef MOTION_BLUR_SPEED
#define MOTION_BLUR_SPEED (0.5f) // how fast/slow motion blur "lags behind TRACKING_SPEED"
#endif

#include <X11/keysym.h>
static const int MOVE_KEYS[][4] = {
	{'w', XK_Up,          0, -1 },
	{'a', XK_Left,       -1,  0 },
	{'s', XK_Down,        0,  1 },
	{'d', XK_Right,       1,  0 },
};
enum present_shader {
	BLURRY,
	NOISY,
} present_shader = NOISY;

#define N_MOVE_KEYS (sizeof(MOVE_KEYS) / sizeof(MOVE_KEYS[0]))

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <assert.h>

#include <X11/Xlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>


static void chkgl(const char* file, const int line)
{
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		fprintf(stderr, "OPENGL ERROR 0x%.4x in %s:%d\n", err, file, line);
		abort();
	}
}
#define CHKGL chkgl(__FILE__, __LINE__);

#define IS_Q0 "(gl_VertexID == 0 || gl_VertexID == 3)"
#define IS_Q1 "(gl_VertexID == 1)"
#define IS_Q2 "(gl_VertexID == 2 || gl_VertexID == 4)"
#define IS_Q3 "(gl_VertexID == 5)"

static GLuint create_shader(GLenum type, const char* src)
{
	GLuint shader = glCreateShader(type); CHKGL;
	glShaderSource(shader, 1, &src, NULL); CHKGL;
	glCompileShader(shader); CHKGL;
	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE) {
		GLint msglen;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &msglen);
		GLchar* msg = (GLchar*) malloc(msglen + 1);
		assert(msg != NULL);
		glGetShaderInfoLog(shader, msglen, NULL, msg);
		const char* stype = type == GL_VERTEX_SHADER ? "VERTEX" : type == GL_FRAGMENT_SHADER ? "FRAGMENT" : "???";
		fprintf(stderr, "%s GLSL COMPILE ERROR: %s in\n\n%s\n", stype, msg, src);
		abort();
	}

	return shader;
}

union rect {
	struct {
		float x0, y0, x1, y1;
	};
	float s[4];
};

Display* display;

int tsoom(Window root, XButtonEvent* initial_event)
{
	int screen = -1;
	const int n_screens = ScreenCount(display);
	for (int i = 0; i < n_screens; i++) {
		if (root == RootWindow(display, i)) {
			screen = i;
			break;
		}
	}
	assert(screen != -1);

	XWindowAttributes root_attrs = {0};
	assert(XGetWindowAttributes(display, root, &root_attrs));

	const int width = root_attrs.width;
	const int height = root_attrs.height;

	XShmSegmentInfo segfo = {0};

	XImage* x_image = XShmCreateImage(
		display,
		DefaultVisual(display, screen),
		DefaultDepthOfScreen(ScreenOfDisplay(display, screen)),
		ZPixmap,
		NULL,
		&segfo,
		width,
		height);
	assert((x_image != NULL) && "XShmCreateImage() failed");

	const size_t bitmap_sz = x_image->bytes_per_line * height;

	segfo.shmid = shmget(IPC_PRIVATE, bitmap_sz, IPC_CREAT | 0666);
	assert(segfo.shmid != -1);
	segfo.shmaddr = shmat(segfo.shmid, NULL, 0);
	x_image->data = segfo.shmaddr;
	segfo.readOnly = 0;
	assert(segfo.shmaddr != NULL);

	assert(XShmAttach(display, &segfo));

	assert(XShmGetImage(display, root, x_image, 0, 0, AllPlanes));

	//printf("%d×%db%d\n", width, height, x_image->bits_per_pixel);
	// assert(1 == fwrite(segfo.shmaddr, bitmap_sz, 1, stdout));
	// $ convert -size 1920x1080 -depth 8 BGRA:zzz.bin out.png

	XVisualInfo* vis = glXChooseVisual(display, 0, (int[]){
		GLX_RGBA,
		GLX_DOUBLEBUFFER,
		None,
	});
	assert(vis != NULL);

	XSetWindowAttributes window_attrs = {0};

	window_attrs.colormap = XCreateColormap(display, root, vis->visual, AllocNone);
	window_attrs.override_redirect = 1;
	window_attrs.save_under = 1;

	Window window = XCreateWindow(
		display,
		root,
		/*x=*/0,
		/*y=*/0,
		width,
		height,
		/*border_width=*/0,
		vis->depth,
		InputOutput,
		vis->visual,
		CWColormap | CWOverrideRedirect | CWSaveUnder,
		&window_attrs);

	char title[1<<10];
	snprintf(title, sizeof title, "tsoomin %dx%db%d", width, height, (int)x_image->bits_per_pixel);
	XStoreName(display, window, title);

	XMapWindow(display, window);

	GLXContext glctx = glXCreateContext(display, vis, NULL, GL_TRUE);
	assert(glXMakeCurrent(display, window, glctx));

	GLuint program = glCreateProgram(); CHKGL;
	GLint u_rect0, u_rect1, u_texture;
	{
		char* vert_src =
		"#version 130\n"
		"\n"
		"uniform vec4 u_rect0;\n"
		"uniform vec4 u_rect1;\n"
		"\n"
		"varying vec2 v_uv0;\n"
		"varying vec2 v_uv1;\n"
		"\n"
		"void main(void)\n"
		"{\n"
		"	vec2 p;\n"
		"	if (" IS_Q0 ") {\n"
		"		p = vec2(-1.0,  1.0);\n"
		"		v_uv0 = u_rect0.xy;\n"
		"		v_uv1 = u_rect1.xy;\n"
		"	} else if (" IS_Q1 ") {\n"
		"		p = vec2( 1.0,  1.0);\n"
		"		v_uv0 = u_rect0.zy;\n"
		"		v_uv1 = u_rect1.zy;\n"
		"	} else if (" IS_Q2 ") {\n"
		"		p = vec2( 1.0, -1.0);\n"
		"		v_uv0 = u_rect0.zw;\n"
		"		v_uv1 = u_rect1.zw;\n"
		"	} else if (" IS_Q3 ") {\n"
		"		p = vec2(-1.0, -1.0);\n"
		"		v_uv0 = u_rect0.xw;\n"
		"		v_uv1 = u_rect1.xw;\n"
		"	}\n"
		"	gl_Position = vec4(p, 0.0, 1.0);\n"
		"}\n"
		;

		char* frag_src =

		(present_shader == BLURRY)
		?
		"#version 130\n"
		"\n"
		"uniform sampler2D u_texture;\n"
		"\n"
		"varying vec2 v_uv0;\n"
		"varying vec2 v_uv1;\n"
		"\n"
		"void main(void)\n"
		"{\n"
		"	const int N = 8;\n"
		"	vec3 acc = vec3(0,0,0);\n"
		"	for (int i = 0; i < N; i++) {\n"
		"		vec2 uv = mix(v_uv0, v_uv1, float(i)/float(N));\n"
		"		if (0.0 <= uv.x && uv.x <= 1.0 && 0.0 <= uv.y && uv.y <= 1.0) {\n"
		"			acc += texture2D(u_texture, uv).xyz;\n"
		"		}\n"
		"	}\n"
		"	acc *= 1.0 / float(N);\n"
		"	gl_FragColor = vec4(acc, 1.0);\n"
		"}\n"

		:
		(present_shader == NOISY)
		?

		"#version 130\n"
		"\n"
		"uniform sampler2D u_texture;\n"
		"\n"
		"varying vec2 v_uv0;\n"
		"varying vec2 v_uv1;\n"
		"\n"
		"float rand(vec2 co)\n"
		"{\n"
		"       return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);\n"
		"}\n"
		"void main(void)\n"
		"{\n"
		"	const int N = 2;\n"
		"	vec3 acc = vec3(0,0,0);\n"
		"	for (int i = 0; i < N; i++) {\n"
		"		vec2 uv = mix(v_uv0, v_uv1, rand(v_uv0+v_uv1+vec2(i,-i)));\n"
		"		if (0.0 <= uv.x && uv.x <= 1.0 && 0.0 <= uv.y && uv.y <= 1.0) {\n"
		"			acc += texture2D(u_texture, uv).xyz;\n"
		"		}\n"
		"	}\n"
		"	acc *= 1.0 / float(N);\n"
		"	gl_FragColor = vec4(acc, 1.0);\n"
		"}\n"

		:
		NULL
		;

		const GLuint vs = create_shader(GL_VERTEX_SHADER, vert_src);
		const GLuint fs = create_shader(GL_FRAGMENT_SHADER, frag_src);
		glAttachShader(program, vs); CHKGL;
		glAttachShader(program, fs); CHKGL;
		glLinkProgram(program); CHKGL;

		GLint status;
		glGetProgramiv(program, GL_LINK_STATUS, &status);
		if (status == GL_FALSE) {
			GLint msglen;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &msglen);
			GLchar* msg = (GLchar*) malloc(msglen + 1);
			glGetProgramInfoLog(program, msglen, NULL, msg);
			fprintf(stderr, "ERROR: shader link error: %s\n", msg);
			exit(EXIT_FAILURE);
		}

		glDeleteShader(vs); CHKGL;
		glDeleteShader(fs); CHKGL;

		u_rect0 = glGetUniformLocation(program, "u_rect0"); CHKGL;
		u_rect1 = glGetUniformLocation(program, "u_rect1"); CHKGL;
		u_texture = glGetUniformLocation(program, "u_texture"); CHKGL;
	}

	GLuint texture;
	glGenTextures(1, &texture); CHKGL;
	glBindTexture(GL_TEXTURE_2D, texture); CHKGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); CHKGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); CHKGL;
	glTexImage2D(GL_TEXTURE_2D, /*level=*/0, GL_RGBA, width, height, /*border=*/0, GL_BGRA, GL_UNSIGNED_BYTE, segfo.shmaddr); CHKGL;

	XDestroyImage(x_image);
	XShmDetach(display, &segfo);
	shmdt(segfo.shmaddr);
	shmctl(segfo.shmid, IPC_RMID, 0);

	int exiting = 0;
	const union rect home_rect = {{0,0,1,1}};
	union rect rect0 = home_rect;
	union rect rect1 =  home_rect;
	union rect target_rect =  home_rect;
	int dzoom = 0;
	int mx = initial_event->x_root;
	int my = initial_event->y_root;
	if (initial_event->button == 4) dzoom++;
	if (initial_event->button == 5) dzoom--;

	int move_x = 0;
	int move_y = 0;
	int is_panning = 0;
	float vx = 0.0f;
	float vy = 0.0f;

	int move_key_state[N_MOVE_KEYS] = {0};

	while (exiting <= N_SNAP_BACK_FRAMES) {
		if (exiting) target_rect = home_rect;
		int pan_x = 0;
		int pan_y = 0;
		while (XPending(display)) {
			XEvent xev;
			XNextEvent(display, &xev);
			if (XFilterEvent(&xev, None)) continue;
			switch (xev.type) {
			case ButtonPress:
			case ButtonRelease: {
				const int is_press = (xev.type == ButtonPress);
				XButtonEvent xb = xev.xbutton;
				mx = xb.x;
				my = xb.y;
				const int b = xb.button;
				if (is_press) {
					if (1 <= b && b <= 3 && b != PAN_MOUSE_BUTTON) exiting++;
					if (b == 4) dzoom++;
					if (b == 5) dzoom--;
				}
				if (b == PAN_MOUSE_BUTTON) is_panning = is_press;
			} break;
			case MotionNotify: {
				if (is_panning) {
					XMotionEvent xm = xev.xmotion;
					pan_x += -(xm.x - mx);
					pan_y += -(xm.y - my);
					mx = xm.x;
					my = xm.y;
				}
			} break;
			case KeyPress:
			case KeyRelease: {
				XKeyEvent xk = xev.xkey;
				KeySym sym = XLookupKeysym(&xk, 0);
				const int is_press = (xev.type == KeyPress);
				if (sym == XK_Escape) exiting++;
				move_x = 0;
				move_y = 0;
				for (int i0 = 0; i0 < N_MOVE_KEYS; i0++) {
					for (int i1 = 0; i1 < 2; i1++) {
						const int set_mask = 1 << i1;
						if (sym == MOVE_KEYS[i0][i1]) {
							if (is_press) {
								move_key_state[i0] |= set_mask;
							} else {
								move_key_state[i0] &= ~set_mask;
							}
						}
					}
					const int st = move_key_state[i0];
					if (st) {
						move_x += MOVE_KEYS[i0][2];
						move_y += MOVE_KEYS[i0][3];
					}
				}
			}
			}
		}

		{
			union rect* r = &target_rect;
			const float m = (float)dzoom * (float)ZOOM_SPEED;
			const float nx = (r->x1 - r->x0) / (float)width;
			const float ny = (r->y1 - r->y0) / (float)height;

			const float cx = r->x0 + (float)mx * nx;
			const float cy = r->y0 + (float)my * ny;

			if (move_x) {
				vx += (float)move_x * MOVE_ACCELERATION;
				vx *= MOVE_FRICTION;
			} else {
				vx *= MOVE_BRAKE;
			}
			if (move_y) {
				vy += (float)move_y * MOVE_ACCELERATION;
				vy *= MOVE_FRICTION;
			} else {
				vy *= MOVE_BRAKE;
			}

			const float d = (float)height * 0.001f;
			const float dx = nx*(vx*d + (float)pan_x);
			const float dy = ny*(vy*d + (float)pan_y);

			r->x0 += m * (cx - r->x0) + dx;
			r->y0 += m * (cy - r->y0) + dy;
			r->x1 += m * (cx - r->x1) + dx;
			r->y1 += m * (cy - r->y1) + dy;

			dzoom = 0;
		}

		glViewport(0, 0, width, height);
		glClearColor(1,0,1,1);
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(program); CHKGL;
		glBindTexture(GL_TEXTURE_2D, texture); CHKGL;
		glUniform4fv(u_rect0, 1, rect0.s);
		glUniform4fv(u_rect1, 1, rect1.s);
		glUniform1i(u_texture, 0);
		glDrawArrays(GL_TRIANGLES, 0, 6); CHKGL;

		glXSwapBuffers(display, window);

		for (int i = 0; i < 4; i++) {
			rect0.s[i] += (target_rect.s[i] - rect0.s[i]) * TRACKING_SPEED;
			rect1.s[i] += (target_rect.s[i] - rect1.s[i]) * TRACKING_SPEED * MOTION_BLUR_SPEED;
		}
		if (exiting) exiting++;
	}

	glDeleteTextures(1, &texture);
	glDeleteProgram(program);

	glXDestroyContext(display, glctx);
	XDestroyWindow(display, window);

	return EXIT_SUCCESS;
}

static void grab(int is_grab, int stage)
{
	unsigned int modifiers;
	int button0, button1;
	int full;
	switch (stage) {
	case 0:
		// when invisible grab ONLY modifier key + mouse wheel
		modifiers = MODIFIER;
		button0 = 4;
		button1 = 5;
		full = 0;
		break;
	case 1:
		// when zooming grab all buttons
		modifiers = AnyModifier;
		button0 = 1;
		button1 = 5;
		full = 1;
		break;
	default: assert(!"unhandled stage");
	}
	for (int screen = 0; screen < ScreenCount(display); screen++) {
		Window root = RootWindow(display, screen);
		if (full) {
			if (is_grab) {
				XGrabKeyboard(
					display,
					root,
					False,
					GrabModeAsync,
					GrabModeAsync,
					CurrentTime);
				XGrabPointer(
					display,
					root,
					False,
					PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
					GrabModeAsync,
					GrabModeAsync,
					None,
					None,
					CurrentTime);
			} else {
				XUngrabPointer(display, CurrentTime);
				XUngrabKeyboard(display, CurrentTime);
			}
		}

		for (int button = button0; button <= button1; button++) {
			if (is_grab) {
				XGrabButton(
					display,
					button,
					modifiers,
					root,
					False,
					ButtonPressMask,
					GrabModeAsync,
					GrabModeAsync,
					None,
					None);
			} else {
				XUngrabButton(
					display,
					button,
					modifiers,
					root);
			}
		}
	}
}

int main(int argc, char** argv)
{
	setlocale(LC_ALL, "");

	XInitThreads();

	display = XOpenDisplay(NULL);
	if (display == NULL) {
		fprintf(stderr, "ERROR: no x11 display :-(\n");
		exit(EXIT_FAILURE);
	}

	XAllowEvents(display, AsyncBoth, CurrentTime);

	grab(1, 0);

	for (;;) {
		XEvent xev;
		XNextEvent(display, &xev);
		if (XFilterEvent(&xev, None)) {
			continue;
		}
		switch (xev.type) {
		case ButtonPress:
			if (xev.xbutton.state == MODIFIER) {
				const int b = xev.xbutton.button;
				if (b == 4 || b == 5) {
					grab(0, 0);
					grab(1, 1);
					tsoom(xev.xbutton.root, &xev.xbutton);
					grab(0, 1);
					grab(1, 0);
				}
			}
			break;
		}
	}

	XCloseDisplay(display);

	return EXIT_SUCCESS;
}
