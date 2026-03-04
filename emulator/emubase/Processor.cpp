/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

/// \file Processor.cpp  KM1801VM2 processor class implementation

#include "stdafx.h"
#include "Processor.h"
#include "Emubase.h"

// Timings ///////////////////////////////////////////////////////////
#include "kvant_031_instimes.hpp"


//////////////////////////////////////////////////////////////////////

#define P_EASRC     (0x00000001u)  // calculate EA of src
#define P_RDSRC     (0x00000002u)  // fetch [EA(src)] (NOTE: read srcR only if used without EASRC)
#define P_EADST     (0x00000004u)  // calculate EA of dst
#define P_RDDST     (0x00000008u)  // fetch [EA(dst)] (NOTE: read dstR only if used without EADST)
#define P_WRDST     (0x00000010u)  // writeback [EA(dst)] (NOTE: write dstR only if used without EADST)
#define P_BYTE      (0x00000020u)  // BYTE operation
#define P_IGBDR     (0x00000040u)  // ignore BYTE (do word) on writeback if destination is register (MOVB, MFPS)
#define P_NOPSW     (0x00000080u)  // don't update PSW

#define P_RDSRCPR   (0x00000100u)  // read source register's pair (EIS only)
#define P_AFINV     (0x00000200u)  // arguments fetch inverse order (dst first, next src)
//#define P_WRSRC     (0x00000000u)  // write source register (EIS only)
//#define P_WRSRCPR   (0x00000000u)  // write source register's pair (EIS only)

#define P_REASRC  (P_EASRC | P_RDSRC)
#define P_READST  (P_EADST | P_RDDST)
#define P_REAbDST (P_EADST | P_RDDST | P_BYTE)
#define P_WRIDST  (P_EADST | P_WRDST)
#define P_RMWDST  (P_EADST | P_RDDST | P_WRDST)
#define P_RMWbDST (P_EADST | P_RDDST | P_WRDST | P_BYTE)

CProcessor::ExecuteMethodRef* CProcessor::m_pExecuteMethodMap = nullptr;

#define RegisterMethodRef(/*uint16_t*/ opstart, /*uint16_t*/ opend, /*CProcessor::ExecuteMethodRef*/ methodref) \
    { \
        for (uint32_t opcode = (opstart); opcode <= (opend); opcode++) \
            m_pExecuteMethodMap[opcode] = (methodref); \
    }

#define OLD_ALU 0

void CProcessor::Init()
{
    ASSERT(m_pExecuteMethodMap == nullptr);
    m_pExecuteMethodMap = static_cast<CProcessor::ExecuteMethodRef*>(::calloc(65536, sizeof(CProcessor::ExecuteMethodRef)));

    // Сначала заполняем таблицу ссылками на метод ExecuteUNKNOWN, выполняющий TRAP 10
    RegisterMethodRef( 0000000, 0177777, &CProcessor::ExecuteUNKNOWN );

    RegisterMethodRef( 0000000, 0000000, &CProcessor::ExecuteHALT );
    RegisterMethodRef( 0000001, 0000001, &CProcessor::ExecuteWAIT );
    RegisterMethodRef( 0000002, 0000002, &CProcessor::ExecuteRTI );
    RegisterMethodRef( 0000003, 0000003, &CProcessor::ExecuteBPT );
    RegisterMethodRef( 0000004, 0000004, &CProcessor::ExecuteIOT );
    RegisterMethodRef( 0000005, 0000005, &CProcessor::ExecuteRESET );
    RegisterMethodRef( 0000006, 0000006, &CProcessor::ExecuteRTT );

    RegisterMethodRef( 0000010, 0000013, &CProcessor::ExecuteRUN );
    RegisterMethodRef( 0000014, 0000017, &CProcessor::ExecuteSTEP );
    RegisterMethodRef( 0000020, 0000020, &CProcessor::ExecuteRSEL );
    RegisterMethodRef( 0000021, 0000021, &CProcessor::ExecuteMFUS );
    RegisterMethodRef( 0000022, 0000023, &CProcessor::ExecuteRCPC );
    RegisterMethodRef( 0000024, 0000027, &CProcessor::ExecuteRCPS );
    RegisterMethodRef( 0000030, 0000030, &CProcessor::Execute000030 );
    RegisterMethodRef( 0000031, 0000031, &CProcessor::ExecuteMTUS );
    RegisterMethodRef( 0000032, 0000033, &CProcessor::ExecuteWCPC );
    RegisterMethodRef( 0000034, 0000037, &CProcessor::ExecuteWCPS );

    RegisterMethodRef( 0000100, 0000177, &CProcessor::ExecuteJMP );
    RegisterMethodRef( 0000200, 0000207, &CProcessor::ExecuteRTS );  // RTS / RETURN

    RegisterMethodRef( 0000240, 0000257, &CProcessor::ExecuteCCC );
    RegisterMethodRef( 0000260, 0000277, &CProcessor::ExecuteSCC );

#if PROCESSOR_USE_NEW_ALU == 0
    RegisterMethodRef( 0000300, 0000377, &CProcessor::ExecuteSWAB );

    RegisterMethodRef( 0000400, 0000777, &CProcessor::ExecuteBR );
    RegisterMethodRef( 0001000, 0001377, &CProcessor::ExecuteBNE );
    RegisterMethodRef( 0001400, 0001777, &CProcessor::ExecuteBEQ );
    RegisterMethodRef( 0002000, 0002377, &CProcessor::ExecuteBGE );
    RegisterMethodRef( 0002400, 0002777, &CProcessor::ExecuteBLT );
    RegisterMethodRef( 0003000, 0003377, &CProcessor::ExecuteBGT );
    RegisterMethodRef( 0003400, 0003777, &CProcessor::ExecuteBLE );

#else
    RegisterMethodRef( 0000300, 0000377, (&CProcessor::op_alu1<vm2::alu::opSWAB, P_RMWDST>));

    RegisterMethodRef( 0000400, 0000777, &CProcessor::op_branch<&vm2::alu::condBR > );
    RegisterMethodRef( 0001000, 0001377, &CProcessor::op_branch<&vm2::alu::condBNE> );
    RegisterMethodRef( 0001400, 0001777, &CProcessor::op_branch<&vm2::alu::condBEQ> );
    RegisterMethodRef( 0002000, 0002377, &CProcessor::op_branch<&vm2::alu::condBGE> );
    RegisterMethodRef( 0002400, 0002777, &CProcessor::op_branch<&vm2::alu::condBLT> );
    RegisterMethodRef( 0003000, 0003377, &CProcessor::op_branch<&vm2::alu::condBGT> );
    RegisterMethodRef( 0003400, 0003777, &CProcessor::op_branch<&vm2::alu::condBLE> );
#endif

    RegisterMethodRef( 0004000, 0004777, &CProcessor::ExecuteJSR );  // JSR / CALL

#if PROCESSOR_USE_NEW_ALU == 0
    RegisterMethodRef( 0005000, 0005077, &CProcessor::ExecuteCLR );
    RegisterMethodRef( 0005100, 0005177, &CProcessor::ExecuteCOM );
    RegisterMethodRef( 0005200, 0005277, &CProcessor::ExecuteINC );
    RegisterMethodRef( 0005300, 0005377, &CProcessor::ExecuteDEC );
    RegisterMethodRef( 0005400, 0005477, &CProcessor::ExecuteNEG );
    RegisterMethodRef( 0005500, 0005577, &CProcessor::ExecuteADC );
    RegisterMethodRef( 0005600, 0005677, &CProcessor::ExecuteSBC );
    RegisterMethodRef( 0005700, 0005777, &CProcessor::ExecuteTST );
    RegisterMethodRef( 0006000, 0006077, &CProcessor::ExecuteROR );
    RegisterMethodRef( 0006100, 0006177, &CProcessor::ExecuteROL );
    RegisterMethodRef( 0006200, 0006277, &CProcessor::ExecuteASR );
    RegisterMethodRef( 0006300, 0006377, &CProcessor::ExecuteASL );
#else
    RegisterMethodRef( 0005000, 0005077, (&CProcessor::op_alu1<vm2::alu::opCLR, P_WRIDST>));
    RegisterMethodRef( 0005100, 0005177, (&CProcessor::op_alu1<vm2::alu::opCOM, P_RMWDST>));
    RegisterMethodRef( 0005200, 0005277, (&CProcessor::op_alu1<vm2::alu::opINC, P_RMWDST>));
    RegisterMethodRef( 0005300, 0005377, (&CProcessor::op_alu1<vm2::alu::opDEC, P_RMWDST>));
    RegisterMethodRef( 0005400, 0005477, (&CProcessor::op_alu1<vm2::alu::opNEG, P_RMWDST>));
    RegisterMethodRef( 0005500, 0005577, (&CProcessor::op_alu1<vm2::alu::opADC, P_RMWDST>));
    RegisterMethodRef( 0005600, 0005677, (&CProcessor::op_alu1<vm2::alu::opSBC, P_RMWDST>));
    RegisterMethodRef( 0005700, 0005777, (&CProcessor::op_alu1<vm2::alu::opTST, P_READST>));
    RegisterMethodRef( 0006000, 0006077, (&CProcessor::op_alu1<vm2::alu::opROR, P_RMWDST>));
    RegisterMethodRef( 0006100, 0006177, (&CProcessor::op_alu1<vm2::alu::opROL, P_RMWDST>));
    RegisterMethodRef( 0006200, 0006277, (&CProcessor::op_alu1<vm2::alu::opASR, P_RMWDST>));
    RegisterMethodRef( 0006300, 0006377, (&CProcessor::op_alu1<vm2::alu::opASL, P_RMWDST>));
#endif

    RegisterMethodRef( 0006400, 0006477, &CProcessor::ExecuteMARK );

#if PROCESSOR_USE_NEW_ALU == 0
    RegisterMethodRef( 0006700, 0006777, &CProcessor::ExecuteSXT );
#else
    RegisterMethodRef( 0006700, 0006777, (&CProcessor::op_alu1<vm2::alu::opSXT, P_WRIDST>));
#endif

#if PROCESSOR_USE_NEW_ALU == 0
    RegisterMethodRef( 0010000, 0017777, &CProcessor::ExecuteMOV );
    RegisterMethodRef( 0020000, 0027777, &CProcessor::ExecuteCMP );
    RegisterMethodRef( 0030000, 0037777, &CProcessor::ExecuteBIT );
    RegisterMethodRef( 0040000, 0047777, &CProcessor::ExecuteBIC );
    RegisterMethodRef( 0050000, 0057777, &CProcessor::ExecuteBIS );
    RegisterMethodRef( 0060000, 0067777, &CProcessor::ExecuteADD );
#else
    RegisterMethodRef( 0010000, 0017777, (&CProcessor::op_alu2<vm2::alu::opMOV, P_REASRC | P_WRIDST>));
    RegisterMethodRef( 0020000, 0027777, (&CProcessor::op_alu2<vm2::alu::opCMP, P_REASRC | P_READST>));
    RegisterMethodRef( 0030000, 0037777, (&CProcessor::op_alu2<vm2::alu::opBIT, P_REASRC | P_READST>));
    RegisterMethodRef( 0040000, 0047777, (&CProcessor::op_alu2<vm2::alu::opBIC, P_REASRC | P_RMWDST>));
    RegisterMethodRef( 0050000, 0057777, (&CProcessor::op_alu2<vm2::alu::opBIS, P_REASRC | P_RMWDST>));
    RegisterMethodRef( 0060000, 0067777, (&CProcessor::op_alu2<vm2::alu::opADD, P_REASRC | P_RMWDST>));
#endif

    RegisterMethodRef( 0070000, 0070777, &CProcessor::ExecuteMUL );
    RegisterMethodRef( 0071000, 0071777, &CProcessor::ExecuteDIV );
    RegisterMethodRef( 0072000, 0072777, &CProcessor::ExecuteASH );
    RegisterMethodRef( 0073000, 0073777, &CProcessor::ExecuteASHC );

#if PROCESSOR_USE_NEW_ALU == 0
    RegisterMethodRef( 0074000, 0074777, &CProcessor::ExecuteXOR );
#else
    RegisterMethodRef( 0074000, 0074777, (&CProcessor::op_alu2<vm2::alu::opXOR, P_RDSRC | P_RMWDST>) );
#endif

    RegisterMethodRef( 0075000, 0075037, &CProcessor::ExecuteFIS );
    RegisterMethodRef( 0077000, 0077777, &CProcessor::ExecuteSOB );

#if PROCESSOR_USE_NEW_ALU == 0
    RegisterMethodRef( 0100000, 0100377, &CProcessor::ExecuteBPL );
    RegisterMethodRef( 0100400, 0100777, &CProcessor::ExecuteBMI );
    RegisterMethodRef( 0101000, 0101377, &CProcessor::ExecuteBHI );
    RegisterMethodRef( 0101400, 0101777, &CProcessor::ExecuteBLOS );
    RegisterMethodRef( 0102000, 0102377, &CProcessor::ExecuteBVC );
    RegisterMethodRef( 0102400, 0102777, &CProcessor::ExecuteBVS );
    RegisterMethodRef( 0103000, 0103377, &CProcessor::ExecuteBHIS );  // BCC
    RegisterMethodRef( 0103400, 0103777, &CProcessor::ExecuteBLO );   // BCS
#else
    RegisterMethodRef( 0100000, 0100377, &CProcessor::op_branch<&vm2::alu::condBPL >);
    RegisterMethodRef( 0100400, 0100777, &CProcessor::op_branch<&vm2::alu::condBMI >);
    RegisterMethodRef( 0101000, 0101377, &CProcessor::op_branch<&vm2::alu::condBHI >);
    RegisterMethodRef( 0101400, 0101777, &CProcessor::op_branch<&vm2::alu::condBLOS>);
    RegisterMethodRef( 0102000, 0102377, &CProcessor::op_branch<&vm2::alu::condBVC >);
    RegisterMethodRef( 0102400, 0102777, &CProcessor::op_branch<&vm2::alu::condBVS >);
    RegisterMethodRef( 0103000, 0103377, &CProcessor::op_branch<&vm2::alu::condBHIS>);  // BCC
    RegisterMethodRef( 0103400, 0103777, &CProcessor::op_branch<&vm2::alu::condBLO >);  // BCS
#endif

    RegisterMethodRef( 0104000, 0104377, &CProcessor::ExecuteEMT );
    RegisterMethodRef( 0104400, 0104777, &CProcessor::ExecuteTRAP );

#if PROCESSOR_USE_NEW_ALU == 0
    RegisterMethodRef( 0105000, 0105077, &CProcessor::ExecuteCLRB );
    RegisterMethodRef( 0105100, 0105177, &CProcessor::ExecuteCOMB );
    RegisterMethodRef( 0105200, 0105277, &CProcessor::ExecuteINCB );
    RegisterMethodRef( 0105300, 0105377, &CProcessor::ExecuteDECB );
    RegisterMethodRef( 0105400, 0105477, &CProcessor::ExecuteNEGB );
    RegisterMethodRef( 0105500, 0105577, &CProcessor::ExecuteADCB );
    RegisterMethodRef( 0105600, 0105677, &CProcessor::ExecuteSBCB );
    RegisterMethodRef( 0105700, 0105777, &CProcessor::ExecuteTSTB );
    RegisterMethodRef( 0106000, 0106077, &CProcessor::ExecuteRORB );
    RegisterMethodRef( 0106100, 0106177, &CProcessor::ExecuteROLB );
    RegisterMethodRef( 0106200, 0106277, &CProcessor::ExecuteASRB );
    RegisterMethodRef( 0106300, 0106377, &CProcessor::ExecuteASLB );
#else
    RegisterMethodRef( 0105000, 0105077, (&CProcessor::op_alu1<vm2::alu::opCLRB, P_RMWbDST>)); // NOTE: CLRB is RMWb (not Wb)
    RegisterMethodRef( 0105100, 0105177, (&CProcessor::op_alu1<vm2::alu::opCOMB, P_RMWbDST>));
    RegisterMethodRef( 0105200, 0105277, (&CProcessor::op_alu1<vm2::alu::opINCB, P_RMWbDST>));
    RegisterMethodRef( 0105300, 0105377, (&CProcessor::op_alu1<vm2::alu::opDECB, P_RMWbDST>));
    RegisterMethodRef( 0105400, 0105477, (&CProcessor::op_alu1<vm2::alu::opNEGB, P_RMWbDST>));
    RegisterMethodRef( 0105500, 0105577, (&CProcessor::op_alu1<vm2::alu::opADCB, P_RMWbDST>));
    RegisterMethodRef( 0105600, 0105677, (&CProcessor::op_alu1<vm2::alu::opSBCB, P_RMWbDST>));
    RegisterMethodRef( 0105700, 0105777, (&CProcessor::op_alu1<vm2::alu::opTSTB, P_REAbDST>));
    RegisterMethodRef( 0106000, 0106077, (&CProcessor::op_alu1<vm2::alu::opRORB, P_RMWbDST>));
    RegisterMethodRef( 0106100, 0106177, (&CProcessor::op_alu1<vm2::alu::opROLB, P_RMWbDST>));
    RegisterMethodRef( 0106200, 0106277, (&CProcessor::op_alu1<vm2::alu::opASRB, P_RMWbDST>));
    RegisterMethodRef( 0106300, 0106377, (&CProcessor::op_alu1<vm2::alu::opASLB, P_RMWbDST>));
#endif


    RegisterMethodRef( 0106400, 0106477, &CProcessor::ExecuteMTPS );
    RegisterMethodRef( 0106700, 0106777, &CProcessor::ExecuteMFPS );

#if PROCESSOR_USE_NEW_ALU == 0
    RegisterMethodRef( 0110000, 0117777, &CProcessor::ExecuteMOVB );
    RegisterMethodRef( 0120000, 0127777, &CProcessor::ExecuteCMPB );
    RegisterMethodRef( 0130000, 0137777, &CProcessor::ExecuteBITB );
    RegisterMethodRef( 0140000, 0147777, &CProcessor::ExecuteBICB );
    RegisterMethodRef( 0150000, 0157777, &CProcessor::ExecuteBISB );
    RegisterMethodRef( 0160000, 0167777, &CProcessor::ExecuteSUB );
#else
    RegisterMethodRef( 0110000, 0117777, (&CProcessor::op_alu2<vm2::alu::opMOVB, P_REASRC | P_RMWbDST | P_IGBDR>) );
    RegisterMethodRef( 0120000, 0127777, (&CProcessor::op_alu2<vm2::alu::opCMPB, P_REASRC | P_REAbDST>));
    RegisterMethodRef( 0130000, 0137777, (&CProcessor::op_alu2<vm2::alu::opBITB, P_REASRC | P_REAbDST>));
    RegisterMethodRef( 0140000, 0147777, (&CProcessor::op_alu2<vm2::alu::opBICB, P_REASRC | P_RMWbDST>));
    RegisterMethodRef( 0150000, 0157777, (&CProcessor::op_alu2<vm2::alu::opBISB, P_REASRC | P_RMWbDST>));
    RegisterMethodRef( 0160000, 0167777, (&CProcessor::op_alu2<vm2::alu::opSUB , P_REASRC | P_RMWDST >));
#endif
}

