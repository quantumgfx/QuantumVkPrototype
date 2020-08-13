#include "device.hpp"
#include "device.hpp"
#include "utils/bitops.hpp"
#include "utils/hash.hpp"

// Vulkan SDK has copies of these libraries. Explicit file names needed to aviod mix up
#include <extern/glslang/glslang/Public/ShaderLang.h>
#include <extern/glslang/SPIRV/GlslangToSpv.h>
#include <extern/glslang/StandAlone/DirStackFileIncluder.h>

namespace Vulkan
{

    static TBuiltInResource* default_t_built_in_resources;

	static inline EShLanguage GetStage(ShaderStage stage)
	{
		switch (stage)
		{
		case Vulkan::ShaderStage::Vertex:         return EShLangVertex;
		case Vulkan::ShaderStage::TessControl:    return EShLangTessControl;
		case Vulkan::ShaderStage::TessEvaluation: return EShLangTessEvaluation;
		case Vulkan::ShaderStage::Geometry:       return EShLangGeometry;
		case Vulkan::ShaderStage::Fragment:       return EShLangFragment;
		case Vulkan::ShaderStage::Compute:        return EShLangCompute;
		}

		QM_LOG_ERROR("Unrecognized shader stage");
		return EShLangVertex;
	}

    void Device::InitGlslang()
    {
        glslang::InitializeProcess();

        default_t_built_in_resources = new TBuiltInResource();

        default_t_built_in_resources->maxLights = 32;
        default_t_built_in_resources->maxClipPlanes = 6;
        default_t_built_in_resources->maxTextureUnits = 32;
        default_t_built_in_resources->maxVertexAttribs = 64;
        default_t_built_in_resources->maxVertexUniformComponents = 4096;
        default_t_built_in_resources->maxVaryingFloats = 64;
        default_t_built_in_resources->maxVertexTextureImageUnits = 32;
        default_t_built_in_resources->maxCombinedTextureImageUnits = 80;
        default_t_built_in_resources->maxTextureImageUnits = 32;
        default_t_built_in_resources->maxFragmentUniformComponents = 4096;
        default_t_built_in_resources->maxDrawBuffers = 32;
        default_t_built_in_resources->maxVertexUniformVectors = 128;
        default_t_built_in_resources->maxVaryingVectors = 8;
        default_t_built_in_resources->maxFragmentUniformVectors = 16;
        default_t_built_in_resources->maxVertexOutputVectors = 16;
        default_t_built_in_resources->maxFragmentInputVectors = 15;
        default_t_built_in_resources->minProgramTexelOffset = -8;
        default_t_built_in_resources->maxProgramTexelOffset = 7;
        default_t_built_in_resources->maxClipDistances = 8;
        default_t_built_in_resources->maxComputeWorkGroupCountX = 65535;
        default_t_built_in_resources->maxComputeWorkGroupCountY = 65535;
        default_t_built_in_resources->maxComputeWorkGroupCountZ = 65535;
        default_t_built_in_resources->maxComputeWorkGroupSizeX = 1024;
        default_t_built_in_resources->maxComputeWorkGroupSizeY = 1024;
        default_t_built_in_resources->maxComputeWorkGroupSizeZ = 64;
        default_t_built_in_resources->maxComputeUniformComponents = 1024;
        default_t_built_in_resources->maxComputeTextureImageUnits = 16;
        default_t_built_in_resources->maxComputeImageUniforms = 8;
        default_t_built_in_resources->maxComputeAtomicCounters = 8;
        default_t_built_in_resources->maxComputeAtomicCounterBuffers = 1;
        default_t_built_in_resources->maxVaryingComponents = 60;
        default_t_built_in_resources->maxVertexOutputComponents = 64;
        default_t_built_in_resources->maxGeometryInputComponents = 64;
        default_t_built_in_resources->maxGeometryOutputComponents = 128;
        default_t_built_in_resources->maxFragmentInputComponents = 128;
        default_t_built_in_resources->maxImageUnits = 8;
        default_t_built_in_resources->maxCombinedImageUnitsAndFragmentOutputs = 8;
        default_t_built_in_resources->maxCombinedShaderOutputResources = 8;
        default_t_built_in_resources->maxImageSamples = 0;
        default_t_built_in_resources->maxVertexImageUniforms = 0;
        default_t_built_in_resources->maxTessControlImageUniforms = 0;
        default_t_built_in_resources->maxTessEvaluationImageUniforms = 0;
        default_t_built_in_resources->maxGeometryImageUniforms = 0;
        default_t_built_in_resources->maxFragmentImageUniforms = 8;
        default_t_built_in_resources->maxCombinedImageUniforms = 8;
        default_t_built_in_resources->maxGeometryTextureImageUnits = 16;
        default_t_built_in_resources->maxGeometryOutputVertices = 256;
        default_t_built_in_resources->maxGeometryTotalOutputComponents = 1024;
        default_t_built_in_resources->maxGeometryUniformComponents = 1024;
        default_t_built_in_resources->maxGeometryVaryingComponents = 64;
        default_t_built_in_resources->maxTessControlInputComponents = 128;
        default_t_built_in_resources->maxTessControlOutputComponents = 128;
        default_t_built_in_resources->maxTessControlTextureImageUnits = 16;
        default_t_built_in_resources->maxTessControlUniformComponents = 1024;
        default_t_built_in_resources->maxTessControlTotalOutputComponents = 4096;
        default_t_built_in_resources->maxTessEvaluationInputComponents = 128;
        default_t_built_in_resources->maxTessEvaluationOutputComponents = 128;
        default_t_built_in_resources->maxTessEvaluationTextureImageUnits = 16;
        default_t_built_in_resources->maxTessEvaluationUniformComponents = 1024;
        default_t_built_in_resources->maxTessPatchComponents = 120;
        default_t_built_in_resources->maxPatchVertices = 32;
        default_t_built_in_resources->maxTessGenLevel = 64;
        default_t_built_in_resources->maxViewports = 16;
        default_t_built_in_resources->maxVertexAtomicCounters = 0;
        default_t_built_in_resources->maxTessControlAtomicCounters = 0;
        default_t_built_in_resources->maxTessEvaluationAtomicCounters = 0;
        default_t_built_in_resources->maxGeometryAtomicCounters = 0;
        default_t_built_in_resources->maxFragmentAtomicCounters = 8;
        default_t_built_in_resources->maxCombinedAtomicCounters = 8;
        default_t_built_in_resources->maxAtomicCounterBindings = 1;
        default_t_built_in_resources->maxVertexAtomicCounterBuffers = 0;
        default_t_built_in_resources->maxTessControlAtomicCounterBuffers = 0;
        default_t_built_in_resources->maxTessEvaluationAtomicCounterBuffers = 0;
        default_t_built_in_resources->maxGeometryAtomicCounterBuffers = 0;
        default_t_built_in_resources->maxFragmentAtomicCounterBuffers = 1;
        default_t_built_in_resources->maxCombinedAtomicCounterBuffers = 1;
        default_t_built_in_resources->maxAtomicCounterBufferSize = 16384;
        default_t_built_in_resources->maxTransformFeedbackBuffers = 4;
        default_t_built_in_resources->maxTransformFeedbackInterleavedComponents = 64;
        default_t_built_in_resources->maxCullDistances = 8;
        default_t_built_in_resources->maxCombinedClipAndCullDistances = 8;
        default_t_built_in_resources->maxSamples = 4;
        //default_t_built_in_resources->maxMeshOutputVerticesNV
        default_t_built_in_resources->limits.nonInductiveForLoops = 1;
        default_t_built_in_resources->limits.generalUniformIndexing = 1;
        default_t_built_in_resources->limits.generalAttributeMatrixVectorIndexing = 1;
        default_t_built_in_resources->limits.generalVaryingIndexing = 1;
        default_t_built_in_resources->limits.generalSamplerIndexing = 1;
        default_t_built_in_resources->limits.generalVariableIndexing = 1;
        default_t_built_in_resources->limits.generalConstantMatrixVectorIndexing = 1;

        default_t_built_in_resources->limits.whileLoops = 1;
        default_t_built_in_resources->limits.doWhileLoops = 1;
    }

