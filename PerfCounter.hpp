#pragma once

#include <cstdint>
#include <vector>

class PerformanceCounter
{
public:
    typedef std::uint64_t counter_t;
    typedef std::uint16_t address_t;
    typedef std::uint16_t opcode_t;

private:

    typedef std::vector<address_t> addresses_list;

    counter_t   ticks_;

public:
    PerformanceCounter()
        : ticks_(0)
    {
    }

    ~PerformanceCounter()
    {
    }

    void NextExecutionTick()
    {
        ++ ticks_;
    }

    void InstructionFetchedAt(address_t pc, opcode_t opcode)
    {
        (void)pc;
        (void)opcode;
    }

    void InstructionExecuteAt()
    {
    }
};