void CProcessor::Done()
{
    ::free(m_pExecuteMethodMap);  m_pExecuteMethodMap = nullptr;
}

//////////////////////////////////////////////////////////////////////


CProcessor::CProcessor (LPCTSTR name)
{
    _tcscpy(m_name, name);
    memset(m_R, 0, sizeof(m_R));
    m_psw = m_savepsw = 0777;
    m_savepc = 0177777;
    m_okStopped = true;
    waitstates_set(0);
    m_pMemoryController = nullptr;
    m_okTrace = false;
    m_waitmode = false;
    m_stepmode = false;
    m_buserror = false;
    m_STRTrq = m_RPLYrq = m_RSVDrq = m_TBITrq = m_ACLOrq = m_HALTrq = m_EVNTrq = false;
    m_ILLGrq = m_FIS_rq = m_BPT_rq = m_IOT_rq = m_EMT_rq = m_TRAPrq = false;
    m_ACLOreset = m_EVNTreset = false; m_VIRQreset = 0;
    m_DCLOpin = m_ACLOpin = true;
    m_haltpin = false;

    m_instruction = m_instructionpc = 0;
    m_regsrc = m_methsrc = 0;
    m_regdest = m_methdest = 0;
    m_addrsrc = m_addrdest = 0;
    memset(m_virq, 0, sizeof(m_virq));
    memset(m_virq_p, 0, sizeof(m_virq_p));

    m_timing = &cpu_instimes;
    if (name[0] == _T('P')) {
        m_timing = &ppu_instimes;
        ic_.s_ = 12;
    }
}

void CProcessor::Execute()
{
    if (m_okStopped) return;  // Processor is stopped - nothing to do

#if 0
    m_internalTick--;
    if (m_internalTick > 0)
        return;

    m_internalTick = 0;  //ANYTHING UNKNOWN WILL CAUSE EXCEPTION (EMT)
#else
    if (ic_.tick())
        return;

#endif
    if (!InterruptProcessing()) {
        memcpy(m_virq_p, m_virq, sizeof(m_virq));
        CommandExecution();
    }

//    m_totalticks += m_internalTick;
}

bool CProcessor::InterruptProcessing ()
{
    uint16_t intrVector = 0xFFFF;
    bool currMode = ((m_psw & 0400) != 0);  // Current processor mode: true = HALT mode, false = USER mode
    bool intrMode = false;  // true = HALT mode interrupt, false = USER mode interrupt

    if (m_stepmode)
        m_stepmode = false;
    else
    {
        m_ACLOreset = m_EVNTreset = false; m_VIRQreset = 0;
        m_TBITrq = (m_psw & 020) != 0;  // T-bit

        if (m_STRTrq)
        {
            intrVector = 0; intrMode = true;
            m_STRTrq = false;
        }
        else if (m_HALTrq)  // HALT command
        {
            intrVector = 0170;  intrMode = true;
            m_HALTrq = false;
        }
        else if (m_BPT_rq)  // BPT command
        {
            intrVector = 0000014;  intrMode = false;
            m_BPT_rq = false;
        }
        else if (m_IOT_rq)  // IOT command
        {
            intrVector = 0000020;  intrMode = false;
            m_IOT_rq = false;
        }
        else if (m_EMT_rq)  // EMT command
        {
            intrVector = 0000030;  intrMode = false;
            m_EMT_rq = false;
        }
        else if (m_TRAPrq)  // TRAP command
        {
            intrVector = 0000034;  intrMode = false;
            m_TRAPrq = false;
        }
        else if (m_FIS_rq)  // FIS commands -- Floating point Instruction Set
        {
            intrVector = 0010;  intrMode = true;
            m_FIS_rq = false;
        }
        else if (m_RPLYrq)  // Зависание, priority 1
        {
            if (m_buserror)
            {
                intrVector = 0174; intrMode = true;
            }
            else if (currMode)
            {
                intrVector = 0004;  intrMode = true;
            }
            else
            {
                intrVector = 0000004; intrMode = false;
            }
            m_buserror = true;
            m_RPLYrq = false;
        }
        else if (m_ILLGrq)
        {
            intrVector = 000004;  intrMode = false;
            m_ILLGrq = false;
        }
        else if (m_RSVDrq)  // Reserved command, priority 2
        {
            intrVector = 000010;  intrMode = false;
            m_RSVDrq = false;
        }
        else if (m_TBITrq && (!m_waitmode))  // T-bit, priority 3
        {
            intrVector = 000014;  intrMode = false;
            m_TBITrq = false;
        }
        else if (m_ACLOrq && (m_psw & 0600) != 0600)  // ACLO, priority 4
        {
            intrVector = 000024;  intrMode = false;
            m_ACLOreset = true;
        }
        else if (m_haltpin && (m_psw & 0400) != 0400)  // HALT signal in USER mode, priority 5
        {
            intrVector = 0170;  intrMode = true;
        }
        else if (m_EVNTrq && (m_psw & 0200) != 0200)  // EVNT signal, priority 6
        {
            intrVector = 0000100;  intrMode = false;
            m_EVNTreset = true;
        }
        else if ((m_psw & 0200) != 0200)  // VIRQ, priority 7
        {
            intrMode = false;
            for (uint8_t irq = 1; irq <= 15; irq++)
            {
                if (m_virq_p[irq] != 0)
                {
                    if (m_virq[irq] != 0) {
                        intrVector = m_virq[irq];
                        m_VIRQreset = irq;
                        break;
                    }
                    else {
                        // TODO: no reply on VIRQ have priority 5
                        m_virq_p[irq] = 0;
                        intrMode = true;
                        intrVector = 0274;
                    }
                }
            }
        }
        if (intrVector != 0xFFFF)
        {
            //if (m_internalTick == 0)
                waitstates_add(m_timing->SINT);  //ANYTHING UNKNOWN WILL CAUSE EXCEPTION (EMT)

            m_waitmode = false;

            if (intrMode)  // HALT mode interrupt
            {
                uint16_t selVector = GetMemoryController()->GetSelRegister() & 0x0ff00;
                uint16_t new_pc, new_psw;
                intrVector |= selVector;
                // Save PC/PSW to CPC/CPSW
                //m_savepc = GetPC();
                //m_savepsw = GetPSW();
                //m_psw |= 0400;
                SetHALT(true);
                new_pc = GetWord(intrVector);
                new_psw = GetWord(intrVector + 2);
                if (!m_RPLYrq)
                {
                    SetPSW(new_psw);
                    SetPC(new_pc);
                }
            }
            else  // USER mode interrupt
            {
                uint16_t new_pc, new_psw;

                SetHALT(false);
                // Save PC/PSW to stack
                SetSP(GetSP() - 2);
                SetWord(GetSP(), GetCPSW());
                SetSP(GetSP() - 2);
                if (!m_RPLYrq)
                {
                    SetWord(GetSP(), GetCPC());
                    if (!m_RPLYrq)
                    {
                        if (m_ACLOreset) m_ACLOrq = false;
                        if (m_EVNTreset) m_EVNTrq = false;
                        if (m_VIRQreset) {
                            m_virq_p[m_VIRQreset] = 0;
                            m_virq[m_VIRQreset] = 0;
                        }
                        new_pc = GetWord(intrVector);
                        new_psw = GetWord(intrVector + 2);
                        if (!m_RPLYrq)
                        {
                            SetLPSW((uint8_t)(new_psw & 0xff));
                            SetPC(new_pc);
                        }
                    }
                }
            }

            return true;
        }
    }
    return false;
}

