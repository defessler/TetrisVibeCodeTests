// vulkan_renderer.cpp — pixel-art sprite renderer for Tetris
//
// Single tiny pipeline: takes a unit quad vertex buffer and a 64-byte
// push-constant block (NDC pos/size + 3 colours + style flag) and rasterises
// every game element — cells, ghost outlines, frame, sidebar text — as
// individually-coloured beveled quads.  No textures, no descriptor sets.

#include "vulkan_renderer.h"
#include "shaders/spirv_embed.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <set>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

// ── Error helper ──────────────────────────────────────────────────────────────

#define VK_CHECK(x) do {                                                      \
    VkResult _r = (x);                                                        \
    if (_r != VK_SUCCESS) {                                                   \
        std::fprintf(stderr, "Vulkan error %d at %s:%d\n",                    \
                     int(_r), __FILE__, __LINE__);                            \
        std::abort();                                                         \
    }                                                                         \
} while (0)

// ── Push constant layout (matches the shader exactly: 64 bytes) ──────────────

struct PushConst {
    float pos[2];   //  8  NDC top-left
    float size[2];  //  8  NDC width/height
    float mid[4];   // 16  rgb + style (0 solid, 1 ghost, 2 empty, 3 flat-color)
    float hi[4];    // 16  rgb + pad
    float lo[4];    // 16  rgb + pad
};
static_assert(sizeof(PushConst) == 64, "PushConst must be exactly 64 bytes");

// ── Vertex layout (unit quad) ────────────────────────────────────────────────

