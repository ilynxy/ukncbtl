/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

/// \file Processor.h  KM1801VM2 processor class

#pragma once

#include "Defines.h"
#include "Memory.h"

#define PROCESSOR_USE_NEW_ALU 1

#include "xpu_instimes.hpp"

//class CMemoryController;

//////////////////////////////////////////////////////////////////////

/// \brief KM1801VM2 processor
namespace vm2
{

struct alu {
    using psw_t  = unsigned int; // NOTE: have to be at least 16 bit
    using data_t = unsigned int; // NOTE: have to be at least 32 bit

    using cond_fn = bool (*)(const psw_t);

    static bool condBR   (const psw_t psw) { return  true;   };
    static bool condBNE  (const psw_t psw) { return !condBEQ(psw); };
    static bool condBEQ  (const psw_t psw) { return (psw & PSW_Z); };
    static bool condBPL  (const psw_t psw) { return !condBMI(psw); };
    static bool condBMI  (const psw_t psw) { return (psw & PSW_N); };
    static bool condBVC  (const psw_t psw) { return !condBVS(psw); };
    static bool condBVS  (const psw_t psw) { return (psw & PSW_V); };
    static bool condBHIS (const psw_t psw) { return !condBLO(psw); }; // BCC
    static bool condBLO  (const psw_t psw) { return (psw & PSW_C); }; // BCS
    static bool condBGE  (const psw_t psw) { return !condBLT(psw);        };
    static bool condBLT  (const psw_t psw) { return condBMI(psw) != condBVS(psw); };
    static bool condBGT  (const psw_t psw) { return !condBLE(psw); };
    static bool condBLE  (const psw_t psw) { return condBEQ(psw) || condBLT(psw); };
    static bool condBHI  (const psw_t psw) { return !condBLOS(psw); };
    static bool condBLOS (const psw_t psw) { return condBLO(psw) || condBEQ(psw); };

    struct alu1_s {
        psw_t   psw;
        data_t  dst; // NOTE: combined { Rn|1, Rn } for EIS
    };
    using alu1_fn = alu1_s (*)(const alu1_s);


    static void NZ_as_u16(psw_t& psw, data_t data)
    {
        psw   &= ~(PSW_N | PSW_Z);
        if (data  & 0x8000) psw |= PSW_N;
        if (data == 0x0000) psw |= PSW_Z;
    }

    static void NZ_as_u08(psw_t& psw, data_t data)
    {
        psw   &= ~(PSW_N | PSW_Z);
        if (data  & 0x80) psw |= PSW_N;
        if (data == 0x00) psw |= PSW_Z;
    }

    static alu1_s opSWAB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = ((s.dst << 8) & 0xFF00) | ((s.dst >> 8) & 0x00FF);

        NZ_as_u08(psw, data & 0xFF); // NOTE: flags by low byte of 16-bit result
        return { psw, data };
    }