void CProcessor::CommandExecution()
{
    if (!m_waitmode)
    {
        m_instructionpc = m_R[7];  // Store address of the current instruction
        FetchInstruction();  // Read next instruction from memory
        if (!m_RPLYrq)
        {
            m_buserror = false;
            TranslateInstruction();  // Execute next instruction
        }
    }
    if (m_HALTrq || m_BPT_rq || m_IOT_rq || m_EMT_rq || m_TRAPrq || m_FIS_rq)
        InterruptProcessing();
}

void CProcessor::TickEVNT()
{
    if (m_okStopped) return;  // Processor is stopped - nothing to do

    m_EVNTrq = true;
}

void CProcessor::SetDCLOPin(bool value)
{
    m_DCLOpin = value;
    if (m_DCLOpin)
    {
        m_okStopped = true;

        m_stepmode = false;
        m_buserror = false;
        m_waitmode = false;
        //m_internalTick = 0;
        waitstates_set(0);
        m_RPLYrq = m_RSVDrq = m_TBITrq = m_ACLOrq = m_HALTrq = m_EVNTrq = false;
        m_ILLGrq = m_FIS_rq = m_BPT_rq = m_IOT_rq = m_EMT_rq = m_TRAPrq = false;
        memset(m_virq, 0, sizeof(m_virq));
        memset(m_virq_p, 0, sizeof(m_virq_p));

        m_ACLOreset = m_EVNTreset = false; m_VIRQreset = 0;
        m_pMemoryController->DCLO_Signal();
        m_pMemoryController->ResetDevices();
    }
}

void CProcessor::SetACLOPin(bool value)
{
    if (m_okStopped && !m_DCLOpin && m_ACLOpin && !value)
    {
        m_okStopped = false;
        //m_internalTick = 0;
        waitstates_set(0);

        m_stepmode = false;
        m_waitmode = false;
        m_buserror = false;
        m_RPLYrq = m_RSVDrq = m_TBITrq = m_ACLOrq = m_HALTrq = m_EVNTrq = false;
        m_ILLGrq = m_FIS_rq = m_BPT_rq = m_IOT_rq = m_EMT_rq = m_TRAPrq = false;
        memset(m_virq, 0, sizeof(m_virq));
        memset(m_virq_p, 0, sizeof(m_virq_p));

        m_ACLOreset = m_EVNTreset = false; m_VIRQreset = 0;

        // "Turn On" interrupt processing
        m_STRTrq = true;
    }
    if (!m_okStopped && !m_DCLOpin && !m_ACLOpin && value)
    {
        m_ACLOrq = true;
    }
    m_ACLOpin = value;
}

void CProcessor::MemoryError()
{
    m_RPLYrq = true;
}


//////////////////////////////////////////////////////////////////////

#if defined(PRODUCT)
static void TraceInstruction(CProcessor* /*pProc*/, uint16_t /*address*/) {}
#else
static void TraceInstruction(CProcessor* pProc, uint16_t address)
{
    CMemoryController* pMemCtl = pProc->GetMemoryController();
    bool okHaltMode = pProc->IsHaltMode();
    uint16_t memory[4];
    int addrtype = 0;
    memory[0] = pMemCtl->GetWordView(address + 0 * 2, okHaltMode, true, &addrtype);
    memory[1] = pMemCtl->GetWordView(address + 1 * 2, okHaltMode, true, &addrtype);
    memory[2] = pMemCtl->GetWordView(address + 2 * 2, okHaltMode, true, &addrtype);
    memory[3] = pMemCtl->GetWordView(address + 3 * 2, okHaltMode, true, &addrtype);

    TCHAR bufaddr[7];
    PrintOctalValue(bufaddr, address);

    TCHAR instr[8];
    TCHAR args[32];
    DisassembleInstruction(memory, address, instr, args);
    TCHAR buffer[64];
    _sntprintf(buffer, sizeof(buffer) / sizeof(TCHAR) - 1, _T("%s\t%s\t%s\t%s\r\n"), pProc->GetName(), bufaddr, instr, args);

    DebugLog(buffer);
}
#endif

void CProcessor::FetchInstruction()
{
    // Считываем очередную инструкцию
    uint16_t pc = GetPC();
    ASSERT((pc & 1) == 0); // it have to be word aligned

//    m_instruction = GetWordExec(pc);
    m_instruction = -1;
    rsp_s rsp = bus_read_raw(pc);
    if (rsp.is_noreply())
        m_RPLYrq = true;
    else {
        m_instruction = rsp.data();
        if (rsp.dtime_.as_integer() != 0)
            ic_.s_ = 12;
        else
            ic_.s_ = 10;
        //fetch_delta_ = rsp.dtime_;
    }

    SetPC(GetPC() + 2);
}

void CProcessor::TranslateInstruction()
{
    if (m_okTrace)
        TraceInstruction(this, m_instructionpc);

    // Prepare values to help decode the command
    m_regdest  = GetDigit(m_instruction, 0);
    m_methdest = GetDigit(m_instruction, 1);
    m_regsrc   = GetDigit(m_instruction, 2);
    m_methsrc  = GetDigit(m_instruction, 3);

    // Find command implementation using the command map
    ExecuteMethodRef methodref = m_pExecuteMethodMap[m_instruction];
    (this->*methodref)();  // Call command implementation method
}

void CProcessor::ExecuteUNKNOWN ()  // Нет такой инструкции - просто вызывается TRAP 10
{
//    DebugPrintFormat(_T(">>Invalid OPCODE = %06o @ %06o\r\n"), m_instruction, GetPC()-2);

    m_RSVDrq = true;
}


// Instruction execution /////////////////////////////////////////////

void CProcessor::ExecuteWAIT ()  // WAIT - Wait for an interrupt
{
    m_waitmode = true;
}

void CProcessor::ExecuteSTEP()  // ШАГ
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
        m_RSVDrq = true;
    else
    {
        SetPC(m_savepc);        // СК <- КРСК
        SetPSW(m_savepsw);      // РСП(8:0) <- КРСП(8:0)
        m_stepmode = true;
    }
}

void CProcessor::ExecuteRSEL()  // RSEL / ЧПТ - Чтение безадресного регистра
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
        m_RSVDrq = true;
    else
    {
        SetReg(0, GetMemoryController()->GetSelRegister());  // R0 <- (SEL)
    }
}

void CProcessor::Execute000030()  // Unknown command
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
    {
        m_RSVDrq = true;
        return;
    }

    // Описание: По этой команде сперва очищается регистр R0. Далее исполняется цикл, окончанием которого
    //           является установка в разряде 07 R0 или R2 единицы. В цикле над регистрами проводятся
    //           следующие действия: регистры с R1 по R3 сдвигаются влево, при этом в R1 в младший разряд
    //           вдвигается ноль, а в R2 и R3 – содержимое разряда C, при этом старшая часть R2 расширяется
    //           знаковым разрядом младшей части, R0 инкрементируется. Так как останов исполнения команды
    //           производится при наличии единицы в разряде 7 в R0 или R2, то после исполнения команды R0
    //           может принимать значения от 0 до 108 или 2008. Значение 2008 получается в том случае,
    //           если до исполнения операции младшая часть R2 была равна нулю и был сброшен бит С.
    // Признаки: N – очищается,
    //           Z – устанавливается, если значение в R0 равно нулю, в противном случае очищается,
    //           V – очищается,
    //           C – очищается.

    SetReg(0, 0);
    while ((GetReg(0) & 0200) == 0 && (GetReg(2) & 0200) == 0)
    {
        SetReg(1, GetReg(1) << 1);
        SetReg(2, (((uint16_t)GetLReg(2)) << 1) | (GetC() ? 1 : 0));
        SetReg(2, ((GetReg(2) & 0200) ? 0xff00 : 0) | GetLReg(2));
        SetReg(3, (GetReg(3) << 1) | (GetC() ? 1 : 0));
        SetReg(0, GetReg(0) + 1);
    }
    SetN(0);
    SetZ(GetReg(0) == 0);
    SetV(0);
    SetC(0);
}

void CProcessor::ExecuteFIS()  // Floating point instruction set: FADD, FSUB, FMUL, FDIV
{
    if (GetMemoryController()->GetSelRegister() & 0200)  // bit 7 set?
        m_RSVDrq = true;  // Программа эмуляции FIS отсутствует, прерывание по резервному коду
    else
        m_FIS_rq = true;  // Прерывание обработки FIS
}

void CProcessor::ExecuteRUN()  // ПУСК / START
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
        m_RSVDrq = true;
    else
    {
        SetPC(m_savepc);        // СК <- КРСК
        SetPSW(m_savepsw);      // РСП(8:0) <- КРСП(8:0)
    }
}

void CProcessor::ExecuteHALT ()  // HALT - Останов
{
    m_HALTrq = true;
}

void CProcessor::ExecuteRCPC()  // ЧКСК - Чтение регистра копии счётчика команд
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
        m_RSVDrq = true;
    else
    {
        SetReg(0, m_savepc);        // R0 <- КРСК
        waitstates_add(m_timing->NOP); // TODO: timing
    }
}
void CProcessor::ExecuteRCPS()  // ЧКСП - Чтение регистра копии слова состояния процессора
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
        m_RSVDrq = true;
    else
    {
        SetReg(0, m_savepsw);       // R0 <- КРСП
        waitstates_add(m_timing->NOP); // TODO: timing
    }
}
void CProcessor::ExecuteWCPC()  // ЗКСК - Запись регистра копии счётчика команд
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
        m_RSVDrq = true;
    else
    {
        m_savepc = GetReg(0);       // КРСК <- R0
        waitstates_add(m_timing->NOP); // TODO: timing
    }
}
void CProcessor::ExecuteWCPS()  // ЗКСП - Запись регистра копии слова состояния процессора
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
        m_RSVDrq = true;
    else
    {
        m_savepsw = GetReg(0);      // КРСП <- R0
        waitstates_add(m_timing->NOP); // TODO: timing
    }
}

void CProcessor::ExecuteMFUS ()  // ЧЧП, move from user space - Чтение памяти адресного пространства USER
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
    {
        m_RSVDrq = true;
        return;
    }

    //r0 = (r5)+
    SetHALT(false);
    uint16_t addr = GetReg(5);
    uint16_t word = GetWord(addr);  // Read in USER mode
    SetHALT(true);
    SetReg(5, addr + 2);
    if (!m_RPLYrq)  SetReg(0, word);

    waitstates_add(m_timing->R_W[2][0]); // TODO: timing
}

