
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#define EXIT(msg) { fputs (msg, stderr); exit (EXIT_FAILURE); }

// global variables declarations

static int device;
static drmModeRes *resources;
static drmModeConnector *connector;
static uint32_t connector_id;
static drmModeEncoder *encoder;
static drmModeModeInfo mode_info;
static drmModeCrtc *crtc;
static struct gbm_device *gbm_device;
static EGLDisplay display;
static EGLContext context;
static struct gbm_surface *gbm_surface;
static EGLSurface egl_surface;
       EGLConfig config;
       EGLint num_config;
       EGLint count=0;
       EGLConfig *configs;
       int config_index;
       int i;
       
static struct gbm_bo *previous_bo = NULL;
static uint32_t previous_fb;       

static EGLint attributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
		};

static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

struct gbm_bo *bo;	
uint32_t handle;
uint32_t pitch;
int32_t fb;
uint64_t modifier;


static drmModeConnector *find_connector (drmModeRes *resources) {

  for (i=0; i<resources->count_connectors; i++) {

    drmModeConnector *connector = drmModeGetConnector (device, resources->connectors[i]);

    if (connector->connection == DRM_MODE_CONNECTED) {

      return connector;

    }

    drmModeFreeConnector (connector);

  }

  return NULL; // if no connector found

}

static drmModeEncoder *find_encoder (drmModeRes *resources, drmModeConnector *connector) {

  if (connector->encoder_id) {

    return drmModeGetEncoder (device, connector->encoder_id);

  }

  return NULL; // if no encoder found

}

static void swap_buffers () {

  eglSwapBuffers (display, egl_surface);

  /*
  
    Lock rendering to the surface's current front buffer until it is
    released with gbm_surface_release_buffer()

  */

  bo = gbm_surface_lock_front_buffer (gbm_surface);


  /*
  
    Render buffer to DRM

  */

  handle = gbm_bo_get_handle (bo).u32;
  pitch = gbm_bo_get_stride (bo);

  drmModeAddFB (
    device, 
    mode_info.hdisplay, 
    mode_info.vdisplay, 
    24, 
    32, 
    pitch, 
    handle, 
    &fb
  );

  drmModeSetCrtc (
    device, 
    crtc->crtc_id, 
    fb, 
    0, 
    0, 
    &connector_id, 
    1, 
    &mode_info
  );

  if (previous_bo) {

    drmModeRmFB (device, previous_fb);
    gbm_surface_release_buffer (gbm_surface, previous_bo);

  }

  previous_bo = bo;
  previous_fb = fb;

}

static void draw (float progress) {

  glClearColor (1.0f - progress, progress / 2.0f, progress, 1.0);
  glClear (GL_COLOR_BUFFER_BIT);
  swap_buffers ();

}

static int match_config_to_visual(EGLDisplay egl_display, EGLint visual_id, EGLConfig *configs, int count) {

  EGLint id;

  for (i = 0; i < count; ++i) {

    if (!eglGetConfigAttrib(egl_display, configs[i], EGL_NATIVE_VISUAL_ID,&id)) 

      continue;

    if (id == visual_id)

      return i;

  }

  return -1;

}

static void clean_up() {

  /*
    
    Set the previous CRTC

  */

  drmModeSetCrtc(
    device, 
    crtc->crtc_id, 
    crtc->buffer_id, 
    crtc->x, crtc->y, 
    &connector_id, 
    1, 
    &crtc->mode
  );

  drmModeFreeCrtc (crtc);

  if (previous_bo) {

    drmModeRmFB (device, previous_fb);
    gbm_surface_release_buffer (gbm_surface, previous_bo);
  
  }

  eglDestroySurface (display, egl_surface);
  gbm_surface_destroy (gbm_surface);
  eglDestroyContext (display, context);
  eglTerminate (display);
  gbm_device_destroy (gbm_device);

  close (device);

}

void signal_handler(int signal) {

  clean_up();

  exit(0);

}

int main () {

  /*
  
    close program on interruption

  */

  signal(SIG_INT, signal_handler);

  /*

    open gpu device

  */

  device = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);

  /*

    retrieve current display configuration information

      - count_fbs - the number of allocated framebuffers
      - count_crtcs - the number of available CRTCs. CRTCs are responsible for sending the framebuffer to the display
      - count_connectors - the number of physical display connectors available
      - count_encoders - the number of encoders available. Encoders convert the pixel stream into a specific device protocol such as HDMI

  */

  resources = drmModeGetResources (device);

  /*
    
    Retrieve the first connected connector found
    Choose the first mode

  */

  connector = find_connector (resources);
  connector_id = connector->connector_id;
  mode_info = connector->modes[0];

  /*
    
    Retrive the encoder currently being used

  */

  encoder = find_encoder (resources, connector);

  /*
    
    Retrieve the currently display mode to restore after program exits

  */

  crtc = drmModeGetCrtc (device, encoder->crtc_id);


  drmModeFreeEncoder (encoder);
  drmModeFreeConnector (connector);
  drmModeFreeResources (resources);

  /*
    
    Setup OpenGL

  */

  gbm_device = gbm_create_device (device);

  gbm_surface = gbm_surface_create (
    gbm_device, 
    mode_info.hdisplay, 
    mode_info.vdisplay, 
    GBM_FORMAT_XRGB8888, 
    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
  );

  display = eglGetDisplay (gbm_device);
  eglInitialize (display, NULL ,NULL);

  /*
    
    Create OpenGL context

  */

  eglBindAPI (EGL_OPENGL_API);
  eglGetConfigs(display, NULL, 0, &count);
  configs = malloc(count * sizeof *configs);

  eglChooseConfig (
    display, 
    attributes, 
    configs, 
    count, 
    &num_config
  );

  config_index = match_config_to_visual(
    display,
    GBM_FORMAT_XRGB8888,
    configs,
    num_config
  );

  context = eglCreateContext (
    display, 
    configs[config_index], 
    EGL_NO_CONTEXT, 
    context_attribs
  );

  /*
    
    Create Generic Buffer Manager and EGL surface

  */

  egl_surface = eglCreateWindowSurface (display, configs[config_index], gbm_surface, NULL);
  free(configs);
  eglMakeCurrent (display, egl_surface, egl_surface, context);

  /*
    
    draw

  */

  float i = 0.0f,
        max = 1000.0f;

  while(1) {

    draw (i / max);

    i++;

    if(i < max)

      i = 0.0f;

  }

  clean_up();

  return 0;

}