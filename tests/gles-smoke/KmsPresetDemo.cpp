/**
 * @file KmsPresetDemo.cpp
 * @brief Full-screen KMS/GBM/EGL preset player for checking the GLES builds on real HDMI output.
 *
 * Renders projectM into an internal-resolution FBO, upscales it to the display mode and page-flips
 * it on a DRM CRTC, cycling through the given presets with soft transitions while feeding a
 * synthetic beat (kick, hi-hat, sine sweep) as PCM. Meant for manual checks on a device such as a
 * Raspberry Pi 3 (Mesa vc4), not for automated testing.
 *
 * All init (GBM, EGL, projectM, first preset) happens before DRM master is requested, so another
 * DRM client can be stopped right after "prepared" is printed. On exit the last frame stays scanned
 * out (drmModeCloseFB, not RmFB) and the device fd stays open for --linger-ms after master is
 * dropped, so the next owner can take over without the console being restored in between.
 *
 * Usage:
 *   projectM-kms-demo [--card /dev/dri/card0] [--mode WxH] [--internal WxH] [--seconds N]
 *                     [--blend S] [--wait-master-ms N] [--linger-ms N] [--texture-path DIR]
 *                     preset.milk...
 *
 * --mode picks a WxH@60 mode; the CRTC's current mode is reused if it matches, so no modeset is
 * needed. SIGINT/SIGTERM stop the demo early with the same clean exit.
 */

#include <projectM-4/projectM.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <poll.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