void CProcessor::ExecuteMTUS()  // ЗЧП, move to user space - Запись в память адресного пространства USER
{
    if ((m_psw & PSW_HALT) == 0)  // Эта команда выполняется только в режиме HALT
    {
        m_RSVDrq = true;
        return;
    }

    // -(r5) = r0
    SetReg(5, GetReg(5) - 2);
    SetHALT(false);
    SetWord(GetReg(5), GetReg(0));  // Write in USER mode
    SetHALT(true);

    waitstates_add(m_timing->R_W[0][4]); // TODO: timing
}

void CProcessor::ExecuteRTI()  // RTI - Return from Interrupt - Возврат из прерывания
{
    uint16_t word;
    word = GetWord(GetSP());
    SetSP( GetSP() + 2 );
    if (m_RPLYrq) return;
    SetPC(word);  // Pop PC
    word = GetWord ( GetSP() );  // Pop PSW --- saving HALT
    SetSP( GetSP() + 2 );
    if (m_RPLYrq) return;
    if (GetPC() < 0160000)
        SetLPSW((uint8_t)(word & 0xff));
    else
        SetPSW(word); //load new mode
    waitstates_add(m_timing->RTx);
}

void CProcessor::ExecuteRTT ()  // RTT - Return from Trace Trap -- Возврат из прерывания
{
    uint16_t word;
    word = GetWord(GetSP());
    SetSP( GetSP() + 2 );
    if (m_RPLYrq) return;
    SetPC(word);  // Pop PC
    word = GetWord ( GetSP() );  // Pop PSW --- saving HALT
    SetSP( GetSP() + 2 );
    if (m_RPLYrq) return;
    if (GetPC() < 0160000)
        SetLPSW((uint8_t)(word & 0xff));
    else
        SetPSW(word); //load new mode

    m_stepmode = (word & PSW_T) ? true : false;

    waitstates_add(m_timing->RTx);
}

void CProcessor::ExecuteBPT ()  // BPT - Breakpoint
{
    m_BPT_rq = true;
//    waitstates_add(m_timing->SINT);
}

void CProcessor::ExecuteIOT ()  // IOT - I/O trap
{
    m_IOT_rq = true;
//    waitstates_add(m_timing->SINT);
}

void CProcessor::ExecuteRESET ()  // Reset input/output devices -- Сброс внешних устройств
{
    m_EVNTrq = false;
    m_pMemoryController->ResetDevices();  // INIT signal

    waitstates_add(m_timing->RESET);
}

void CProcessor::ExecuteRTS ()  // RTS - return from subroutine - Возврат из процедуры
{
    uint16_t word;
    SetPC(GetReg(m_regdest));
    word = GetWord(GetSP());
    SetSP(GetSP() + 2);
    if (m_RPLYrq) return;
    SetReg(m_regdest, word);

    waitstates_add(m_timing->RTS);
}

void CProcessor::ExecuteCCC ()
{
    SetLPSW(GetLPSW() &  ~((uint8_t)(m_instruction & 0xff) & 017));
    waitstates_add(m_timing->NOP);
}
void CProcessor::ExecuteSCC ()
{
    SetLPSW(GetLPSW() |  ((uint8_t)(m_instruction & 0xff) & 017));
    waitstates_add(m_timing->NOP);
}

constexpr instime_t VM2_RPLY_TIMEOUT  { 120.0 };

#if 0
void CProcessor::ExecuteJMP ()  // JMP - jump: PC = &d (a-mode > 0)
{
    if (m_methdest == 0)  // Неправильный метод адресации
    {
        m_ILLGrq = true;
        //waitstates_add(m_timing->SINT);
    }
    else
    {
        uint16_t word;
        word = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq)
            return;

        SetPC(word);

        waitstates_add(m_timing->JMP[m_methdest]);
    }
}
#else
void CProcessor::ExecuteJMP()
{
    constexpr unsigned int flags = P_EADST | P_NOPSW;
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    if (estate.dst.ea.is_reg()) {
        m_ILLGrq = true;
        return;
    }

    SetPC(estate.dst.ea.addr());

    op_exec_finalize<flags>(estate);

    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it = m_timing->JMP[md];
    it -= estate.delta;
    waitstates_add(it);
}
#endif
//////////////////////////////////////////////////////////////////////////////////
#if 1

template<bool byte>
CProcessor::ea_s CProcessor::op_calculate_ea(x_estate_s& estate, unsigned int m77)
{
    unsigned int reg = m77 & 007;
    unsigned int adm = m77 & 060;
    unsigned int ref = m77 & 010;

    // delta is 1 only if BYTE & rN not SP|PC & it not deref mode
    const unsigned int delta = 2 - (byte && (reg < 6) && (ref == 0));

    ea_s ea{ ea_s::reg_index{reg} };
    switch (adm) {

    case 020:
        {
            unsigned int ro = GetReg(reg);
            ea = ea_s{ ea_s::mem_addr{ro} };
            ro += delta;
            SetReg(reg, ro);
        }
        break;

    case 040:
        {
            unsigned int ro = GetReg(reg);
            ro -= delta;
            ea = ea_s{ ea_s::mem_addr{ro} };
            SetReg(reg, ro);
        }
        break;

    case 060:
        {
            unsigned int pc = GetReg(7);
            SetReg(7, pc + 2);
            unsigned int ro = GetReg(reg);

            unsigned int ba = -1;
            rsp_s rsp = bus_read_raw(pc);
            if (rsp.is_noreply())
                m_RPLYrq = true;
            else {
                ba = rsp.data();
                estate.delta += rsp.dtime_;
            }

            ea = ea_s { ea_s::mem_addr { ba + ro } };
        }
        break;
    }

    if (ref && !m_RPLYrq) {
        unsigned int deref;
        if (ea.is_mem()) {
            //deref = bus_read(ea.addr());
            rsp_s rsp = bus_read_raw(ea.addr());
            if (rsp.is_noreply())
                m_RPLYrq = true;
            else {
                deref = rsp.data();
                estate.delta += rsp.dtime_;
            }
        }
        else
            deref = GetReg(ea.reg());

        ea = ea_s { ea_s::mem_addr{deref} };
    }

    return ea;
}

template<bool byte, CProcessor::rmw_e t>
void CProcessor::op_exec_fetch_field(x_estate_s& estate, x_op_arg_s& field)
{
    const auto& ea = field.ea;
    unsigned int u16;

    if (ea.is_mem()) {
        rsp_s rsp = bus_read_raw(ea.addr(), t);
        if (rsp.is_noreply())
            m_RPLYrq = true;
        else {
            u16 = rsp.data();
            estate.delta += rsp.dtime_;
        }
    }
    else
        u16 = GetReg(ea.reg());

    if (m_RPLYrq)
        return;

    field.u16 = u16;

    unsigned int alu_u16 = u16;
    if constexpr (byte) {
        if (ea.is_mem() && (ea.addr() & 1))
            alu_u16 >>= 8;

        alu_u16 &= 0x00FF;
    }

    field.alu_u16 = alu_u16;
}

template<unsigned int flags>
void CProcessor::op_exec_fetch_src(x_estate_s& estate)
{
    unsigned int opcode = m_instruction;

    constexpr bool byte = (flags & P_BYTE);

    constexpr unsigned int m70bm = (flags & P_EASRC) ? 077 : 000;
    constexpr unsigned int m07bm = (flags & P_RDSRC) ? 007 : 000;
    constexpr unsigned int m77bm = m70bm | m07bm;
    if constexpr (m77bm) {
        unsigned int m77 = (opcode >> 6);
        estate.src.ea = op_calculate_ea<byte>(estate, m77 & m77bm);
        if (m_RPLYrq)
            return;

        if constexpr (m07bm) {
            op_exec_fetch_field<byte, rmw_e::single>(estate, estate.src);
            if (m_RPLYrq)
                return;

            if constexpr (flags & P_RDSRCPR) {
                const unsigned int rb = estate.src.ea.reg();
                const unsigned int rp = rb | 1;
                const unsigned int rp_u16 = GetReg(rp);
                estate.src.alu_u16 <<= 16;
                estate.src.alu_u16 |=  rp_u16;
            }
        }
    }
}

template<unsigned int flags>
void CProcessor::op_exec_fetch_dst(x_estate_s& estate)
{
    unsigned int opcode = m_instruction;

    constexpr bool byte = (flags & P_BYTE);

    constexpr unsigned int m70bm = (flags & P_EADST) ? 077 : 000;
    constexpr unsigned int m07bm = (flags & (P_RDDST | P_WRDST)) ? 007 : 000;
    constexpr unsigned int m77bm = m70bm | m07bm;
    if constexpr (m77bm) {
        unsigned int m77 = (opcode >> 0);
        estate.dst.ea = op_calculate_ea<byte>(estate, m77 & m77bm);
        if (m_RPLYrq)
            return;

        if constexpr (flags & P_RDDST) {
            constexpr rmw_e t = ((flags & (P_RDDST | P_WRDST)) == (P_RDDST | P_WRDST)) ? rmw_e::rmw : rmw_e::single;
            op_exec_fetch_field<byte, t>(estate, estate.dst);
            if (m_RPLYrq)
                return;
        }
    }
}


template<unsigned int flags>
void CProcessor::op_exec_prepare(x_estate_s& estate)
{
#if 0
    unsigned int opcode = m_instruction;

    constexpr bool byte = (flags & P_BYTE);

    if constexpr (true)
    {
        constexpr unsigned int m70bm = (flags & P_EASRC) ? 077 : 000;
        constexpr unsigned int m07bm = (flags & P_RDSRC) ? 007 : 000;
        constexpr unsigned int m77bm = m70bm | m07bm;
        if constexpr (m77bm) {
            unsigned int m77 = (opcode >> 6);
            estate.src.ea = op_calculate_ea<byte>(estate, m77 & m77bm);
            if (m_RPLYrq)
                return;

            if constexpr (m07bm) {
                op_exec_fetch_field<byte, rmw_e::single>(estate, estate.src);
                if (m_RPLYrq)
                    return;

                if constexpr (flags & P_RDSRCPR) {
                    const unsigned int rb = estate.src.ea.reg();
                    const unsigned int rp = rb | 1;
                    const unsigned int rp_u16 = GetReg(rp);
                    estate.src.alu_u16 <<= 16;
                    estate.src.alu_u16 |=  rp_u16;
                }
            }
        }
    }

    if constexpr (true) {
        constexpr unsigned int m70bm = (flags & P_EADST) ? 077 : 000;
        constexpr unsigned int m07bm = (flags & (P_RDDST | P_WRDST)) ? 007 : 000;
        constexpr unsigned int m77bm = m70bm | m07bm;
        if constexpr (m77bm) {
            unsigned int m77 = (opcode >> 0);
            estate.dst.ea = op_calculate_ea<byte>(estate, m77 & m77bm);
            if (m_RPLYrq)
                return;

            if constexpr (flags & P_RDDST) {
                constexpr rmw_e t = ((flags & (P_RDDST | P_WRDST)) == (P_RDDST | P_WRDST)) ? rmw_e::rmw : rmw_e::single;
                op_exec_fetch_field<byte, t>(estate, estate.dst);
                if (m_RPLYrq)
                    return;
            }
        }

    }
#endif

    if constexpr (flags & P_AFINV) { // EIS only
        op_exec_fetch_dst<flags>(estate); // DST first
        op_exec_fetch_src<flags>(estate); // SRC second
    }
    else {
        op_exec_fetch_src<flags>(estate); // SRC first
        op_exec_fetch_dst<flags>(estate); // DST second
    }
    estate.psw = GetLPSW();
}

template<unsigned int flags>
void CProcessor::op_exec_finalize(x_estate_s& estate)
{

//    if constexpr (flags & P_WRSRC) {
//        static_assert((flags & P_EASRC) == 0); // write SRC only for registers and its' pairs
//        static_assert((flags & P_WRDST) == 0); // have not to be write DST if SRC is written
//
//        const unsigned int rb = estate.src.ea.reg();
//        const unsigned int res = estate.res;
//        SetReg(rb, res & 0xFFFF);
//
//        if constexpr (flags & P_WRSRCPR) {
//            const unsigned int rp = rb | 1;
//            SetReg(rp, res >> 16);
//        }
//    }
//    else
    if constexpr (flags & P_WRDST) {
        constexpr bool byte = (flags & P_BYTE);

        const ea_s ea = estate.dst.ea;
        unsigned int res = estate.res;

        if (ea.is_mem()) {
            unsigned int a16 = ea.addr();

            if constexpr (byte) {
                unsigned int du16 = estate.dst.u16;
                if (a16 & 1)
                    res = ((res << 8) & 0xFF00) | (du16 & 0x00FF);
                else
                    res = ((res << 0) & 0x00FF) | (du16 & 0xFF00);
            }

            constexpr rmw_e t = ((flags & P_RMWDST) == P_RMWDST) ? rmw_e::rmw : rmw_e::single;

            rsp_s rsp = bus_write_raw(a16, res, byte, t);
            if (rsp.is_noreply())
                m_RPLYrq = true;
            else {
                estate.delta += rsp.dtime_;
            }
        }
        else {
            unsigned int ri = ea.reg();

            constexpr bool igbdr = (flags & P_IGBDR);

            if (byte & !igbdr)
                SetLReg(ri, res);
            else
                SetReg(ri, res);
        }
    }

    if (m_RPLYrq)
        return;

    if constexpr ( (flags & P_NOPSW) == 0 ) {
        SetLPSW(estate.psw);
    }
}

