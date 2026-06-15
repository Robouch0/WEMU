#include "VulkanOutputWindow.hpp"

#include <QCloseEvent>
#include <QKeyEvent>
#include <stdexcept>

VulkanOutputWindow::VulkanOutputWindow(QWindow *parent)
    : QWindow(parent)
{
    setSurfaceType(QWindow::VulkanSurface);
    resize(1280, 720);

    if (!m_vkInstance.create())
        throw std::runtime_error("VulkanOutputWindow: failed to create QVulkanInstance");

    setVulkanInstance(&m_vkInstance);

    create();

    m_surface = m_vkInstance.surfaceForWindow(this);
    if (m_surface == VK_NULL_HANDLE)
        throw std::runtime_error("VulkanOutputWindow: failed to obtain VkSurfaceKHR");
}

VulkanOutputWindow::~VulkanOutputWindow()
{
    destroy();
}

VkInstance VulkanOutputWindow::vkInstance() const
{
    return m_vkInstance.vkInstance();
}

VkSurfaceKHR VulkanOutputWindow::vkSurface() const
{
    return m_surface;
}

void VulkanOutputWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        emit escapePressed();
    QWindow::keyPressEvent(event);
}

void VulkanOutputWindow::closeEvent(QCloseEvent *event)
{
    emit escapePressed();
    event->ignore();
}
