#include "pch.h"
#include "Model.h"
#include "StringHelper.h"

Model::Model()
{
}


Model::~Model()
{
}

bool Model::Initialize(std::string const& filePath, ID3D11Device* device, ID3D11DeviceContext* deviceContext)
{
    this->device = device;
    this->deviceContext = deviceContext;
    
    if (!LoadModel(filePath))
        return false;

    shader.InitializeShaders(deviceContext);
    return true;
}

void Model::Draw(DirectX::XMMATRIX world, DirectX::XMMATRIX view, DirectX::XMMATRIX proj)
{
    for (uint32 i = 0; i < meshes.size(); ++i)
    {
        shader.SetShaderParameters(deviceContext, meshes[i].GetTransformMatrix() * world, view, proj);
        meshes[i].Draw();
        shader.RenderShader(deviceContext, meshes[i].GetIndexCount());
    }
}

bool Model::LoadModel(std::string const& filePath)
{
    directory = StringHelper::GetDirectoryFromPath(filePath);
    Assimp::Importer importer;

    aiScene const* pScene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ConvertToLeftHanded);

    if (pScene == nullptr)
        return false;

    this->ProcessNode(pScene->mRootNode, pScene, DirectX::XMMatrixIdentity());
    return true;
}

void Model::ProcessNode(aiNode* node, aiScene const* scene, DirectX::XMMATRIX const& parentTransformMatrix)
{
    DirectX::XMMATRIX nodeTransformMatrix = DirectX::XMMatrixTranspose(DirectX::XMMATRIX(&node->mTransformation.a1)) * parentTransformMatrix;

    for (uint32 i = 0; i < node->mNumMeshes; ++i)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(ProcessMesh(mesh, scene, nodeTransformMatrix));
    }

    for (uint32 i = 0; i < node->mNumChildren; ++i)
    {
        ProcessNode(node->mChildren[i], scene, nodeTransformMatrix);
    }
}

Mesh Model::ProcessMesh(aiMesh* mesh, aiScene const* scene, DirectX::XMMATRIX const& transformMatrix)
{
    std::vector<MD5Vertex> vertices;
    std::vector<DWORD> indices;

    // Get vertices
    for (uint32 i = 0; i < mesh->mNumVertices; ++i)
    {
        MD5Vertex vertex;
        vertex.position.x = mesh->mVertices[i].x;
        vertex.position.y = mesh->mVertices[i].y;
        vertex.position.z = mesh->mVertices[i].z;

        vertex.normal.x = mesh->mNormals[i].x;
        vertex.normal.y = mesh->mNormals[i].y;
        vertex.normal.z = mesh->mNormals[i].z;

        if (mesh->mTextureCoords[0])
        {
            vertex.textureCoordinate.x = static_cast<float>(mesh->mTextureCoords[0][i].x);
            vertex.textureCoordinate.y = static_cast<float>(mesh->mTextureCoords[0][i].y);
        }

        vertices.push_back(vertex);
    }

    // Get indices
    for (uint32 i = 0; i < mesh->mNumFaces; ++i)
    {
        aiFace face = mesh->mFaces[i];

        for (uint32 j = 0; j < face.mNumIndices; ++j)
            indices.push_back(face.mIndices[j]);
    }

    std::vector<Texture> textures;
    aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
    std::vector<Texture> diffuseTextures = LoadMaterialTextures(material, aiTextureType::aiTextureType_DIFFUSE, scene);
    textures.insert(textures.end(), diffuseTextures.begin(), diffuseTextures.end());

    return Mesh(device, deviceContext, vertices, indices, textures, transformMatrix);
}

