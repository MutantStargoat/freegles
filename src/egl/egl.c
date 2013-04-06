#include <stdlib.h>
#include <alloca.h>
#include <X11/Xlib.h>
#include <GL/glx.h>
#include "EGL/egl.h"

#define HG_REV_STR	"foo"

#define XDPY(x)	(((struct display*)(x))->xdpy)

#define ADD_GLXATTR(ptr, attr, val) \
	(*(ptr)++ = (attr), *(ptr)++ = (val))

#define ERR_RETURN(err, ret) \
	do { \
		eglerr = err; \
		return ret; \
	} while(0)


struct display {
	Display *xdpy;
};

enum { SURF_WINDOW, SURF_PBUFFER, SURF_PIXMAP };

struct surface {
	int type;
	XID surf;
};


static int egl_to_glxattr(int attr);
static int glx_caveat(int c);
static int glx_transparent_type(int t);
static unsigned int glx_drawable_bits(int bits);


static EGLDisplay dpy;
static EGLSurface draw_surf, read_surf;
static EGLContext ctx;

static EGLint eglerr;
static EGLenum cur_api = EGL_OPENGL_ES_API;


EGLint eglGetError(void)
{
	EGLint err = eglerr;
	eglerr = 0;
	return err;
}

EGLDisplay eglGetDisplay(EGLNativeDisplayType xdpy)
{
	struct display *dpy = malloc(sizeof *dpy);
	if(!dpy) {
		return 0;
	}
	dpy->xdpy = xdpy;
	return dpy;
}

EGLBoolean eglInitialize(EGLDisplay display, EGLint *major, EGLint *minor)
{
	if(!display)
		ERR_RETURN(EGL_BAD_DISPLAY, EGL_FALSE);

	if(!XDPY(display)) {
		if(!(XDPY(display) = XOpenDisplay(0))) {
			ERR_RETURN(EGL_NOT_INITIALIZED, EGL_FALSE);
		}
	}
	dpy = display;

	*major = 1;
	*minor = 4;
	return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, EGL_FALSE);

	if(XDPY(dpy)) {
		XCloseDisplay(XDPY(dpy));
	}
	return EGL_TRUE;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, 0);
	if(!XDPY(dpy))
		ERR_RETURN(EGL_NOT_INITIALIZED, 0);

	switch(name) {
	case EGL_CLIENT_APIS:
		return "OpenGL OpenGL_ES";

	case EGL_VENDOR:
		return "FreeGLES";

	case EGL_VERSION:
		return "1.4 FreeGLES rev " HG_REV_STR;

	case EGL_EXTENSIONS:
		return "";

	default:
		break;
	}

	ERR_RETURN(EGL_BAD_PARAMETER, 0);
}


EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint cfgsize, EGLint *num_cfg)
{
	int i, num_glxcfg, count;
	GLXFBConfig *glxcfg;

	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, EGL_FALSE);
	if(!XDPY(dpy))
		ERR_RETURN(EGL_NOT_INITIALIZED, EGL_FALSE);
	if(!num_cfg)
		ERR_RETURN(EGL_BAD_PARAMETER, EGL_FALSE);

	glxcfg = glXGetFBConfigs(XDPY(dpy), DefaultScreen(XDPY(dpy)), &num_glxcfg);
	count = cfgsize < num_glxcfg ? cfgsize : num_glxcfg;

	for(i=0; i<count; i++) {
		configs[i] = glxcfg + i;
	}
	*num_cfg = count;
	return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs,
		EGLint cfgsize, EGLint *num_cfg)
{
	int i, count, *glx_attr, glx_nelems, num_attr;
	int *glx_aptr;
	const int *egl_aptr;
	GLXFBConfig *glxcfg;
	unsigned int drawable_type = 0;

	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, EGL_FALSE);
	if(!XDPY(dpy))
		ERR_RETURN(EGL_NOT_INITIALIZED, EGL_FALSE);
	if(!num_cfg)
		ERR_RETURN(EGL_BAD_PARAMETER, EGL_FALSE);

	for(i=0; attrib_list[i] != EGL_NONE; i++);
	num_attr = i + 1;

	egl_aptr = attrib_list;
	glx_aptr = glx_attr = alloca(num_attr * sizeof *glx_attr);

	for(;;) {
		int eattr, arg;

		eattr = *egl_aptr++;

		if(eattr == EGL_NONE) {
			*glx_aptr = None;
			break;
		}
		arg = *egl_aptr++;

		switch(eattr) {
		case EGL_BUFFER_SIZE:
		case EGL_RED_SIZE:
		case EGL_GREEN_SIZE:
		case EGL_BLUE_SIZE:
		case EGL_ALPHA_SIZE:
		case EGL_DEPTH_SIZE:
		case EGL_STENCIL_SIZE:
		case EGL_SAMPLES:
		case EGL_SAMPLE_BUFFERS:
		case EGL_CONFIG_ID:
		case EGL_LEVEL:
		case EGL_TRANSPARENT_RED_VALUE:
		case EGL_TRANSPARENT_GREEN_VALUE:
		case EGL_TRANSPARENT_BLUE_VALUE:
			ADD_GLXATTR(glx_aptr, egl_to_glxattr(eattr), arg);
			break;

		case EGL_CONFIG_CAVEAT:
			ADD_GLXATTR(glx_aptr, GLX_CONFIG_CAVEAT, glx_caveat(arg));
			break;

		case EGL_TRANSPARENT_TYPE:
			ADD_GLXATTR(glx_aptr, GLX_TRANSPARENT_TYPE, glx_transparent_type(arg));
			break;

		case EGL_BIND_TO_TEXTURE_RGB:
		case EGL_BIND_TO_TEXTURE_RGBA:
			drawable_type |= GLX_WINDOW_BIT | GLX_PBUFFER_BIT;
			break;
		case EGL_SURFACE_TYPE:
			drawable_type |= glx_drawable_bits(arg);
			break;

			/* ignore the rest for now ... */
		/*case EGL_LUMINANCE_SIZE:
		case EGL_COLOR_BUFFER_TYPE:
		case EGL_CONFORMANT:
		case EGL_RENDERABLE_TYPE:*/
		default:
			break;
		}
	}

	if(drawable_type) {
		ADD_GLXATTR(glx_aptr, GLX_DRAWABLE_TYPE, drawable_type);
		*glx_aptr++ = None;	/* re-insert a None at the end */
	}

	if(!(glxcfg = glXChooseFBConfig(XDPY(dpy), DefaultScreen(XDPY(dpy)), glx_attr, &glx_nelems))) {
		*num_cfg = 0;
		return EGL_TRUE;
	}

	count = glx_nelems < cfgsize ? glx_nelems : cfgsize;

	for(i=0; i<count; i++) {
		*(GLXFBConfig*)configs[i] = glxcfg[i];
	}
	XFree(glxcfg);

	*num_cfg = count;
	return EGL_TRUE;
}

/* TODO fix this */
EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attrib, EGLint *val)
{
	int glx_attr = egl_to_glxattr(attrib);

	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, EGL_FALSE);
	if(!XDPY(dpy))
		ERR_RETURN(EGL_NOT_INITIALIZED, EGL_FALSE);
	if(!config)
		ERR_RETURN(EGL_BAD_CONFIG, EGL_FALSE);
	if(glx_attr == -1)
		ERR_RETURN(EGL_BAD_ATTRIBUTE, EGL_FALSE);

	if(glXGetFBConfigAttrib(XDPY(dpy), config, egl_to_glxattr(attrib), val) != 0) {
		return EGL_FALSE;
	}

	return EGL_TRUE;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg, EGLNativeWindowType win, const EGLint *attr_list)
{
	struct surface *surf;
	if(!(surf = malloc(sizeof *surf))) {
		ERR_RETURN(EGL_BAD_ALLOC, 0);
	}
	surf->type = SURF_WINDOW;
	surf->surf = win;

	return surf;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig cfg, const EGLint *attrib_list)
{
	abort();	/* TODO */
	return 0;
}

EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig cfg, EGLNativePixmapType pixmap, const EGLint *attr_list)
{
	abort();	/* TODO */
	return 0;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surf)
{
	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, EGL_FALSE);
	if(!XDPY(dpy))
		ERR_RETURN(EGL_NOT_INITIALIZED, EGL_FALSE);
	if(!surf || !((struct surface*)surf)->surf)
		ERR_RETURN(EGL_BAD_SURFACE, EGL_FALSE);

	free(surf);
	return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surf, EGLint attr, EGLint *val)
{
	abort();
	return EGL_FALSE;
}

EGLBoolean eglBindAPI(EGLenum api)
{
	switch(api) {
	case EGL_OPENGL_API:
	case EGL_OPENGL_ES_API:
		cur_api = api;

	case EGL_OPENVG_API:
		return EGL_FALSE;

	default:
		break;
	}

	ERR_RETURN(EGL_BAD_PARAMETER, EGL_FALSE);
}

EGLenum eglQueryAPI(void)
{
	return cur_api;
}

EGLBoolean eglWaitClient(void)
{
	glFinish();
	return EGL_TRUE;
}

EGLBoolean eglReleaseThread(void)
{
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglBindAPI(EGL_OPENGL_ES_API);
	return EGL_TRUE;
}

EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype,
		EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list)
{
	abort();	/* TODO */
	return 0;
}

EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
{
	abort();	/* TODO */
	return EGL_FALSE;
}

EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
	abort();	/* TODO */
	return EGL_FALSE;
}

EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
	abort();	/* TODO */
	return EGL_FALSE;
}


EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
	abort();	/* TODO */
	return EGL_FALSE;
}


EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share_ctx,
		const EGLint *attrib_list)
{
	XVisualInfo *vis;
	GLXContext ctx;

	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, 0);
	if(!XDPY(dpy))
		ERR_RETURN(EGL_NOT_INITIALIZED, 0);

	if(!(vis = glXGetVisualFromFBConfig(XDPY(dpy), *(GLXFBConfig*)cfg))) {
		ERR_RETURN(EGL_BAD_CONFIG, 0);
	}

	/* TODO
	 * setup X error handler to catch and translate:
	 * - BadMatch -> EGL_BAD_MATCH
	 * - BadValue -> EGL_BAD_CONFIG
	 * - GLXBadContext -> EGL_BAD_CONTEXT
	 * - BadAlloc -> EGL_BAD_ALLOC
	 */
	if(!(ctx = glXCreateContext(XDPY(dpy), vis, share_ctx, True))) {
		ERR_RETURN(EGL_BAD_CONTEXT, 0);
	}

	XFree(vis);
	return ctx;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, EGL_FALSE);
	if(!XDPY(dpy))
		ERR_RETURN(EGL_NOT_INITIALIZED, EGL_FALSE);

	/* TODO catch X error GLXBadContext and return EGL_BAD_CONTEXT */
	glXDestroyContext(XDPY(dpy), ctx);
	return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext context)
{
	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, EGL_FALSE);
	if(!XDPY(dpy))
		ERR_RETURN(EGL_NOT_INITIALIZED, EGL_FALSE);

	/* TODO X11 err -> EGL error mapping:
	 * - BadMatch -> EGL_BAD_MATCH
	 * - BadAccess -> EGL_BAD_ACCESS
	 * - GLXBadDrawable -> EGL_BAD_SURFACE (?)
	 * - GLXBadContext -> EGL_BAD_CONTEXT
	 * - GLXBadCurrentWindow -> EGL_BAD_NATIVE_WINDOW
	 * - BadAlloc -> EGL_BAD_ALLOC
	 */
	if(glXMakeCurrent(XDPY(dpy), ((struct surface*)draw)->surf, context)) {
		ctx = context;
		draw_surf = draw;
		read_surf = read;
		return EGL_TRUE;
	}
	return EGL_FALSE;
}

EGLContext eglGetCurrentContext(void)
{
	return ctx;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw)
{
	switch(readdraw) {
	case EGL_DRAW:
		return draw_surf;
	case EGL_READ:
		return read_surf;
	default:
		break;
	}
	return 0;
}

