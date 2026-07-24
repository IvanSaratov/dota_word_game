#include "dk/dxgi_capture.hpp"

#ifndef _WIN32
#error "dxgi_capture_win32.cpp is only available on Windows"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace dk {
namespace {

using Microsoft::WRL::ComPtr;

[[noreturn]] void throw_hresult(const char* operation, HRESULT result) {
    std::ostringstream message;
    message << operation << " failed (HRESULT 0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(result) << ')';
    throw std::runtime_error(message.str());
}

void require_success(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw_hresult(operation, result);
    }
}

struct RegionEdges {
    std::int64_t left;
    std::int64_t top;
    std::int64_t right;
    std::int64_t bottom;
};

RegionEdges validate_region(const Box& region) {
    if (region.width <= 0 || region.height <= 0) {
        throw std::invalid_argument("DXGI capture region must have positive width and height");
    }

    const auto left = static_cast<std::int64_t>(region.x);
    const auto top = static_cast<std::int64_t>(region.y);
    return RegionEdges{
        left,
        top,
        left + static_cast<std::int64_t>(region.width),
        top + static_cast<std::int64_t>(region.height),
    };
}

bool contains_region(const DXGI_OUTPUT_DESC& output, const RegionEdges& region) {
    const auto& bounds = output.DesktopCoordinates;
    return region.left >= static_cast<std::int64_t>(bounds.left) &&
           region.top >= static_cast<std::int64_t>(bounds.top) &&
           region.right <= static_cast<std::int64_t>(bounds.right) &&
           region.bottom <= static_cast<std::int64_t>(bounds.bottom);
}

class FrameReleaseGuard {
public:
    ~FrameReleaseGuard() {
        if (duplication_) {
            duplication_->ReleaseFrame();
        }
    }

    FrameReleaseGuard(const FrameReleaseGuard&) = delete;
    FrameReleaseGuard& operator=(const FrameReleaseGuard&) = delete;

    void activate(IDXGIOutputDuplication* duplication) noexcept {
        duplication_ = duplication;
    }

private:
    IDXGIOutputDuplication* duplication_{};
};

class UnmapGuard {
public:
    UnmapGuard(ID3D11DeviceContext* context, ID3D11Resource* resource) noexcept
        : context_(context), resource_(resource) {}

    ~UnmapGuard() {
        if (context_ && resource_) {
            context_->Unmap(resource_, 0);
        }
    }

    UnmapGuard(const UnmapGuard&) = delete;
    UnmapGuard& operator=(const UnmapGuard&) = delete;

private:
    ID3D11DeviceContext* context_;
    ID3D11Resource* resource_;
};

}  // namespace

class DxgiCapture::Impl {
public:
    explicit Impl(Box screen_region)
        : screen_region_(screen_region), edges_(validate_region(screen_region)) {
        initialize();
    }

    [[nodiscard]] std::optional<CapturedFrame> next_frame() {
        FrameReleaseGuard release_frame;
        DXGI_OUTDUPL_FRAME_INFO frame_info{};
        ComPtr<IDXGIResource> acquired_resource;
        const auto acquire_result = duplication_->AcquireNextFrame(
            5, &frame_info, acquired_resource.GetAddressOf());

        if (acquire_result == DXGI_ERROR_WAIT_TIMEOUT) {
            return std::nullopt;
        }
        if (acquire_result == DXGI_ERROR_ACCESS_LOST) {
            recreate_duplication();
            return std::nullopt;
        }
        require_success(acquire_result, "acquire duplicated desktop frame");

        release_frame.activate(duplication_.Get());
        const auto captured_at = std::chrono::steady_clock::now();

        ComPtr<ID3D11Texture2D> desktop_texture;
        require_success(
            acquired_resource.As(&desktop_texture),
            "query duplicated frame texture");

        context_->CopySubresourceRegion(
            staging_texture_.Get(),
            0,
            0,
            0,
            0,
            desktop_texture.Get(),
            0,
            &source_box_);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        require_success(
            context_->Map(
                staging_texture_.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "map DXGI staging texture");
        UnmapGuard unmap{context_.Get(), staging_texture_.Get()};

        const auto row_bytes =
            static_cast<std::size_t>(screen_region_.width) * sizeof(std::uint32_t);
        if (static_cast<std::size_t>(mapped.RowPitch) < row_bytes) {
            throw std::runtime_error("mapped DXGI row pitch is smaller than the capture row");
        }

        const auto* source = static_cast<const std::byte*>(mapped.pData);
        for (int row = 0; row < screen_region_.height; ++row) {
            std::memcpy(
                frame_buffer_.ptr(row),
                source + static_cast<std::size_t>(row) * mapped.RowPitch,
                row_bytes);
        }

        return CapturedFrame{frame_buffer_, captured_at};
    }

private:
    void initialize() {
        ComPtr<IDXGIFactory1> factory;
        require_success(
            CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())),
            "create DXGI factory");