TextureStorageType Model::DetermineTextureStorageType(aiScene const* pScene, aiMaterial* pMat, unsigned int index, aiTextureType textureType)
{
    if (pMat->GetTextureCount(textureType) == 0)
        return TextureStorageType::None;

    aiString path;
    pMat->GetTexture(textureType, index, &path);
    std::string texturePath = path.C_Str();

    // Check if texture is an embedded indexed texture by seeing if the file path is an index #
    if (texturePath[0] == '*')
    {
        if (pScene->mTextures[0]->mHeight == 0)
            return TextureStorageType::EmbeddedIndexCompressed;
        else
            return TextureStorageType::EmbeddedIndexNonCompressed;
    }
    // Check if texture is an embedded texture but not indexed (path will be the texture's name instead of #)
    else if (auto pTex = pScene->GetEmbeddedTexture(texturePath.c_str()))
    {
        if (pTex->mHeight == 0)
            return TextureStorageType::EmbeddedCompressed;
        else
            return TextureStorageType::EmbeddedIndexNonCompressed;
    }
    //Lastly check if texture is a filepath by checking for period before extension name.
    else if (texturePath.find('.') != std::string::npos)
        return TextureStorageType::Disk;

    return TextureStorageType::None; // No texture exists
}

std::vector<Texture> Model::LoadMaterialTextures(aiMaterial* pMaterial, aiTextureType textureType, aiScene const* pScene)
{
    std::vector<Texture> materialTextures;
    TextureStorageType storeType = TextureStorageType::Invalid;
    unsigned int textureCount = pMaterial->GetTextureCount(textureType);

    if (textureCount == 0) // If there are no textures.
    {
        storeType = TextureStorageType::None;
        aiColor3D aiColor(0.0f, 0.0f, 0.0f);

        switch (textureType)
        {
            case aiTextureType::aiTextureType_DIFFUSE:
            {
                pMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, aiColor);
                if (aiColor.IsBlack())
                {
                    materialTextures.push_back(Texture(device, Colors::UnloadedTextureColor, textureType));
                    return materialTextures;
                }

                materialTextures.push_back(Texture(device, Color(static_cast<uint8>(aiColor.r * 255), static_cast<uint8>(aiColor.g * 255), static_cast<uint8>(aiColor.b * 255)), textureType));
                return materialTextures;
            }
        }
    }
    else
    {
        for (UINT i = 0; i < textureCount; ++i)
        {
            aiString path;
            pMaterial->GetTexture(textureType, i, &path);
            storeType = DetermineTextureStorageType(pScene, pMaterial, i, textureType);

            switch (storeType)
            {
                case TextureStorageType::EmbeddedIndexCompressed:
                {
                    int index = GetTextureIndex(&path);
                    Texture embeddedIndexedTexture(device, reinterpret_cast<uint8*>(pScene->mTextures[index]->pcData),
                        pScene->mTextures[index]->mWidth, textureType);
                    materialTextures.push_back(embeddedIndexedTexture);
                    break;
                }
                case TextureStorageType::EmbeddedIndexNonCompressed:
                {
                    int index = GetTextureIndex(&path);
                    Texture embeddedIndexNonCompressed(device, reinterpret_cast<uint8*>(pScene->mTextures[index]->pcData),
                        pScene->mTextures[index]->mWidth * pScene->mTextures[index]->mHeight, textureType);
                    materialTextures.push_back(embeddedIndexNonCompressed);
                    break;
                }
                case TextureStorageType::EmbeddedCompressed:
                {
                    aiTexture const* pTexture = pScene->GetEmbeddedTexture(path.C_Str());
                    Texture embeddedTexture(device, reinterpret_cast<uint8*>(pTexture->pcData),
                        pTexture->mWidth, textureType);
                    materialTextures.push_back(embeddedTexture);
                    break;
                }
                case TextureStorageType::EmbeddedNonCompressed:
                {
                    aiTexture const* pTexture = pScene->GetEmbeddedTexture(path.C_Str());
                    Texture embeddedNonCompressed(device, reinterpret_cast<uint8*>(pTexture->pcData),
                        pTexture->mWidth * pTexture->mHeight, textureType);
                    materialTextures.push_back(embeddedNonCompressed);
                }
                case TextureStorageType::Disk:
                {
                    std::string fileName = directory + '\\' + path.C_Str();
                    Texture diskTexture(device, fileName, textureType);
                    materialTextures.push_back(diskTexture);
                    break;
                }
            }
        }
    }

    if (materialTextures.size() == 0)
    {
        materialTextures.push_back(Texture(device, Colors::UnhandledTextureColor, textureType));
    }
    return materialTextures;
}

int Model::GetTextureIndex(aiString* pStr)
{
    assert(pStr->length >= 2);
    return atoi(&pStr->C_Str()[1]);
}