    static alu1_s opCLR(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_N | PSW_Z | PSW_V | PSW_C);
        data_t data  = 0;
        psw |= PSW_Z;
        return { psw, data };
    }

    static alu1_s opCLRB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_N | PSW_Z | PSW_V | PSW_C);
        data_t data  = 0;
        psw |= PSW_Z;
        return { psw, data };
    }

    static alu1_s opCOM(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = (~s.dst) & 0xFFFF;

        NZ_as_u16(psw, data);
        psw |= PSW_C;
        return { psw, data };
    }

    static alu1_s opCOMB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = (~s.dst) & 0xFF;

        NZ_as_u08(psw, data);
        psw |= PSW_C;
        return { psw, data };
    }

    static alu1_s opINC(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);

        data_t data  = (s.dst + 1) & 0xFFFF;

        NZ_as_u16(psw, data);
        if (data == 0x8000) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opINCB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);

        data_t data  = (s.dst + 1) & 0xFF;

        NZ_as_u08(psw, data);
        if (data == 0x80) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opDEC(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);

        data_t data  = (s.dst - 1) & 0xFFFF;

        NZ_as_u16(psw, data);
        if (data == 0x7FFF) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opDECB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);

        data_t data  = (s.dst - 1) & 0xFF;

        NZ_as_u08(psw, data);
        if (data == 0x7F) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opNEG(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = (0u - s.dst) & 0xFFFF;

        NZ_as_u16(psw, data);
        if (data == 0x8000) psw |= PSW_V;
        if (data != 0x0000) psw |= PSW_C;
        return { psw, data };
    }

    static alu1_s opNEGB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = (0u - s.dst) & 0xFF;

        NZ_as_u08(psw, data);
        if (data == 0x80) psw |= PSW_V;
        if (data != 0x00) psw |= PSW_C;
        return { psw, data };
    }

    static alu1_s opADC(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        unsigned int CF = (s.psw & PSW_C) != 0;
        data_t data  = (s.dst + CF) & 0xFFFF;

        NZ_as_u16(psw, data);
        if ((data == 0x8000) && CF) psw |= PSW_V;
        if ((data == 0x0000) && CF) psw |= PSW_C;
        return { psw, data };
    }

    static alu1_s opADCB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        unsigned int CF = (s.psw & PSW_C) != 0;
        data_t data  = (s.dst + CF) & 0xFF;

        NZ_as_u08(psw, data);
        if ((data == 0x80) && CF) psw |= PSW_V;
        if ((data == 0x00) && CF) psw |= PSW_C;
        return { psw, data };
    }

    static alu1_s opSBC(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        unsigned int CF = (s.psw & PSW_C) != 0;
        data_t data  = (s.dst - CF) & 0xFFFF;

        NZ_as_u16(psw, data);
        if ((data == 0x7FFF) && CF) psw |= PSW_V;
        if ((data == 0xFFFF) && CF) psw |= PSW_C;
        return { psw, data };
    }

    static alu1_s opSBCB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        unsigned int CF = (s.psw & PSW_C) != 0;
        data_t data  = (s.dst - CF) & 0xFF;

        NZ_as_u08(psw, data);
        if ((data == 0x7F) && CF) psw |= PSW_V;
        if ((data == 0xFF) && CF) psw |= PSW_C;
        return { psw, data };
    }

    static alu1_s opTST(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = s.dst & 0xFFFF;

        NZ_as_u16(psw, data);
        return { psw, data };
    }

    static alu1_s opTSTB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);
        data_t data  = s.dst & 0xFF;

        NZ_as_u08(psw, data);
        return { psw, data };
    }

    static alu1_s opROR(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        unsigned int CF = (s.psw & PSW_C) != 0;
        data_t data  = (s.dst >> 1) | (CF << 15);

        NZ_as_u16(psw, data);
        if (s.dst & 0x0001) psw |= PSW_C;
        if (((psw & PSW_N) != 0) != ((psw & PSW_C) != 0)) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opRORB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        unsigned int CF = (s.psw & PSW_C) != 0;
        data_t data  = (s.dst >> 1) | (CF << 7);

        NZ_as_u08(psw, data);
        if (s.dst & 0x01) psw |= PSW_C;
        if (((psw & PSW_N) != 0) != ((psw & PSW_C) != 0)) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opROL(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        unsigned int CF = (s.psw & PSW_C) != 0;
        data_t data  = ((s.dst << 1) | CF) & 0xFFFF;

        NZ_as_u16(psw, data);
        if (s.dst & 0x8000) psw |= PSW_C;
        if (((psw & PSW_N) != 0) != ((psw & PSW_C) != 0)) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opROLB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        unsigned int CF = (s.psw & PSW_C) != 0;
        data_t data  = ((s.dst << 1) | CF) & 0xFF;

        NZ_as_u08(psw, data);
        if (s.dst & 0x80) psw |= PSW_C;
        if (((psw & PSW_N) != 0) != ((psw & PSW_C) != 0)) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opASR(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = (s.dst >> 1) | (s.dst & 0x8000);

        NZ_as_u16(psw, data);
        if (s.dst & 0x0001) psw |= PSW_C;
        if (((psw & PSW_N) != 0) != ((psw & PSW_C) != 0)) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opASRB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = (s.dst >> 1) | (s.dst & 0x80);

        NZ_as_u08(psw, data);
        if (s.dst & 0x01) psw |= PSW_C;
        if (((psw & PSW_N) != 0) != ((psw & PSW_C) != 0)) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opASL(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = (s.dst << 1) & 0xFFFF;

        NZ_as_u16(psw, data);
        if (s.dst & 0x8000) psw |= PSW_C;
        if (((psw & PSW_N) != 0) != ((psw & PSW_C) != 0)) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opASLB(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);

        data_t data  = (s.dst << 1) & 0xFF;

        NZ_as_u08(psw, data);
        if (s.dst & 0x80) psw |= PSW_C;
        if (((psw & PSW_N) != 0) != ((psw & PSW_C) != 0)) psw |= PSW_V;
        return { psw, data };
    }

    static alu1_s opSXT(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_Z | PSW_V);

        unsigned int NF = (s.psw & PSW_N) != 0;
        data_t data  = (-NF) & 0xFFFF;

        if (!NF) psw |= PSW_Z;
        return { psw, data };
    }

    static alu1_s opMTPS(const alu1_s s)
    {
        psw_t psw = s.psw & PSW_T;
        psw |= (s.dst & 0xFF) & ~(PSW_T);
        return { psw, s.dst };
    }

    static alu1_s opMFPS(const alu1_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);

        data_t data = ((s.psw & 0xFF) ^ 0x80) - 0x80;

        NZ_as_u08(psw, data);
        return { psw, data };
    }

    struct alu2_s {
        psw_t   psw;
        data_t  src;    // NOTE: combined { Rn|1, Rn } for EIS
        data_t  dst;
    };
    using alu2_fn = alu1_s (*)(const alu2_s);

    static alu1_s uopAND(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);
        data_t data  = (s.dst & s.src) & 0xFFFF;

        NZ_as_u16(psw, data);
        return { psw, data };
    }

    static alu1_s uopANDB(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);
        data_t data  = (s.dst & s.src) & 0xFF;

        NZ_as_u08(psw, data);
        return { psw, data };
    }

    static alu1_s uopOR(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);
        data_t data  = (s.dst | s.src) & 0xFFFF;

        NZ_as_u16(psw, data);
        return { psw, data };
    }

    static alu1_s uopORB(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);
        data_t data  = (s.dst | s.src) & 0xFF;

        NZ_as_u08(psw, data);
        return { psw, data };
    }

    static alu1_s uopADD(const alu2_s s)
    {
        // TODO: add is sub(-a, b) + CF inversion
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);
        data_t data  = (s.dst + s.src) & 0xFFFF;

        NZ_as_u16(psw, data);
        if ((~(s.src ^ s.dst) & (data  ^ s.dst)) & 0x8000) psw |= PSW_V;
        if (((s.src & s.dst) | ((s.src ^ s.dst) & ~data)) & 0x8000) psw |= PSW_C;
        return { psw, data };
    }

