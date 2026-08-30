# Package descriptor for deki-engine auto-discovery
set(PACKAGE_DISPLAY_NAME "ESP32 HAL")
set(PACKAGE_PREFIX "DekiESP32HAL")
set(PACKAGE_UPPER "ESP32_HAL")
set(PACKAGE_TARGET "deki-esp32-hal")
set(PACKAGE_FILE_PREFIX "ESP32HAL")
set(PACKAGE_ENTRY ESP32HALPackage.cpp)
# Link to deki-rendering so blit/S3PIEBlitKernels can register row kernels
# via QuadBlit::RegisterKernel at package init.
set(PACKAGE_LINK_DEPS deki-rendering)
