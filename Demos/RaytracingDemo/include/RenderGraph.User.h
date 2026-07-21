#pragma once

#include <memory>

#include <RenderGraph/RenderGraphRoot.h>

class CommandList;
class RaytracingDemo;

namespace RenderGraph
{
    class User
    {
    public:
        static std::unique_ptr<RenderGraphRoot> Create(RaytracingDemo& demo, CommandList& commandList);
    };
}