struct Vertex { float x, y; };
static const Vertex QUAD_VERTS[4] = {
    {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
};
static const uint16_t QUAD_INDICES[6] = { 0, 1, 2, 2, 3, 0 };

// ── Piece palette  (0=empty, 1-7 pieces, 8=ghost handled separately) ────────

struct RGB { float r, g, b; };
struct Theme { RGB hi, mid, lo; };

static constexpr Theme THEMES[8] = {
    {{0.10f,0.11f,0.16f}, {0.04f,0.05f,0.08f}, {0.03f,0.04f,0.07f}}, // 0 empty
    {{0.59f,1.00f,1.00f}, {0.00f,0.78f,0.86f}, {0.00f,0.43f,0.57f}}, // 1 I cyan
    {{1.00f,0.96f,0.31f}, {0.90f,0.65f,0.00f}, {0.59f,0.39f,0.00f}}, // 2 O gold
    {{0.94f,0.35f,1.00f}, {0.73f,0.06f,0.90f}, {0.43f,0.00f,0.63f}}, // 3 T violet
    {{0.37f,1.00f,0.45f}, {0.04f,0.76f,0.20f}, {0.00f,0.47f,0.10f}}, // 4 S lime
    {{1.00f,0.35f,0.29f}, {0.86f,0.04f,0.04f}, {0.57f,0.00f,0.00f}}, // 5 Z red
    {{0.31f,0.53f,1.00f}, {0.04f,0.20f,0.86f}, {0.00f,0.04f,0.57f}}, // 6 J cobalt
    {{1.00f,0.76f,0.22f}, {0.86f,0.43f,0.00f}, {0.57f,0.24f,0.00f}}, // 7 L amber
};

// "TETRIS" rainbow letters
static constexpr RGB TITLE_COLS[6] = {
    {0.73f,0.06f,0.90f},  // T  violet
    {0.04f,0.76f,0.20f},  // E  lime
    {0.00f,0.78f,0.86f},  // T  cyan
    {0.86f,0.43f,0.00f},  // R  amber
    {0.86f,0.04f,0.04f},  // I  red
    {0.04f,0.20f,0.86f},  // S  cobalt
};

static constexpr RGB BG          = {0.030f, 0.035f, 0.055f};
static constexpr RGB FRAME_COLOR = {0.20f,  0.55f,  0.86f};
static constexpr RGB LABEL_COLOR = {0.55f,  0.62f,  0.78f};
static constexpr RGB VALUE_COLOR = {1.00f,  1.00f,  1.00f};
static constexpr RGB GHOST_COLOR = {0.30f,  0.36f,  0.52f};

// ── 5×7 bitmap font ───────────────────────────────────────────────────────────
// Each row is the lower 5 bits, bit 4 = leftmost pixel.

static const uint8_t* glyphFor(char c) {
    static constexpr uint8_t BLANK[7] = {0,0,0,0,0,0,0};
#define G(name, r0,r1,r2,r3,r4,r5,r6) \
    static constexpr uint8_t name[7] = {r0,r1,r2,r3,r4,r5,r6}

    G(D0, 0x0e,0x11,0x13,0x15,0x19,0x11,0x0e);
    G(D1, 0x04,0x0c,0x04,0x04,0x04,0x04,0x0e);
    G(D2, 0x0e,0x11,0x01,0x02,0x04,0x08,0x1f);
    G(D3, 0x1f,0x02,0x04,0x02,0x01,0x11,0x0e);
    G(D4, 0x02,0x06,0x0a,0x12,0x1f,0x02,0x02);
    G(D5, 0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e);
    G(D6, 0x06,0x08,0x10,0x1e,0x11,0x11,0x0e);
    G(D7, 0x1f,0x01,0x02,0x04,0x08,0x08,0x08);
    G(D8, 0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e);
    G(D9, 0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c);

    G(LA, 0x0e,0x11,0x11,0x1f,0x11,0x11,0x11);
    G(LC, 0x0f,0x10,0x10,0x10,0x10,0x10,0x0f);
    G(LD, 0x1e,0x11,0x11,0x11,0x11,0x11,0x1e);
    G(LE, 0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f);
    G(LG, 0x0e,0x11,0x10,0x17,0x11,0x11,0x0e);
    G(LH, 0x11,0x11,0x11,0x1f,0x11,0x11,0x11);
    G(LI, 0x1f,0x04,0x04,0x04,0x04,0x04,0x1f);
    G(LK, 0x11,0x12,0x14,0x18,0x14,0x12,0x11);
    G(LL, 0x10,0x10,0x10,0x10,0x10,0x10,0x1f);
    G(LM, 0x11,0x1b,0x15,0x11,0x11,0x11,0x11);
    G(LN, 0x11,0x19,0x15,0x13,0x11,0x11,0x11);
    G(LO, 0x0e,0x11,0x11,0x11,0x11,0x11,0x0e);
    G(LP, 0x1e,0x11,0x11,0x1e,0x10,0x10,0x10);
    G(LR, 0x1e,0x11,0x11,0x1e,0x14,0x12,0x11);
    G(LS, 0x0e,0x11,0x10,0x0e,0x01,0x11,0x0e);
    G(LT, 0x1f,0x04,0x04,0x04,0x04,0x04,0x04);
    G(LU, 0x11,0x11,0x11,0x11,0x11,0x11,0x0e);
    G(LV, 0x11,0x11,0x11,0x11,0x11,0x0a,0x04);
    G(LX, 0x11,0x11,0x0a,0x04,0x0a,0x11,0x11);
    G(LY, 0x11,0x11,0x0a,0x04,0x04,0x04,0x04);
    G(LZ, 0x1f,0x01,0x02,0x04,0x08,0x10,0x1f);

    G(COL,0x00,0x04,0x00,0x00,0x00,0x04,0x00);
    G(BNG,0x04,0x04,0x04,0x04,0x04,0x00,0x04);

#undef G

    if (c >= '0' && c <= '9') {
        switch (c) {
            case '0': return D0; case '1': return D1; case '2': return D2;
            case '3': return D3; case '4': return D4; case '5': return D5;
            case '6': return D6; case '7': return D7; case '8': return D8;
            case '9': return D9;
        }
    }
    switch (c) {
        case ' ': return BLANK;
        case ':': return COL;
        case '!': return BNG;
        case 'A': return LA; case 'C': return LC; case 'D': return LD;
        case 'E': return LE; case 'G': return LG; case 'H': return LH;
        case 'I': return LI; case 'K': return LK; case 'L': return LL;
        case 'M': return LM; case 'N': return LN; case 'O': return LO;
        case 'P': return LP; case 'R': return LR; case 'S': return LS;
        case 'T': return LT; case 'U': return LU; case 'V': return LV;
        case 'X': return LX; case 'Y': return LY; case 'Z': return LZ;
    }
    return BLANK;
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void VulkanRenderer::init() {
    if (!glfwInit())
        throw std::runtime_error("glfwInit failed");
    if (!glfwVulkanSupported())
        throw std::runtime_error("Vulkan not supported on this system");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    _window = glfwCreateWindow(WIN_W, WIN_H, "Tetris", nullptr, nullptr);
    if (!_window)
        throw std::runtime_error("glfwCreateWindow failed");

    glfwSetWindowUserPointer(_window, this);
    glfwSetKeyCallback(_window, &VulkanRenderer::keyCb);

    createInstance();
    createSurface();
    pickPhysical();
    createDevice();
    createSwapchain();
    createRenderPass();
    createPipeline();
    createFramebuffers();
    createQuadBuffers();
    createCommandBuffers();
    createSync();
}

void VulkanRenderer::shutdown() {
    if (_dev) vkDeviceWaitIdle(_dev);
    cleanupVulkan();
    if (_window) glfwDestroyWindow(_window);
    glfwTerminate();
}

void VulkanRenderer::cleanupVulkan() {
    if (!_dev) return;

    for (auto& s : _imgReady)   if (s) vkDestroySemaphore(_dev, s, nullptr);
    for (auto& s : _renderDone) if (s) vkDestroySemaphore(_dev, s, nullptr);
    for (auto& f : _inFlight)   if (f) vkDestroyFence    (_dev, f, nullptr);

    if (_cmdPool) vkDestroyCommandPool(_dev, _cmdPool, nullptr);

    if (_vbuf) vkDestroyBuffer(_dev, _vbuf, nullptr);
    if (_vmem) vkFreeMemory   (_dev, _vmem, nullptr);
    if (_ibuf) vkDestroyBuffer(_dev, _ibuf, nullptr);
    if (_imem) vkFreeMemory   (_dev, _imem, nullptr);

    for (auto fb : _fbufs)     vkDestroyFramebuffer(_dev, fb, nullptr);
    for (auto v  : _swapViews) vkDestroyImageView (_dev, v,  nullptr);

    if (_pipe)   vkDestroyPipeline      (_dev, _pipe,   nullptr);
    if (_layout) vkDestroyPipelineLayout(_dev, _layout, nullptr);
    if (_rpass)  vkDestroyRenderPass    (_dev, _rpass,  nullptr);
    if (_swap)   vkDestroySwapchainKHR  (_dev, _swap,   nullptr);

    vkDestroyDevice(_dev, nullptr);
    if (_surf) vkDestroySurfaceKHR(_inst, _surf, nullptr);
    if (_inst) vkDestroyInstance(_inst, nullptr);
    _dev = VK_NULL_HANDLE;
}

// ── Vulkan init helpers ──────────────────────────────────────────────────────

void VulkanRenderer::createInstance() {
    VkApplicationInfo app{};
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "Tetris";
    app.applicationVersion = VK_MAKE_VERSION(1,0,0);
    app.pEngineName        = "TetrisVk";
    app.engineVersion      = VK_MAKE_VERSION(1,0,0);
    app.apiVersion         = VK_API_VERSION_1_0;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = glfwExtCount;
    ci.ppEnabledExtensionNames = glfwExts;
    VK_CHECK(vkCreateInstance(&ci, nullptr, &_inst));
}

void VulkanRenderer::createSurface() {
    VK_CHECK(glfwCreateWindowSurface(_inst, _window, nullptr, &_surf));
}

void VulkanRenderer::pickPhysical() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(_inst, &n, nullptr);
    if (n == 0) throw std::runtime_error("No Vulkan-capable GPUs");
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(_inst, &n, devs.data());

    auto suitable = [&](VkPhysicalDevice d, uint32_t* outFam) -> bool {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> q(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, q.data());
        for (uint32_t i = 0; i < qn; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, _surf, &present);
            if ((q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                // Verify swapchain extension
                uint32_t en = 0;
                vkEnumerateDeviceExtensionProperties(d, nullptr, &en, nullptr);
                std::vector<VkExtensionProperties> ext(en);
                vkEnumerateDeviceExtensionProperties(d, nullptr, &en, ext.data());
                for (auto& e : ext)
                    if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                        *outFam = i;
                        return true;
                    }
            }
        }
        return false;
    };

    // Prefer discrete GPU
    for (auto d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            suitable(d, &_gfxFam)) {
            _pdev = d; return;
        }
    }
    for (auto d : devs)
        if (suitable(d, &_gfxFam)) { _pdev = d; return; }

    throw std::runtime_error("No suitable GPU");
}

