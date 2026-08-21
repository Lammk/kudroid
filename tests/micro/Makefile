CC = aarch64-linux-gnu-gcc
CFLAGS = -shared -fPIC -O2 -Wall -nostdlib -z max-page-size=65536

TARGETS = gpu_triangle_micro.so gpu_screen_render.so gpu_es3_fbo.so gpu_3d_cube.so vulkan_micro_test.so libengine_provider.so libgame_consumer.so gpu_es3_advanced.so gpu_es3_multithread_shared.so gpu_es3_compressed_texture.so gpu_opengl_info.so sensor_orientation_haptic_test.so

all: $(TARGETS)

libengine_provider.so: libengine_provider.c
	$(CC) $(CFLAGS) -Wl,-soname,libengine_provider.so -o $@ $<
	aarch64-linux-gnu-strip $@
	@echo "✔ Built Provider ARM64 shared library: $@ ($$(wc -c < $@) bytes)"

libgame_consumer.so: libgame_consumer.c libengine_provider.so
	$(CC) $(CFLAGS) -Wl,-soname,libgame_consumer.so -L. -l:libengine_provider.so -o $@ $<
	aarch64-linux-gnu-strip $@
	@echo "✔ Built Consumer ARM64 shared library: $@ ($$(wc -c < $@) bytes)"

%.so: %.c
	$(CC) $(CFLAGS) -Wl,-soname,$(notdir $@) -o $@ $<
	aarch64-linux-gnu-strip $@
	@echo "✔ Built ARM64 shared library: $@ ($$(wc -c < $@) bytes)"

clean:
	rm -f *.so