EGLDisplay eglGetCurrentDisplay(void)
{
	return dpy;
}

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
{
	/* TODO */
	abort();
	return EGL_FALSE;
}

EGLBoolean eglWaitGL(void)
{
	/*EGLenum api = eglQueryAPI();
	EGLBoolean res = eglBindAPI(EGL_OPENGL_ES_API);
	eglWaitClient();
	eglBindAPI(api);
	return res;*/

	glXWaitGL();
	return EGL_TRUE;
}

EGLBoolean eglWaitNative(EGLint engine)
{
	if(engine != EGL_CORE_NATIVE_ENGINE) {
		ERR_RETURN(EGL_BAD_PARAMETER, EGL_FALSE);
	}

	/* TODO X errors:
	 * - GLXBadCurrentWindow -> EGL_BAD_CURRENT_SURFACE
	 */
	glXWaitX();
	return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
	if(!dpy)
		ERR_RETURN(EGL_BAD_DISPLAY, EGL_FALSE);
	if(!XDPY(dpy))
		ERR_RETURN(EGL_NOT_INITIALIZED, EGL_FALSE);

	/* TODO X errors:
	 * - GLXBadDrawable -> EGL_BAD_SURFACE
	 * - GLXBadCurrentWindow -> EGL_BAD_SURFACE
	 */
	glXSwapBuffers(XDPY(dpy), ((struct surface*)surface)->surf);
	return EGL_TRUE;
}

EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
{
	abort();
	return EGL_FALSE;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
	return glXGetProcAddress((const unsigned char*)procname);
}

static int egl_to_glxattr(int attr)
{
	switch(attr) {
	case EGL_BUFFER_SIZE: return GLX_BUFFER_SIZE;
	case EGL_RED_SIZE: return GLX_RED_SIZE;
	case EGL_GREEN_SIZE: return GLX_GREEN_SIZE;
	case EGL_BLUE_SIZE: return GLX_BLUE_SIZE;
	case EGL_ALPHA_SIZE: return GLX_ALPHA_SIZE;
	case EGL_DEPTH_SIZE: return GLX_DEPTH_SIZE;
	case EGL_STENCIL_SIZE: return GLX_STENCIL_SIZE;
	case EGL_SAMPLES: return GLX_SAMPLES;
	case EGL_SAMPLE_BUFFERS: return GLX_SAMPLE_BUFFERS;
	case EGL_CONFIG_ID: return GLX_FBCONFIG_ID;
	case EGL_LEVEL: return GLX_LEVEL;
	case EGL_TRANSPARENT_RED_VALUE: return GLX_TRANSPARENT_RED_VALUE;
	case EGL_TRANSPARENT_GREEN_VALUE: return GLX_TRANSPARENT_GREEN_VALUE;
	case EGL_TRANSPARENT_BLUE_VALUE: return GLX_TRANSPARENT_BLUE_VALUE;
	default:
		break;
	}
	return -1;
}

static int glx_caveat(int c)
{
	switch(c) {
	case EGL_NONE:
		return GLX_NONE;
	case EGL_SLOW_CONFIG:
		return GLX_SLOW_CONFIG;
	case EGL_NON_CONFORMANT_CONFIG:
		return GLX_NON_CONFORMANT_CONFIG;
	case EGL_DONT_CARE:
	default:
		break;
	}
	return GLX_DONT_CARE;
}

static int glx_transparent_type(int t)
{
	switch(t) {
	case EGL_NONE:
		return GLX_NONE;
	case EGL_TRANSPARENT_RGB:
		return GLX_TRANSPARENT_RGB;
	default:
		break;
	}
	return -1;
}

static unsigned int glx_drawable_bits(int bits)
{
	unsigned int res = 0;

	if(bits & EGL_PBUFFER_BIT)
		res |= GLX_PBUFFER_BIT;
	if(bits & EGL_WINDOW_BIT)
		res |= GLX_WINDOW_BIT;
	if(bits & EGL_PIXMAP_BIT)
		res |= GLX_PIXMAP_BIT;

	return res;
}
