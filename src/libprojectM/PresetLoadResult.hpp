#pragma once

#include <cstdint>
#include <string>

namespace libprojectM {

/**
 * @brief Outcome of loading a preset, mainly of its warp and composite shaders.
 */
struct PresetLoadResult
{
    enum class Status
    {
        Ok,             //!< Loaded without errors.
        TranslateError, //!< A preset shader failed to preprocess or translate to GLSL.
        CompileError,   //!< A preset shader failed to compile.
        LinkError,      //!< A preset shader program failed to link.
        OtherError      //!< Anything else, e.g. the file could not be read or parsed.
    };

    static constexpr uint32_t NoStage = 0;        //!< Not a shader stage.
    static constexpr uint32_t WarpStage = 1;      //!< The warp shader. Also a bit mask value.
    static constexpr uint32_t CompositeStage = 2; //!< The composite shader. Also a bit mask value.

    Status status{Status::Ok}; //!< The first failure, or Ok.
    uint32_t stage{NoStage};   //!< The stage of the first failure, NoStage if Ok or not shader-related.
    std::string message;       //!< The error message of the first failure.
    uint32_t shaderStages{};   //!< Bit mask of stages that run a translated preset shader.
    uint32_t cachedStages{};   //!< Bit mask of shaderStages whose GLSL came from the shader cache.
};

} // namespace libprojectM