namespace {

#ifdef PROJECTM_SMOKE_GLES2
constexpr int ContextMajorVersion = 2;
#else
constexpr int ContextMajorVersion = 3;
#endif

constexpr int SampleRate = 44100;

volatile std::sig_atomic_t g_stop = 0;

void OnSignal(int)
{
    g_stop = 1;
}

auto RealtimeMs() -> long long
{
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

auto MonotonicMs() -> double
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}

__attribute__((format(printf, 1, 2))) void Log(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    std::printf("@%lld %s\n", RealtimeMs(), buffer);
    std::fflush(stdout);
}

void LogCallback(const char* message, projectm_log_level level, void*)
{
    if (level >= PROJECTM_LOG_LEVEL_INFO)
    {
        Log("projectM: %s", message);
    }
}

void* LoadProc(const char* name, void*)
{
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

struct Options
{
    std::string card{"/dev/dri/card0"};
    int modeWidth{1280};
    int modeHeight{720};
    int internalWidth{640};
    int internalHeight{360};
    double seconds{75.0};
    double blend{0.5};
    int waitMasterMs{10000};
    int lingerMs{3000};
    std::string texturePath;
    std::vector<std::string> presets;
};

auto ParseOptions(int argc, char** argv, Options& options) -> bool
{
    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--card" && hasValue)
        {
            options.card = argv[++i];
        }
        else if (arg == "--mode" && hasValue)
        {
            if (std::sscanf(argv[++i], "%dx%d", &options.modeWidth, &options.modeHeight) != 2)
            {
                return false;
            }
        }
        else if (arg == "--internal" && hasValue)
        {
            if (std::sscanf(argv[++i], "%dx%d", &options.internalWidth, &options.internalHeight) != 2)
            {
                return false;
            }
        }
        else if (arg == "--seconds" && hasValue)
        {
            options.seconds = std::atof(argv[++i]);
        }
        else if (arg == "--blend" && hasValue)
        {
            options.blend = std::atof(argv[++i]);
        }
        else if (arg == "--wait-master-ms" && hasValue)
        {
            options.waitMasterMs = std::atoi(argv[++i]);
        }
        else if (arg == "--linger-ms" && hasValue)
        {
            options.lingerMs = std::atoi(argv[++i]);
        }
        else if (arg == "--texture-path" && hasValue)
        {
            options.texturePath = argv[++i];
        }
        else if (arg.rfind("--", 0) == 0)
        {
            return false;
        }
        else
        {
            options.presets.push_back(arg);
        }
    }
    return !options.presets.empty();
}

/**
 * Synthetic "music": 120 BPM kick on the beat, hi-hat noise on the off-beat, and a sine sweeping
 * 110 Hz to 1.76 kHz over 8 s.
 */
class SyntheticBeat
{
public:
    void Generate(std::vector<float>& stereo, size_t frames)
    {
        stereo.resize(frames * 2);
        for (size_t i = 0; i < frames; i++)
        {
            const double t = static_cast<double>(m_sample++) / SampleRate;
            const double beat = std::fmod(t, 0.5);
            const double offBeat = std::fmod(t + 0.25, 0.5);

            // Kick: sine with a pitch drop from ~170 Hz to 50 Hz.
            const double kickPhase = 2.0 * M_PI * (50.0 * beat + 4.0 * (1.0 - std::exp(-beat * 30.0)));
            const double kick = std::exp(-beat * 9.0) * std::sin(kickPhase);
            const double hat = std::exp(-offBeat * 60.0) * m_noise(m_random) * 0.35;

            const double sweepPosition = std::fmod(t, 8.0) / 8.0;
            const double sweepFrequency = 110.0 * std::pow(16.0, sweepPosition);
            m_sweepPhase += 2.0 * M_PI * sweepFrequency / SampleRate;
            const double sweep = 0.25 * std::sin(m_sweepPhase);

            stereo[2 * i] = static_cast<float>(std::max(-1.0, std::min(1.0, 0.8 * kick + hat + sweep)));
            stereo[2 * i + 1] = static_cast<float>(std::max(-1.0, std::min(1.0, 0.8 * kick + 0.7 * hat + sweep)));
        }
    }

private:
    uint64_t m_sample{};
    double m_sweepPhase{};
    std::mt19937 m_random{12345};
    std::uniform_real_distribution<double> m_noise{-1.0, 1.0};
};

struct Kms
{
    int fd{-1};
    drmModeConnector* connector{};
    uint32_t crtcId{};
    drmModeModeInfo mode{};
    bool modeMatchesCrtc{};
};

auto FindOutput(Kms& kms, int width, int height) -> bool
{
    drmModeRes* resources = drmModeGetResources(kms.fd);
    if (resources == nullptr)
    {
        Log("ERROR drmModeGetResources: %s", std::strerror(errno));
        return false;
    }

    for (int i = 0; i < resources->count_connectors && kms.connector == nullptr; i++)
    {
        // Current state only, without an EDID re-probe.
        drmModeConnector* connector = drmModeGetConnectorCurrent(kms.fd, resources->connectors[i]);
        if (connector != nullptr && connector->connector_type == DRM_MODE_CONNECTOR_HDMIA &&
            connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0)
        {
            kms.connector = connector;
        }
        else if (connector != nullptr)
        {
            drmModeFreeConnector(connector);
        }
    }
    if (kms.connector == nullptr)
    {
        Log("ERROR no connected HDMI connector");
        drmModeFreeResources(resources);
        return false;
    }

    uint32_t crtcId = 0;
    if (kms.connector->encoder_id != 0)
    {
        drmModeEncoder* encoder = drmModeGetEncoder(kms.fd, kms.connector->encoder_id);
        if (encoder != nullptr)
        {
            crtcId = encoder->crtc_id;
            drmModeFreeEncoder(encoder);
        }
    }
    for (int i = 0; crtcId == 0 && i < kms.connector->count_encoders; i++)
    {
        drmModeEncoder* encoder = drmModeGetEncoder(kms.fd, kms.connector->encoders[i]);
        if (encoder == nullptr)
        {
            continue;
        }
        for (int j = 0; j < resources->count_crtcs; j++)
        {
            if ((encoder->possible_crtcs & (1u << j)) != 0)
            {
                crtcId = resources->crtcs[j];
                break;
            }
        }
        drmModeFreeEncoder(encoder);
    }
    drmModeFreeResources(resources);
    if (crtcId == 0)
    {
        Log("ERROR no CRTC for the HDMI connector");
        return false;
    }
    kms.crtcId = crtcId;

    drmModeCrtc* crtc = drmModeGetCrtc(kms.fd, crtcId);
    if (crtc != nullptr && crtc->mode_valid && crtc->mode.hdisplay == width && crtc->mode.vdisplay == height)
    {
        kms.mode = crtc->mode;
        kms.modeMatchesCrtc = true;
    }
    if (crtc != nullptr)
    {
        drmModeFreeCrtc(crtc);
    }

    if (!kms.modeMatchesCrtc)
    {
        bool found = false;
        for (int i = 0; i < kms.connector->count_modes && !found; i++)
        {
            const drmModeModeInfo& mode = kms.connector->modes[i];
            if (mode.hdisplay == width && mode.vdisplay == height && mode.vrefresh == 60 &&
                (mode.flags & DRM_MODE_FLAG_INTERLACE) == 0)
            {
                kms.mode = mode;
                found = true;
            }
        }
        if (!found)
        {
            Log("ERROR mode %dx%d@60 not offered", width, height);
            return false;
        }
    }

    Log("output connector=%u crtc=%u mode=%s %dx%d@%u (%s)", kms.connector->connector_id, kms.crtcId, kms.mode.name,
        kms.mode.hdisplay, kms.mode.vdisplay, kms.mode.vrefresh,
        kms.modeMatchesCrtc ? "same as CRTC, no modeset" : "modeset needed");
    return true;
}

void DestroyFramebuffer(gbm_bo* bo, void* data)
{
    const auto id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    if (id != 0)
    {
        drmModeRmFB(gbm_device_get_fd(gbm_bo_get_device(bo)), id);
    }
}

auto FramebufferForBo(int fd, gbm_bo* bo) -> uint32_t
{
    if (auto cached = reinterpret_cast<uintptr_t>(gbm_bo_get_user_data(bo)))
    {
        return static_cast<uint32_t>(cached);
    }
    uint32_t handles[4] = {gbm_bo_get_handle(bo).u32};
    uint32_t strides[4] = {gbm_bo_get_stride(bo)};
    uint32_t offsets[4] = {0};
    uint64_t modifiers[4] = {gbm_bo_get_modifier(bo)};
    uint32_t id = 0;
    int result;
    if (modifiers[0] != DRM_FORMAT_MOD_INVALID)
    {
        result = drmModeAddFB2WithModifiers(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), gbm_bo_get_format(bo),
                                            handles, strides, offsets, modifiers, &id, DRM_MODE_FB_MODIFIERS);
    }
    else
    {
        result = drmModeAddFB2(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), gbm_bo_get_format(bo),
                               handles, strides, offsets, &id, 0);
    }
    if (result != 0)
    {
        Log("ERROR AddFB2: %s", std::strerror(errno));
        return 0;
    }
    gbm_bo_set_user_data(bo, reinterpret_cast<void*>(static_cast<uintptr_t>(id)), &DestroyFramebuffer);
    return id;
}