//    static alu1_s uopADDB(const alu2_s s)
//    {
//        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);
//        data_t data  = (s.dst + s.src) & 0xFF;
//
//        NZ_as_u08(psw, data);
//        if ((~(s.src ^ s.dst) & (s.src ^ data)) & 0x80) psw |= PSW_V;
//        if (((s.src & s.dst) | ((s.src ^ s.dst) & ~data)) & 0x80) psw |= PSW_C;
//        return { psw, data };
//    }

    static alu1_s uopSUB(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);
        data_t data  = (s.dst - s.src) & 0xFFFF;

        NZ_as_u16(psw, data);
        if (((s.src ^  s.dst) & ~(data  ^ s.src)) & 0x8000) psw |= PSW_V;
        if (((s.src & ~s.dst) | (~(s.src ^ s.dst) & data)) & 0x8000) psw |= PSW_C;
        return { psw, data };
    }

    static alu1_s uopSUBB(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V | PSW_C);
        data_t data  = (s.dst - s.src) & 0xFF;

        NZ_as_u08(psw, data);
        if (((s.src ^  s.dst) & ~(data  ^ s.src)) & 0x80) psw |= PSW_V;
        if (((s.src & ~s.dst) | (~(s.src ^ s.dst) & data)) & 0x80) psw |= PSW_C;
        return { psw, data };
    }


    static alu1_s opMOV(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);
        data_t data  = s.src & 0xFFFF;

        NZ_as_u16(psw, data);
        return { psw, data };
    }

    static alu1_s opMOVB(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);
        data_t data  = ((s.src & 0xFF) ^ 0x80) - 0x80;

        NZ_as_u08(psw, data);
        return { psw, data };
    }

    static alu1_s opCMP(const alu2_s s)
    {
        return uopSUB({s.psw, s.dst, s.src}); // NOTE: reversed order
    }

    static alu1_s opCMPB(const alu2_s s)
    {
        return uopSUBB({s.psw, s.dst, s.src});// NOTE: reversed order
    }

    static alu1_s opBIT(const alu2_s s)
    {
        return uopAND(s);
    }

    static alu1_s opBITB(const alu2_s s)
    {
        return uopANDB(s);
    }

    static alu1_s opBIC(const alu2_s s)
    {
        return uopAND({s.psw, ~s.src, s.dst}); // NOTE: not-and
    }

    static alu1_s opBICB(const alu2_s s)
    {
        return uopANDB({s.psw, ~s.src, s.dst}); // NOTE: not-and
    }

    static alu1_s opBIS(const alu2_s s)
    {
        return uopOR(s);
    }

    static alu1_s opBISB(const alu2_s s)
    {
        return uopORB(s);
    }

    static alu1_s opADD(const alu2_s s)
    {
        return uopADD(s);
    }

    static alu1_s opSUB(const alu2_s s)
    {
        return uopSUB(s);
    }

    static alu1_s opXOR(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_V);
        data_t data  = (s.dst ^ s.src) & 0xFFFF;

        NZ_as_u16(psw, data);
        return { psw, data };
    }

    // EIS
    static alu1_s opASHC(const alu2_s s)
    {
        psw_t  psw = s.psw & ~(PSW_N | PSW_Z | PSW_V | PSW_C);
        int src    = ((s.src & 0xFFFFFFFF) ^ 0x80000000) - 0x80000000;
        int shamt  = ((s.dst & 0x3F) ^ 0x20) - 0x20;
        do {
            if (shamt == 0)
                break;

            if (shamt > 0) {
                int so = src >> (31 - shamt);
                src <<= (+shamt);

                if (so & 0x00000002u)
                    psw |= PSW_C;

                if ((so != 0) && (so != -1))
                    psw |= PSW_V;
            }
            else {
                src >>= (-shamt - 1);
                if (src & 0x00000001u)
                    psw |= PSW_C;
                src >>= 1;
            }
        } while (false);

        data_t data = static_cast<data_t>(src);

        // NOTE: flags by whole product
        if (src  < 0)  psw |= PSW_N;
        if (src == 0)  psw |= PSW_Z;
        return { psw, data };
    }

    static alu1_s opASH(const alu2_s s)
    {
        psw_t  psw = s.psw & ~(PSW_N | PSW_Z | PSW_V | PSW_C);
        int    src = ((s.src & 0xFFFF) ^ 0x8000) - 0x8000;

        int shamt = ((s.dst & 0x3F) ^ 0x20) - 0x20;
        do {
            if (shamt == 0)
                break;

            if (shamt > 0) {

                int so = (src << 16) >> (31 - shamt);
                src <<= (+shamt);

                if (so & 0x00000002u)
                    psw |= PSW_C;

                if ((so != 0) && (so != -1))
                    psw |= PSW_V;

            }
            else {
                src >>= (-shamt - 1);
                if (src & 0x0001)
                    psw |= PSW_C;
                src >>= 1;
            }

        } while (false);

        data_t data = src & 0xFFFF;

        NZ_as_u16(psw, data);
        return { psw, data };
    }

    static alu1_s opMUL(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_N | PSW_Z | PSW_V | PSW_C);

        int md = ((s.src & 0xFFFF) ^ 0x8000) - 0x8000; // multipilcand
        int mr = ((s.dst & 0xFFFF) ^ 0x8000) - 0x8000; // multiplier
        int pd = md * mr; // product

        // NOTE: flags by whole product
        if (pd  < 0)  psw |= PSW_N;
        if (pd == 0)  psw |= PSW_Z;
        if ( (pd > 32767) || (pd < -32768) )
            psw |= PSW_C;

        data_t data = static_cast<data_t>(pd);
        return { psw, data };
    }

    static alu1_s opDIV(const alu2_s s)
    {
        psw_t  psw   = s.psw & ~(PSW_N | PSW_Z | PSW_V | PSW_C);
        data_t data  = s.src;

        // NOTE: VM2 don't modify target if there was overflow
        do {
            int den = ((s.dst & 0xFFFF) ^ 0x8000) - 0x8000;
            if (den == 0) {
                psw |= PSW_V | PSW_C;
                break; // jump to retpoint
            }

            if ( (s.src == 0x80000000) && (den == -1) ) {
                psw |= PSW_V;
                break; // jump to retpoint

            }
            // C++20 and onward guarantees two's complement arithmetics
            int num = ((s.src & 0xFFFFFFFF) ^ 0x80000000) - 0x80000000;

            int quo = num / den;
            int rem = num % den;

            if ( (quo > 32767) || (quo < -32768) ) {
                psw |= PSW_V;
                break; // jump to retpoint
            }

            unsigned int rb = quo & 0xFFFFu;
            unsigned int rp = rem & 0xFFFFu;

            data = (rb << 16) | rp;

            NZ_as_u16(psw, rb);

        } while (false);

//retpoint:
        return { psw, data };
    }
};

}