void VulkanRenderer::createDevice() {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = _gfxFam;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char* extName = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkPhysicalDeviceFeatures feats{};

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = &extName;
    ci.pEnabledFeatures        = &feats;

    VK_CHECK(vkCreateDevice(_pdev, &ci, nullptr, &_dev));
    vkGetDeviceQueue(_dev, _gfxFam, 0, &_gfxQ);
}

void VulkanRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_pdev, _surf, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(_pdev, _surf, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(_pdev, _surf, &fmtCount, fmts.data());

    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f; break;
        }
    }
    _swapFmt = chosen.format;

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(_pdev, _surf, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> pms(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(_pdev, _surf, &pmCount, pms.data());
    VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;
    for (auto& m : pms) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { present = m; break; }

    if (caps.currentExtent.width != UINT32_MAX) {
        _swapExt = caps.currentExtent;
    } else {
        int w, h;
        glfwGetFramebufferSize(_window, &w, &h);
        _swapExt.width  = std::max(caps.minImageExtent.width,
                          std::min(caps.maxImageExtent.width,  uint32_t(w)));
        _swapExt.height = std::max(caps.minImageExtent.height,
                          std::min(caps.maxImageExtent.height, uint32_t(h)));
    }

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = _surf;
    ci.minImageCount    = imgCount;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = _swapExt;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = present;
    ci.clipped          = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(_dev, &ci, nullptr, &_swap));

    vkGetSwapchainImagesKHR(_dev, _swap, &imgCount, nullptr);
    _swapImgs.resize(imgCount);
    vkGetSwapchainImagesKHR(_dev, _swap, &imgCount, _swapImgs.data());

    _swapViews.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image    = _swapImgs[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = chosen.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(_dev, &vi, nullptr, &_swapViews[i]));
    }
}

