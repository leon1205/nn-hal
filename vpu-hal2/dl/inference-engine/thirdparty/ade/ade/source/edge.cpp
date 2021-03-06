////////////////////////////////////////////////////////////////////////////////////
//
//                   INTEL CORPORATION PROPRIETARY INFORMATION
//      This software is supplied under the terms of a license agreement or
//      nondisclosure agreement with Intel Corporation and may not be copied
//      or disclosed except in accordance with the terms of that agreement.
//        Copyright(c) 2006-2016 Intel Corporation. All Rights Reserved.
//
//

#include "edge.hpp"

#include "util/assert.hpp"

#include "node.hpp"
#include "graph.hpp"

namespace ade
{

Edge::Edge(Node* prev, Node* next)
{
    ASSERT(nullptr != prev);
    ASSERT(nullptr != next);
    resetPrevNode(prev);
    resetNextNode(next);
}

Edge::~Edge()
{
    unlink();
}

void Edge::unlink()
{
    resetPrevNode(nullptr);
    resetNextNode(nullptr);
}

void Edge::resetPrevNode(Node* newNode)
{
    if (newNode == m_prevNode)
    {
        return;
    }

    if (nullptr != m_prevNode)
    {
        m_prevNode->removeOutEdge(this);
        m_prevNode = nullptr;
    }
    if (nullptr != newNode)
    {
        newNode->addOutEdge(this);
        m_prevNode = newNode;
    }
}

void Edge::resetNextNode(Node* newNode)
{
    if (newNode == m_nextNode)
    {
        return;
    }

    if (nullptr != m_nextNode)
    {
        m_nextNode->removeInEdge(this);
        m_nextNode = nullptr;
    }
    if (nullptr != newNode)
    {
        newNode->addInEdge(this);
        m_nextNode = newNode;
    }
}

Graph* Edge::getParent() const
{
    if (nullptr != m_prevNode)
    {
        return m_prevNode->getParent();
    }
    if (nullptr != m_nextNode)
    {
        return m_nextNode->getParent();
    }
    return nullptr;
}

NodeHandle Edge::srcNode() const
{
    ASSERT_STRONG(nullptr != m_prevNode);
    return Graph::HandleMapper()(m_prevNode);
}

NodeHandle Edge::dstNode() const
{
    ASSERT_STRONG(nullptr != m_nextNode);
    return Graph::HandleMapper()(m_nextNode);
}

}
