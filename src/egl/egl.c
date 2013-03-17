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


static int egl_to_glxattr(int attr);
static int glx_caveat(int c);
static int glx_transparent_type(int t);
static unsigned int glx_drawable_bits(int bits);


/*static EGLDisplay dpy;*/
static EGLSurface draw_surf, read_surf;
static EGLContext ctx;

static EGLint eglerr;


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

	if(glXGetFBConfigAttrib(XDPY(dpy), config, egl_to_glxattr(attrib), value) != 0) {
		return EGL_FALSE;
	}

	return EGL_TRUE;
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
