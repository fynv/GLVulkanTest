#include "com_example_glvulkantest_Native.h"
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>
#include <vector>
#include <string>
#include "vulkan/vulkan.h"
#include "vulkan/vulkan_android.h"

static std::string g_vertex_blit =
R"(#version 310 es
precision highp float;

layout (location = 0) out vec2 vUV;
void main()
{
    vec2 grid = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vec2 vpos = grid * vec2(2.0, 2.0) + vec2(-1.0, -1.0);
    gl_Position = vec4(vpos, 1.0, 1.0);
    vUV = vec2(grid.x, 1.0 - grid.y);
}
)";

static std::string g_frag_blit =
R"(#version 310 es
precision highp float;

layout (location = 0) in vec2 vUV;
layout (location = 0) out vec4 outColor;
layout (location = 0) uniform sampler2D uTex;
void main()
{
    vec4 col = texture(uTex, vUV);
    outColor = vec4(col.xyz, 1.0);
}
)";


class GLShader
{
public:
    unsigned m_type = 0;
    unsigned m_id = -1;
    GLShader(unsigned type, const char* code);
    ~GLShader();
private:
    GLShader(const GLShader&);
};


class GLProgram
{
public:
    unsigned m_id = -1;
    GLProgram(const GLShader& vertexShader, const GLShader& fragmentShader);
    GLProgram(const GLShader& vertexShader, const GLShader& geometryShader, const GLShader& fragmentShader);
    GLProgram(const GLShader& computeShader);
    ~GLProgram();
private:
    GLProgram(const GLProgram&);
};


GLShader::GLShader(unsigned type, const char* code)
{
    m_type = type;
    m_id = glCreateShader(type);
    glShaderSource(m_id, 1, &code, nullptr);
    glCompileShader(m_id);

    GLint compileResult;
    glGetShaderiv(m_id, GL_COMPILE_STATUS, &compileResult);
    if (compileResult == 0)
    {
        GLint infoLogLength;
        glGetShaderiv(m_id, GL_INFO_LOG_LENGTH, &infoLogLength);
        std::vector<GLchar> infoLog(infoLogLength);
        glGetShaderInfoLog(m_id, (GLsizei)infoLog.size(), NULL, infoLog.data());

        __android_log_print(ANDROID_LOG_ERROR, "Engine", "Shader compilation failed: %s", std::string(infoLog.begin(), infoLog.end()).c_str());
    }
}

GLShader::~GLShader()
{
    if (m_id != -1)
        glDeleteShader(m_id);
}


GLProgram::GLProgram(const GLShader& vertexShader, const GLShader& fragmentShader)
{
    m_id = glCreateProgram();
    glAttachShader(m_id, vertexShader.m_id);
    glAttachShader(m_id, fragmentShader.m_id);
    glLinkProgram(m_id);

    GLint linkResult;
    glGetProgramiv(m_id, GL_LINK_STATUS, &linkResult);
    if (linkResult == 0)
    {
        GLint infoLogLength;
        glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &infoLogLength);
        std::vector<GLchar> infoLog(infoLogLength);
        glGetProgramInfoLog(m_id, (GLsizei)infoLog.size(), NULL, infoLog.data());

        __android_log_print(ANDROID_LOG_ERROR, "Engine", "Shader link failed: %s", std::string(infoLog.begin(), infoLog.end()).c_str());
    }
    glDetachShader(m_id, vertexShader.m_id);
    glDetachShader(m_id, fragmentShader.m_id);
}