void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription att{};
    att.format         = _swapFmt;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &att;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;
    VK_CHECK(vkCreateRenderPass(_dev, &ci, nullptr, &_rpass));
}

VkShaderModule VulkanRenderer::createShaderModule(const uint32_t* code, std::size_t bytes) const {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode    = code;
    VkShaderModule m{};
    VK_CHECK(vkCreateShaderModule(_dev, &ci, nullptr, &m));
    return m;
}

void VulkanRenderer::createPipeline() {
    VkShaderModule vert = createShaderModule(CELL_VERT_SPV, sizeof(CELL_VERT_SPV));
    VkShaderModule frag = createShaderModule(CELL_FRAG_SPV, sizeof(CELL_FRAG_SPV));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(Vertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.location = 0;
    attr.binding  = 0;
    attr.format   = VK_FORMAT_R32G32_SFLOAT;
    attr.offset   = 0;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bind;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions    = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{0, 0, float(_swapExt.width), float(_swapExt.height), 0, 1};
    VkRect2D   sc{{0, 0}, _swapExt};
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports    = &vp;
    vps.scissorCount  = 1;
    vps.pScissors     = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(PushConst);

    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcr;
    VK_CHECK(vkCreatePipelineLayout(_dev, &pli, nullptr, &_layout));

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount          = 2;
    gpi.pStages             = stages;
    gpi.pVertexInputState   = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState      = &vps;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState   = &ms;
    gpi.pColorBlendState    = &cb;
    gpi.layout              = _layout;
    gpi.renderPass          = _rpass;
    gpi.subpass             = 0;

    VK_CHECK(vkCreateGraphicsPipelines(_dev, VK_NULL_HANDLE, 1, &gpi, nullptr, &_pipe));
    vkDestroyShaderModule(_dev, vert, nullptr);
    vkDestroyShaderModule(_dev, frag, nullptr);
}

void VulkanRenderer::createFramebuffers() {
    _fbufs.resize(_swapViews.size());
    for (size_t i = 0; i < _swapViews.size(); ++i) {
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = _rpass;
        ci.attachmentCount = 1;
        ci.pAttachments    = &_swapViews[i];
        ci.width           = _swapExt.width;
        ci.height          = _swapExt.height;
        ci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(_dev, &ci, nullptr, &_fbufs[i]));
    }
}