class CProcessor
{
public:  // Constructor / initialization
    CProcessor(LPCTSTR name);
    /// \brief Link the processor and memory controller
    void        AttachMemoryController(CMemoryController* ctl) { m_pMemoryController = ctl; }
    void        SetHALTPin(bool value) { m_haltpin = value; }
    void        SetDCLOPin(bool value);
    void        SetACLOPin(bool value);
    void        MemoryError();
    /// \brief Get the processor name, assigned in the constructor
    LPCTSTR     GetName() const { return m_name; }

public:
    static void Init();  ///< Initialize static tables
    static void Done();  ///< Release memory used for static tables
    unsigned long long m_totalticks = 0;
protected:  // Statics
    typedef void ( CProcessor::*ExecuteMethodRef )();
    static ExecuteMethodRef* m_pExecuteMethodMap;


protected:  // Processor state
    TCHAR       m_name[5];          ///< Processor name (DO NOT use it inside the processor code!!!)

    //int16_t     m_internalTick;     ///< How many ticks waiting to the end of current instruction
    const xpu_instimes *m_timing;
    instime_counter_t   ic_;
    void waitstates_set(const instime_t instime) { ic_.set(instime); }
    void waitstates_add(const instime_t instime) { ic_.add(instime); }

    uint16_t    m_psw;              ///< Processor Status Word (PSW)
    uint16_t    m_R[8];             ///< Registers (R0..R5, R6=SP, R7=PC)
    uint16_t    m_savepc;           ///< CPC register
    uint16_t    m_savepsw;          ///< CPSW register
    bool        m_okStopped;        ///< "Processor stopped" flag
    bool        m_stepmode;         ///< Read true if it's step mode
    bool        m_buserror;         ///< Read true if occured bus error for implementing double bus error if needed
    bool        m_haltpin;          ///< HALT pin
    bool        m_DCLOpin;          ///< DCLO pin
    bool        m_ACLOpin;          ///< ACLO pin
    bool        m_waitmode;         ///< WAIT

protected:  // Current instruction processing
    uint16_t    m_instruction;      ///< Curent instruction
    instime_t   fetch_delta_;
    uint16_t    m_instructionpc;    ///< Address of the current instruction
    uint8_t     m_regsrc;           ///< Source register number
    uint8_t     m_methsrc;          ///< Source address mode
    uint16_t    m_addrsrc;          ///< Source address
    uint8_t     m_regdest;          ///< Destination register number
    uint8_t     m_methdest;         ///< Destination address mode
    uint16_t    m_addrdest;         ///< Destination address
protected:  // Interrupt processing
    bool        m_STRTrq;           ///< Start interrupt pending
    bool        m_RPLYrq;           ///< Hangup interrupt pending
    bool        m_ILLGrq;           ///< Illegal instruction interrupt pending
    bool        m_RSVDrq;           ///< Reserved instruction interrupt pending
    bool        m_TBITrq;           ///< T-bit interrupt pending
    bool        m_ACLOrq;           ///< Power down interrupt pending
    bool        m_HALTrq;           ///< HALT command or HALT signal
    bool        m_EVNTrq;           ///< Timer event interrupt pending
    bool        m_FIS_rq;           ///< FIS command interrupt pending
    bool        m_BPT_rq;           ///< BPT command interrupt pending
    bool        m_IOT_rq;           ///< IOT command interrupt pending
    bool        m_EMT_rq;           ///< EMT command interrupt pending
    bool        m_TRAPrq;           ///< TRAP command interrupt pending