GLProgram::GLProgram(const GLShader& vertexShader, const GLShader& geometryShader, const GLShader& fragmentShader)
{
    m_id = glCreateProgram();
    glAttachShader(m_id, vertexShader.m_id);
    glAttachShader(m_id, geometryShader.m_id);
    glAttachShader(m_id, fragmentShader.m_id);
    glLinkProgram(m_id);

    GLint linkResult;
    glGetProgramiv(m_id, GL_LINK_STATUS, &linkResult);
    if (linkResult == 0)
    {
        GLint infoLogLength;
        glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &infoLogLength);
        std::vector<GLchar> infoLog(infoLogLength);
        glGetProgramInfoLog(m_id, (GLsizei)infoLog.size(), NULL, infoLog.data());

        __android_log_print(ANDROID_LOG_ERROR, "Engine", "Shader link failed: %s", std::string(infoLog.begin(), infoLog.end()).c_str());
    }
    glDetachShader(m_id, vertexShader.m_id);
    glDetachShader(m_id, geometryShader.m_id);
    glDetachShader(m_id, fragmentShader.m_id);
}

GLProgram::GLProgram(const GLShader& computeShader)
{
    m_id = glCreateProgram();
    glAttachShader(m_id, computeShader.m_id);
    glLinkProgram(m_id);

    GLint linkResult;
    glGetProgramiv(m_id, GL_LINK_STATUS, &linkResult);
    if (linkResult == 0)
    {
        GLint infoLogLength;
        glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &infoLogLength);
        std::vector<GLchar> infoLog(infoLogLength);
        glGetProgramInfoLog(m_id, (GLsizei)infoLog.size(), NULL, infoLog.data());

        __android_log_print(ANDROID_LOG_ERROR, "Engine", "Shader link failed: %s", std::string(infoLog.begin(), infoLog.end()).c_str());
    }
    glDetachShader(m_id, computeShader.m_id);
}

GLProgram::~GLProgram()
{
    if (m_id != -1)
        glDeleteProgram(m_id);
}


PFN_vkGetAndroidHardwareBufferPropertiesANDROID pfnGetAndroidHardwareBufferProperties;

class  GLVulkanTest
{
public:

