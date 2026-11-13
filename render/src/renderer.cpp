#include <md/render/renderer.h>

namespace md::render {

Renderer::Renderer(std::shared_ptr<RenderDevice> device)
    : device_(std::move(device))
{
}

void Renderer::setDevice(std::shared_ptr<RenderDevice> device)
{
    if (target_ != nullptr) {
        lastError_ = QStringLiteral("cannot change the device while a frame is open; "
                                    "call end() first");
        return;
    }

    device_ = std::move(device);
    lastError_.clear();
}

bool Renderer::begin(RenderTarget& target)
{
    if (target_ != nullptr) {
        lastError_ = QStringLiteral("begin() called while a frame is already open");
        return false;
    }

    if (!device_ || !device_->isAvailable()) {
        lastError_ = QStringLiteral("begin() refused: no available render device");
        return false;
    }

    if (!target.isValid()) {
        lastError_ = QStringLiteral("begin() refused: invalid render target");
        return false;
    }

    target_ = &target;
    lastError_.clear();
    return true;
}

gfx::RasterStats Renderer::submit(const gfx::CommandBuffer& commands)
{
    if (target_ == nullptr) {
        lastError_ = QStringLiteral("submit() called without begin()");
        return {};
    }

    if (!device_) {
        lastError_ = QStringLiteral("submit() called without a render device");
        return {};
    }

    const gfx::RasterStats stats = device_->submit(*target_, commands);
    const QString deviceError = device_->lastError();
    if (!deviceError.isEmpty())
        lastError_ = deviceError;

    return stats;
}

void Renderer::end()
{
    if (target_ == nullptr)
        return;

    target_ = nullptr;
}

} // namespace md::render