    uint16_t    m_virq[16];         ///< VIRQ vector
    uint16_t    m_virq_p[16];       ///< Postponed VIRQ

    bool        m_ACLOreset;        ///< Power fail interrupt request reset
    bool        m_EVNTreset;        ///< EVNT interrupt request reset;
    uint8_t     m_VIRQreset;        ///< VIRQ request reset for given device
protected:
    CMemoryController* m_pMemoryController;
    bool m_okTrace;                 ///< Trace mode on/off

public:
    CMemoryController* GetMemoryController() { return m_pMemoryController; }

public:  // Register control
    uint16_t    GetPSW() const { return m_psw; }  ///< Get the processor status word register value
    uint16_t    GetCPSW() const { return m_savepsw; }
    uint8_t     GetLPSW() const { return (uint8_t)(m_psw & 0xff); }  ///< Get PSW lower byte
    void        SetPSW(uint16_t word);  ///< Set the processor status word register value
    void        SetCPSW(uint16_t word) { m_savepsw = word; }
    void        SetLPSW(uint8_t byte);
    uint16_t    GetReg(int regno) const { return m_R[regno]; }  ///< Get register value, regno=0..7
    void        SetReg(int regno, uint16_t word);  ///< Set register value
    uint8_t     GetLReg(int regno) const { return (uint8_t)(m_R[regno] & 0xff); }
    void        SetLReg(int regno, uint8_t byte);
    uint16_t    GetSP() const { return m_R[6]; }
    void        SetSP(uint16_t word) { m_R[6] = word; }
    uint16_t    GetPC() const { return m_R[7]; }
    uint16_t    GetCPC() const { return m_savepc; }
    void        SetPC(uint16_t word);
    void        SetCPC(uint16_t word) { m_savepc = word; }

public:  // PSW bits control
    void        SetC(bool bFlag);
    uint16_t    GetC() const { return (m_psw & PSW_C) != 0; }
    void        SetV(bool bFlag);
    uint16_t    GetV() const { return (m_psw & PSW_V) != 0; }
    void        SetN(bool bFlag);
    uint16_t    GetN() const { return (m_psw & PSW_N) != 0; }
    void        SetZ(bool bFlag);
    uint16_t    GetZ() const { return (m_psw & PSW_Z) != 0; }
    void        SetHALT(bool bFlag);
    uint16_t    GetHALT() const { return (m_psw & PSW_HALT) != 0; }

public:  // Processor state
    /// \brief "Processor stopped" flag
    bool        IsStopped() const { return m_okStopped; }
    /// \brief HALT flag (true - HALT mode, false - USER mode)
    bool        IsHaltMode() const { return ((m_psw & 0400) != 0); }
public:  // Processor control
    void        TickEVNT();  ///< EVNT signal
    /// \brief External interrupt via VIRQ signal
    void        InterruptVIRQ(int que, uint16_t interrupt);
    uint16_t    GetVIRQ(int que) { return m_virq[que]; }
    /// \brief Execute one processor tick
    void        Execute();
    /// \brief Process pending interrupt requests
    bool        InterruptProcessing();
    /// \brief Execute next command and process interrupts
    void        CommandExecution();
//    int         GetInternalTick() const { return m_internalTick; }
    void        ClearInternalTick() { waitstates_set(0); }
    void        SetTrace(bool okTrace) { m_okTrace = okTrace; }  ///< Set trace mode on/off

public:  // Saving/loading emulator status (pImage addresses up to 32 bytes)
    void        SaveToImage(uint8_t* pImage) const;
    void        LoadFromImage(const uint8_t* pImage);

protected:  // Implementation
    void        FetchInstruction();      ///< Read next instruction
    void        TranslateInstruction();  ///< Execute the instruction
protected:  // Implementation - memory access
    /// \brief Read word from the bus for execution
    uint16_t    GetWordExec(uint16_t address) { return m_pMemoryController->GetWord(address, IsHaltMode(), true); }
    /// \brief Read word from the bus
    uint16_t    GetWord(uint16_t address) { return m_pMemoryController->GetWord(address, IsHaltMode(), false); }
    void        SetWord(uint16_t address, uint16_t word) { m_pMemoryController->SetWord(address, IsHaltMode(), word); }
    uint8_t     GetByte(uint16_t address) { return m_pMemoryController->GetByte(address, IsHaltMode()); }
    void        SetByte(uint16_t address, uint8_t byte) { m_pMemoryController->SetByte(address, IsHaltMode(), byte); }

#if BUS_USE_NEW_IO
    using       rsp_s = CMemoryController::rsp_s;
    using       rmw_e = CMemoryController::rmw_e;

