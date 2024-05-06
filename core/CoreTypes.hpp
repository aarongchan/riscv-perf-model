// <CoreTypes.hpp> -*- C++ -*-

#pragma once

#include "sparta/resources/Scoreboard.hpp"

namespace olympia::core_types
{
    using RegisterBitMask = sparta::Scoreboard::RegisterBitMask;

    //! Register file types
    enum RegFile : uint8_t
    {
        RF_INTEGER,
        RF_FLOAT,
        RF_VECTOR,
        RF_INVALID,
        N_REGFILES = RF_INVALID
    };

    // std::vector<core_types::RegFile> reg_files = {core_types::RF_INTEGER, core_types::RF_FLOAT,
    // core_types::RF_VECTOR};

    static inline const char* const regfile_names[] = {"integer", "float", "vector"};

    static inline const char* const issue_queue_types[] = {"alu", "fpu", "br", "vint", "vset"};

    inline std::ostream & operator<<(std::ostream & os, const RegFile & rf)
    {
        sparta_assert(rf < RegFile::RF_INVALID, "RF index off into the weeds " << rf);
        os << regfile_names[rf];
        return os;
    }
} // namespace olympia::core_types
