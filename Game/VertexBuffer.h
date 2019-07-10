//
// VertexBuffer.h - Helper to create vertex buffer.
// 

#pragma once

#include <type_traits>
#include "Vertex.h"
#include "Bindable.h"

namespace DX // @todo: Move to Bind namespace.
{
    template<typename T>
    class VertexBuffer : public Bind::Bindable
    {
        public:
            VertexBuffer() = default;

            explicit VertexBuffer(_In_ ID3D11Device* device, _In_ T* data, uint32 vertexCount)
            {
                Create(device, data, vertexCount);
            }

            // Only works with template argument: VertexBufferData.
            template<typename = typename std::enable_if<std::is_same<T, VertexBufferData>::value>::type>
            explicit VertexBuffer(_In_ ID3D11Device* device, _In_ VertexBufferData const& data)
            {
                Create(device, data);
            }

            VertexBuffer(VertexBuffer const&) = default;
            VertexBuffer& operator=(VertexBuffer const&) = default;

            // Only works with template argument: VertexBufferData.
            template<typename = typename std::enable_if<std::is_same<T, VertexBufferData>::value>::type>
            void Create(_In_ ID3D11Device* device, _In_ VertexBufferData const& data)
            {
                stride = static_cast<UINT>(data.GetLayout().Size());

                D3D11_BUFFER_DESC desc = {};
                desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.ByteWidth = static_cast<UINT>(data.SizeBytes());
                desc.StructureByteStride = stride;

                D3D11_SUBRESOURCE_DATA vertexBufferData = {};
                vertexBufferData.pSysMem = data.GetData();

                DX::ThrowIfFailed(
                    device->CreateBuffer(&desc, &vertexBufferData, &buffer)
                );
            }

            void Create(_In_ ID3D11Device* device, _In_ T* data, uint32 vertexCount)
            {
                this->vertexCount = vertexCount;

                D3D11_BUFFER_DESC desc = { };

                desc.ByteWidth = stride * vertexCount;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

                D3D11_SUBRESOURCE_DATA vertexBufferData = { };
                vertexBufferData.pSysMem = data;

                DX::ThrowIfFailed(
                    device->CreateBuffer(&desc, &vertexBufferData, buffer.ReleaseAndGetAddressOf())
                );
            }

            virtual void Bind(DeviceResources* deviceResources) noexcept override
            {
                UINT const offset = 0;
                GetContext(deviceResources)->IASetVertexBuffers(0u, 1u, buffer.GetAddressOf(), &stride, &offset);
            }

            ID3D11Buffer* Get() const { return buffer.Get(); }
            ID3D11Buffer* const* GetAddressOf() const { return buffer.GetAddressOf(); }
            uint32 VertexCount() const { return vertexCount; }

            uint32 const Stride() const { return stride; }
            uint32 const* StridePtr() const { return &stride; }

        private:
            Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
            uint32 stride = static_cast<uint32>(sizeof(T));
            uint32 vertexCount = 0;
    };
}