uint32_t VulkanRenderer::findMemType(uint32_t filter, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(_pdev, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("no suitable memory type");
}

void VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags props,
                                  VkBuffer& outBuf, VkDeviceMemory& outMem) const {
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(_dev, &bi, nullptr, &outBuf));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(_dev, outBuf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemType(req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(_dev, &ai, nullptr, &outMem));
    vkBindBufferMemory(_dev, outBuf, outMem, 0);
}

void VulkanRenderer::createQuadBuffers() {
    // Vertex buffer
    createBuffer(sizeof(QUAD_VERTS),
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 _vbuf, _vmem);
    void* vp = nullptr;
    vkMapMemory(_dev, _vmem, 0, sizeof(QUAD_VERTS), 0, &vp);
    std::memcpy(vp, QUAD_VERTS, sizeof(QUAD_VERTS));
    vkUnmapMemory(_dev, _vmem);

    // Index buffer
    createBuffer(sizeof(QUAD_INDICES),
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 _ibuf, _imem);
    void* ip = nullptr;
    vkMapMemory(_dev, _imem, 0, sizeof(QUAD_INDICES), 0, &ip);
    std::memcpy(ip, QUAD_INDICES, sizeof(QUAD_INDICES));
    vkUnmapMemory(_dev, _imem);
}

void VulkanRenderer::createCommandBuffers() {
    VkCommandPoolCreateInfo pi{};
    pi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = _gfxFam;
    VK_CHECK(vkCreateCommandPool(_dev, &pi, nullptr, &_cmdPool));

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = _cmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = FRAMES;
    VK_CHECK(vkAllocateCommandBuffers(_dev, &ai, _cmdBufs.data()));
}

void VulkanRenderer::createSync() {
    VkSemaphoreCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};     fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < FRAMES; ++i) {
        VK_CHECK(vkCreateSemaphore(_dev, &si, nullptr, &_imgReady[i]));
        VK_CHECK(vkCreateSemaphore(_dev, &si, nullptr, &_renderDone[i]));
        VK_CHECK(vkCreateFence    (_dev, &fi, nullptr, &_inFlight[i]));
    }
}

// ── Drawing helpers ──────────────────────────────────────────────────────────

static inline float pxToNdcX(int px, int w) { return float(px) / float(w) * 2.0f - 1.0f; }
static inline float pxToNdcY(int py, int h) { return float(py) / float(h) * 2.0f - 1.0f; }
static inline float pxToNdcW(int px, int w) { return float(px) / float(w) * 2.0f; }
static inline float pxToNdcH(int py, int h) { return float(py) / float(h) * 2.0f; }

void VulkanRenderer::drawQuadPx(VkCommandBuffer cmd,
                                int x, int y, int w, int h,
                                float mr, float mg, float mb, float style,
                                float hr, float hg, float hb,
                                float lr, float lg, float lb) {
    PushConst pc{};
    pc.pos[0]  = pxToNdcX(x, _swapExt.width);
    pc.pos[1]  = pxToNdcY(y, _swapExt.height);
    pc.size[0] = pxToNdcW(w, _swapExt.width);
    pc.size[1] = pxToNdcH(h, _swapExt.height);
    pc.mid[0] = mr; pc.mid[1] = mg; pc.mid[2] = mb; pc.mid[3] = style;
    pc.hi [0] = hr; pc.hi [1] = hg; pc.hi [2] = hb; pc.hi [3] = 0;
    pc.lo [0] = lr; pc.lo [1] = lg; pc.lo [2] = lb; pc.lo [3] = 0;

    vkCmdPushConstants(cmd, _layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConst), &pc);
    vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
}