#endif


#if 0

#define PROCESSOR_USE_ONLY_WORD_IO 1

#if PROCESSOR_USE_ONLY_WORD_IO
template<unsigned int flags>
void CProcessor::op_exec_read(estate_s& args)
{
    if (flags & P_EASRC) {
        if (m_methsrc) {
            if (flags & P_BYTE)
                args.src.ea = GetByteAddr(m_methsrc, m_regsrc);
            else
                args.src.ea = GetWordAddr(m_methsrc, m_regsrc);
            if (m_RPLYrq)
                return;
        }
    }
#if 0
    if constexpr (flags & P_RDSRC) {
        if ((flags & P_EASRC) && m_methsrc) {
            args.src.u16 = GetWord(args.src.ea);
            if (m_RPLYrq)
                return;

            if (flags & P_BYTE) {
                if (args.src.ea & 1)
                    args.src.u16 >>= 8;

                args.src.u16 &= 0x00FF;
            }
        }
        else {
            if (flags & P_BYTE)
                args.src.u16 = GetLReg(m_regsrc);
            else
                args.src.u16 = GetReg(m_regsrc);
        }
    }
#else
    if (flags & P_RDSRC) {
        if ((flags & P_EASRC) && m_methsrc)
#if BUS_USE_NEW_IO
            args.src.u16 = bus_read(args.src.ea);
#else
            args.src.u16 = GetWord(args.src.ea);
#endif
        else
            args.src.u16 = GetReg(m_regsrc);
        if (m_RPLYrq)
            return;

        args.alu_src = args.src.u16;

        if (flags & P_BYTE) {

            if ((flags & P_EASRC) && m_methsrc) {
                if (args.src.ea & 1)
                    args.alu_src >>= 8;
            }

            args.alu_src &= 0x00FF;
        }
    }
#endif

    if (flags & P_EADST) {
        if (m_methdest) {
            if (flags & P_BYTE)
                args.dst.ea = GetByteAddr(m_methdest, m_regdest);
            else
                args.dst.ea = GetWordAddr(m_methdest, m_regdest);
            if (m_RPLYrq)
                return;
        }
    }

#if 0
    if (flags & P_RDDST) {
        static_assert(flags & P_EADST);

        if ((flags & P_EADST) && m_methdest) {
            args.dst.u16 = GetWord(args.dst.ea);
            if (m_RPLYrq)
                return;
        }
        else {
            if (flags & P_BYTE)
                args.dst.u16 = GetLReg(m_regdest);
            else
                args.dst.u16 = GetReg(m_regdest);
        }
    }
#else
    if (flags & P_RDDST) {
        if ((flags & P_EADST) && m_methdest)
#if BUS_USE_NEW_IO
            args.dst.u16 = bus_read(args.dst.ea);
#else
            args.dst.u16 = GetWord(args.dst.ea);
#endif
        else
            args.dst.u16 = GetReg(m_regdest);
        if (m_RPLYrq)
            return;

        args.alu_dst = args.dst.u16;

        if (flags & P_BYTE) {

            if ((flags & P_EADST) && m_methdest) {
                if (args.dst.ea & 1)
                    args.alu_dst >>= 8;
            }

            args.alu_dst &= 0x00FF;
        }
    }
#endif
}

template<unsigned int flags>
void CProcessor::op_exec_writeback(const op_arg_s& arg, unsigned int res)
{
    if (flags & P_WRDST) {
        if ((flags & P_EADST) && m_methdest) {
//            if (flags & P_BYTE)
//                SetByte(arg.ea, res);
//            else
//                SetWord(arg.ea, res);
            if (flags & P_BYTE) {
                if (arg.ea & 1)
                    res = ((res << 8) & 0xFF00) | (arg.u16 & 0x00FF);
                else
                    res = ((res << 0) & 0x00FF) | (arg.u16 & 0xFF00);
            }
#if BUS_USE_NEW_IO
            bus_write(arg.ea, res, (flags & P_BYTE));
#else
            SetWord(arg.ea, res);
#endif
            if (m_RPLYrq)
                return;
        }
        else {
            if (flags & P_BYTE) {
                if (flags & P_SXBDR) {
                    //unsigned int sxu16 = ((res ^ 0x80) - 0x80);
                    SetReg(m_regdest, res);
                }
                else
                    SetLReg(m_regdest, res);
            }
            else
                SetReg(m_regdest, res);
        }
    }
}
#else
template<unsigned int flags>
void CProcessor::op_read_args(op_args_s& args)
{
    if (flags & P_EASRC) {
        if (m_methsrc) {
            if (flags & P_BYTE)
                args.src.ea = GetByteAddr(m_methsrc, m_regsrc);
            else
                args.src.ea = GetWordAddr(m_methsrc, m_regsrc);
            if (m_RPLYrq) return;
        }
    }

    if constexpr (flags & P_RDSRC) {
        if ((flags & P_EASRC) && m_methsrc) {
            if (flags & P_BYTE)
                args.src.u16 = GetByte(args.src.ea);
            else
                args.src.u16 = GetWord(args.src.ea);
            if (m_RPLYrq) return;
        }
        else {
            if (flags & P_BYTE)
                args.src.u16 = GetLReg(m_regsrc);
            else
                args.src.u16 = GetReg(m_regsrc);
        }
    }

    if (flags & P_EADST) {
        if (m_methdest) {
            if (flags & P_BYTE)
                args.dst.ea = GetByteAddr(m_methdest, m_regdest);
            else
                args.dst.ea = GetWordAddr(m_methdest, m_regdest);
            if (m_RPLYrq) return;
        }
    }

    if (flags & P_RDDST) {
        static_assert(flags & P_EADST);

        if (m_methdest) {
            if (flags & P_BYTE)
                args.dst.u16 = GetByte(args.dst.ea);
            else
                args.dst.u16 = GetWord(args.dst.ea);
            if (m_RPLYrq) return;
        }
        else {
            if (flags & P_BYTE)
                args.dst.u16 = GetLReg(m_regdest);
            else
                args.dst.u16 = GetReg(m_regdest);
        }
    }
}

template<unsigned int flags>
void CProcessor::op_writeback(const op_arg_s& arg)
{
    if (flags & P_WRDST) {
        static_assert(flags & P_EADST);

        if (m_methdest) {
            if (flags & P_BYTE)
                SetByte(arg.ea, arg.u16);
            else
                SetWord(arg.ea, arg.u16);
            if (m_RPLYrq) return;
        }
        else {
            if (flags & P_BYTE) {
                if (flags & P_SXBDR) {
                    unsigned int sxu16 = ((arg.u16 ^ 0x80) - 0x80);
                    SetReg(m_regdest, sxu16);
                }
                else
                    SetLReg(m_regdest, arg.u16);
            }
            else
                SetReg(m_regdest, arg.u16);
        }
    }
}
#endif
#endif

#if 0
template<vm2::alu::alu1_fn fn, unsigned int flags>
void CProcessor::op_alu1()
{
    uint8_t  psw = GetLPSW();

    estate_s args;
    op_exec_read<flags>(args);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

#if PROCESSOR_USE_ONLY_WORD_IO == 1

    vm2::alu::alu1_s r = fn({psw, args.alu_dst});

    op_exec_writeback<flags>(args.dst, r.dst);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }
#else
    vm2::alu::alu1_s r = fn({psw, args.dst.u16});

    args.dst.u16 = r.dst;

    op_writeback<flags>(args.dst);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }
#endif
    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;

    SetLPSW(r.psw);
    switch(flags & (P_RDDST | P_WRDST))
    {
    case P_RDDST:
        waitstates_add(m_timing->R[md]);
        break;
    case P_WRDST:
        if (flags & P_BYTE)
            waitstates_add(m_timing->Wb[md]);
        else
            waitstates_add(m_timing->W[md]);
        break;
    case P_RDDST | P_WRDST:
        if (flags & P_BYTE)
            waitstates_add(m_timing->RMWb[md]);
        else
            waitstates_add(m_timing->RMW[md]);
        break;
    }
}
#else
template<vm2::alu::alu1_fn fn, unsigned int flags>
void CProcessor::op_alu1()
{
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    vm2::alu::alu1_s r = fn({estate.psw, estate.dst.alu_u16});

    estate.psw = r.psw;
    estate.res = r.dst;

    op_exec_finalize<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it;
    switch(flags & (P_RDDST | P_WRDST))
    {
    case P_RDDST:
        it = m_timing->R[md];
        break;
    case P_WRDST:
        if (flags & P_BYTE)
            it = m_timing->Wb[md];
        else
            it = m_timing->W[md];
        break;
    case P_RDDST | P_WRDST:
        if (flags & P_BYTE)
            it = m_timing->RMWb[md];
        else
            it = m_timing->RMW[md];
        break;
    }

    it -= estate.delta;

//    if (fetch_delta_.as_integer() != 0) {
//        it -= instime_t{ 2.0 };
//        assert( it.as_integer() > 0 );
//    }

    waitstates_add(it);
}

#endif

#if 0
template<vm2::alu::alu2_fn fn, unsigned int flags>
void CProcessor::op_alu2()
{
    uint8_t  psw = GetLPSW();

    estate_s args;
    op_exec_read<flags>(args);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }
#if PROCESSOR_USE_ONLY_WORD_IO == 1

    vm2::alu::alu1_s r = fn({ psw, args.alu_src, args.alu_dst });

    op_exec_writeback<flags>(args.dst, r.dst);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

#else
    vm2::alu::alu1_s r = fn({ psw, args.src.u16, args.dst.u16 });

    args.dst.u16 = r.dst;

    op_writeback<flags>(args.dst);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }
#endif
    const unsigned int ms = (flags & P_EASRC) ? m_methsrc  : 0;
    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    SetLPSW(r.psw);
    switch(flags & (P_RDDST | P_WRDST))
    {
    case P_RDDST:
        waitstates_add(m_timing->R_R[ms][md]);
        break;
    case P_WRDST:
        if (flags & P_BYTE)
            waitstates_add(m_timing->R_Wb[ms][md]);
        else
            waitstates_add(m_timing->R_W[ms][md]);
        break;
    case P_RDDST | P_WRDST:
        if (flags & P_BYTE)
            waitstates_add(m_timing->R_RMWb[ms][md]);
        else
            waitstates_add(m_timing->R_RMW[ms][md]);
        break;
    }
}
#else
template<vm2::alu::alu2_fn fn, unsigned int flags>
void CProcessor::op_alu2()
{
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    vm2::alu::alu1_s r = fn({estate.psw, estate.src.alu_u16, estate.dst.alu_u16});

    estate.psw = r.psw;
    estate.res = r.dst;

    op_exec_finalize<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    const unsigned int ms = (flags & P_EASRC) ? m_methsrc  : 0;
    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it;

    switch(flags & (P_RDDST | P_WRDST))
    {
    case P_RDDST:
        it = m_timing->R_R[ms][md];
        break;
    case P_WRDST:
        if (flags & P_BYTE)
            it = m_timing->R_Wb[ms][md];
        else
            it = m_timing->R_W[ms][md];
        break;
    case P_RDDST | P_WRDST:
        if (flags & P_BYTE)
            it = m_timing->R_RMWb[ms][md];
        else
            it = m_timing->R_RMW[ms][md];
        break;
    }

    it -= estate.delta;

    waitstates_add(it);
}
#endif


