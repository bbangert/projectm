/**
 * @file GlesSmokeTest.cpp
 * @brief Headless runtime smoke test for the OpenGL ES builds (ES 3 and the ES 2 backend).
 *
 * Creates an OpenGL ES context with EGL on either the Mesa surfaceless platform or a GBM device
 * (e.g. a DRM render node), renders presets into an FBO and checks the result:
 *  - the context is the ES major version the library was built for (2 for ENABLE_GLES2),
 *  - every frame completes without GL errors,
 *  - the last frame of each preset is not entirely black.
 *
 * Usage:
 *   projectM-gles-smoke [--gbm <device>] [--size <w>x<h>] [--frames <n>] [--texture-path <dir>]
 *                       [--dump <prefix>] [--expect-failure <preset>] <preset.milk>...
 *
 * On the ES 2 backend, PROJECTM_GLES2_NO_SAMPLER_OBJECTS=1 exercises the per-texture sampler state
 * fallback.
 *
 * Each preset after the first is loaded with a soft transition, which also exercises the transition
 * shaders. Exits 0 when all checks pass.
 *
 * Mesa answers an ES 2.0 context request with its highest ES version; on llvmpipe, set
 * MESA_GLES_VERSION_OVERRIDE=2.0 to get a real 2.0 context for the ES 2 backend.
 */

#include <projectM-4/projectM.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef PROJECTM_SMOKE_HAVE_GBM
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

namespace {

#ifdef PROJECTM_SMOKE_GLES2
constexpr int ContextMajorVersion = 2;
#else
constexpr int ContextMajorVersion = 3;
#endif

struct Options
{
    std::string gbmDevice;
    int width{640};
    int height{360};
    int frames{60};
    std::string texturePath;
    std::string dumpPrefix;
    std::set<std::string> expectFailure;
    std::vector<std::string> presets;
};

struct PresetResult
{
    std::string preset;
    bool switchFailed{false};
    std::string failureMessage;
    unsigned glErrors{};
    double meanMs{};
    double maxMs{};
    double nonBlackFraction{};
    double meanLuma{};
};

std::vector<std::string> g_switchFailures;
std::string g_lastSwitchFailureMessage;

void LogCallback(const char* message, projectm_log_level level, void*)
{
    static const char* const names[] = {"NOTSET", "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    const int index = std::max(0, std::min(6, static_cast<int>(level)));
    std::fprintf(stderr, "[projectM %s] %s\n", names[index], message);
}

void SwitchFailedCallback(const char* presetFilename, const char* message, void*)
{
    g_switchFailures.emplace_back(presetFilename ? presetFilename : "");
    g_lastSwitchFailureMessage = message ? message : "";
}

void* LoadProc(const char* name, void*)
{
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

auto HasExtension(const char* extensions, const char* name) -> bool
{
    if (extensions == nullptr)
    {
        return false;
    }
    const size_t length = std::strlen(name);
    for (const char* p = std::strstr(extensions, name); p != nullptr; p = std::strstr(p + length, name))
    {
        if ((p == extensions || p[-1] == ' ') && (p[length] == ' ' || p[length] == '\0'))
        {
            return true;
        }
    }
    return false;
}

auto ParseOptions(int argc, char** argv, Options& options) -> bool
{
    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "Missing value for %s\n", arg.c_str());
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--gbm")
        {
            options.gbmDevice = next();
        }
        else if (arg == "--size")
        {
            if (std::sscanf(next(), "%dx%d", &options.width, &options.height) != 2)
            {
                return false;
            }
        }
        else if (arg == "--frames")
        {
            options.frames = std::max(1, std::atoi(next()));
        }
        else if (arg == "--texture-path")
        {
            options.texturePath = next();
        }
        else if (arg == "--dump")
        {
            options.dumpPrefix = next();
        }
        else if (arg == "--expect-failure")
        {
            options.expectFailure.insert(next());
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

void DumpPPM(const std::string& path, const std::vector<unsigned char>& rgba, int width, int height)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
    {
        return;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; y--)
    {
        for (int x = 0; x < width; x++)
        {
            std::fwrite(&rgba[(static_cast<size_t>(y) * width + x) * 4], 1, 3, file);
        }
    }
    std::fclose(file);
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        std::fprintf(stderr, "usage: %s [--gbm <device>] [--size WxH] [--frames N] [--texture-path DIR] "
                             "[--dump PREFIX] [--expect-failure PRESET] preset.milk...\n",
                     argv[0]);
        return 2;
    }

    // EGL display: GBM device (e.g. /dev/dri/renderD128) or the Mesa surfaceless platform.
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay == nullptr)
    {
        std::fprintf(stderr, "FAIL: eglGetPlatformDisplayEXT is not available\n");
        return 1;
    }

    EGLDisplay display = EGL_NO_DISPLAY;
#ifdef PROJECTM_SMOKE_HAVE_GBM
    int drmFd = -1;
    gbm_device* gbm = nullptr;
#endif
    if (!options.gbmDevice.empty())
    {
#ifdef PROJECTM_SMOKE_HAVE_GBM
        drmFd = open(options.gbmDevice.c_str(), O_RDWR | O_CLOEXEC);
        if (drmFd < 0)
        {
            std::fprintf(stderr, "FAIL: cannot open %s\n", options.gbmDevice.c_str());
            return 1;
        }
        gbm = gbm_create_device(drmFd);
        display = gbm ? getPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, nullptr) : EGL_NO_DISPLAY;
#else
        std::fprintf(stderr, "FAIL: built without GBM support\n");
        return 1;
#endif
    }
    else
    {
        display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    }