namespace expr
{
    Model::Model(DX::DeviceResources* deviceResources, std::string const& fileName)
    {
        Assimp::Importer importer;
        auto const pScene = importer.ReadFile(fileName,
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ConvertToLeftHanded);

        for (size_t i = 0; i < pScene->mNumMeshes; ++i)
        {
            meshPtrs.push_back(ParseMesh(deviceResources, *pScene->mMeshes[i]));
        }

        pRoot = ParseNode(*pScene->mRootNode);
    }

    void Model::Draw(DX::DeviceResources* deviceResources, DirectX::FXMMATRIX transform) const
    {
        pRoot->Draw(deviceResources, transform);
    }

    std::unique_ptr<Mesh> Model::ParseMesh(DX::DeviceResources* deviceResources, aiMesh const& mesh)
    {
        VertexBufferData vbuf(std::move(
            VertexLayout{}
            << VertexLayout::Position3D
            << VertexLayout::Normal
            //<< VertexLayout::Texture2D
        ));

        for (unsigned int i = 0; i < mesh.mNumVertices; ++i)
        {
            vbuf.EmplaceBack(
                *reinterpret_cast<DirectX::XMFLOAT3*>(&mesh.mVertices[i]),
                *reinterpret_cast<DirectX::XMFLOAT3*>(&mesh.mNormals[i])
                //*reinterpret_cast<DirectX::XMFLOAT3*>(&mesh.mTextureCoords[0][i])
            );
        }

        std::vector<unsigned int> indices;
        indices.reserve(static_cast<size_t>(mesh.mNumFaces) * 3);

        for (unsigned int i = 0; i < mesh.mNumFaces; ++i)
        {
            auto const& face = mesh.mFaces[i];
            assert(face.mNumIndices == 3);
            indices.push_back(face.mIndices[0]);
            indices.push_back(face.mIndices[1]);
            indices.push_back(face.mIndices[2]);
        }

        std::vector<std::unique_ptr<Bind::Bindable>> bindablePtrs;

        bindablePtrs.push_back(std::make_unique<Bind::VertexBuffer<VertexBufferData>>(deviceResources, vbuf));
        bindablePtrs.push_back(std::make_unique<Bind::IndexBuffer<unsigned int>>(deviceResources, indices));

        auto pvs = std::make_unique<Bind::VertexShader>(deviceResources, L"VertexShader.vs");
        auto pvsbc = pvs->GetBytecode();
        bindablePtrs.push_back(std::move(pvs));

        bindablePtrs.push_back(std::make_unique<Bind::PixelShader>(deviceResources, L"PixelShader.ps"));

        //bindablePtrs.push_back(std::make_unique<Bind::Texture>(deviceResources, L""));

        bindablePtrs.push_back(std::make_unique<Bind::InputLayout>(deviceResources, vbuf.GetLayout().GetD3DLayout(), pvsbc));

        struct PSMaterialConstant
        {
            DirectX::XMFLOAT3 color = { 0.6f, 0.6f, 0.8f };
            float specularIntensity = 0.6f;
            float specularPower = 30.0f;
            float padding[3];
        } pmc;

        bindablePtrs.push_back(std::make_unique<Bind::PixelConstantBuffer<PSMaterialConstant>>(deviceResources, pmc, 1u));

        return std::make_unique<Mesh>(deviceResources, std::move(bindablePtrs));
    }

    std::unique_ptr<Node> Model::ParseNode(aiNode const& node)
    {
        auto const transform = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(
            reinterpret_cast<DirectX::XMFLOAT4X4 const*>(&node.mTransformation)
        ));

        std::vector<Mesh*> curMeshPtrs;
        curMeshPtrs.reserve(node.mNumMeshes);
        for (size_t i = 0; i < node.mNumMeshes; ++i)
        {
            auto const meshIndex = node.mMeshes[i];
            curMeshPtrs.push_back(meshPtrs.at(meshIndex).get());
        }

        auto pNode = std::make_unique<Node>(std::move(curMeshPtrs), transform);
        for (size_t i = 0; i < node.mNumChildren; ++i)
        {
            pNode->AddChild(ParseNode(*node.mChildren[i]));
        }

        return pNode;
    }
}