void VulkanRenderer::drawCellPx(VkCommandBuffer cmd, int col, int row, int id) {
    const int x = BOARD_X + col * CELL;
    const int y = BOARD_Y + row * CELL;

    if (id == 0) {
        const Theme& t = THEMES[0];
        drawQuadPx(cmd, x, y, CELL, CELL,
                   t.mid.r, t.mid.g, t.mid.b, 2.0f,
                   t.hi.r,  t.hi.g,  t.hi.b,
                   t.lo.r,  t.lo.g,  t.lo.b);
        return;
    }
    if (id == 8) {
        drawQuadPx(cmd, x, y, CELL, CELL,
                   0, 0, 0, 1.0f,
                   GHOST_COLOR.r, GHOST_COLOR.g, GHOST_COLOR.b,
                   0, 0, 0);
        return;
    }
    const Theme& t = THEMES[id];
    drawQuadPx(cmd, x, y, CELL, CELL,
               t.mid.r, t.mid.g, t.mid.b, 0.0f,
               t.hi.r,  t.hi.g,  t.hi.b,
               t.lo.r,  t.lo.g,  t.lo.b);
}

int VulkanRenderer::textWidth(const char* text, int scale) const {
    int n = 0; while (text[n]) ++n;
    return n * 6 * scale;   // 5 px char + 1 px gap
}

void VulkanRenderer::drawText(VkCommandBuffer cmd, int x, int y, int scale,
                              float r, float g, float b, const char* text) {
    int cx = x;
    for (const char* p = text; *p; ++p) {
        const uint8_t* g7 = glyphFor(*p);
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = g7[row];
            for (int col = 0; col < 5; ++col) {
                if (bits & (1u << (4 - col))) {
                    drawQuadPx(cmd,
                               cx + col * scale, y + row * scale,
                               scale, scale,
                               r, g, b, 3.0f,
                               r, g, b,
                               r, g, b);
                }
            }
        }
        cx += 6 * scale;
    }
}

