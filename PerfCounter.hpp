#pragma once

#include <cstdint>
#include <vector>

class PerformanceCounter
{
public:
  typedef std::uint64_t counter_t;
  typedef std::uint16_t address_t;
  typedef std::uint16_t opcode_t;

  struct psegment_t
  {
    address_t   begin_;
    address_t   end_;
    std::string name_;
    counter_t   total_hits_;
    counter_t   total_ticks_;

    bool        in_progress_;
    counter_t   start_ticks_;

    psegment_t(address_t begin, address_t end, std::string name)
      : begin_(begin)
      , end_(end)
      , name_(name)
      , total_hits_(0)
      , total_ticks_(0)
      , in_progress_(false)
    {
    }

    ~psegment_t()
    {
    }

    void reset() noexcept
    {
      total_hits_ = 0;
      total_ticks_ = 0;
      in_progress_ = false;
    }

    void start(counter_t ticks)
    {
      if (in_progress_ == false)
      {
        start_ticks_ = ticks;
        in_progress_ = true;
      }
    }

    void stop(counter_t ticks)
    {
      if (in_progress_ == true)
      {
        ++ total_hits_;
        total_ticks_ += (ticks - start_ticks_);
        in_progress_ = false;
      }
    }

    void check(address_t addr, counter_t ticks)
    {
      if (addr == end_)
      {
        stop(ticks);
      }
      else if (addr == begin_)
      {
        start(ticks);
      }
    }
  };

private:

  std::vector<psegment_t> segments_;

  counter_t   ticks_;

public:
  PerformanceCounter()
    : ticks_(0)
  {
    segments_.push_back(psegment_t(0115170, 0115264, "Draw sprite -2 px"));
    segments_.push_back(psegment_t(0115266, 0115372, "Draw sprite  4 px"));
    segments_.push_back(psegment_t(0115374, 0115470, "Draw sprite  2 px"));
    segments_.push_back(psegment_t(0115472, 0115532, "Draw sprite  0 px"));
  }

  void dump_stats()
  {
    counter_t total_ticks = 0;
    for (auto &s : segments_)
      total_ticks += s.total_ticks_;

    std::vector<double> rel;
    std::vector<double> avg;
    rel.reserve(segments_.size());
    avg.reserve(segments_.size());

    for (auto &s : segments_)
    {
      rel.push_back((1.0 * s.total_ticks_) / total_ticks);
      avg.push_back((1.0 * s.total_ticks_) / s.total_hits_);
    }
  }

  ~PerformanceCounter()
  {
    dump_stats();
  }

  void NextExecutionTick()
  {
    ++ ticks_;
  }

  void InstructionFetchedAt(address_t pc, opcode_t opcode)
  {
    (void)opcode;

    for (auto &s : segments_)
    {
      s.check(pc, ticks_);
    }
  }

  void InstructionExecuteAt()
  {
  }
};