    unsigned int bus_read(unsigned int a16) {
        bool sel = IsHaltMode();
        auto rsp = m_pMemoryController->read_word(a16, sel);
        if (rsp.is_noreply())
            m_RPLYrq = true;

        return rsp.data();
    }

    void bus_write(unsigned int a16, unsigned int d16, bool byte = false) {
        bool sel = IsHaltMode();
        auto rsp = m_pMemoryController->write_word(a16, sel, d16, byte);
        if (rsp.is_noreply())
            m_RPLYrq = true;
    }

    rsp_s bus_read_raw(unsigned int a16, rmw_e t = rmw_e::single) {
        bool sel = IsHaltMode();
        auto rsp = m_pMemoryController->read_word(a16, sel, t);
        return rsp;
    }

    rsp_s bus_write_raw(unsigned int a16, unsigned int d16, bool byte = false, rmw_e t = rmw_e::single) {
        bool sel = IsHaltMode();
        auto rsp = m_pMemoryController->write_word(a16, sel, d16, byte, t);
        return rsp;
    }

#endif

protected:  // PSW bits calculations
    bool static CheckForNegative(uint8_t byte) { return (byte & 0200) != 0; }
    bool static CheckForNegative(uint16_t word) { return (word & 0100000) != 0; }
    bool static CheckForZero(uint8_t byte) { return byte == 0; }
    bool static CheckForZero(uint16_t word) { return word == 0; }
    bool static CheckAddForOverflow(uint8_t a, uint8_t b);
    bool static CheckAddForOverflow(uint16_t a, uint16_t b);
    bool static CheckSubForOverflow(uint8_t a, uint8_t b);
    bool static CheckSubForOverflow(uint16_t a, uint16_t b);
    bool static CheckAddForCarry(uint8_t a, uint8_t b);
    bool static CheckAddForCarry(uint16_t a, uint16_t b);
    bool static CheckSubForCarry(uint8_t a, uint8_t b);
    bool static CheckSubForCarry(uint16_t a, uint16_t b);

protected:  // Implementation - instruction execution
    // No fields
    uint16_t    GetWordAddr (uint8_t meth, uint8_t reg);
    uint16_t    GetByteAddr (uint8_t meth, uint8_t reg);

    void        ExecuteUNKNOWN ();  ///< There is no such instruction -- just call TRAP 10
    void        ExecuteHALT ();
    void        ExecuteWAIT ();
    void        ExecuteRCPC();
    void        ExecuteRCPS ();
    void        ExecuteWCPC();
    void        ExecuteWCPS();
    void        ExecuteMFUS ();
    void        ExecuteMTUS ();
    void        ExecuteRTI ();
    void        ExecuteBPT ();
    void        ExecuteIOT ();
    void        ExecuteRESET ();
    void        ExecuteSTEP();
    void        ExecuteRSEL ();
    void        Execute000030 ();
    void        ExecuteFIS ();
    void        ExecuteRUN();
    void        ExecuteRTT ();
    void        ExecuteCCC ();
    void        ExecuteSCC ();

    // One fiels
    void        ExecuteRTS ();

    // Two fields
    void        ExecuteJMP ();
#if PROCESSOR_USE_NEW_ALU == 0
    void        ExecuteSWAB ();
    void        ExecuteCLR ();
    void        ExecuteCLRB ();
    void        ExecuteCOM ();
    void        ExecuteCOMB ();
    void        ExecuteINC ();
    void        ExecuteINCB ();
    void        ExecuteDEC ();
    void        ExecuteDECB ();
    void        ExecuteNEG ();
    void        ExecuteNEGB ();
    void        ExecuteADC ();
    void        ExecuteADCB ();
    void        ExecuteSBC ();
    void        ExecuteSBCB ();
    void        ExecuteTST ();
    void        ExecuteTSTB ();
    void        ExecuteROR ();
    void        ExecuteRORB ();
    void        ExecuteROL ();
    void        ExecuteROLB ();
    void        ExecuteASR ();
    void        ExecuteASRB ();
    void        ExecuteASL ();
    void        ExecuteASLB ();
#endif
    void        ExecuteMARK ();

#if PROCESSOR_USE_NEW_ALU == 0
    void        ExecuteSXT ();
#endif
    void        ExecuteMTPS ();
    void        ExecuteMFPS ();

#if PROCESSOR_USE_NEW_ALU == 0
    // Branchs & interrupts
    void        ExecuteBR ();
    void        ExecuteBNE ();
    void        ExecuteBEQ ();
    void        ExecuteBGE ();
    void        ExecuteBLT ();
    void        ExecuteBGT ();
    void        ExecuteBLE ();
    void        ExecuteBPL ();
    void        ExecuteBMI ();
    void        ExecuteBHI ();
    void        ExecuteBLOS ();
    void        ExecuteBVC ();
    void        ExecuteBVS ();
    void        ExecuteBHIS ();
    void        ExecuteBLO ();
#endif
/*
    bool condBR   () const { return  true;   };
    bool condBNE  () const { return !GetZ(); };
    bool condBEQ  () const { return  GetZ(); };
    bool condBPL  () const { return !GetN(); };
    bool condBMI  () const { return  GetN(); };
    bool condBVC  () const { return !GetV(); };
    bool condBVS  () const { return  GetV(); };
    bool condBHIS () const { return !GetC(); }; // BCC
    bool condBLO  () const { return  GetC(); }; // BCS
    bool condBGE  () const { return !condBLT();        };
    bool condBLT  () const { return  GetN() != GetV(); };
    bool condBGT  () const { return !condBLE(); };
    bool condBLE  () const { return  GetZ() || condBLT(); };
    bool condBHI  () const { return !condBLOS(); };
    bool condBLOS () const { return  GetC() || GetZ(); };
*/

public:
    struct ea_s {
        unsigned int ea;