#if PROCESSOR_USE_NEW_ALU == 0
void CProcessor::ExecuteSWAB ()
{
    uint16_t ea = 0;
    uint16_t dst;
    uint8_t new_psw = GetLPSW() & 0xF0;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regdest);

    dst = ((dst >> 8) & 0377) | (dst << 8);

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);

    if (m_RPLYrq) return;

    if ((dst & 0200) != 0) new_psw |= PSW_N;
    if ((uint8_t)(dst & 0xff) == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteCLR ()  // CLR
{
    uint16_t dst_addr;

    if (m_methdest)
    {
        dst_addr = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        SetWord(dst_addr, 0);
        if (m_RPLYrq) return;
    }
    else
        SetReg(m_regdest, 0);

    SetLPSW((GetLPSW() & 0xF0) | PSW_Z);
    waitstates_add(m_timing->W[m_methdest]);
}

void CProcessor::ExecuteCLRB ()  // CLRB
{
    uint16_t dst_addr;

    if (m_methdest)
    {
        dst_addr = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        GetByte(dst_addr);
        if (m_RPLYrq) return;
        SetByte(dst_addr, 0);
        if (m_RPLYrq) return;
    }
    else
        SetLReg(m_regdest, 0);

    SetLPSW((GetLPSW() & 0xF0) | PSW_Z);
    waitstates_add(m_timing->Wb[m_methdest]);
}

void CProcessor::ExecuteCOM()  // COM
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t dst;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regdest);

    dst = ~dst;

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteCOMB()  // COM
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint8_t dst;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetLReg(m_regdest);

    dst = ~dst;

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteINC()  // INC - Инкремент
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint16_t dst;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regdest);

    dst = dst + 1;

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (dst == 0100000) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}
void CProcessor::ExecuteINCB()  // INCB - Инкремент
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint8_t dst;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetLReg(m_regdest);

    dst = dst + 1;

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (dst == 0200) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteDEC()  // DEC - Декремент
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint16_t dst;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regdest);

    dst = dst - 1;

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (dst == 077777) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteDECB ()  // DECB - Декремент
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint8_t dst;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetLReg(m_regdest);

    dst = dst - 1;

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (dst == 0177) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteNEG()
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t dst;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regdest);

    dst = 0 - dst;

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (dst == 0100000) new_psw |= PSW_V;
    if (dst != 0) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteNEGB ()
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint8_t dst;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetLReg(m_regdest);

    dst = 0 - dst ;

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (dst == 0200) new_psw |= PSW_V;
    if (dst != 0) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteADC()
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t dst;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regdest);

    dst = dst + (GetC() ? 1 : 0);

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if ((dst == 0100000) && GetC()) new_psw |= PSW_V;
    if ((dst == 0) && GetC()) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteADCB()  // ADCB
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint8_t dst;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetLReg(m_regdest);

    dst = dst + (GetC() ? 1 : 0);

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if ((dst == 0200) && GetC()) new_psw |= PSW_V;
    if ((dst == 0) && GetC()) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteSBC()
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t dst;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regdest);

    dst = dst - (GetC() ? 1 : 0);

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if ((dst == 077777) && GetC()) new_psw |= PSW_V;
    if ((dst == 0177777) && GetC()) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteSBCB()
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint8_t dst;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetLReg(m_regdest);

    dst = dst - (GetC() ? 1 : 0);

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if ((dst == 0177) && GetC()) new_psw |= PSW_V;
    if ((dst == 0377) && GetC()) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteTST()  // TST
{
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t dst;

    if (m_methdest)
    {
        uint16_t ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regdest);

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R[m_methdest]);
}

void CProcessor::ExecuteTSTB()  // TSTB
{
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint8_t dst;

    if (m_methdest)
    {
        uint16_t ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetLReg(m_regdest);

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R[m_methdest]);
}

