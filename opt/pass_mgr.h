/*@
XOC Release License

Copyright (c) 2013-2014, Alibaba Group, All rights reserved.

    compiler@aliexpress.com

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Su Zhenyu nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

author: Su Zhenyu
@*/
#ifndef __PASS_MGR_H__
#define __PASS_MGR_H__

namespace xoc {

//Time Info.
#define TI_pn(ti)        (ti)->pass_name
#define TI_pt(ti)        (ti)->pass_time
class TimeInfo {
public:
    CHAR const* pass_name;
    ULONGLONG pass_time;

public:
    COPY_CONSTRUCTOR(TimeInfo);
};

typedef TMap<PASS_TYPE, Pass*> PassTab;
typedef TMapIter<PASS_TYPE, Pass*> PassTabIter;
typedef TMap<PASS_TYPE, xcom::Graph*> GraphPassTab;
typedef TMapIter<PASS_TYPE, xcom::Graph*> GraphPassTabIter;

class PassMgr {
protected:
    List<TimeInfo*> m_ti_list;
    SMemPool * m_pool;
    Region * m_rg;
    RegionMgr * m_rumgr;
    TypeMgr * m_tm;
    CDG * m_cdg;
    PassTab m_registered_pass;
    GraphPassTab m_registered_graph_based_pass;

protected:
    void * xmalloc(size_t size)
    {
        void * p = smpoolMalloc(size, m_pool);
        if (p == NULL) return NULL;
        ::memset(p, 0, size);
        return p;
    }
    xcom::Graph * registerGraphBasedPass(PASS_TYPE opty);

public:
    PassMgr(Region * rg);
    COPY_CONSTRUCTOR(PassMgr);
    virtual ~PassMgr()
    {
        destroyAllPass();
        smpoolDelete(m_pool);
    }

    void appendTimeInfo(CHAR const* pass_name, ULONGLONG t)
    {
        TimeInfo * ti = (TimeInfo*)xmalloc(sizeof(TimeInfo));
        TI_pn(ti) = pass_name;
        TI_pt(ti) = t;
        m_ti_list.append_tail(ti);
    }

    virtual xcom::Graph * allocCDG();
    virtual Pass * allocCFG();
    virtual Pass * allocAA();
    virtual Pass * allocDUMgr();
    virtual Pass * allocCopyProp();
    virtual Pass * allocGCSE();
    virtual Pass * allocLCSE();
    virtual Pass * allocRP();
    virtual Pass * allocPRE();
    virtual Pass * allocIVR();
    virtual Pass * allocLICM();
    virtual Pass * allocDCE();
    virtual Pass * allocDSE();
    virtual Pass * allocRCE();
    virtual Pass * allocGVN();
    virtual Pass * allocLoopCvt();
    virtual Pass * allocPRSSAMgr();
    virtual Pass * allocMDSSAMgr();
    virtual Pass * allocCCP();
    virtual Pass * allocExprTab();
    virtual Pass * allocCfsMgr();
    virtual Pass * allocIPA();
    virtual Pass * allocInliner();
    virtual Pass * allocRefineDUChain();

    void destroyAllPass();
    void destroyPass(Pass * pass);
    void destroyPass(PASS_TYPE passtype);

    void dump_pass_time_info()
    {
        if (g_tfile == NULL) { return; }
        note("\n==---- PASS TIME INFO ----==");
        for (TimeInfo * ti = m_ti_list.get_head(); ti != NULL;
             ti = m_ti_list.get_next()) {
            note("\n * %s --- use %llu ms ---",
                    TI_pn(ti), TI_pt(ti));
        }
        note("\n===----------------------------------------===");
        fflush(g_tfile);
    }

    Pass * registerPass(PASS_TYPE opty);

    Pass * queryPass(PASS_TYPE opty)
    {
        if (opty == PASS_CDG) {
            return (Pass*)m_registered_graph_based_pass.get(opty);
        }
        return m_registered_pass.get(opty);
    }

    virtual void performScalarOpt(OptCtx & oc);
};

} //namespace xoc
#endif
