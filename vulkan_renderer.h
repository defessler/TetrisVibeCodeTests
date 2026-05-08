#pragma once
// Vulkan + GLFW sprite renderer.
//
// Each Tetris cell is drawn as a single bevel-shaded quad via a tiny
// pipeline that takes a unit quad VB and per-quad push constants.
// Text labels and digits are rendered with a 5×7 monochrome bitmap font
// where each set pixel is its own little quad.

#include "renderer.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <array>
#include <vector>

class VulkanRenderer : public Renderer {
public:
    void   init()                    override;
    void   shutdown()                override;
    void   draw(const GameState& s)  override;
    void   drawGameOver(int score)   override;
    Action pollInput()               override;

private:
    // ── Window layout (pixels) ────────────────────────────────────────────────
    static constexpr int CELL    = 32;                     // px per Tetris cell
    static constexpr int BOARD_X = 24;                     // top-left of board
    static constexpr int BOARD_Y = 40;
    static constexpr int BORD    = 4;                      // border thickness
    static constexpr int BOARD_W_PX = BOARD_W * CELL;      // 320
    static constexpr int BOARD_H_PX = BOARD_H * CELL;      // 640
    static constexpr int SIDE_X  = BOARD_X + BOARD_W_PX + 24;
    static constexpr int WIN_W   = SIDE_X + 220;           //  ≈ 588
    static constexpr int WIN_H   = BOARD_Y + BOARD_H_PX + 40; // 720

    // ── GLFW + Vulkan handles ─────────────────────────────────────────────────
    GLFWwindow*       _window  = nullptr;
    VkInstance        _inst    = VK_NULL_HANDLE;
    VkSurfaceKHR      _surf    = VK_NULL_HANDLE;
    VkPhysicalDevice  _pdev    = VK_NULL_HANDLE;
    VkDevice          _dev     = VK_NULL_HANDLE;
    uint32_t          _gfxFam  = 0;
    VkQueue           _gfxQ    = VK_NULL_HANDLE;

    VkSwapchainKHR    _swap    = VK_NULL_HANDLE;
    VkFormat          _swapFmt = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D        _swapExt{};
    std::vector<VkImage>       _swapImgs;
    std::vector<VkImageView>   _swapViews;
    std::vector<VkFramebuffer> _fbufs;

    VkRenderPass      _rpass   = VK_NULL_HANDLE;
    VkPipelineLayout  _layout  = VK_NULL_HANDLE;
    VkPipeline        _pipe    = VK_NULL_HANDLE;

    VkBuffer          _vbuf    = VK_NULL_HANDLE;
    VkDeviceMemory    _vmem    = VK_NULL_HANDLE;
    VkBuffer          _ibuf    = VK_NULL_HANDLE;
    VkDeviceMemory    _imem    = VK_NULL_HANDLE;

    static constexpr int FRAMES = 2;
    VkCommandPool     _cmdPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, FRAMES> _cmdBufs{};
    std::array<VkSemaphore,     FRAMES> _imgReady{};
    std::array<VkSemaphore,     FRAMES> _renderDone{};
    std::array<VkFence,         FRAMES> _inFlight{};
    uint32_t          _frameIdx = 0;

    // ── Input plumbing ────────────────────────────────────────────────────────
    Action _pendingAction = Action::None;
    static void keyCb(GLFWwindow* w, int key, int sc, int action, int mods);

    // ── Vulkan init helpers ───────────────────────────────────────────────────
    void createInstance();
    void createSurface();
    void pickPhysical();
    void createDevice();
    void createSwapchain();
    void createRenderPass();
    void createPipeline();
    void createFramebuffers();
    void createQuadBuffers();
    void createCommandBuffers();
    void createSync();
    void cleanupVulkan();

    [[nodiscard]] uint32_t findMemType(uint32_t filter, VkMemoryPropertyFlags flags) const;
    VkShaderModule createShaderModule(const uint32_t* code, std::size_t bytes) const;
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkBuffer& outBuf, VkDeviceMemory& outMem) const;

    // ── Drawing helpers (fill cmd buffer) ─────────────────────────────────────
    void recordFrame(VkCommandBuffer cmd, VkFramebuffer fb,
                     const GameState& s, int gameOverScore);
    void drawQuadPx(VkCommandBuffer cmd,
                    int x, int y, int w, int h,
                    float mr, float mg, float mb, float style,
                    float hr, float hg, float hb,
                    float lr, float lg, float lb);
    void drawCellPx(VkCommandBuffer cmd, int col, int row, int id);
    void drawText  (VkCommandBuffer cmd, int x, int y, int scale,
                    float r, float g, float b, const char* text);
    int  textWidth(const char* text, int scale) const;
};