bool g_flipPending = false;

void OnPageFlip(int, unsigned int, unsigned int, unsigned int, void*)
{
    g_flipPending = false;
}

auto CompileShader(GLenum type, const char* source) -> GLuint
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        Log("ERROR upscale shader: %s", log);
    }
    return shader;
}

constexpr const char* UpscaleVertexShader =
    "attribute vec2 position;\n"
    "varying vec2 uv;\n"
    "void main() { uv = position * 0.5 + 0.5; gl_Position = vec4(position, 0.0, 1.0); }\n";

constexpr const char* UpscaleFragmentShader =
    "precision mediump float;\n"
    "varying vec2 uv;\n"
    "uniform sampler2D image;\n"
    "void main() { gl_FragColor = vec4(texture2D(image, uv).rgb, 1.0); }\n";

auto GLESMajorVersion() -> int
{
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    int major{};
    if (version == nullptr || std::sscanf(version, "OpenGL ES %d", &major) != 1)
    {
        return 0;
    }
    return major;
}

auto HasExtension(const char* extension) -> bool
{
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const size_t length = std::strlen(extension);
    for (const char* found = extensions ? std::strstr(extensions, extension) : nullptr; found != nullptr;
         found = std::strstr(found + length, extension))
    {
        if ((found == extensions || found[-1] == ' ') && (found[length] == ' ' || found[length] == '\0'))
        {
            return true;
        }
    }
    return false;
}

