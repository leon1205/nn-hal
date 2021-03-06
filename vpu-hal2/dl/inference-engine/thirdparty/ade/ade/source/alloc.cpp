////////////////////////////////////////////////////////////////////////////////////
//
//                   INTEL CORPORATION PROPRIETARY INFORMATION
//      This software is supplied under the terms of a license agreement or
//      nondisclosure agreement with Intel Corporation and may not be copied
//      or disclosed except in accordance with the terms of that agreement.
//        Copyright(c) 2017 Intel Corporation. All Rights Reserved.
//
//

#include <memory/alloc.hpp>


#ifndef __APPLE__
#include <malloc.h>
#else
#include <stdlib.h>
void * memalign(size_t alignment, size_t size) {
    void *buffer;
    posix_memalign(&buffer, alignment, size);
    return buffer;
}
#endif

#include <util/math.hpp>
#include <util/assert.hpp>

namespace ade
{

void* aligned_alloc(std::size_t size, std::size_t alignment)
{
    ASSERT(util::is_pow2(alignment));
#ifdef WIN32
    return _aligned_malloc(size, alignment);
#else
    return memalign(alignment, size);
#endif
}

void aligned_free(void* ptr)
{
#ifdef WIN32
    return _aligned_free(ptr);
#else
    return free(ptr);
#endif
}

}
