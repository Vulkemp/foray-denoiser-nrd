#pragma once
#include "include_ndr.hpp"
#include <foray_exception.hpp>

namespace foray::nrdd
{
    inline void AssertNrdResult(nrd::Result result)
    {
        Assert(result == nrd::Result::SUCCESS);
    }
} // namespace foray::nrdd