void VulkanRenderer::recordFrame(VkCommandBuffer cmd, VkFramebuffer fb,
                                 const GameState& s, int gameOverScore) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkClearValue clr{};
    clr.color = { { BG.r, BG.g, BG.b, 1.0f } };

    VkRenderPassBeginInfo rpb{};
    rpb.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpb.renderPass        = _rpass;
    rpb.framebuffer       = fb;
    rpb.renderArea.extent = _swapExt;
    rpb.clearValueCount   = 1;
    rpb.pClearValues      = &clr;
    vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipe);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &_vbuf, &off);
    vkCmdBindIndexBuffer  (cmd, _ibuf, 0, VK_INDEX_TYPE_UINT16);

    // Board frame: 4 thin slabs forming a double-line look
    auto frame = [&](int x, int y, int w, int h) {
        drawQuadPx(cmd, x, y, w, h,
                   FRAME_COLOR.r, FRAME_COLOR.g, FRAME_COLOR.b, 3.0f,
                   FRAME_COLOR.r, FRAME_COLOR.g, FRAME_COLOR.b,
                   FRAME_COLOR.r, FRAME_COLOR.g, FRAME_COLOR.b);
    };
    const int fx = BOARD_X - BORD;
    const int fy = BOARD_Y - BORD;
    const int fw = BOARD_W_PX + 2 * BORD;
    const int fh = BOARD_H_PX + 2 * BORD;
    frame(fx, fy,         fw,  BORD);                 // top
    frame(fx, fy + fh - BORD, fw, BORD);              // bottom
    frame(fx, fy,         BORD, fh);                  // left
    frame(fx + fw - BORD, fy, BORD, fh);              // right

    // Build back buffer with ghost + active piece
    Board back = s.board;
    const auto& shape = PIECES[s.curPiece][s.curRot];

    if (s.ghostY != s.curY)
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (shape[r][c]) {
                    int br = s.ghostY + r, bc = s.curX + c;
                    if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W && !back[br][bc])
                        back[br][bc] = 8;
                }

    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (shape[r][c]) {
                int br = s.curY + r, bc = s.curX + c;
                if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W)
                    back[br][bc] = s.curPiece + 1;
            }

    for (int r = 0; r < BOARD_H; ++r)
        for (int c = 0; c < BOARD_W; ++c)
            drawCellPx(cmd, c, r, back[r][c]);

    // ── Sidebar ──────────────────────────────────────────────────────────────
    int sy = BOARD_Y;

    // TETRIS rainbow title
    {
        const int sc = 4;
        int cx = SIDE_X;
        const char* TITLE = "TETRIS";
        for (int i = 0; i < 6; ++i) {
            char ch[2] = { TITLE[i], 0 };
            drawText(cmd, cx, sy, sc,
                     TITLE_COLS[i].r, TITLE_COLS[i].g, TITLE_COLS[i].b, ch);
            cx += 6 * sc;
        }
    }
    sy += 7 * 4 + 16;

    // SCORE
    drawText(cmd, SIDE_X, sy, 2, LABEL_COLOR.r, LABEL_COLOR.g, LABEL_COLOR.b, "SCORE");
    sy += 7 * 2 + 4;
    {
        char buf[16]; std::snprintf(buf, sizeof(buf), "%d", s.score);
        drawText(cmd, SIDE_X, sy, 3, VALUE_COLOR.r, VALUE_COLOR.g, VALUE_COLOR.b, buf);
        sy += 7 * 3 + 12;
    }
    drawText(cmd, SIDE_X, sy, 2, LABEL_COLOR.r, LABEL_COLOR.g, LABEL_COLOR.b, "LEVEL");
    sy += 7 * 2 + 4;
    {
        char buf[16]; std::snprintf(buf, sizeof(buf), "%d", s.level);
        drawText(cmd, SIDE_X, sy, 3, VALUE_COLOR.r, VALUE_COLOR.g, VALUE_COLOR.b, buf);
        sy += 7 * 3 + 12;
    }
    drawText(cmd, SIDE_X, sy, 2, LABEL_COLOR.r, LABEL_COLOR.g, LABEL_COLOR.b, "LINES");
    sy += 7 * 2 + 4;
    {
        char buf[16]; std::snprintf(buf, sizeof(buf), "%d", s.totalLines);
        drawText(cmd, SIDE_X, sy, 3, VALUE_COLOR.r, VALUE_COLOR.g, VALUE_COLOR.b, buf);
        sy += 7 * 3 + 12;
    }
    drawText(cmd, SIDE_X, sy, 2, LABEL_COLOR.r, LABEL_COLOR.g, LABEL_COLOR.b, "NEXT");
    sy += 7 * 2 + 6;

    // Next-piece preview (small cells) – hidden during game-over overlay
    if (gameOverScore < 0) {
        const int psize = 18;
        const int origX = SIDE_X;
        const int origY = sy;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (PIECES[s.nextPiece][0][r][c]) {
                    const Theme& t = THEMES[s.nextPiece + 1];
                    drawQuadPx(cmd,
                               origX + c * psize, origY + r * psize,
                               psize, psize,
                               t.mid.r, t.mid.g, t.mid.b, 0.0f,
                               t.hi.r,  t.hi.g,  t.hi.b,
                               t.lo.r,  t.lo.g,  t.lo.b);
                }
    }

    // ── Game-over overlay ────────────────────────────────────────────────────
    if (gameOverScore >= 0) {
        const int ow = 280;
        const int oh = 180;
        const int ox = BOARD_X + (BOARD_W_PX - ow) / 2;
        const int oy = BOARD_Y + (BOARD_H_PX - oh) / 2;
        drawQuadPx(cmd, ox - 4, oy - 4, ow + 8, oh + 8,
                   1, 1, 1, 3,  1, 1, 1,  1, 1, 1);
        drawQuadPx(cmd, ox, oy, ow, oh,
                   0.5f, 0.0f, 0.05f, 3,
                   0.5f, 0.0f, 0.05f,
                   0.5f, 0.0f, 0.05f);

        int tx = ox + (ow - textWidth("GAME OVER", 4)) / 2;
        drawText(cmd, tx, oy + 24, 4, 1, 1, 1, "GAME OVER");

        char sc[32]; std::snprintf(sc, sizeof(sc), "SCORE: %d", gameOverScore);
        int sx = ox + (ow - textWidth(sc, 2)) / 2;
        drawText(cmd, sx, oy + 80, 2, 1, 1, 1, sc);

        const char* press = "PRESS ANY KEY";
        int px = ox + (ow - textWidth(press, 2)) / 2;
        drawText(cmd, px, oy + 130, 2, 0.85f, 0.85f, 0.85f, press);
    }

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
}

// ── Per-frame draw ────────────────────────────────────────────────────────────