    EGLint eglMajor{};
    EGLint eglMinor{};
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &eglMajor, &eglMinor))
    {
        std::fprintf(stderr, "FAIL: EGL initialization failed (0x%x)\n", eglGetError());
        return 1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint configAttributes[] = {
        EGL_RENDERABLE_TYPE, ContextMajorVersion == 2 ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, 0,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE};
    EGLConfig config{};
    EGLint configCount{};
    eglChooseConfig(display, configAttributes, &config, 1, &configCount);
    if (configCount < 1)
    {
        // Surfaceless contexts don't need a config if EGL_KHR_no_config_context is available.
        config = EGL_NO_CONFIG_KHR;
    }

    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, ContextMajorVersion, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
    {
        std::fprintf(stderr, "FAIL: creating an OpenGL ES %d.0 context failed (0x%x)\n", ContextMajorVersion, eglGetError());
        return 1;
    }

    const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* glExtensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    std::printf("EGL %d.%d, platform %s\n", eglMajor, eglMinor, options.gbmDevice.empty() ? "surfaceless" : options.gbmDevice.c_str());
    std::printf("GL_RENDERER: %s\nGL_VERSION: %s\nGL_SHADING_LANGUAGE_VERSION: %s\n",
                glRenderer, glVersion, reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

    int contextMajor{};
    if (glVersion == nullptr || std::sscanf(glVersion, "OpenGL ES %d", &contextMajor) != 1 || contextMajor != ContextMajorVersion)
    {
        std::fprintf(stderr, "FAIL: expected an OpenGL ES %d.x context, got \"%s\"\n", ContextMajorVersion, glVersion ? glVersion : "(null)");
        return 1;
    }

    // Extension entry points relevant to the ES 2 backend. eglGetProcAddress may return dispatch
    // stubs for unsupported functions, so the extension string is reported as well.
    struct ExtensionProbe
    {
        const char* extension;
        std::vector<const char*> functions;
    };
    const ExtensionProbe probes[] = {
        {"GL_OES_vertex_array_object", {"glGenVertexArraysOES", "glBindVertexArrayOES", "glDeleteVertexArraysOES"}},
        {"GL_MESA_sampler_objects", {"glGenSamplers", "glBindSampler", "glSamplerParameteri", "glDeleteSamplers"}},
        {"GL_EXT_draw_buffers", {"glDrawBuffersEXT"}},
    };
    for (const auto& probe : probes)
    {
        std::string resolved;
        for (const auto* function : probe.functions)
        {
            resolved += std::string(" ") + function + (eglGetProcAddress(function) ? "=ok" : "=NULL");
        }
        std::printf("EXT %s: %s;%s\n", probe.extension, HasExtension(glExtensions, probe.extension) ? "listed" : "not listed", resolved.c_str());
    }

    // Render target
    GLuint targetTexture{};
    GLuint targetFramebuffer{};
    glGenTextures(1, &targetTexture);
    glBindTexture(GL_TEXTURE_2D, targetTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, options.width, options.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &targetFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "FAIL: render target framebuffer is incomplete\n");
        return 1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    projectm_set_log_callback(&LogCallback, false, nullptr);
    projectm_set_log_level(PROJECTM_LOG_LEVEL_INFO, false);

    auto startTime = std::chrono::steady_clock::now();
    projectm_handle projectM = projectm_create_with_opengl_load_proc(&LoadProc, nullptr);
    auto createMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
    if (projectM == nullptr)
    {
        std::fprintf(stderr, "FAIL: projectm_create_with_opengl_load_proc() failed\n");
        return 1;
    }
    std::printf("projectM created in %.1f ms\n", createMs);

    projectm_set_window_size(projectM, static_cast<size_t>(options.width), static_cast<size_t>(options.height));
    projectm_set_mesh_size(projectM, 48, 36);
    projectm_set_preset_locked(projectM, true);
    projectm_set_soft_cut_duration(projectM, 0.5);
    projectm_set_preset_switch_failed_event_callback(projectM, &SwitchFailedCallback, nullptr);
    if (!options.texturePath.empty())
    {
        const char* paths[] = {options.texturePath.c_str()};
        projectm_set_texture_search_paths(projectM, paths, 1);
    }

    bool success = true;
    double frameTime = 0.0;
    std::vector<float> pcm(2 * 735);
    std::vector<unsigned char> pixels(static_cast<size_t>(options.width) * options.height * 4);
    std::vector<PresetResult> results;

    for (size_t presetIndex = 0; presetIndex < options.presets.size(); presetIndex++)
    {
        PresetResult result;
        result.preset = options.presets[presetIndex];
        const size_t failuresBefore = g_switchFailures.size();

        projectm_load_preset_file(projectM, result.preset.c_str(), presetIndex > 0);
        result.switchFailed = g_switchFailures.size() != failuresBefore;
        result.failureMessage = result.switchFailed ? g_lastSwitchFailureMessage : "";

        std::vector<double> frameMs;
        for (int frame = 0; frame < options.frames; frame++)
        {
            // 30 fps of a stereo 110 Hz + 1.3 kHz mix with a 2 Hz amplitude envelope.
            for (size_t i = 0; i < pcm.size() / 2; i++)
            {
                const double t = frameTime + static_cast<double>(i) / 44100.0;
                const auto envelope = static_cast<float>(0.5 + 0.5 * std::sin(2.0 * M_PI * 2.0 * t));
                pcm[2 * i] = envelope * static_cast<float>(0.6 * std::sin(2.0 * M_PI * 110.0 * t));
                pcm[2 * i + 1] = envelope * static_cast<float>(0.3 * std::sin(2.0 * M_PI * 1300.0 * t));
            }
            projectm_pcm_add_float(projectM, pcm.data(), static_cast<unsigned int>(pcm.size() / 2), PROJECTM_STEREO);
            projectm_set_frame_time(projectM, frameTime);
            frameTime += 1.0 / 30.0;

            const auto frameStart = std::chrono::steady_clock::now();
            projectm_opengl_render_frame_fbo(projectM, targetFramebuffer);
            glFinish();
            frameMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count());

            for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
            {
                if (result.glErrors++ == 0)
                {
                    std::fprintf(stderr, "GL error 0x%04x after frame %d of %s\n", error, frame, result.preset.c_str());
                }
            }
        }

        // Skip the first frames (shader compiles) for the timing.
        const size_t skip = std::min<size_t>(5, frameMs.size() - 1);
        double sum = 0.0;
        for (size_t i = skip; i < frameMs.size(); i++)
        {
            sum += frameMs[i];
            result.maxMs = std::max(result.maxMs, frameMs[i]);
        }
        result.meanMs = sum / static_cast<double>(frameMs.size() - skip);

        glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
        glReadPixels(0, 0, options.width, options.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (glGetError() != GL_NO_ERROR)
        {
            result.glErrors++;
        }

        size_t nonBlack = 0;
        double lumaSum = 0.0;
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            const int maxChannel = std::max({pixels[i], pixels[i + 1], pixels[i + 2]});
            nonBlack += maxChannel > 8 ? 1 : 0;
            lumaSum += 0.299 * pixels[i] + 0.587 * pixels[i + 1] + 0.114 * pixels[i + 2];
        }
        const double pixelCount = static_cast<double>(pixels.size() / 4);
        result.nonBlackFraction = static_cast<double>(nonBlack) / pixelCount;
        result.meanLuma = lumaSum / pixelCount;

        if (!options.dumpPrefix.empty())
        {
            DumpPPM(options.dumpPrefix + std::to_string(presetIndex) + ".ppm", pixels, options.width, options.height);
        }

        const std::string baseName = result.preset.substr(result.preset.find_last_of('/') + 1);
        const bool expectFailure = options.expectFailure.count(baseName) > 0;
        bool ok = result.glErrors == 0 && result.switchFailed == expectFailure;
        if (!result.switchFailed)
        {
            ok = ok && result.nonBlackFraction > 0.001;
        }
        success = success && ok;

        std::printf("%s %s: frames=%d mean=%.2f ms max=%.2f ms gl_errors=%u non_black=%.1f%% mean_luma=%.1f%s%s\n",
                    ok ? "PASS" : "FAIL", baseName.c_str(), options.frames, result.meanMs, result.maxMs, result.glErrors,
                    100.0 * result.nonBlackFraction, result.meanLuma,
                    result.switchFailed ? " switch_failed=" : "", result.switchFailed ? (expectFailure ? "expected" : result.failureMessage.c_str()) : "");
        results.push_back(result);
    }

    projectm_destroy(projectM);

    glDeleteFramebuffers(1, &targetFramebuffer);
    glDeleteTextures(1, &targetTexture);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
#ifdef PROJECTM_SMOKE_HAVE_GBM
    if (gbm != nullptr)
    {
        gbm_device_destroy(gbm);
    }
    if (drmFd >= 0)
    {
        close(drmFd);
    }
#endif

    std::printf("%s\n", success ? "RESULT: PASS" : "RESULT: FAIL");
    return success ? 0 : 1;
}