    void Device::DeinitGlslang()
    {
        delete default_t_built_in_resources;

        glslang::FinalizeProcess();
    }

    ShaderHandle Device::CreateShader(const uint32_t* code, size_t size)
    {
        return ShaderHandle(handle_pool.shaders.allocate(this, code, size));
    }

    ShaderHandle Device::CreateShaderGLSL(const char* glsl_code, ShaderStage stage, std::vector<std::string> include_dirs)
	{
		EShLanguage shader_type = GetStage(stage);
		glslang::TShader shader(shader_type);
		shader.setStrings(&glsl_code, 1);

		//Set up Vulkan/SpirV Environment
        int defualt_version = 100;
		glslang::EShTargetClientVersion vulkanClientVersion = glslang::EShTargetVulkan_1_0;

        if (ext->supports_vulkan_12_device) { defualt_version = 120;  vulkanClientVersion = glslang::EShTargetVulkan_1_2; }
        else if (ext->supports_vulkan_11_device) { defualt_version = 110;  vulkanClientVersion = glslang::EShTargetVulkan_1_1; }

		glslang::EShTargetLanguageVersion targetVersion = glslang::EShTargetSpv_1_5;

		shader.setEnvInput(glslang::EShSourceGlsl, shader_type, glslang::EShClientVulkan, defualt_version);
		shader.setEnvClient(glslang::EShClientVulkan, vulkanClientVersion);
		shader.setEnvTarget(glslang::EShTargetSpv, targetVersion);

		DirStackFileIncluder includer;

        for (auto& dir : include_dirs)
        {
            includer.pushExternalLocalDirectory(dir);
        }

		EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

		std::string preprocessed_glsl;

		if (!shader.preprocess(default_t_built_in_resources, defualt_version, ENoProfile, false, false, messages, &preprocessed_glsl, includer))
		{
			QM_LOG_INFO("GLSL Preprocessing Failed\n");
			QM_LOG_INFO("%s\n", shader.getInfoLog());
			QM_LOG_INFO("%s\n", shader.getInfoDebugLog());

            return ShaderHandle{ nullptr };
		}

		const char* preprocessed_cstr = preprocessed_glsl.c_str();
		shader.setStrings(&preprocessed_cstr, 1);

		if (!shader.parse(default_t_built_in_resources, defualt_version, false, messages))
		{
			QM_LOG_INFO("GLSL Parsing Failed\n");
			QM_LOG_INFO("%s\n", shader.getInfoLog());
			QM_LOG_INFO("%s\n", shader.getInfoDebugLog());

            return ShaderHandle{ nullptr };
		}

        glslang::TProgram program;
        program.addShader(&shader);

        if (!program.link(messages))
        {
            QM_LOG_ERROR("GLSL Linking Failed");
            QM_LOG_ERROR("%s", program.getInfoLog());
            QM_LOG_ERROR("%s", program.getInfoDebugLog());
        }

        std::vector<uint32_t> spirv;
        spv::SpvBuildLogger logger;
        glslang::SpvOptions spvOptions;
        glslang::GlslangToSpv(*program.getIntermediate(shader_type), spirv, &logger, &spvOptions);

        return CreateShader(spirv.data(), spirv.size() * sizeof(uint32_t));
	}

	ProgramHandle Device::CreateGraphicsProgram(const GraphicsProgramShaders& shaders)
	{
		return ProgramHandle(handle_pool.programs.allocate(this, shaders));
	}

	ProgramHandle Device::CreateComputeProgram(const ComputeProgramShaders& shaders)
	{
		return ProgramHandle(handle_pool.programs.allocate(this, shaders));
	}

}