void VulkanRenderer::draw(const GameState& s) {
    glfwPollEvents();
    if (glfwWindowShouldClose(_window)) {
        _pendingAction = Action::Quit;
        return;
    }

    vkWaitForFences(_dev, 1, &_inFlight[_frameIdx], VK_TRUE, UINT64_MAX);
    vkResetFences  (_dev, 1, &_inFlight[_frameIdx]);

    uint32_t imgIdx = 0;
    VkResult acq = vkAcquireNextImageKHR(_dev, _swap, UINT64_MAX,
                                          _imgReady[_frameIdx], VK_NULL_HANDLE, &imgIdx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) return;

    VkCommandBuffer cmd = _cmdBufs[_frameIdx];
    vkResetCommandBuffer(cmd, 0);
    recordFrame(cmd, _fbufs[imgIdx], s, -1);

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &_imgReady[_frameIdx];
    si.pWaitDstStageMask    = &wait;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &_renderDone[_frameIdx];
    VK_CHECK(vkQueueSubmit(_gfxQ, 1, &si, _inFlight[_frameIdx]));

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &_renderDone[_frameIdx];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &_swap;
    pi.pImageIndices      = &imgIdx;
    vkQueuePresentKHR(_gfxQ, &pi);

    _frameIdx = (_frameIdx + 1) % FRAMES;
}

void VulkanRenderer::drawGameOver(int score) {
    // Render the overlay frame, then block on key input
    while (!glfwWindowShouldClose(_window)) {
        glfwPollEvents();

        vkWaitForFences(_dev, 1, &_inFlight[_frameIdx], VK_TRUE, UINT64_MAX);
        vkResetFences  (_dev, 1, &_inFlight[_frameIdx]);

        uint32_t imgIdx = 0;
        if (vkAcquireNextImageKHR(_dev, _swap, UINT64_MAX,
                                  _imgReady[_frameIdx], VK_NULL_HANDLE, &imgIdx)
            != VK_SUCCESS) break;

        VkCommandBuffer cmd = _cmdBufs[_frameIdx];
        vkResetCommandBuffer(cmd, 0);
        Board empty{};
        GameState dummy{empty, 0, 0, 0, -1, -1, 0, 0, 0, 0};
        recordFrame(cmd, _fbufs[imgIdx], dummy, score);

        VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = &_imgReady[_frameIdx];
        si.pWaitDstStageMask    = &wait;
        si.commandBufferCount   = 1;
        si.pCommandBuffers      = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &_renderDone[_frameIdx];
        VK_CHECK(vkQueueSubmit(_gfxQ, 1, &si, _inFlight[_frameIdx]));

        VkPresentInfoKHR pi{};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = &_renderDone[_frameIdx];
        pi.swapchainCount     = 1;
        pi.pSwapchains        = &_swap;
        pi.pImageIndices      = &imgIdx;
        vkQueuePresentKHR(_gfxQ, &pi);

        _frameIdx = (_frameIdx + 1) % FRAMES;

        if (pollInput() != Action::None) break;
        std::this_thread::sleep_for(16ms);
    }
}

// ── Input ────────────────────────────────────────────────────────────────────

void VulkanRenderer::keyCb(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    auto* self = static_cast<VulkanRenderer*>(glfwGetWindowUserPointer(w));
    if (!self) return;

    switch (key) {
        case GLFW_KEY_LEFT:   case GLFW_KEY_A:  self->_pendingAction = Action::Left;     break;
        case GLFW_KEY_RIGHT:  case GLFW_KEY_D:  self->_pendingAction = Action::Right;    break;
        case GLFW_KEY_DOWN:   case GLFW_KEY_S:  self->_pendingAction = Action::SoftDrop; break;
        case GLFW_KEY_UP:     case GLFW_KEY_W:  self->_pendingAction = Action::Rotate;   break;
        case GLFW_KEY_SPACE:                    self->_pendingAction = Action::HardDrop; break;
        case GLFW_KEY_Q:      case GLFW_KEY_ESCAPE:
                                                self->_pendingAction = Action::Quit;     break;
        default: break;
    }
}

Action VulkanRenderer::pollInput() {
    // glfwPollEvents() is called once per frame inside draw() so callbacks
    // fire there; here we just consume any pending action.
    Action a = _pendingAction;
    _pendingAction = Action::None;
    return a;
}