        constexpr ea_s() : ea{0x80000000} {};

        struct reg_index {
            unsigned int ri;
        };

        struct mem_addr {
            unsigned int ma;
        };

        ea_s(const reg_index r)
            : ea { ~(r.ri & 0xF) }
        {
        }

        ea_s(const mem_addr m)
            : ea { (m.ma & 0xFFFF) }
        {
        }

        constexpr bool is_reg() const {
            return (ea & 0x80000000);
        }

        constexpr bool is_mem() const {
            return !is_reg();
        }

        constexpr unsigned int addr() const {
            return ea;
        }

        constexpr unsigned int reg() const {
            return ~ea;
        }
    };

    struct op_arg_s {
        int ea;
        unsigned int u16;
    };

    struct estate_s {
        op_arg_s        src;
        op_arg_s        dst;
        unsigned int    alu_src;
        unsigned int    alu_dst;
    };

    struct x_op_arg_s {
        ea_s            ea;
        unsigned int    u16;
        unsigned int    alu_u16;
    };

    struct x_estate_s {
        x_op_arg_s      src;
        x_op_arg_s      dst;

        instime_t       delta;

        unsigned int    psw;
        unsigned int    res;
    };

    template<bool byte>
    ea_s op_calculate_ea(x_estate_s& estate, unsigned int m77);

    template<bool byte, CProcessor::rmw_e t>
    void op_exec_fetch_field(x_estate_s& estate, x_op_arg_s& field);

    template<unsigned int flags>
    void op_exec_fetch_src(x_estate_s& estate);

    template<unsigned int flags>
    void op_exec_fetch_dst(x_estate_s& estate);

    template<unsigned int flags>
    void op_exec_prepare(x_estate_s& estate);

    template<unsigned int flags>
    void op_exec_finalize(x_estate_s& estate);


//    template<unsigned int flags>
//    void op_exec_read(estate_s& args);

//    template<unsigned int flags>
//    void op_exec_writeback(const op_arg_s& arg, unsigned int res);

    template<vm2::alu::cond_fn fn>
    void op_branch();

    template<vm2::alu::alu1_fn fn, unsigned int flags>
    void op_alu1();

//    template<vm2::alu::alu1_fn fn, unsigned int flags>
//    void op_alu15();

    template<vm2::alu::alu2_fn fn, unsigned int flags>
    void op_alu2();

protected:
    void        ExecuteEMT ();
    void        ExecuteTRAP ();

    // Three fields
    void        ExecuteJSR ();
#if PROCESSOR_USE_NEW_ALU == 0
    void        ExecuteXOR ();
#endif
    void        ExecuteSOB ();
    void        ExecuteMUL ();
    void        ExecuteDIV ();
    void        ExecuteASH ();
    void        ExecuteASHC ();

#if PROCESSOR_USE_NEW_ALU == 0
    // Four fields
    void        ExecuteMOV ();
    void        ExecuteMOVB ();
    void        ExecuteCMP ();
    void        ExecuteCMPB ();
    void        ExecuteBIT ();
    void        ExecuteBITB ();
    void        ExecuteBIC ();
    void        ExecuteBICB ();
    void        ExecuteBIS ();
    void        ExecuteBISB ();

    void        ExecuteADD ();
    void        ExecuteSUB ();
#endif
};

inline void CProcessor::SetPSW(uint16_t word)
{
    m_psw = word & 0777;
    if ((m_psw & 0600) != 0600) m_savepsw = m_psw;
}
inline void CProcessor::SetLPSW(uint8_t byte)
{
    m_psw = (m_psw & 0xFF00) | (uint16_t)byte;
    if ((m_psw & 0600) != 0600) m_savepsw = m_psw;
}
inline void CProcessor::SetReg(int regno, uint16_t word)
{
    m_R[regno] = word;
    if ((regno == 7) && ((m_psw & 0600) != 0600)) m_savepc = word;
}
inline void CProcessor::SetLReg(int regno, uint8_t byte)
{
    m_R[regno] = (m_R[regno] & 0xFF00) | (uint16_t)byte;
    if ((regno == 7) && ((m_psw & 0600) != 0600)) m_savepc = m_R[7];
}
inline void CProcessor::SetPC(uint16_t word)
{
    m_R[7] = word;
    if ((m_psw & 0600) != 0600) m_savepc = word;
}

