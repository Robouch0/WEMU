#pragma once

#include <QVulkanInstance>
#include <QWindow>
#include <vulkan/vulkan.h>

class VulkanOutputWindow : public QWindow {
    Q_OBJECT
public:
    explicit VulkanOutputWindow(QWindow *parent = nullptr);
    ~VulkanOutputWindow() override;

    VkInstance   vkInstance() const;
    VkSurfaceKHR vkSurface()  const;

signals:
    void escapePressed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QVulkanInstance m_vkInstance;
    VkSurfaceKHR    m_surface = VK_NULL_HANDLE;
};
