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
#include "cominc.h"

namespace xoc {

//
//START CDG
//
void CDG::dumpDOT(CHAR const* name) const
{
    if (name == nullptr) {
        name = "graph_cdg.dot";
    }
    Graph::dumpDOT(name);
}


void CDG::dumpVCG(CHAR const* name) const
{
    ASSERTN(m_ec_pool != nullptr, ("not yet initialized."));
    if (name == nullptr) {
        name = "graph_cdg.vcg";
    }
    Graph::dumpVCG(name);
}


bool CDG::dump() const
{
    if (!getRegion()->isLogMgrInit()) { return true; }
    note(getRegion(), "\n==---- DUMP Control Dependence '%s' ----==",
         m_rg->getRegionName());
    INT c;
    for (xcom::Vertex * v = get_first_vertex(c);
         v != nullptr; v = get_next_vertex(c)) {
        xcom::EdgeC * in = VERTEX_in_list(v);
        if (in == nullptr) {
            note(getRegion(), "\nBB%d has NO ctrl BB", v->id());
            continue;
        }
        note(getRegion(), "\nBB%d ctrl BB is: ", v->id());
        while (in != nullptr) {
            xcom::Vertex * pred = in->getFrom();
            prt(getRegion(), "%d,", pred->id());
            in = EC_next(in);
        }
    }
    note(getRegion(), "\n");
    return true;
}


void CDG::get_cd_preds(UINT id, OUT List<xcom::Vertex*> & lst)
{
    xcom::Vertex * v = getVertex(id);
    ASSERT0(v != nullptr);
    xcom::EdgeC * in = VERTEX_in_list(v);
    while (in != nullptr) {
        xcom::Vertex * pred = in->getFrom();
        lst.append_tail(pred);
        in = EC_next(in);
    }
}


//Return true if vertex b is control dependent on vertex a.
//e.g:a
//    |\
//    | b
//    |/
//    end
// a controls b.
bool CDG::is_control(Vertex const* a, Vertex const* b) const
{
    ASSERT0(a && b);
    xcom::EdgeC * out = a->getOutList();
    while (out != nullptr) {
        if (out->getTo() == b) {
            return true;
        }
        out = out->get_next();
    }
    return false;
}


//Return true if vertex is only control itself.
bool CDG::is_only_control_self(UINT vid) const
{
    Vertex const* v = getVertex(vid);
    ASSERT0(v);
    xcom::EdgeC * out = v->getOutList();
    while (out != nullptr) {
        xcom::Vertex * succ = out->getTo();
        if (succ != v) { return false; }
        out = out->get_next();
    }
    return true;
}


void CDG::get_cd_succs(UINT id, OUT List<xcom::Vertex*> & lst)
{
    xcom::Vertex * v = getVertex(id);
    ASSERT0(v != nullptr);
    xcom::EdgeC * out = VERTEX_out_list(v);
    while (out != nullptr) {
        xcom::Vertex * succ = out->getTo();
        lst.append_tail(succ);
        out = out->get_next();
    }
}


void CDG::rebuild(MOD OptCtx & oc, xcom::DGraph & cfg)
{
    erase();
    build(oc, cfg);
}


void CDG::build(MOD OptCtx & oc, xcom::DGraph & cfg)
{
    if (cfg.getVertexNum() == 0) { return; }

    START_TIMER(t, "Build CDG");
    ASSERT0(oc.is_cfg_valid());
    m_rg->getPassMgr()->checkValidAndRecompute(&oc, PASS_PDOM, PASS_UNDEF);

    START_TIMER(t2, "Build CDG: Get Dom Tree");
    xcom::Graph pdom_tree;
    cfg.get_pdom_tree(pdom_tree);
    pdom_tree.reverseEdges();
    END_TIMER(t2, "Build CDG: Get Dom Tree");

    if (pdom_tree.getVertexNum() == 0) {
        END_TIMER(t, "Build CDG");
        return;
    }

    Vector<Vertex*> top_order;
    bool has_cyc = pdom_tree.sortInTopologOrder(top_order);
    CHECK0_DUMMYUSE(!has_cyc);

    xcom::DefMiscBitSetMgr bs_mgr;

    //Record vertex set by which current vertex controlled.
    Vector<xcom::DefSBitSet*> cd_set;
    for (INT j = 0; j <= top_order.get_last_idx(); j++) {
        UINT ii = top_order.get(j)->id();
        xcom::Vertex const* v = cfg.getVertex(ii);
        ASSERT0(v != nullptr);
        addVertex(v->id());

        //Get control-set of v.
        xcom::DefSBitSet * cd_of_v = cd_set.get(v->id());
        if (cd_of_v == nullptr) {
            cd_of_v = bs_mgr.allocSBitSet();
            cd_set.set(v->id(), cd_of_v);
        }

        //Predecessor controls current vertex if
        //vertex is not ipdom of predecessor.
        for (xcom::EdgeC const* in = v->getInList();
             in != nullptr; in = in->get_next()) {
            xcom::Vertex const* pred = in->getFrom();
            if (v->id() == cfg.get_ipdom(pred->id())) { continue; }
            cd_of_v->bunion(pred->id());
            if (m_allow_cycle || pred != v) {
                addEdge(pred->id(), v->id());
            }
        }

        //Transfer controlling to vertex in control-set of current vertex by
        //iterating each vertex whose ipdom is current vex.
        Vertex const* z = pdom_tree.getVertex(v->id());
        ASSERT0(z);

        for (xcom::EdgeC const* zec = z->getInList();
             zec != nullptr; zec = zec->get_next()) {
            Vertex const* pred = zec->getFrom();
            ASSERT0(cfg.get_ipdom(pred->id()) == v->id());

            //Get control-set of pred.
            xcom::DefSBitSet * cd_of_pred = cd_set.get(pred->id());
            if (cd_of_pred == nullptr) { continue; }
            xcom::DefSBitSetIter it = nullptr;
            for (INT i = cd_of_pred->get_first(&it); i != -1;
                 i = cd_of_pred->get_next(i, &it)) {
                if (v->id() == cfg.get_ipdom(i)) { continue; }
                cd_of_v->bunion(i);
                if (m_allow_cycle || i != (INT)v->id()) {
                    addEdge(i, v->id());
                }
            }
        }
    }
    OC_is_cdg_valid(oc) = true;
    END_TIMER(t, "Build CDG");

    for (INT i = 0; i <= cd_set.get_last_idx(); i++) {
        xcom::DefSBitSet * sbs = cd_set.get(i);
        if (sbs != nullptr) {
            sbs->clean();
        }
    }
    if (g_dump_opt.isDumpAfterPass() && g_dump_opt.isDumpCDG()) {
        START_TIMER(t3, "Build CDG:dump");
        dump();
        END_TIMER(t3, "Build CDG:dump");
    }
}


bool CDG::perform(OptCtx & oc)
{
    IRCFG * cfg = m_rg->getCFG();
    if (cfg == nullptr) { return false; }
    rebuild(oc, *cfg);
    return false;
}
//END CDG

} //namespace xoc