    VkShaderModule createShaderModule_from_spv(const unsigned char* spv, size_t size)
    {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = size;
        createInfo.pCode = reinterpret_cast<const uint32_t*>(spv);
        VkShaderModule shaderModule;
        vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule);
        return shaderModule;
    }

    GLVulkanTest(JNIEnv* env, jobject assetManager, jint width, jint height)
    {
        m_width = width;
        m_height = height;
        m_asset_manager = AAssetManager_fromJava( env, assetManager );
        load_bin("vert.spv", spv_vert);
        load_bin("frag.spv", spv_frag);

        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "GLVulkanTest";
        appInfo.pEngineName = "GLVulkanTest";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_1;

        {
            std::vector<const char *> name_extensions = {};

            char str_validationLayers[] = "VK_LAYER_KHRONOS_validation";
            const char* validationLayers[] = { str_validationLayers };

            VkInstanceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = (uint32_t) name_extensions.size();
            createInfo.ppEnabledExtensionNames = name_extensions.data();
            createInfo.enabledLayerCount = 1;
            createInfo.ppEnabledLayerNames = validationLayers;
            vkCreateInstance(&createInfo, nullptr, &m_instance);
        }

        m_physicalDevice = VK_NULL_HANDLE;
        {
            // select physical device
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
            std::vector<VkPhysicalDevice> ph_devices(deviceCount);
            vkEnumeratePhysicalDevices(m_instance, &deviceCount, ph_devices.data());
            m_physicalDevice = ph_devices[0];

            //__android_log_print(ANDROID_LOG_INFO, "Vulkan", "deviceCount: %d", deviceCount);
        }

        {
            VkPhysicalDeviceProperties prop;
            vkGetPhysicalDeviceProperties(m_physicalDevice, &prop);
            __android_log_print(ANDROID_LOG_INFO, "Vulkan", "device_vendor: %s", prop.deviceName);
        }

        {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

            for (uint32_t i = 0; i < queueFamilyCount; i++)
            {
                if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    if (m_QueueFamily == (uint32_t)(-1))
                    {
                        m_QueueFamily = i;
                    }
                }
            }
        }

        float queuePriority = 1.0f;
        {
            VkDeviceQueueCreateInfo queueCreateInfo[1] = {};
            queueCreateInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo[0].queueFamilyIndex = m_QueueFamily;
            queueCreateInfo[0].queueCount = 1;
            queueCreateInfo[0].pQueuePriorities = &queuePriority;

            std::vector<const char*> name_extensions = {
                VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
                VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME
            };

            VkDeviceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pQueueCreateInfos = queueCreateInfo;
            createInfo.queueCreateInfoCount = 1;
            createInfo.enabledExtensionCount = (uint32_t)name_extensions.size();
            createInfo.ppEnabledExtensionNames = name_extensions.data();

            vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
        }
        vkGetDeviceQueue(m_device, m_QueueFamily, 0, &m_Queue);

        // Command Pool
        {
            VkCommandPoolCreateInfo poolInfo = {};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = m_QueueFamily;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
        }

        pfnGetAndroidHardwareBufferProperties = (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetDeviceProcAddr(m_device, "vkGetAndroidHardwareBufferPropertiesANDROID");

        // render pass
        {
            VkAttachmentDescription colorAttachment = {};
            colorAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorAttachmentRef = {};
            colorAttachmentRef.attachment = 0;
            colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass = {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorAttachmentRef;

            VkSubpassDependency dependency = {};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = 1;
            renderPassInfo.pAttachments = &colorAttachment;
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpass;
            renderPassInfo.dependencyCount = 1;
            renderPassInfo.pDependencies = &dependency;

            vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass);

        }

        // pipeline
        {
            m_vertShaderModule = createShaderModule_from_spv(spv_vert.data(), spv_vert.size());
            m_fragShaderModule = createShaderModule_from_spv(spv_frag.data(), spv_frag.size());

            VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
            vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertShaderStageInfo.module = m_vertShaderModule;
            vertShaderStageInfo.pName = "main";

            VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
            fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = m_fragShaderModule;
            fragShaderStageInfo.pName = "main";

            VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

            VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.pVertexBindingDescriptions = nullptr;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;
            vertexInputInfo.pVertexAttributeDescriptions = nullptr;

            VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            VkViewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = 0.0f;
            viewport.height = 0.0f;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor = {};
            scissor.offset = { 0, 0 };
            scissor.extent = {0,0};

            VkPipelineViewportStateCreateInfo viewportState = {};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizer = {};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisampling = {};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
            dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicStateInfo.dynamicStateCount = 2;
            dynamicStateInfo.pDynamicStates = dynamicStates;

            VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

            VkPipelineColorBlendStateCreateInfo colorBlending = {};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

            vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);

            VkGraphicsPipelineCreateInfo pipelineInfo = {};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicStateInfo;
            pipelineInfo.layout = m_pipelineLayout;
            pipelineInfo.renderPass = m_renderPass;
            pipelineInfo.subpass = 0;

            vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);

        }

        // OpenGL
        GLShader shader_blit_vert(GL_VERTEX_SHADER, g_vertex_blit.c_str());
        GLShader shader_blit_frag(GL_FRAGMENT_SHADER, g_frag_blit.c_str());
        m_prog_blit = std::unique_ptr<GLProgram>(new GLProgram(shader_blit_vert, shader_blit_frag));

        glGenTextures(1, &m_gl_color_buf);
        glBindTexture(GL_TEXTURE_2D, m_gl_color_buf);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        unsigned char data[16] = { 255,255,255,255, 0,0,0,255, 0,0,0,255, 255,255,255,255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);

        init(); // initialize framebuffers and other size-dependent resources

    }

    ~GLVulkanTest()
    {
        glDeleteTextures(1, &m_gl_color_buf);
        vkDeviceWaitIdle(m_device);
        clear();
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        vkDestroyShaderModule(m_device, m_fragShaderModule, nullptr);
        vkDestroyShaderModule(m_device, m_vertShaderModule, nullptr);
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        vkDestroyDevice(m_device, nullptr);
        vkDestroyInstance(m_instance, nullptr);

    }

    void Resize(int w, int h)
    {
        m_width = w;
        m_height = h;

        init();
    }

    void Draw(JNIEnv* env)
    {
        vkQueueWaitIdle(m_Queue);
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;
        vkQueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE);

        glViewport(0,0,m_width, m_height);
        glClearColor(1.0f,0.0f,1.0f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        glUseProgram(m_prog_blit->m_id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_gl_color_buf);
        glUniform1i(0, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);

    }