auto BaseName(const std::string& path) -> std::string
{
    return path.substr(path.find_last_of('/') + 1);
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        std::fprintf(stderr, "usage: %s [--card DEV] [--mode WxH] [--internal WxH] [--seconds N] [--blend S] "
                             "[--wait-master-ms N] [--linger-ms N] [--texture-path DIR] preset.milk...\n",
                     argv[0]);
        return 2;
    }

    std::signal(SIGINT, &OnSignal);
    std::signal(SIGTERM, &OnSignal);

    const double startMs = MonotonicMs();

    Kms kms;
    kms.fd = open(options.card.c_str(), O_RDWR | O_CLOEXEC);
    if (kms.fd < 0)
    {
        Log("ERROR open %s: %s", options.card.c_str(), std::strerror(errno));
        return 1;
    }
    if (!FindOutput(kms, options.modeWidth, options.modeHeight))
    {
        return 1;
    }
    const int width = kms.mode.hdisplay;
    const int height = kms.mode.vdisplay;

    // GBM + EGL. None of this needs DRM master.
    gbm_device* gbm = gbm_create_device(kms.fd);
    gbm_surface* gbmSurface = gbm ? gbm_surface_create(gbm, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                                       GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)
                                  : nullptr;
    if (gbmSurface == nullptr)
    {
        Log("ERROR GBM surface creation failed");
        return 1;
    }
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, nullptr) : EGL_NO_DISPLAY;
    EGLint eglMajor{};
    EGLint eglMinor{};
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &eglMajor, &eglMinor))
    {
        Log("ERROR eglInitialize 0x%x", eglGetError());
        return 1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
                                       EGL_RENDERABLE_TYPE, ContextMajorVersion == 2 ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES3_BIT,
                                       EGL_NONE};
    EGLConfig configs[64];
    EGLint configCount = 0;
    eglChooseConfig(display, configAttributes, configs, 64, &configCount);
    EGLConfig config = nullptr;
    for (int i = 0; i < configCount && config == nullptr; i++)
    {
        EGLint visual{};
        eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID, &visual);
        if (visual == static_cast<EGLint>(GBM_FORMAT_XRGB8888))
        {
            config = configs[i];
        }
    }
    if (config == nullptr)
    {
        Log("ERROR no XRGB8888 EGL config");
        return 1;
    }
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, ContextMajorVersion, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    EGLSurface surface = context != EGL_NO_CONTEXT ? eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(gbmSurface), nullptr)
                                                   : EGL_NO_SURFACE;
    if (surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context))
    {
        Log("ERROR EGL context/surface creation failed 0x%x", eglGetError());
        return 1;
    }
    Log("GL_RENDERER=%s GL_VERSION=%s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
        reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // Internal render target and upscale program.
    GLuint internalTexture{};
    GLuint internalFramebuffer{};
    glGenTextures(1, &internalTexture);
    glBindTexture(GL_TEXTURE_2D, internalTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, options.internalWidth, options.internalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &internalFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, internalFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, internalTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        Log("ERROR internal framebuffer incomplete");
        return 1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GLuint upscaleProgram = glCreateProgram();
    glAttachShader(upscaleProgram, CompileShader(GL_VERTEX_SHADER, UpscaleVertexShader));
    glAttachShader(upscaleProgram, CompileShader(GL_FRAGMENT_SHADER, UpscaleFragmentShader));
    glBindAttribLocation(upscaleProgram, 0, "position");
    glLinkProgram(upscaleProgram);
    const GLfloat quad[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    GLuint quadBuffer{};
    glGenBuffers(1, &quadBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, quadBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // projectM leaves its own VAO and sampler bindings alone after drawing, but reset them anyway.
    // eglGetProcAddress can return dispatch stubs for functions the context doesn't support, so
    // check the context version and extensions rather than the pointers.
    const int glesMajor = GLESMajorVersion();
    PFNGLBINDVERTEXARRAYOESPROC bindVertexArray = nullptr;
    if (glesMajor >= 3 || HasExtension("GL_OES_vertex_array_object"))
    {
        bindVertexArray = reinterpret_cast<PFNGLBINDVERTEXARRAYOESPROC>(eglGetProcAddress(glesMajor >= 3 ? "glBindVertexArray" : "glBindVertexArrayOES"));
    }
    using BindSamplerProc = void (*)(GLuint, GLuint);
    BindSamplerProc bindSampler = nullptr;
    if (glesMajor >= 3 || HasExtension("GL_MESA_sampler_objects"))
    {
        bindSampler = reinterpret_cast<BindSamplerProc>(eglGetProcAddress("glBindSampler"));
    }

    // projectM
    projectm_set_log_callback(&LogCallback, false, nullptr);
    projectm_set_log_level(PROJECTM_LOG_LEVEL_INFO, false);
    projectm_handle projectM = projectm_create_with_opengl_load_proc(&LoadProc, nullptr);
    if (projectM == nullptr)
    {
        Log("ERROR projectm_create failed");
        return 1;
    }
    projectm_set_window_size(projectM, static_cast<size_t>(options.internalWidth), static_cast<size_t>(options.internalHeight));
    projectm_set_mesh_size(projectM, 48, 36);
    projectm_set_fps(projectM, 30);
    projectm_set_preset_locked(projectM, true);
    projectm_set_preset_duration(projectM, 1e9);
    projectm_set_hard_cut_enabled(projectM, false);
    projectm_set_soft_cut_duration(projectM, options.blend);
    if (!options.texturePath.empty())
    {
        const char* paths[] = {options.texturePath.c_str()};
        projectm_set_texture_search_paths(projectM, paths, 1);
    }

    SyntheticBeat beat;
    std::vector<float> pcm;
    size_t presetIndex = 0;
    Log("PRESET 1/%zu %s (%.0f s)", options.presets.size(), BaseName(options.presets[0]).c_str(), options.seconds);
    projectm_load_preset_file(projectM, options.presets[0].c_str(), false);

    auto renderFrame = [&]() {
        projectm_opengl_render_frame_fbo(projectM, internalFramebuffer);

        if (bindVertexArray != nullptr)
        {
            bindVertexArray(0);
        }
        if (bindSampler != nullptr)
        {
            bindSampler(0, 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND);
        glUseProgram(upscaleProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, internalTexture);
        glUniform1i(glGetUniformLocation(upscaleProgram, "image"), 0);
        glBindBuffer(GL_ARRAY_BUFFER, quadBuffer);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        eglSwapBuffers(display, surface);
    };

    // Warm up the first preset (shader compiles) before taking over the display.
    beat.Generate(pcm, SampleRate / 30);
    projectm_pcm_add_float(projectM, pcm.data(), static_cast<unsigned int>(pcm.size() / 2), PROJECTM_STEREO);
    renderFrame();
    gbm_bo* bo = gbm_surface_lock_front_buffer(gbmSurface);
    uint32_t framebuffer = bo ? FramebufferForBo(kms.fd, bo) : 0;
    if (framebuffer == 0)
    {
        return 1;
    }
    Log("prepared in %.0f ms", MonotonicMs() - startMs);

    // DRM master: spin until the current owner lets go.
    const double masterWaitStart = MonotonicMs();
    if (!drmIsMaster(kms.fd))
    {
        Log("waiting up to %d ms for DRM master", options.waitMasterMs);
        while (drmSetMaster(kms.fd) != 0)
        {
            if (g_stop || MonotonicMs() - masterWaitStart > options.waitMasterMs)
            {
                Log("ERROR no DRM master after %.0f ms: %s", MonotonicMs() - masterWaitStart, std::strerror(errno));
                return 3;
            }
            usleep(2000);
        }
    }
    Log("master acquired after %.0f ms", MonotonicMs() - masterWaitStart);

    if (drmModeSetCrtc(kms.fd, kms.crtcId, framebuffer, 0, 0, &kms.connector->connector_id, 1, &kms.mode) != 0)
    {
        Log("ERROR SetCrtc: %s", std::strerror(errno));
        drmDropMaster(kms.fd);
        return 4;
    }
    Log("presenting %dx%d, internal %dx%d", width, height, options.internalWidth, options.internalHeight);

    drmEventContext eventContext{};
    eventContext.version = 2;
    eventContext.page_flip_handler = &OnPageFlip;

    gbm_bo* previous = bo;
    double presetStart = MonotonicMs();
    double lastFrame = presetStart;
    double renderMsSum = 0.0;
    double worstRenderMs = 0.0;
    int framesInPreset = 0;
    int lateFlips = 0;
    double lastFlip = presetStart;
    const double vblankMs = 1000.0 / std::max(1u, kms.mode.vrefresh);
    int exitCode = 0;

    while (!g_stop)
    {
        const double now = MonotonicMs();
        if (now - presetStart >= options.seconds * 1000.0)
        {
            Log("STATS %s: %d frames, %.1f fps, render+swap avg %.1f ms max %.1f ms, flips later than 1.5 vblanks %d",
                BaseName(options.presets[presetIndex]).c_str(), framesInPreset,
                framesInPreset * 1000.0 / (now - presetStart), renderMsSum / std::max(1, framesInPreset), worstRenderMs,
                lateFlips);
            if (++presetIndex >= options.presets.size())
            {
                break;
            }
            Log("PRESET %zu/%zu %s (%.0f s)", presetIndex + 1, options.presets.size(),
                BaseName(options.presets[presetIndex]).c_str(), options.seconds);
            projectm_load_preset_file(projectM, options.presets[presetIndex].c_str(), true);
            presetStart = MonotonicMs();
            renderMsSum = worstRenderMs = 0.0;
            framesInPreset = lateFlips = 0;
        }

        // Audio for the wall time since the last frame.
        const auto samples = static_cast<size_t>(std::min(0.1, (now - lastFrame) / 1000.0) * SampleRate);
        lastFrame = now;
        if (samples > 0)
        {
            beat.Generate(pcm, samples);
            projectm_pcm_add_float(projectM, pcm.data(), static_cast<unsigned int>(samples), PROJECTM_STEREO);
        }

        const double renderStart = MonotonicMs();
        renderFrame();
        bo = gbm_surface_lock_front_buffer(gbmSurface);
        framebuffer = bo ? FramebufferForBo(kms.fd, bo) : 0;
        const double renderMs = MonotonicMs() - renderStart;
        renderMsSum += renderMs;
        worstRenderMs = std::max(worstRenderMs, renderMs);
        if (framebuffer == 0)
        {
            exitCode = 5;
            break;
        }

        g_flipPending = true;
        if (drmModePageFlip(kms.fd, kms.crtcId, framebuffer, DRM_MODE_PAGE_FLIP_EVENT, nullptr) != 0)
        {
            Log("ERROR PageFlip: %s", std::strerror(errno));
            gbm_surface_release_buffer(gbmSurface, bo);
            exitCode = 6;
            break;
        }
        while (g_flipPending)
        {
            pollfd poll_fd{kms.fd, POLLIN, 0};
            if (poll(&poll_fd, 1, 1000) <= 0)
            {
                Log("ERROR page flip timeout");
                exitCode = 7;
                break;
            }
            drmHandleEvent(kms.fd, &eventContext);
        }
        if (exitCode != 0)
        {
            break;
        }
        const double flipDone = MonotonicMs();
        lateFlips += (flipDone - lastFlip > 1.5 * vblankMs) ? 1 : 0;
        lastFlip = flipDone;

        gbm_surface_release_buffer(gbmSurface, previous);
        previous = bo;
        framesInPreset++;
    }
    if (g_stop)
    {
        Log("stopped by signal");
    }

    // Keep the last frame scanned out: CloseFB drops our handle without disabling the plane.
    if (previous != nullptr)
    {
        const auto id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gbm_bo_get_user_data(previous)));
        if (id != 0 && drmModeCloseFB(kms.fd, id) == 0)
        {
            gbm_bo_set_user_data(previous, nullptr, nullptr);
        }
        gbm_surface_release_buffer(gbmSurface, previous);
    }

    projectm_destroy(projectM);
    glDeleteProgram(upscaleProgram);
    glDeleteBuffers(1, &quadBuffer);
    glDeleteFramebuffers(1, &internalFramebuffer);
    glDeleteTextures(1, &internalTexture);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
    gbm_surface_destroy(gbmSurface);
    gbm_device_destroy(gbm);

    drmDropMaster(kms.fd);
    Log("master dropped");
    if (options.lingerMs > 0)
    {
        usleep(static_cast<useconds_t>(options.lingerMs) * 1000);
    }
    drmModeFreeConnector(kms.connector);
    close(kms.fd);
    Log("exit %d", exitCode);
    return exitCode;
}
