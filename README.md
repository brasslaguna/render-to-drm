
# Render to DRM

Rendering to the DRM without a window manager.

`
	gcc -o render-to-drm render-to-drm.c -ldrm -lgbm -lEGL -lGL -I/usr/include/libdrm
`