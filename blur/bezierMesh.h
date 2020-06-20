#pragma once
#include "../magma/magma.h"

class BezierPatchMesh
{
public:
    explicit BezierPatchMesh(const uint32_t patches[][16],
        const uint32_t numPatches,
        const float patchVertices[][3],
        const uint32_t subdivisionDegree,
        std::shared_ptr<magma::CommandBuffer> cmdBuffer);
    void draw(std::shared_ptr<magma::CommandBuffer> cmdBuffer) const;
    const magma::VertexInputState& getVertexInput() const;

private:
    struct Patch;

    std::vector<std::shared_ptr<Patch>> patches;
    std::shared_ptr<magma::IndexBuffer> indexBuffer;
};

struct BezierPatchMesh::Patch
{
    Patch(std::shared_ptr<magma::CommandBuffer> cmdBuffer,
        std::shared_ptr<magma::SrcTransferBuffer> vertices,
        std::shared_ptr<magma::SrcTransferBuffer> normals,
        std::shared_ptr<magma::SrcTransferBuffer> texCoords);

    std::shared_ptr<magma::VertexBuffer> vertexBuffer;
    std::shared_ptr<magma::VertexBuffer> normalBuffer;
    std::shared_ptr<magma::VertexBuffer> texCoordBuffer;
};