        DXGI_OUTPUT_DESC selected_desc{};
        bool found_output = false;

        for (UINT adapter_index = 0;; ++adapter_index) {
            ComPtr<IDXGIAdapter1> adapter;
            const auto adapter_result =
                factory->EnumAdapters1(adapter_index, adapter.GetAddressOf());
            if (adapter_result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            require_success(adapter_result, "enumerate DXGI adapter");

            for (UINT output_index = 0;; ++output_index) {
                ComPtr<IDXGIOutput> output;
                const auto output_result =
                    adapter->EnumOutputs(output_index, output.GetAddressOf());
                if (output_result == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                require_success(output_result, "enumerate DXGI output");

                DXGI_OUTPUT_DESC description{};
                require_success(output->GetDesc(&description), "describe DXGI output");
                if (!contains_region(description, edges_)) {
                    continue;
                }
                if (found_output) {
                    throw std::invalid_argument(
                        "DXGI capture region matches more than one monitor output");
                }

                selected_adapter_ = adapter;
                selected_output_ = output;
                selected_desc = description;
                found_output = true;
            }
        }

        if (!found_output) {
            throw std::invalid_argument(
                "DXGI capture region must fit entirely within one monitor output; "
                "regions outside or spanning monitors are unsupported");
        }

        require_success(
            selected_output_.As(&output1_), "query DXGI 1.2 output interface");

        D3D_FEATURE_LEVEL feature_level{};
        require_success(
            D3D11CreateDevice(
                selected_adapter_.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                device_.GetAddressOf(),
                &feature_level,
                context_.GetAddressOf()),
            "create D3D11 capture device");

        recreate_duplication();

        D3D11_TEXTURE2D_DESC texture_description{};
        texture_description.Width = static_cast<UINT>(screen_region_.width);
        texture_description.Height = static_cast<UINT>(screen_region_.height);
        texture_description.MipLevels = 1;
        texture_description.ArraySize = 1;
        texture_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texture_description.SampleDesc.Count = 1;
        texture_description.Usage = D3D11_USAGE_STAGING;
        texture_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        require_success(
            device_->CreateTexture2D(
                &texture_description, nullptr, staging_texture_.GetAddressOf()),
            "create DXGI staging texture");

        const auto output_left =
            static_cast<std::int64_t>(selected_desc.DesktopCoordinates.left);
        const auto output_top =
            static_cast<std::int64_t>(selected_desc.DesktopCoordinates.top);
        source_box_.left = static_cast<UINT>(edges_.left - output_left);
        source_box_.top = static_cast<UINT>(edges_.top - output_top);
        source_box_.right = static_cast<UINT>(edges_.right - output_left);
        source_box_.bottom = static_cast<UINT>(edges_.bottom - output_top);
        source_box_.front = 0;
        source_box_.back = 1;

        frame_buffer_.create(screen_region_.height, screen_region_.width, CV_8UC4);
    }

    void recreate_duplication() {
        duplication_.Reset();
        require_success(
            output1_->DuplicateOutput(device_.Get(), duplication_.GetAddressOf()),
            "duplicate DXGI output");
    }

    Box screen_region_;
    RegionEdges edges_;
    ComPtr<IDXGIAdapter1> selected_adapter_;
    ComPtr<IDXGIOutput> selected_output_;
    ComPtr<IDXGIOutput1> output1_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> staging_texture_;
    D3D11_BOX source_box_{};
    cv::Mat frame_buffer_;
};

DxgiCapture::DxgiCapture(Box screen_region)
    : impl_(std::make_unique<Impl>(std::move(screen_region))) {}

DxgiCapture::~DxgiCapture() = default;

std::optional<CapturedFrame> DxgiCapture::next_frame() {
    return impl_->next_frame();
}

}  // namespace dk
