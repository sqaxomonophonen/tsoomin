// $ cc -Wall -std=c11 $(pkg-config --cflags x11 xext gl) tsoomin.c -o tsoomin $(pkg-config --libs x11 xext gl)

#ifndef MODIFIER
#define MODIFIER     Mod4Mask // OS-key; you can change it with -D options or whatever
#endif

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

enum present {
	BLURRY,
	NOISY,
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

	enum present present = BLURRY;

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

		(present == BLURRY)
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
		(present == NOISY)
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
		"	vec2 uv = mix(v_uv0, v_uv1, rand(v_uv0+v_uv1));\n"
		"	if (0.0 <= uv.x && uv.x <= 1.0 && 0.0 <= uv.y && uv.y <= 1.0) {\n"
		"		gl_FragColor = vec4(texture2D(u_texture, uv).xyz, 1.0);\n"
		"	} else {\n"
		"		gl_FragColor = vec4(0,0,0,1);\n"
		"	}\n"
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
	shmctl(segfo.shmid, IPC_RMID, 0);

	int exiting = 0;
	union rect rect0 = {{0,0,1,1}};
	union rect rect1 = rect0;
	union rect target_rect = rect0;
	int dzoom = 0;
	int mx = initial_event->x_root;
	int my = initial_event->y_root;
	if (initial_event->button == 4) dzoom++;
	if (initial_event->button == 5) dzoom--;
	while (!exiting) {
		while (XPending(display)) {
			XEvent xev;
			XNextEvent(display, &xev);
			if (XFilterEvent(&xev, None)) continue;
			switch (xev.type) {
			case ButtonPress: {
				mx = xev.xbutton.x;
				my = xev.xbutton.y;
				const int b = xev.xbutton.button;
				if (1 <= b && b <= 3) exiting = 1;
				if (b == 4) dzoom++;
				if (b == 5) dzoom--;
			} break;
			}
		}

		if (dzoom) {
			union rect* r = &target_rect;
			const float m = (float)dzoom * 0.03f;
			const float cx = r->x0 + (r->x1-r->x0) * ((float)mx / (float)width);
			const float cy = r->y0 + (r->y1-r->y0) * ((float)my / (float)height);
			r->x0 += m * (cx - r->x0);
			r->y0 += m * (cy - r->y0);
			r->x1 += m * (cx - r->x1);
			r->y1 += m * (cy - r->y1);
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
			rect0.s[i] += (target_rect.s[i] - rect0.s[i]) * 0.7f;
			rect1.s[i] += (target_rect.s[i] - rect1.s[i]) * 0.3f;
		}
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
	switch (stage) {
	case 0:
		// when invisible grab ONLY modifier key + mouse wheel
		modifiers = MODIFIER;
		button0 = 4;
		button1 = 5;
		break;
	case 1:
		// when zooming grab all buttons
		modifiers = AnyModifier;
		button0 = 1;
		button1 = 5;
		break;
	default: assert(!"unhandled stage");
	}
	for (int screen = 0; screen < ScreenCount(display); screen++) {
		for (int button = button0; button <= button1; button++) {
			if (is_grab) {
				XGrabButton(
					display,
					button,
					modifiers,
					RootWindow(display, screen),
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
					RootWindow(display, screen));
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