// PSW bits control - implementation
inline void CProcessor::SetC (bool bFlag)
{
    if (bFlag) m_psw |= PSW_C; else m_psw &= ~PSW_C;
    if ((m_psw & 0600) != 0600) m_savepsw = m_psw;
}
inline void CProcessor::SetV (bool bFlag)
{
    if (bFlag) m_psw |= PSW_V; else m_psw &= ~PSW_V;
    if ((m_psw & 0600) != 0600) m_savepsw = m_psw;
}
inline void CProcessor::SetN (bool bFlag)
{
    if (bFlag) m_psw |= PSW_N; else m_psw &= ~PSW_N;
    if ((m_psw & 0600) != 0600) m_savepsw = m_psw;
}
inline void CProcessor::SetZ (bool bFlag)
{
    if (bFlag) m_psw |= PSW_Z; else m_psw &= ~PSW_Z;
    if ((m_psw & 0600) != 0600) m_savepsw = m_psw;
}

inline void CProcessor::SetHALT (bool bFlag)
{
    if (bFlag) m_psw |= PSW_HALT; else m_psw &= ~PSW_HALT;
}

inline void CProcessor::InterruptVIRQ(int que, uint16_t interrupt)
{
    if (m_okStopped) return;  // Processor is stopped - nothing to do
    m_virq[que] = interrupt;
}

// PSW bits calculations - implementation
inline bool CProcessor::CheckAddForOverflow (uint8_t a, uint8_t b)
{
#if defined(_M_IX86) && defined(_MSC_VER) && !defined(_MANAGED)
    bool bOverflow = false;
    _asm
    {
        pushf
        push cx
        mov cl, byte ptr [a]
        add cl, byte ptr [b]
        jno end
        mov dword ptr [bOverflow], 1
        end:
        pop cx
        popf
    }
    return bOverflow;
#else
    //uint16_t sum = a < 0200 ? (uint16_t)a + (uint16_t)b + 0200 : (uint16_t)a + (uint16_t)b - 0200;
    //return HIBYTE (sum) != 0;
    uint8_t sum = a + b;
    return ((~a ^ b) & (a ^ sum)) & 0200;
#endif
}
inline bool CProcessor::CheckAddForOverflow (uint16_t a, uint16_t b)
{
#if defined(_M_IX86) && defined(_MSC_VER) && !defined(_MANAGED)
    bool bOverflow = false;
    _asm
    {
        pushf
        push cx
        mov cx, word ptr [a]
        add cx, word ptr [b]
        jno end
        mov dword ptr [bOverflow], 1
        end:
        pop cx
        popf
    }
    return bOverflow;
#else
    //uint32_t sum =  a < 0100000 ? (uint32_t)a + (uint32_t)b + 0100000 : (uint32_t)a + (uint32_t)b - 0100000;
    //return HIWORD (sum) != 0;
    uint16_t sum = a + b;
    return ((~a ^ b) & (a ^ sum)) & 0100000;
#endif
}

inline bool CProcessor::CheckSubForOverflow (uint8_t a, uint8_t b)
{
#if defined(_M_IX86) && defined(_MSC_VER) && !defined(_MANAGED)
    bool bOverflow = false;
    _asm
    {
        pushf
        push cx
        mov cl, byte ptr [a]
        sub cl, byte ptr [b]
        jno end
        mov dword ptr [bOverflow], 1
        end:
        pop cx
        popf
    }
    return bOverflow;
#else
    //uint16_t sum = a < 0200 ? (uint16_t)a - (uint16_t)b + 0200 : (uint16_t)a - (uint16_t)b - 0200;
    //return HIBYTE (sum) != 0;
    uint8_t sum = a - b;
    return ((a ^ b) & (~b ^ sum)) & 0200;
#endif
}
inline bool CProcessor::CheckSubForOverflow (uint16_t a, uint16_t b)
{
#if defined(_M_IX86) && defined(_MSC_VER) && !defined(_MANAGED)
    bool bOverflow = false;
    _asm
    {
        pushf
        push cx
        mov cx, word ptr [a]
        sub cx, word ptr [b]
        jno end
        mov dword ptr [bOverflow], 1
        end:
        pop cx
        popf
    }
    return bOverflow;
#else
    //uint32_t sum =  a < 0100000 ? (uint32_t)a - (uint32_t)b + 0100000 : (uint32_t)a - (uint32_t)b - 0100000;
    //return HIWORD (sum) != 0;
    uint16_t sum = a - b;
    return ((a ^ b) & (~b ^ sum)) & 0100000;
#endif
}
inline bool CProcessor::CheckAddForCarry (uint8_t a, uint8_t b)
{
    uint16_t sum = (uint16_t)a + (uint16_t)b;
    return (uint8_t)((sum >> 8) & 0xff) != 0;
}
inline bool CProcessor::CheckAddForCarry (uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a + (uint32_t)b;
    return (uint16_t)((sum >> 16) & 0xffff) != 0;
}
inline bool CProcessor::CheckSubForCarry (uint8_t a, uint8_t b)
{
    uint16_t sum = (uint16_t)a - (uint16_t)b;
    return (uint8_t)((sum >> 8) & 0xff) != 0;
}
inline bool CProcessor::CheckSubForCarry (uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a - (uint32_t)b;
    return (uint16_t)((sum >> 16) & 0xffff) != 0;
}


//////////////////////////////////////////////////////////////////////