void CProcessor::ExecuteROR()  // ROR
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t src;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        src = GetReg(m_regdest);

    uint16_t dst = (src >> 1) | (GetC() ? 0100000 : 0);

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (src & 1) new_psw |= PSW_C;
    if (((new_psw & PSW_N) != 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteRORB()  // RORB
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint8_t src;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        src = GetLReg(m_regdest);

    uint8_t dst = (src >> 1) | (GetC() ? 0200 : 0);

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (src & 1) new_psw |= PSW_C;
    if (((new_psw & PSW_N) != 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteROL()  // ROL
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t src, dst;

    if (m_methdest)
    {
        ea = GetWordAddr((uint8_t)m_methdest, (uint8_t)m_regdest);
        if (m_RPLYrq) return;
        src = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        src = GetReg(m_regdest);

    dst = (src << 1) | (GetC() ? 1 : 0);

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (src & 0100000) new_psw |= PSW_C;
    if (((new_psw & PSW_N) != 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteROLB()  // ROLB
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint8_t src, dst;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        src = GetLReg(m_regdest);

    dst = (src << 1) | (GetC() ? 1 : 0);

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (src & 0200) new_psw |= PSW_C;
    if (((new_psw & PSW_N) != 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteASR()  // ASR
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t src, dst;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        src = GetReg(m_regdest);

    dst = (src >> 1) | (src & 0100000);

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (src & 1) new_psw |= PSW_C;
    if (((new_psw & PSW_N) != 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteASRB()  // ASRB
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint8_t src, dst;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        src = GetLReg(m_regdest);

    dst = (src >> 1) | (src & 0200);

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (src & 1) new_psw |= PSW_C;
    if (((new_psw & PSW_N) != 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteASL()  // ASL
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t src, dst;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        src = GetReg(m_regdest);

    dst = src << 1;

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (src & 0100000) new_psw |= PSW_C;
    if (((new_psw & PSW_N) != 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMW[m_methdest]);
}

void CProcessor::ExecuteASLB()  // ASLB
{
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint8_t src, dst;

    if (m_methdest)
    {
        ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        src = GetLReg(m_regdest);

    dst = src << 1;

    if (m_methdest)
        SetByte(ea, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (src & 0200) new_psw |= PSW_C;
    if (((new_psw & PSW_N) != 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
    SetLPSW(new_psw);
    waitstates_add(m_timing->RMWb[m_methdest]);
}

void CProcessor::ExecuteSXT()  // SXT - sign-extend
{
    uint8_t new_psw = GetLPSW() & 0xF9;
    if (m_methdest)
    {
        uint16_t ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        SetWord(ea, GetN() ? 0177777 : 0);
        if (m_RPLYrq) return;
    }
    else
        SetReg(m_regdest, GetN() ? 0177777 : 0); //sign extend

    if (!GetN()) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->W[m_methdest]);
}
#endif


#if 0
void CProcessor::ExecuteMTPS()  // MTPS - move to PS
{
    uint8_t dst;
    if (m_methdest)
    {
        uint16_t ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetByte(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetLReg(m_regdest);

    SetLPSW((GetLPSW() & 0x10) | (dst & 0xEF));
    SetPC(GetPC());
    waitstates_add(m_timing->MTPS[m_methdest]);
}
#else
void CProcessor::ExecuteMTPS()
{
    constexpr unsigned int flags = P_REAbDST;
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    vm2::alu::alu1_s r = vm2::alu::opMTPS({estate.psw, estate.dst.alu_u16});

    estate.psw = r.psw;
    estate.res = r.dst;

    op_exec_finalize<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }
    SetPC(GetPC());

    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it = m_timing->MTPS[md];
    it -= estate.delta;
    waitstates_add(it);
}
#endif

#if 0
void CProcessor::ExecuteMFPS()  // MFPS - move from PS
{
    uint8_t psw = GetLPSW();
    uint8_t new_psw = psw & 0xF1;

    if (m_methdest)
    {
        uint16_t ea = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        GetByte(ea);
        if (m_RPLYrq) return;
        SetByte(ea, psw);
        if (m_RPLYrq) return;
    }
    else
        SetReg(m_regdest, (uint16_t)(signed short)(char)psw); //sign extend

    if (psw & 0200) new_psw |= PSW_N;
    if (psw == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->MFPS[m_methdest]);
}
#else
void CProcessor::ExecuteMFPS()
{
    constexpr unsigned int flags = P_RMWbDST | P_IGBDR;
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    vm2::alu::alu1_s r = vm2::alu::opMFPS({estate.psw, estate.dst.alu_u16});

    estate.psw = r.psw;
    estate.res = r.dst;

    op_exec_finalize<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it = m_timing->MFPS[md];
    it -= estate.delta;
    waitstates_add(it);
}
#endif

template<vm2::alu::cond_fn fn>
void CProcessor::op_branch()
{
    bool taken = fn(GetPSW());
    if (taken) {
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
        waitstates_add(m_timing->Bxx[1]);
    }
    else
        waitstates_add(m_timing->Bxx[0]);
}

#if PROCESSOR_USE_NEW_ALU == 0
void CProcessor::ExecuteBR()
{
    SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    waitstates_add(m_timing->Bxx[1]);
}

void CProcessor::ExecuteBNE()
{
    if (GetZ())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
        waitstates_add(m_timing->Bxx[1]);
    }
}

void CProcessor::ExecuteBEQ()
{
    if (!GetZ())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBGE()
{
    if (GetN() != GetV())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBLT()
{
    if (GetN() == GetV())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBGT()
{
    if ((GetN() != GetV()) || GetZ())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBLE()
{
    if (! ((GetN() != GetV()) || GetZ()))
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBPL()
{
    if (GetN())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBMI()
{
    if (!GetN())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBHI()
{
    if (GetZ() || GetC())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBLOS()
{
    if (!(GetZ() || GetC()))
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBVC()
{
    if (GetV())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBVS()
{
    if (!GetV())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBHIS()
{
    if (GetC())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteBLO()
{
    if (!GetC())
        waitstates_add(m_timing->Bxx[0]);
    else
    {
        waitstates_add(m_timing->Bxx[1]);
        SetPC(GetPC() + ((short)(char)(uint8_t)(m_instruction & 0xff)) * 2 );
    }
}

void CProcessor::ExecuteXOR()  // XOR
{
    uint16_t dst;
    uint16_t ea = 0;
    uint8_t new_psw = GetLPSW() & 0xF1;

    if (m_methdest)
    {
        ea = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        dst = GetWord(ea);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regdest);

    dst = dst ^ GetReg(m_regsrc);

    if (m_methdest)
        SetWord(ea, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_RMW[0][m_methdest]);
}
#endif

#if 0
void CProcessor::ExecuteMUL()  // MUL - multiply
{
    uint16_t dst = GetReg(m_regsrc);
    uint16_t src, ea = 0;
    int res;
    uint8_t new_psw = GetLPSW() & 0xF0;

    if (m_methdest) ea = GetWordAddr(m_methdest, m_regdest);
    if (m_RPLYrq) return;
    src = m_methdest ? GetWord(ea) : GetReg(m_regdest);
    if (m_RPLYrq) return;

    res = (signed short)dst * (signed short)src;

    SetReg(m_regsrc, (uint16_t)((res >> 16) & 0xffff));
    SetReg(m_regsrc | 1, (uint16_t)(res & 0xffff));

    if (res < 0) new_psw |= PSW_N;
    if (res == 0) new_psw |= PSW_Z;
    if ((res > 32767) || (res < -32768)) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->MUL[m_methdest]);
}
#else
void CProcessor::ExecuteMUL()
{
    constexpr unsigned int flags = P_RDSRC | P_READST | P_AFINV;
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    vm2::alu::alu1_s r = vm2::alu::opMUL({estate.psw, estate.src.alu_u16, estate.dst.alu_u16});

    const unsigned int rb = estate.src.ea.reg();
    const unsigned int rp = rb | 1;

    const unsigned int res = r.dst;

    // NOTE: order of write is important
    SetReg(rb, (res >> 16) & 0xFFFF); // set base reg with high part
    SetReg(rp, (res >>  0) & 0xFFFF); // set pair reg with low part

    // NOTE: there are no 'no-reply' state on writeback (destination is srcR and it's pair)
    // NOTE: update PSW only
    estate.psw = r.psw;
    op_exec_finalize<flags>(estate);

    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it = m_timing->MUL[md];
    it -= estate.delta;
    waitstates_add(it);
}
#endif

#if 0
void CProcessor::ExecuteDIV()  // DIV - divide
{
    uint16_t ea = 0;
    int32_t longsrc;
    int res, res1, src2;
    uint8_t new_psw = GetLPSW() & 0xF0;

    if (m_methdest) ea = GetWordAddr(m_methdest, m_regdest);
    if (m_RPLYrq) return;
    src2 = (int)(signed short)(m_methdest ? GetWord(ea) : GetReg(m_regdest));
    if (m_RPLYrq) return;

    longsrc = (int32_t)(((uint32_t)GetReg(m_regsrc | 1)) | ((uint32_t)GetReg(m_regsrc) << 16));

    waitstates_add(m_timing->DIV[m_methdest]);
    constexpr instime_t DIV_OVERFLOW_ADD { 4.0 };

    if (src2 == 0)
    {
        waitstates_add(DIV_OVERFLOW_ADD);
        new_psw |= (PSW_V | PSW_C); //если делят на 0 -- то устанавливаем V и C
        SetLPSW(new_psw);
        return;
    }
    if ((longsrc == (int32_t)020000000000) && (src2 == -1))
    {
        waitstates_add(DIV_OVERFLOW_ADD);
        new_psw |= PSW_V; // переполняемся, товарищи
        SetLPSW(new_psw);
        return;
    }

    res = longsrc / src2;
    res1 = longsrc % src2;

    if ((res > 32767) || (res < -32768))
    {
        waitstates_add(DIV_OVERFLOW_ADD);
        new_psw |= PSW_V; // переполняемся, товарищи
        SetLPSW(new_psw);
        return;
    }

    SetReg(m_regsrc | 1, res1 & 0177777);
    SetReg(m_regsrc, res & 0177777);

    if (res < 0) new_psw |= PSW_N;
    if (res == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
}
#else
void CProcessor::ExecuteDIV()
{
    constexpr unsigned int flags = P_RDSRC | P_RDSRCPR | P_READST | P_AFINV;
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    vm2::alu::alu1_s r = vm2::alu::opDIV({estate.psw, estate.src.alu_u16, estate.dst.alu_u16});

    const unsigned int rb = estate.src.ea.reg();
    const unsigned int rp = rb | 1;

    const unsigned int res = r.dst;

    // NOTE: completely skip register(s) update on overflow
    if ( (r.psw & PSW_V) == 0 ) {
        // NOTE: order of write is important
        SetReg(rp, (res >>  0) & 0xFFFF); // set pair reg with low part (rem)
        SetReg(rb, (res >> 16) & 0xFFFF); // set base reg with high part (quo)
    }

    // NOTE: there are no 'no-reply' state on writeback (destination is srcR and it's pair)
    // NOTE: update PSW only
    estate.psw = r.psw;
    op_exec_finalize<flags>(estate);

    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it = m_timing->DIV[md];
    it -= estate.delta;
    if (r.psw & PSW_V)
        it += { 4.0 }; // additional 4 cycles on overflow

    waitstates_add(it);
}
#endif

#if 0
void CProcessor::ExecuteASH()  // ASH - arithmetic shift
{
    uint16_t ea = 0;
    short src;
    short dst;
    uint8_t new_psw = GetLPSW() & 0xF0;

    if (m_methdest) ea = GetWordAddr(m_methdest, m_regdest);
    if (m_RPLYrq) return;
    src = (short)(m_methdest ? GetWord(ea) : GetReg(m_regdest));
    if (m_RPLYrq) return;
    src &= 0x3F;
    src |= (src & 040) ? 0177700 : 0;
    dst = (short)GetReg(m_regsrc);

    waitstates_add(m_timing->ASH[m_methdest]);

    constexpr double ASH_S_TIMING = 4.0;

    if (src >= 0)
    {
        waitstates_add(src * ASH_S_TIMING);
        while (src--)
        {
            if (dst & 0100000) new_psw |= PSW_C; else new_psw &= ~PSW_C;
            dst <<= 1;
            if ((dst < 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
        }
    }
    else
    {
        waitstates_add( -( (1 + src) * ASH_S_TIMING ) );
        while (src++)
        {
            if (dst & 1) new_psw |= PSW_C; else new_psw &= ~PSW_C;
            dst >>= 1;
        }
    }

    SetReg(m_regsrc, dst);

    if (dst < 0) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
}
#else
void CProcessor::ExecuteASH()
{
    constexpr unsigned int flags = P_RDSRC | P_READST | P_AFINV;
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    vm2::alu::alu1_s r = vm2::alu::opASH({estate.psw, estate.src.alu_u16, estate.dst.alu_u16});

    const unsigned int rb  = estate.src.ea.reg();
    const unsigned int res = r.dst;

    SetReg(rb, res & 0xFFFF);

    // NOTE: there are no 'no-reply' state on writeback (destination is srcR and it's pair)
    // NOTE: update PSW only
    estate.psw = r.psw;
    op_exec_finalize<flags>(estate);

    constexpr double ASH_S_STEP = 4.0;
    unsigned int shamt  = estate.dst.alu_u16 & 0x3F;
    if (shamt & 0x20)
        shamt ^= 0x3F;

    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it = m_timing->ASH[md];
    it += ASH_S_STEP * shamt;
    it -= estate.delta;
    waitstates_add(it);
}
#endif

#if 0
void CProcessor::ExecuteASHC()  // ASHC - arithmetic shift combined
{
    uint16_t ea = 0;
    int16_t src;
    int32_t dst;
    uint8_t new_psw = GetLPSW() & 0xF0;

    if (m_methdest) ea = GetWordAddr(m_methdest, m_regdest);
    if (m_RPLYrq) return;
    src = (int16_t)(m_methdest ? GetWord(ea) : GetReg(m_regdest));
    if (m_RPLYrq) return;
    src &= 0x3F;
    src |= (src & 040) ? 0177700 : 0;
    dst = ((uint32_t)GetReg(m_regsrc | 1)) | ((uint32_t)GetReg(m_regsrc) << 16);
    waitstates_add(m_timing->ASHC[m_methdest]);
    constexpr double ASHC_S_TIMING = 4.0;

    if (src >= 0)
    {
        waitstates_add(src * ASHC_S_TIMING);
        while (src--)
        {
            if (dst & (int32_t)0x80000000L) new_psw |= PSW_C; else new_psw &= ~PSW_C;
            dst <<= 1;
            if ((dst < 0) != ((new_psw & PSW_C) != 0)) new_psw |= PSW_V;
        }
    }
    else
    {
        waitstates_add( -( (1 + src) * ASHC_S_TIMING ) );

        while (src++)
        {
            if (dst & 1) new_psw |= PSW_C; else new_psw &= ~PSW_C;
            dst >>= 1;
        }
    }

    SetReg(m_regsrc, (uint16_t)((dst >> 16) & 0xffff));
    SetReg(m_regsrc | 1, (uint16_t)(dst & 0xffff));

    SetN(dst < 0);
    SetZ(dst == 0);
    if (dst < 0) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
}
#else
void CProcessor::ExecuteASHC()
{
    constexpr unsigned int flags = P_RDSRC | P_RDSRCPR | P_READST | P_AFINV;
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    vm2::alu::alu1_s r = vm2::alu::opASHC({estate.psw, estate.src.alu_u16, estate.dst.alu_u16});

    const unsigned int rb = estate.src.ea.reg();
    const unsigned int rp = rb | 1;

    const unsigned int res = r.dst;

    // NOTE: order of write is important
    SetReg(rb, (res >> 16) & 0xFFFF); // set base reg with high part
    SetReg(rp, (res >>  0) & 0xFFFF); // set pair reg with low part

    // NOTE: there are no 'no-reply' state on writeback (destination is srcR and it's pair)
    // NOTE: update PSW only
    estate.psw = r.psw;
    op_exec_finalize<flags>(estate);

    constexpr double ASHC_S_STEP = 4.0;
    unsigned int shamt  = estate.dst.alu_u16 & 0x3F;
    if (shamt & 0x20)
        shamt ^= 0x3F;

    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it = m_timing->ASHC[md];
    it += ASHC_S_STEP * shamt;
    it -= estate.delta;
    waitstates_add(it);
}
#endif

void CProcessor::ExecuteSOB()  // SOB - subtract one: R = R - 1 ; if R != 0 : PC = PC - 2*nn
{
    uint16_t dst = GetReg(m_regsrc);
    --dst;
    SetReg(m_regsrc, dst);

    instime_t it = m_timing->SOB[0];
    if (dst)
    {
        it = m_timing->SOB[1];
        SetPC(GetPC() - (m_instruction & (uint16_t)077) * 2 );
    }

    waitstates_add(it);
}

#if PROCESSOR_USE_NEW_ALU == 0
void CProcessor::ExecuteMOV()  // MOV - move
{
    uint16_t src_addr, dst_addr;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint16_t dst;

    if (m_methsrc)
    {
        src_addr = GetWordAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        dst = GetWord(src_addr);
        if (m_RPLYrq) return;
    }
    else
        dst = GetReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        SetWord(dst_addr, dst);
        if (m_RPLYrq) return;
    }
    else
        SetReg(m_regdest, dst);

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_W[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteMOVB()  // MOVB - move byte
{
    uint16_t src_addr, dst_addr;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint8_t dst;

    if (m_methsrc)
    {
        src_addr = GetByteAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        dst = GetByte(src_addr);
        if (m_RPLYrq) return;
    }
    else
        dst = GetLReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        GetByte(dst_addr);
        if (m_RPLYrq) return;
        SetByte(dst_addr, dst);
        if (m_RPLYrq) return;
    }
    else
        SetReg(m_regdest, (uint16_t)(signed short)(char)dst);

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_Wb[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteCMP()  // CMP - compare
{
    uint16_t src_addr, dst_addr;
    uint8_t new_psw = GetLPSW() & 0xF0;

    uint16_t src;
    uint16_t src2;
    uint16_t dst;

    if (m_methsrc)
    {
        src_addr = GetWordAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetWord(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src = GetReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetWord(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetReg(m_regdest);

    dst = src - src2;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (((src ^ src2) & ~(dst ^ src2)) & 0100000) new_psw |= PSW_V;
    if (((~src & src2) | (~(src ^ src2) & dst)) & 0100000) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_R[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteCMPB()  // CMPB - compare byte
{
    uint16_t src_addr, dst_addr;
    uint8_t new_psw = GetLPSW() & 0xF0;

    uint8_t src;
    uint8_t src2;
    uint8_t dst;

    if (m_methsrc)
    {
        src_addr = GetByteAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetByte(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src = GetLReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetByte(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetLReg(m_regdest);

    dst = src - src2;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (((src ^ src2) & ~(dst ^ src2)) & 0200) new_psw |= PSW_V;
    if (((~src & src2) | (~(src ^ src2) & dst)) & 0200) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_R[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteBIT()  // BIT - bit test
{
    uint16_t src_addr, dst_addr;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint16_t src;
    uint16_t src2;
    uint16_t dst;

    if (m_methsrc)
    {
        src_addr = GetWordAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetWord(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src  = GetReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetWord(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetReg(m_regdest);

    dst = src2 & src;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_R[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteBITB()  // BITB - bit test on byte
{
    uint16_t src_addr, dst_addr;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint8_t src;
    uint8_t src2;
    uint8_t dst;

    if (m_methsrc)
    {
        src_addr = GetByteAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetByte(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src = GetLReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetByte(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetLReg(m_regdest);

    dst = src2 & src;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_R[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteBIC()  // BIC - bit clear
{
    uint16_t src_addr, dst_addr = 0;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint16_t src;
    uint16_t src2;
    uint16_t dst;

    if (m_methsrc)
    {
        src_addr = GetWordAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetWord(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src  = GetReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetWord(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetReg(m_regdest);

    dst = src2 & (~src);

    if (m_methdest)
        SetWord(dst_addr, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_RMW[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteBICB()  // BICB - bit clear
{
    uint16_t src_addr, dst_addr = 0;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint8_t src;
    uint8_t src2;
    uint8_t dst;

    if (m_methsrc)
    {
        src_addr = GetByteAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetByte(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src = GetLReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetByte(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetLReg(m_regdest);

    dst = src2 & (~src);


    if (m_methdest)
        SetByte(dst_addr, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_RMWb[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteBIS()  // BIS - bit set
{
    uint16_t src_addr, dst_addr = 0;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint16_t src;
    uint16_t src2;
    uint16_t dst;

    if (m_methsrc)
    {
        src_addr = GetWordAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetWord(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src  = GetReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetWord(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetReg(m_regdest);

    dst = src2 | src;

    if (m_methdest)
        SetWord(dst_addr, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_RMW[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteBISB()  // BISB - bit set on byte
{
    uint16_t src_addr, dst_addr = 0;
    uint8_t new_psw = GetLPSW() & 0xF1;
    uint8_t src;
    uint8_t src2;
    uint8_t dst;

    if (m_methsrc)
    {
        src_addr = GetByteAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetByte(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src = GetLReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetByteAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetByte(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetLReg(m_regdest);

    dst = src2 | src;

    if (m_methdest)
        SetByte(dst_addr, dst);
    else
        SetLReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0200) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_RMWb[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteADD()  // ADD
{
    uint16_t src_addr, dst_addr = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t src, src2, dst;

    if (m_methsrc)
    {
        src_addr = GetWordAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetWord(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src = GetReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetWord(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetReg(m_regdest);

    dst = src2 + src;

    if (m_methdest)
        SetWord(dst_addr, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if ((~(src ^ src2) & (dst ^ src2)) & 0100000) new_psw |= PSW_V;
    if (((src & src2) | ((src ^ src2) & ~dst)) & 0100000) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_RMW[m_methsrc][m_methdest]);
}

void CProcessor::ExecuteSUB()  // SUB
{
    uint16_t src_addr, dst_addr = 0;
    uint8_t new_psw = GetLPSW() & 0xF0;
    uint16_t src, src2, dst;

    if (m_methsrc)
    {
        src_addr = GetWordAddr(m_methsrc, m_regsrc);
        if (m_RPLYrq) return;
        src = GetWord(src_addr);
        if (m_RPLYrq) return;
    }
    else
        src = GetReg(m_regsrc);

    if (m_methdest)
    {
        dst_addr = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;
        src2 = GetWord(dst_addr);
        if (m_RPLYrq) return;
    }
    else
        src2 = GetReg(m_regdest);

    dst = src2 - src;

    if (m_methdest)
        SetWord(dst_addr, dst);
    else
        SetReg(m_regdest, dst);
    if (m_RPLYrq) return;

    if (dst & 0100000) new_psw |= PSW_N;
    if (dst == 0) new_psw |= PSW_Z;
    if (((src ^ src2) & ~(dst ^ src)) & 0100000) new_psw |= PSW_V;
    if (((src & ~src2) | (~(src ^ src2) & dst)) & 0100000) new_psw |= PSW_C;
    SetLPSW(new_psw);
    waitstates_add(m_timing->R_RMW[m_methsrc][m_methdest]);
}
#endif

void CProcessor::ExecuteEMT()  // EMT - emulator trap
{
    m_EMT_rq = true;
//    waitstates_add(m_timing->SINT);
}

void CProcessor::ExecuteTRAP()
{
    m_TRAPrq = true;
//    waitstates_add(m_timing->SINT);
}

#if 0
void CProcessor::ExecuteJSR()  // JSR - jump subroutine: *--SP = R; R = PC; PC = &d (a-mode > 0)
{
    if (m_methdest == 0)
    {
        // Неправильный метод адресации
        m_ILLGrq = true;
        //waitstates_add(m_timing->SINT);
    }
    else
    {
        uint16_t dst;
        dst = GetWordAddr(m_methdest, m_regdest);
        if (m_RPLYrq) return;

        SetSP( GetSP() - 2 );
        SetWord( GetSP(), GetReg(m_regsrc) );
        SetReg(m_regsrc, GetPC());
        SetPC(dst);
        if (m_RPLYrq) return;

        waitstates_add(m_timing->JSR[m_methdest]);
    }
}
#else
void CProcessor::ExecuteJSR()
{
    constexpr unsigned int flags = P_RDSRC | P_EADST | P_NOPSW | P_AFINV;
    x_estate_s estate;
    op_exec_prepare<flags>(estate);
    if (m_RPLYrq) {
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    if (estate.dst.ea.is_reg()) {
        m_ILLGrq = true;
        return;
    }

    const unsigned int addr = estate.dst.ea.addr();

    unsigned int sp = GetSP();
    sp -= 2;
    SetSP(sp);
    rsp_s rsp = bus_write_raw(sp, estate.src.u16);

    unsigned int pc = GetPC();
    SetReg(estate.src.ea.reg(), pc);
    SetPC(addr);

    if (rsp.is_noreply()) {
        m_RPLYrq = true;
        waitstates_set(VM2_RPLY_TIMEOUT);
        return;
    }

    op_exec_finalize<flags>(estate);
    const unsigned int md = (flags & P_EADST) ? m_methdest : 0;
    instime_t it = m_timing->JSR[md];
    it -= estate.delta;
    waitstates_add(it);
}
#endif
void CProcessor::ExecuteMARK ()  // MARK
{
    SetSP( GetPC() + (m_instruction & 0x003F) * 2 );
    SetPC( GetReg(5) );
    SetReg(5, GetWord( GetSP() ));
    SetSP( GetSP() + 2 );
    if (m_RPLYrq) return;

    waitstates_add(m_timing->MARK);
}


//////////////////////////////////////////////////////////////////////
//
// CPU image format (64 bytes):
//   2   bytes      PSW
//   2*8 bytes      Registers R0..R7
//   2*2 bytes      Saved PC and PSW
//   2   byte       Stopped flag: 1 - stopped, 0 - not stopped
//   2   bytes      Internal tick count
//   3   bytes      Flags
//   1   byte       VIRQ reset request
//   2   bytes      Reserved
//  32   bytes      VIRQ vectors

void CProcessor::SaveToImage(uint8_t* pImage) const
{
    // Processor data                               // Offset Size
    uint16_t* pwImage = (uint16_t*) pImage;         //    0    --
    *pwImage++ = m_psw;                             //    0     2   PSW
    memcpy(pwImage, m_R, 2 * 8);  pwImage += 8;     //    2    16   Registers R0-R7
    *pwImage++ = m_savepc;                          //   18     2   PC'
    *pwImage++ = m_savepsw;                         //   20     2   PSW'
    *pwImage++ = (m_okStopped ? 1 : 0);             //   22     2   Stopped
    *pwImage++ = 0;//TODO: insert real value m_internalTick;                    //   24     2   Internal tick count
    uint8_t* pbImage = (uint8_t*) pwImage;
    uint8_t flags0 = 0;
    flags0 |= (m_stepmode ?   1 : 0);
    flags0 |= (m_buserror ?   2 : 0);
    flags0 |= (m_haltpin  ?   4 : 0);
    flags0 |= (m_DCLOpin  ?   8 : 0);
    flags0 |= (m_ACLOpin  ?  16 : 0);
    flags0 |= (m_waitmode ?  32 : 0);
    *pbImage++ = flags0;                            //   26     1   Flags
    uint8_t flags1 = 0;
    flags1 |= (m_STRTrq ?   1 : 0);
    flags1 |= (m_RPLYrq ?   2 : 0);
    flags1 |= (m_ILLGrq ?   4 : 0);
    flags1 |= (m_RSVDrq ?   8 : 0);
    flags1 |= (m_TBITrq ?  16 : 0);
    flags1 |= (m_ACLOrq ?  32 : 0);
    flags1 |= (m_HALTrq ?  64 : 0);
    flags1 |= (m_EVNTrq ? 128 : 0);
    *pbImage++ = flags1;                            //   27     1   Flags
    uint8_t flags2 = 0;
    flags2 |= (m_FIS_rq ?   1 : 0);
    flags2 |= (m_BPT_rq ?   2 : 0);
    flags2 |= (m_IOT_rq ?   4 : 0);
    flags2 |= (m_EMT_rq ?   8 : 0);
    flags2 |= (m_TRAPrq ?  16 : 0);
    flags2 |= (m_ACLOreset ? 32 : 0);
    flags2 |= (m_EVNTreset ? 64 : 0);
    *pbImage++ = flags2;                            //   28     1   Flags
    *pbImage++ = m_VIRQreset;                       //   29     1   VIRQ reset request
    //                                              //   30     2   Reserved
    memcpy(pImage + 32, m_virq, 2 * 16);            //   32    32   VIRQ vectors
}

void CProcessor::LoadFromImage(const uint8_t* pImage)
{
    const uint16_t* pwImage = (const uint16_t*) pImage;  //    0    --
    m_psw = *pwImage++;                             //    0     2   PSW
    memcpy(m_R, pwImage, 2 * 8);  pwImage += 8;     //    2    16   Registers R0-R7
    m_savepc    = *pwImage++;                       //   18     2   PC'
    m_savepsw   = *pwImage++;                       //   20     2   PSW'
    m_okStopped = (*pwImage++ != 0);                //   22     2   Stopped
    /* m_internalTick = */ *pwImage++; // TODO: insert real value                     //   24     2   Internal tick count
    const uint8_t* pbImage = (const uint8_t*) pwImage;
    uint8_t flags0 = *pbImage++;                    //   26     1   Flags
    m_stepmode  = ((flags0 &  1) != 0);
    m_buserror  = ((flags0 &  2) != 0);
    m_haltpin   = ((flags0 &  4) != 0);
    m_DCLOpin   = ((flags0 &  8) != 0);
    m_ACLOpin   = ((flags0 & 16) != 0);
    m_waitmode  = ((flags0 & 32) != 0);
    uint8_t flags1 = *pbImage++;                    //   27     1   Flags
    m_STRTrq    = ((flags1 &   1) != 0);
    m_RPLYrq    = ((flags1 &   2) != 0);
    m_ILLGrq    = ((flags1 &   4) != 0);
    m_RSVDrq    = ((flags1 &   8) != 0);
    m_TBITrq    = ((flags1 &  16) != 0);
    m_ACLOrq    = ((flags1 &  32) != 0);
    m_HALTrq    = ((flags1 &  64) != 0);
    m_EVNTrq    = ((flags1 & 128) != 0);
    uint8_t flags2 = *pbImage++;                    //   28     1   Flags
    m_FIS_rq    = ((flags2 &  1) != 0);
    m_BPT_rq    = ((flags2 &  2) != 0);
    m_IOT_rq    = ((flags2 &  4) != 0);
    m_EMT_rq    = ((flags2 &  8) != 0);
    m_TRAPrq    = ((flags2 & 16) != 0);
    m_ACLOreset = ((flags2 & 32) != 0);
    m_EVNTreset = ((flags2 & 64) != 0);
    m_VIRQreset = *pbImage++;                       //   29     1   VIRQ reset request
    //                                              //   30     2   Reserved
    memcpy(m_virq, pImage + 32, 2 * 16);            //   32    32   VIRQ vectors
}

#if 0
uint16_t CProcessor::GetWordAddr (uint8_t meth, uint8_t reg)
{
    switch (meth)
    {
    case 1:   //(R)
        return GetReg(reg);
    case 2:   //(R)+
        {
            uint16_t addr = GetReg(reg);
            SetReg(reg, addr + 2);
            return addr;
        }
    case 3:  //@(R)+
        {
            uint16_t addr = GetReg(reg);
            SetReg(reg, addr + 2);
            return GetWord(addr);
        }
    case 4: //-(R)
        SetReg(reg, GetReg(reg) - 2);
        return GetReg(reg);
    case 5: //@-(R)
        {
            SetReg(reg, GetReg(reg) - 2);
            uint16_t addr = GetReg(reg);
            return GetWord(addr);
        }
    case 6: //d(R)
        {
            uint16_t addr = GetWord(GetPC());
            SetPC(GetPC() + 2);
            return GetReg(reg) + addr;
        }
    case 7: //@d(r)
        {
            uint16_t addr = GetWord(GetPC());
            SetPC(GetPC() + 2);
            addr = GetReg(reg) + addr;
            if (!m_RPLYrq)
                return GetWord(addr);
            return addr;
        }
    }
    return 0;
}

uint16_t CProcessor::GetByteAddr (uint8_t meth, uint8_t reg)
{
    uint16_t addr;

    addr = 0;
    switch (meth)
    {
    case 1:
        addr = GetReg(reg);
        break;
    case 2:
        addr = GetReg(reg);
        SetReg(reg, addr + (reg < 6 ? 1 : 2));
        break;
    case 3:
        addr = GetReg(reg);
        SetReg(reg, addr + 2);
        addr = GetWord(addr);
        break;
    case 4:
        SetReg(reg, GetReg(reg) - (reg < 6 ? 1 : 2));
        addr = GetReg(reg);
        break;
    case 5:
        SetReg(reg, GetReg(reg) - 2);
        addr = GetReg(reg);
        addr = GetWord(addr);
        break;
    case 6: //d(R)
        addr = GetWord(GetPC());
        SetPC(GetPC() + 2);
        addr = GetReg(reg) + addr;
        break;
    case 7: //@d(r)
        addr = GetWord(GetPC());
        SetPC(GetPC() + 2);
        addr = GetReg(reg) + addr;
        if (!m_RPLYrq) addr = GetWord(addr);
        break;
    }

    return addr;
}
#endif

//////////////////////////////////////////////////////////////////////