private:
    JNIEnv* m_cur_env = nullptr;
    AAssetManager* m_asset_manager = nullptr;
    std::vector<unsigned char> spv_vert, spv_frag;

    void load_bin(const char* filename, std::vector<unsigned char>& data)
    {
        AAsset* asset =  AAssetManager_open(m_asset_manager, filename, AASSET_MODE_UNKNOWN);
        off_t len = AAsset_getLength(asset);
        data.resize(len);
        AAsset_read(asset, data.data(), len);
        AAsset_close(asset);
    }

    std::string load_text(const char* filename)
    {
        AAsset* asset =  AAssetManager_open(m_asset_manager, filename, AASSET_MODE_UNKNOWN);
        off_t len = AAsset_getLength(asset);
        std::vector<char> text(len+1, 0);
        AAsset_read(asset, text.data(), len);
        AAsset_close(asset);
        return text.data();
    }

    int m_width = -1;
    int m_height = -1;

    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_QueueFamily = (uint32_t)(-1);
    VkQueue m_Queue = VK_NULL_HANDLE;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    AHardwareBuffer* m_color_buf_ahb = nullptr;
    VkImage m_color_buf = VK_NULL_HANDLE;
    VkDeviceMemory m_color_buf_mem = VK_NULL_HANDLE;
    VkImageView m_color_buf_view = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;

    EGLImageKHR m_egl_color_buf = nullptr;

    VkRenderPass m_renderPass;
    VkShaderModule m_vertShaderModule;
    VkShaderModule m_fragShaderModule;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_pipeline;

    // OpenGL
    std::unique_ptr<GLProgram> m_prog_blit;
    unsigned m_gl_color_buf;



    void clear()
    {
        if (m_egl_color_buf!=nullptr)
        {
            EGLDisplay dpy = eglGetCurrentDisplay();
            eglDestroyImageKHR(dpy, m_egl_color_buf);
        }
        if (m_framebuffer !=VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
        }

        if (m_color_buf!=VK_NULL_HANDLE )
        {
            vkDestroyImageView(m_device, m_color_buf_view, nullptr);
            vkDestroyImage(m_device, m_color_buf, nullptr);
            vkFreeMemory(m_device, m_color_buf_mem, nullptr);
        }
        if (m_color_buf_ahb != nullptr)
        {
            AHardwareBuffer_release(m_color_buf_ahb);
        }
    }

    void init()
    {
        clear();
        // color target
        {
            AHardwareBuffer_Desc ahb_desc = {};
            ahb_desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
            ahb_desc.width = m_width;
            ahb_desc.height = m_height;
            ahb_desc.layers = 1;
            ahb_desc.usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
            AHardwareBuffer_allocate(&ahb_desc, &m_color_buf_ahb);

            VkAndroidHardwareBufferFormatPropertiesANDROID formatInfo ={};
            formatInfo.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;

            VkAndroidHardwareBufferPropertiesANDROID properties = {};
            properties.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
            properties.pNext = &formatInfo;
            pfnGetAndroidHardwareBufferProperties(m_device, m_color_buf_ahb, &properties);

            VkExternalMemoryImageCreateInfo externalCreateInfo = {};
            externalCreateInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
            externalCreateInfo.pNext = nullptr;
            externalCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

            VkImageCreateInfo imageInfo = {};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.pNext = &externalCreateInfo;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = m_width;
            imageInfo.extent.height = m_height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = formatInfo.format;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage =
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

            vkCreateImage(m_device, &imageInfo, nullptr, &m_color_buf);

            VkImportAndroidHardwareBufferInfoANDROID androidHardwareBufferInfo = {};
            androidHardwareBufferInfo.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
            androidHardwareBufferInfo.buffer = m_color_buf_ahb;

            VkMemoryDedicatedAllocateInfo memoryAllocateInfo = {};
            memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
            memoryAllocateInfo.pNext = &androidHardwareBufferInfo;
            memoryAllocateInfo.image = m_color_buf;

            VkPhysicalDeviceMemoryProperties memProperties;
            vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

            uint32_t memoryTypeIndex = VK_MAX_MEMORY_TYPES;
            for (uint32_t k = 0; k < memProperties.memoryTypeCount; k++)
            {
                if ((properties.memoryTypeBits & (1 << k)) == 0) continue;
                memoryTypeIndex = k;
                break;
            }

            VkMemoryAllocateInfo allocateInfo = {};
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.pNext = &memoryAllocateInfo;
            allocateInfo.allocationSize = properties.allocationSize;
            allocateInfo.memoryTypeIndex = memoryTypeIndex;
            vkAllocateMemory(m_device, &allocateInfo, nullptr, &m_color_buf_mem);
            vkBindImageMemory(m_device, m_color_buf, m_color_buf_mem, 0);

            VkImageViewCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_color_buf;
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(m_device, &createInfo, nullptr, &m_color_buf_view);

        }

        // framebuffer
        {
            VkImageView attachments[] = { m_color_buf_view };
            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = m_width;
            framebufferInfo.height = m_height;
            framebufferInfo.layers = 1;

            vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffer);
        }

        // command buffer
        {
            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = m_commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer);
        }

        // record command buffer
        {
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

            VkViewport viewport;
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = m_width;
            viewport.height = m_height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor = {};
            scissor.offset = { (int)0, (int)0 };
            scissor.extent = { (unsigned)m_width, (unsigned)m_height };

            vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = m_renderPass;
            renderPassInfo.framebuffer = m_framebuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = { (unsigned)m_width, (unsigned)m_height};

            VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(m_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(m_commandBuffer);

            vkEndCommandBuffer(m_commandBuffer);
        }

        //EGL
        EGLDisplay dpy = eglGetCurrentDisplay();
        EGLint attrs[] = {
                EGL_IMAGE_PRESERVED_KHR,    EGL_TRUE,
                EGL_NONE,
        };
        m_egl_color_buf = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, eglGetNativeClientBufferANDROID(m_color_buf_ahb), attrs);

        glBindTexture(GL_TEXTURE_2D, m_gl_color_buf);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_egl_color_buf);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

};

jlong JNICALL Java_com_example_glvulkantest_Native_create(JNIEnv *env, jclass, jobject asset_manager, jobject view, jint width, jint height)
{
    GLVulkanTest* test = new GLVulkanTest(env, asset_manager, width, height);
    return (jlong)(test);
}

void JNICALL Java_com_example_glvulkantest_Native_destroy(JNIEnv *env, jclass, jlong ptr)
{
    GLVulkanTest* test = (GLVulkanTest*)ptr;
    delete test;
}

void JNICALL Java_com_example_glvulkantest_Native_resize(JNIEnv *env, jclass, jlong ptr, jint w, jint h)
{
    GLVulkanTest* test = (GLVulkanTest*)ptr;
    test->Resize(w,h);
}

void JNICALL Java_com_example_glvulkantest_Native_draw(JNIEnv *env, jclass, jlong ptr)
{
    GLVulkanTest* test = (GLVulkanTest*)ptr;
    test->Draw(env);
}
