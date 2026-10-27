#include "thumbnail/renderer.h"
#include "render_geometry.h"
#include "windows_helpers.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>

namespace thumbnail {
namespace {

using Microsoft::WRL::ComPtr;
constexpr unsigned kSupersampling = 2;
constexpr char kShader[] = R"(
struct VertexInput { float3 position : POSITION; float3 color : COLOR; };
struct VertexOutput { float4 position : SV_POSITION; float3 color : COLOR; };
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}
float4 pixel_main(VertexOutput input) : SV_TARGET { return float4(input.color, 1.0); }
)";

struct RenderContext {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11DepthStencilView> depth_view;
};

RenderContext create_context(unsigned size) {
    RenderContext result;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &result.device, nullptr, &result.context), "Create WARP device");
    D3D11_TEXTURE2D_DESC description{};
    description.Width = size;
    description.Height = size;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    check_hresult(result.device->CreateTexture2D(&description, nullptr, &result.target), "Create render target");
    check_hresult(result.device->CreateRenderTargetView(result.target.Get(), nullptr, &result.target_view), "Create target view");
    description.Format = DXGI_FORMAT_D32_FLOAT;
    description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    check_hresult(result.device->CreateTexture2D(&description, nullptr, &result.depth), "Create depth buffer");
    check_hresult(result.device->CreateDepthStencilView(result.depth.Get(), nullptr, &result.depth_view), "Create depth view");
    return result;
}

ComPtr<ID3DBlob> compile_shader(const char* entry, const char* profile) {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    check_hresult(D3DCompile(kShader, std::strlen(kShader), nullptr, nullptr, nullptr, entry, profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors), "Compile thumbnail shader");
    return code;
}

void draw_mesh(RenderContext& render, const std::vector<RenderVertex>& vertices, unsigned size, RgbColor background_color) {
    const auto vertex_code = compile_shader("vertex_main", "vs_5_0");
    const auto pixel_code = compile_shader("pixel_main", "ps_5_0");
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    check_hresult(render.device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(),
        nullptr, &vertex_shader), "Create vertex shader");
    check_hresult(render.device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(),
        nullptr, &pixel_shader), "Create pixel shader");
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    ComPtr<ID3D11InputLayout> layout;
    check_hresult(render.device->CreateInputLayout(elements, 2, vertex_code->GetBufferPointer(),
        vertex_code->GetBufferSize(), &layout), "Create vertex layout");
    D3D11_BUFFER_DESC buffer_description{};
    buffer_description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(RenderVertex));
    buffer_description.Usage = D3D11_USAGE_IMMUTABLE;
    buffer_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial_data{};
    initial_data.pSysMem = vertices.data();
    ComPtr<ID3D11Buffer> buffer;
    check_hresult(render.device->CreateBuffer(&buffer_description, &initial_data, &buffer), "Create mesh buffer");
    D3D11_RASTERIZER_DESC raster_description{};
    raster_description.FillMode = D3D11_FILL_SOLID;
    raster_description.CullMode = D3D11_CULL_NONE;
    raster_description.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> raster_state;
    check_hresult(render.device->CreateRasterizerState(&raster_description, &raster_state), "Create rasterizer");
    const float background[] = {background_color.red / 255.0f, background_color.green / 255.0f,
        background_color.blue / 255.0f, 1.0f};
    render.context->ClearRenderTargetView(render.target_view.Get(), background);
    render.context->ClearDepthStencilView(render.depth_view.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    render.context->OMSetRenderTargets(1, render.target_view.GetAddressOf(), render.depth_view.Get());
    render.context->RSSetState(raster_state.Get());
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(size), static_cast<float>(size), 0, 1};
    render.context->RSSetViewports(1, &viewport);
    const UINT stride = sizeof(RenderVertex);
    const UINT offset = 0;
    render.context->IASetInputLayout(layout.Get());
    render.context->IASetVertexBuffers(0, 1, buffer.GetAddressOf(), &stride, &offset);
    render.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    render.context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    render.context->PSSetShader(pixel_shader.Get(), nullptr, 0);
    render.context->Draw(static_cast<UINT>(vertices.size()), 0);
}

Image read_image(RenderContext& render, unsigned size) {
    D3D11_TEXTURE2D_DESC description{};
    render.target->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check_hresult(render.device->CreateTexture2D(&description, nullptr, &staging), "Create readback texture");
    render.context->CopyResource(staging.Get(), render.target.Get());
    Image image{size, size, std::vector<std::uint8_t>(static_cast<std::size_t>(size) * size * 4)};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check_hresult(render.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read back thumbnail");
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    for (unsigned y = 0; y < size; ++y) {
        for (unsigned x = 0; x < size; ++x) {
            for (unsigned channel = 0; channel < 4; ++channel) {
                unsigned sum = 0;
                for (unsigned dy = 0; dy < kSupersampling; ++dy) {
                    for (unsigned dx = 0; dx < kSupersampling; ++dx) {
                        sum += pixels[(y * kSupersampling + dy) * mapped.RowPitch +
                            (x * kSupersampling + dx) * 4 + channel];
                    }
                }
                image.rgba[(static_cast<std::size_t>(y) * size + x) * 4 + channel] =
                    static_cast<std::uint8_t>(sum / (kSupersampling * kSupersampling));
            }
        }
    }
    render.context->Unmap(staging.Get(), 0);
    return image;
}

} // namespace

Image render_thumbnail(const Mesh& mesh, unsigned size, const Appearance& appearance) {
    if (size < 64 || size > 1024) {
        throw std::runtime_error("Thumbnail size must be between 64 and 1024 pixels.");
    }
    const auto vertices = prepare_vertices(mesh, appearance.model);
    auto context = create_context(size * kSupersampling);
    draw_mesh(context, vertices, size * kSupersampling, appearance.background);
    return read_image(context, size);
}

} // namespace thumbnail
