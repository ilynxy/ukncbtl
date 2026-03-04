#ifndef XPU_ACCESS_TIME_HPP
#define XPU_ACCESS_TIME_HPP

class instime_counter_t {
public:
  static constexpr inline unsigned short mul = 10;
  using counter_type = signed int;

  class instime_t
  {
  public:
    using storage_type = signed short;

  private:
    explicit
    constexpr instime_t(storage_type t)
      : t_ { t }
    {
    }
  public:
    constexpr instime_t()
      : t_ { 0 }
    {
    }

    constexpr instime_t(double t)
      : t_ { static_cast<storage_type>(t * mul) }
    {
    }

    constexpr counter_type as_integer() const {
      return t_;
    }

    constexpr instime_t operator+(const instime_t& rhs) const {
      return instime_t { static_cast<storage_type>(t_ + rhs.t_) };
    }

    constexpr instime_t operator-(const instime_t& rhs) const {
      return instime_t { static_cast<storage_type>(t_ - rhs.t_) };
    }

    constexpr instime_t& operator+=(const instime_t& rhs) {
      t_ += rhs.t_;
      return *this;
    }

    constexpr instime_t& operator-=(const instime_t& rhs) {
      t_ -= rhs.t_;
      return *this;
    }

  private:
    storage_type t_;
  };

  instime_counter_t()
    : c_ { 0 }
    , s_ { mul }
  {
  }

  void add(const instime_t instime)
  {
    c_ += instime.as_integer();
  }

  void set(const instime_t instime)
  {
    c_ = instime.as_integer();
  }

  bool tick() // return true if not done
  {
    if (c_ > 0)
      c_ -= s_;

    return (c_ > s_);
  }

  counter_type s_;
private:
  counter_type c_;
};

using instime_t = instime_counter_t::instime_t;

struct xpu_instimes
{
  instime_t R_W[8][8];    // two op read->write               (mov s, d)
  instime_t R_Wb[8][8];   // two op read->writeb              (movb s, d, NOTE: the same as R_RMWb)
  instime_t R_R[8][8];    // two op read->read                (bit/cmp s, s)
  instime_t R_RMW[8][8];  // two op read->read-modify-write   (bic/bis/add/sub s, sd)
  instime_t R_RMWb[8][8]; // two op read->read-modify-writeb  (bicb/bisb)

  instime_t R[8];         // one op read                (tst s)
  instime_t W[8];         // one op write               (clr/sxt d)
  instime_t Wb[8];        // one op writeb              (clrb d, NOTE: the same as RMWb)
  instime_t RMW[8];       // one op read-modify-write   (inc/dec/...)
  instime_t RMWb[8];      // one op read-modify-write   (incb/decb/...)

  instime_t Bxx[2];       // 0 - not taken, 1 -- taken
  instime_t SOB[2];       // 0 - not taken (rN == 1), 1 -- taken (rN != 1)

  instime_t NOP;
  instime_t MARK;
  instime_t MTPS[8];
  instime_t MFPS[8];

  instime_t JMP[8];
  instime_t JSR[8];
  instime_t RTS;

  instime_t SINT;         // soft trap (emt/trap/iot/bpt)
  instime_t RTx;          // rti/rtt

  instime_t ASH[8];
  instime_t ASHC[8];
  instime_t MUL[8];
  instime_t DIV[8];

  instime_t RESET;
};

#endif // XPU_ACCESS_TIME_HPP
