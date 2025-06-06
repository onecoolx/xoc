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
#include "comopt.h"

namespace xoc {

class DOTDesc {
public:
    UINT fontsize;
    CHAR const* shape;
    CHAR const* font;
    CHAR const* color;
    CHAR const* style;
public:
    DOTDesc()
    {
        fontsize = 12;
        shape = "box";
        font = "courB";
        color = "black";
        style = "bold";
    }
};


class VCGDesc {
public:
    UINT vertical_order;
    UINT scale;
    CHAR const* shape;
    CHAR const* font;
    CHAR const* color;
public:
    VCGDesc()
    {
        vertical_order = 1;
        scale = 1;
        shape = "box";
        font = "courB";
        color = "gold";
    }
};


//Dump the node of DOT with detail.
static void dumpVertexDetail(
    xcom::Vertex const* v, IRBB const* bb, DOTDesc const& dd,
    IRCFG const* cfg, bool dump_mdssa, MDSSAMgr const* mdssamgr, FILE * h)
{
    bool is_mdssa_valid = mdssamgr != nullptr && mdssamgr->is_valid();
    ASSERT0(h);
    xcom::StrBuf namebuf(32);
    fprintf(h,
            "\nnode%d [font=\"%s\",fontsize=%u,color=%s,"
            "shape=%s,style=%s,label=\"BB%u ",
            v->id(), dd.font, dd.fontsize, dd.color, dd.shape, dd.style,
            v->id());
    IRBB * pbb = const_cast<IRBB*>(bb);
    dumpBBLabel(pbb->getLabelList(), cfg->getRegion());
    fprintf(h, " ");
    fprintf(h, "rpo:%d ", bb->rpo());

    //Dump fallthrough attr.
    if (bb->is_fallthrough()) {
        BBListIter it;
        cfg->getBBList()->find(bb, &it);
        ASSERT0(it);
        IRBB const* next = cfg->getBBList()->get_next(&it);
        if (next != nullptr) {
            fprintf(h, "fallthrough:BB%u ", next->id());
        }
    }

    //Dump MDSSA Phi List.
    if (dump_mdssa && is_mdssa_valid) {
        ASSERT0(mdssamgr);
        MDPhiList const* philist = mdssamgr->getPhiList(bb);
        if (philist != nullptr) {
            mdssamgr->dumpPhiList(philist);
        }
    }

    //Dump IR list.
    for (IR * ir = BB_first_ir(pbb); ir != nullptr; ir = BB_next_ir(pbb)) {
        //The first \l is very important to display
        //DOT in a fine manner.
        fprintf(h, "\\l");

        //TODO: implement dump_ir_buf();
        if (dump_mdssa && is_mdssa_valid) {
            ASSERT0(mdssamgr);
            mdssamgr->dumpIRWithMDSSA(ir, DumpFlag::combineIRID(IR_DUMP_KID));
        } else {
            dumpIR(ir, cfg->getRegion(), nullptr,
                   DumpFlag::combineIRID(IR_DUMP_KID));
        }
    }

    //The last \l is very important to display DOT file in a fine manner.
    //Print carriage-return for DOT file, use \l instead of \n to
    //avoid DOT complaint.
    fprintf(h, "\\l");

    //The end char of properties.
    fprintf(h, "\"];");
    fflush(h);
}


//Dump dot-node without detail.
static void dumpVertexBrief(xcom::Vertex const* v, DOTDesc const& dd, FILE * h)
{
    ASSERT0(h);
    fprintf(h,
            "\nnode%d [font=\"%s\",fontsize=%d,color=%s,"
            "shape=%s,style=%s,label=\"BB%d\"];",
            v->id(), dd.font, dd.fontsize, dd.color, dd.shape, dd.style,
            v->id());
    fflush(h);
}


static void dumpVertex(
    xcom::Vertex const* v, UINT flag, IRCFG const* cfg, bool dump_detail,
    bool dump_mdssa, MDSSAMgr const* mdssamgr, FILE * h)
{
    //Set dot-node properties.
    DOTDesc dd;
    IRBB * bb = cfg->getBB(v->id());
    ASSERT0(bb);
    if (bb->is_catch_start()) {
        dd.font = "Times Bold";
        dd.fontsize = 18;
        dd.color = "lightblue";
        dd.style = "filled";
    }
    if (bb->is_entry() || bb->is_exit()) {
        dd.font = "Times Bold";
        dd.fontsize = 18;
        dd.color = "cyan";
        dd.style = "filled";
    }
    if (dump_detail) {
        dumpVertexDetail(v, bb, dd, cfg, dump_mdssa, mdssamgr, h);
        return;
    }
    dumpVertexBrief(v, dd, h);
}


static void dumpDotNodeOutOfSubGraph(
    BBSet const& subset, FILE * h, UINT flag, IRCFG const* cfg)
{
    bool dump_detail = false;
    bool dump_mdssa = false;
    //Record the predecessors of v in subset which does not belong to subset.
    xcom::TTab<VexIdx> printed;
    BBSetIter it;
    for (BSIdx i = subset.get_first(&it);
         i != BS_UNDEF; i = subset.get_next(i, &it)) {
        xcom::Vertex const* vex = cfg->getVertex(i);
        ASSERT0(vex);
        AdjVertexIter iti;
        for (Vertex const* in = Graph::get_first_in_vertex(vex, iti);
             in != nullptr; in = Graph::get_next_in_vertex(iti)) {
            if (subset.is_contain(in->id())) { continue; }
            if (printed.find(in->id())) { continue; }
            dumpVertex(in, flag, cfg, dump_detail, dump_mdssa, nullptr, h);
            printed.append(in->id());
        }
        AdjVertexIter ito;
        for (Vertex const* out = Graph::get_first_out_vertex(vex, ito);
             out != nullptr; out = Graph::get_next_out_vertex(ito)) {
            if (subset.is_contain(out->id())) { continue; }
            if (printed.find(out->id())) { continue; }
            dumpVertex(out, flag, cfg, dump_detail, dump_mdssa, nullptr, h);
            printed.append(out->id());
        }
    }
}


static void dumpDotNodeInSubGraph(
    BBSet const& subset, FILE * h, UINT flag, IRCFG const* cfg)
{
    bool dump_mdssa = HAVE_FLAG(flag, IRCFG::DUMP_MDSSA);
    MDSSAMgr const* mdssamgr = (MDSSAMgr const*)cfg->getRegion()->getMDSSAMgr();
    if (mdssamgr == nullptr || !mdssamgr->is_valid()) {
        dump_mdssa = false;
    }
    bool dump_detail = HAVE_FLAG(flag, IRCFG::DUMP_DETAIL);

    //Print node
    BBSetIter it;
    for (BSIdx i = subset.get_first(&it);
         i != BS_UNDEF; i = subset.get_next(i, &it)) {
        xcom::Vertex const* v = cfg->getVertex(i);
        ASSERT0(v);
        dumpVertex(v, flag, cfg, dump_detail, dump_mdssa, mdssamgr, h);
    }
    //Print node that surrounding the node in subset.
    dumpDotNodeOutOfSubGraph(subset, h, flag, cfg);
}


static void dumpDotEdgeImpl(
    xcom::Edge const* e, FILE * h, UINT flag, UINT * pos)
{
    bool dump_eh = HAVE_FLAG(flag, IRCFG::DUMP_EH);
    CFGEdgeInfo const* ei = (CFGEdgeInfo const*)e->info();
    if (ei == nullptr) {
        if (pos != nullptr) {
            fprintf(h, "\nnode%d->node%d[style=bold, "
                       "color=maroon, label=\"p%d %s\"]",
                       e->from()->id(), e->to()->id(), *pos, "");
        } else {
            fprintf(h, "\nnode%d->node%d[style=bold, "
                       "color=maroon, label=\"%s\"]",
                       e->from()->id(), e->to()->id(), "");
        }
        return;
    }
    if (ei->is_eh()) {
        if (dump_eh) {
            if (pos != nullptr) {
                fprintf(h, "\nnode%d->node%d[style=dotted, "
                           "color=darkslategray, label=\"p%d %s\"]",
                           e->from()->id(), e->to()->id(), *pos, "");
            } else {
                fprintf(h, "\nnode%d->node%d[style=dotted, "
                           "color=darkslategray, label=\"%s\"]",
                           e->from()->id(), e->to()->id(), "");
            }
        }
        return;
    }
    ASSERTN(0, ("unsupported CFGEdgeInfo"));
}


static void dumpDotEdgeInSubGraph(
    BBSet const& subset, FILE * h, UINT flag, IRCFG const* cfg)
{
    xcom::TTab<xcom::Edge const*> printed;
    BBSetIter it;
    for (BSIdx i = subset.get_first(&it);
         i != BS_UNDEF; i = subset.get_next(i, &it)) {
        xcom::Vertex const* vex = cfg->getVertex(i);
        ASSERT0(vex);
        UINT pos = 0;
        for (xcom::EdgeC const* ec = vex->getInList();
             ec != nullptr; ec = ec->get_next(), pos++) {
            xcom::Edge const* e = ec->getEdge();
            if (printed.find(e)) { continue; }
            dumpDotEdgeImpl(e, h, flag, &pos);
            printed.append(e);
        }
        for (xcom::EdgeC const* ec = vex->getOutList();
             ec != nullptr; ec = ec->get_next()) {
            xcom::Edge const* e = ec->getEdge();
            if (printed.find(e)) { continue; }
            dumpDotEdgeImpl(e, h, flag, nullptr);
            printed.append(e);
        }
    }
}


//
//START Lab2BB
//
void Lab2BB::dump(Region * rg) const
{
    note(rg, "\n==-- DUMP LAB2BB --==");
    Lab2BBIter it;
    IRBB * bb;
    for (LabelInfo const* li = get_first(it, &bb);
         !it.end(); li = get_next(it, &bb)) {
        note(rg, "\n");
        li->dumpName(rg);
        ASSERT0(bb);
        prt(rg, ":BB%u", bb->id());
    }
}
//END Lab2BB


//IRCFG
IRCFG::IRCFG(BBList * bbl, Region * rg, UINT vertex_hash_size)
    : Pass(rg), OptimizedCFG<IRBB, IR>(bbl, vertex_hash_size)
{
    m_tm = rg->getTypeMgr();
    ASSERT0(rg->getBBMgr());
    setBitSetMgr(rg->getBitSetMgr());
    ASSERT0(getEntry() == nullptr);
    initEntryAndExit();
}


IRCFG::IRCFG(IRCFG const& src, BBList * bbl,
             bool clone_edge_info, bool clone_vex_info)
    : Pass(src.getRegion()), OptimizedCFG<IRBB, IR>(bbl, src.m_vex_hash_size)
{
    clone(src, clone_edge_info, clone_vex_info);
}


//Control flow optimization
void IRCFG::cf_opt()
{
    bool change = true;
    while (change) {
        change = false;
        BBList * bbl = getBBList();
        for (IRBB * bb = bbl->get_head(); bb != nullptr; bb = bbl->get_next()) {
            change = goto_opt(bb);
            if (change) { break; }
            change = if_opt(bb);
            if (change) { break; }
        }
    }
}


//Construct EH edge after cfg built.
void IRCFG::buildEHEdge()
{
    ASSERTN(m_bb_list, ("bb_list is emt"));
    BBListIter bbit;
    for (m_bb_list->get_head(&bbit);
         bbit != m_bb_list->end(); bbit = m_bb_list->get_next(bbit)) {
        IRBB const* bb = bbit->val();
        IRListIter irit;
        IR * x = const_cast<IRBB*>(bb)->getIRList().get_tail(&irit);
        if (x != nullptr && x->isMayThrow(true) && x->getAI() != nullptr) {
            EHLabelAttachInfo const* ehlab =
                (EHLabelAttachInfo const*)x->getAI()->get(AI_EH_LABEL);
            if (ehlab == nullptr) { continue; }

            xcom::SC<LabelInfo*> * sc;
            SList<LabelInfo*> const& labs = ehlab->read_labels();
            for (sc = labs.get_head();
                 sc != labs.end(); sc = labs.get_next(sc)) {
                ASSERT0(sc);
                IRBB const* tgt = findBBbyLabel(sc->val());
                ASSERT0(tgt);
                xcom::Edge * e = DGraph::addEdge(bb->id(), tgt->id());
                EDGE_info(e) = xmalloc(sizeof(CFGEdgeInfo));
                CFGEI_is_eh((CFGEdgeInfo*)EDGE_info(e)) = true;
                m_has_eh_edge = true;
            }
        }
    }
}


//Construct EH edge after cfg built.
//This function use a conservative method, and this method
//may generate lots of redundant exception edges.
void IRCFG::buildEHEdgeNaive()
{
    ASSERTN(m_bb_list, ("bb_list is emt"));
    List<IRBB*> maythrow;
    List<IRBB*> ehl;
    IRBB * entry = nullptr;
    BBListIter ct;
    for (m_bb_list->get_head(&ct);
         ct != m_bb_list->end(); ct = m_bb_list->get_next(ct)) {
        IRBB * bb = ct->val();
        if (isRegionEntry(bb)) {
            entry = bb;
            break;
        }
    }

    ASSERTN(entry, ("Region does not have an entry"));
    xcom::BitSet mainstreambbs;
    computeMainStreamBBSet(entry, mainstreambbs);

    xcom::BitSet ehbbs; //Record all BB in exception region.
    for (m_bb_list->get_head(&ct);
         ct != m_bb_list->end(); ct = m_bb_list->get_next(ct)) {
        IRBB * bb = ct->val();

        if (bb->isExceptionHandler()) {
            ehl.append_tail(bb);
            findEHRegion(bb, mainstreambbs, ehbbs);
        }

        if (bb->mayThrowException() && !ehbbs.is_contain(bb->id())) {
            maythrow.append_tail(bb);
        }
    }

    if (ehl.get_elem_count() == 0) { return; }

    if (maythrow.get_elem_count() == 0) {
        ASSERTN(entry, ("multi entries"));
        maythrow.append_tail(entry);
    }

    for (ehl.get_head(&ct); ct != ehl.end(); ct = ehl.get_next(ct)) {
         IRBB * b = ct->val();
         BBListIter ct2;
         for (maythrow.get_head(&ct2);
              ct2 != maythrow.end(); ct2 = maythrow.get_next(ct2)) {
             IRBB * a = ct2->val();
             if (ehbbs.is_contain(a->id())) { continue; }

             xcom::Edge * e = DGraph::addEdge(a->id(), b->id());
             EDGE_info(e) = xmalloc(sizeof(CFGEdgeInfo));
             CFGEI_is_eh((CFGEdgeInfo*)EDGE_info(e)) = true;
             m_has_eh_edge = true;
         }
    }
}


bool IRCFG::useMDSSADU() const
{
    //MDSSAMgr may be changed during the CFG lifetime.
    MDSSAMgr * mgr = getRegion()->getMDSSAMgr();
    return mgr != nullptr && mgr->is_valid();
}


bool IRCFG::usePRSSADU() const
{
    //PRSSAMgr may be changed during the CFG lifetime.
    PRSSAMgr * mgr = getRegion()->getPRSSAMgr();
    return mgr != nullptr && mgr->is_valid();
}


//The function verify whether the branch target is match to the BB.
bool IRCFG::verifyBranchTarget() const
{
    IRCFG * pthis = const_cast<IRCFG*>(this);
    BBListIter it;
    BBList * lst = pthis->getBBList();
    for (IRBB * bb = lst->get_head(&it); bb != nullptr;
         bb = lst->get_next(&it)) {
        IR const* last = BB_last_ir(bb);
        if (last == nullptr || !last->isBranch()) { continue; }
        if (last->isConditionalBr()) {
            LabelInfo const* lab = last->getLabel();
            ASSERT0_DUMMYUSE(lab);
            IRBB * succ0 = getNthSucc(bb, 0);
            IRBB * succ1 = getNthSucc(bb, 1);
            ASSERT0_DUMMYUSE(succ0 && succ1);
            BBListIter next_it = it;
            lst->get_next(&next_it);
            ASSERT0(next_it);
            //Find the taken-branch-target and check whether the label is over
            //there.
            if (succ1 != next_it->val()) {
                ASSERT0(findBBbyLabel(lab) == succ1);
            } else {
                ASSERT0(findBBbyLabel(lab) == succ0);
            }
            continue;
        }
        if (last->isUnconditionalBr()) {
            LabelInfo const* lab = last->getLabel();
            IRBB * succ0 = getNthSucc(bb, 0);
            ASSERT0_DUMMYUSE(succ0);
            //Find the taken-branch-target and check whether the label is over
            //there.
            ASSERT0_DUMMYUSE(findBBbyLabel(lab) == succ0);
            continue;
        }
        if (last->isMultiConditionalBr() || last->isIndirectBr()) {
            List<LabelInfo const*> lablst;
            List<IRBB*> bblst;
            get_succs(bblst, bb);
            last->collectLabel(lablst);
            for (LabelInfo const* lab = lablst.get_head(); lab != nullptr;
                 lab = lablst.get_next()) {
                bool find = false;
                for (IRBB * succ = bblst.get_head(); succ != nullptr;
                     succ = bblst.get_next()) {
                    if (findBBbyLabel(lab) == succ) {
                        find = true;
                        break;
                    }
                }
                ASSERT0_DUMMYUSE(find);
            }
            continue;
        }
    }
    return true;
}


//The function verify the relationship between BB and LabelInfo.
bool IRCFG::verifyLabel2BB() const
{
    IRCFG * pthis = const_cast<IRCFG*>(this);
    for (IRBB * bb = pthis->getBBList()->get_head(); bb != nullptr;
         bb = pthis->getBBList()->get_next()) {
        for (LabelInfo const* li = bb->getLabelList().get_head();
             li != nullptr; li = bb->getLabelList().get_next()) {
            IRBB * libb = m_lab2bb.get(li);
            ASSERT0_DUMMYUSE(libb == bb);
        }
        bb->verifyBranchLabel(m_lab2bb);
    }
    return true;
}


//Verification at building SSA mode by ir parser.
static bool verifyPhiEdgeImpl(IRCFG const* cfg, IR const* phi,
                              IR2Lab const& ir2lab)
{
    xcom::Vertex * bbvex = phi->getBB()->getVex();
    xcom::EdgeC * opnd_pred = bbvex->getInList();
    IR * opnd = PHI_opnd_list(phi);
    for (; opnd != nullptr && opnd_pred != nullptr;
         opnd = opnd->get_next(), opnd_pred = EC_next(opnd_pred)) {
        LabelInfo const* opnd_label = ir2lab.get(opnd);
        IRBB * incoming_bb = cfg->findBBbyLabel(opnd_label);
        ASSERT0(incoming_bb);
        if (opnd_pred->getFromId() != incoming_bb->id()) {
            return false;
        }
    }
    ASSERT0(opnd == nullptr && opnd_pred == nullptr);
    return true;
}


bool IRCFG::verifyPhiEdge(IR2Lab const& ir2lab) const
{
    BBListIter ct;
    BBList * bblst = getBBList();
    ASSERT0(bblst);
    for (bblst->get_head(&ct); ct != bblst->end(); ct = bblst->get_next(ct)) {
        IRBB const* bb = ct->val();
        INT phi_opnd_num = -1;
        IRListIter ct2;
        for (BB_irlist(const_cast<IRBB*>(bb)).get_head(&ct2);
             ct2 != nullptr; BB_irlist(const_cast<IRBB*>(bb)).get_next(&ct2)) {
            IR * x = ct2->val();
            ASSERT0(x);
            if (!x->is_phi()) { break; }
            if (phi_opnd_num == -1) {
                //Only sort predecessor once.
                phi_opnd_num = xcom::cnt_list(PHI_opnd_list(x));
            }

            //Verify all PHI have the same number of operands.
            //Verify whether the others PHI's operands are in correct order.
            ASSERTN((UINT)phi_opnd_num == xcom::cnt_list(PHI_opnd_list(x)),
                    ("the number of operand is inconsistent"));
            ASSERT0((UINT)phi_opnd_num == bb->getVex()->getInDegree());
            ASSERT0(verifyPhiEdgeImpl(this, x, ir2lab));
            DUMMYUSE(verifyPhiEdgeImpl);
        }
    }
    return true;
}


UINT IRCFG::afterReplacePredInCase2(
    IRBB const* bb, IRBB const* succ, List<UINT> const& newpreds,
    OUT CfgOptCtx & ctx, UINT orgpos, UINT orgnum, UINT newpredstartpos)
{
    ASSERT0(newpreds.get_elem_count() > 1);
    PRSSAMgr * prssamgr = getRegion()->getPRSSAMgr();
    MDSSAMgr * mdssamgr = getRegion()->getMDSSAMgr();
    //Update phi operation.
    //Copy the operand that is in the replacement position. The original
    //livein value of the operand should livein from new predecessors as well.
    if (usePRSSADU()) {
        prssamgr->dupAndInsertPhiOpnd(
            succ, newpredstartpos, newpreds.get_elem_count());
    }
    if (useMDSSADU()) {
        mdssamgr->dupAndInsertPhiOpnd(
            succ, newpredstartpos, newpreds.get_elem_count());
    }
    if (!ctx.needUpdateDomInfo()) {
        //No need to duplicate operand if there is only one predecessor.
        return newpredstartpos;
    }
    xcom::VexTab modset;
    tryUpdateDomInfoAndMDSSA(modset, bb->getVex(), succ->getVex(), ctx);

    //Handle new predecessors.
    List<UINT>::Iter it;
    modset.clean();
    for (UINT bbid = newpreds.get_head(&it);
         bbid != BBID_UNDEF; bbid = newpreds.get_next(&it)) {
        tryUpdateDomInfoAndMDSSA(modset, getVertex(bbid), succ->getVex(), ctx);
    }
    //No need to duplicate operand if there is only one predecessor.
    return newpredstartpos;
}


UINT IRCFG::afterReplacePredInCase1(
    IRBB const* bb, IRBB const* succ, List<UINT> const& newpreds,
    OUT CfgOptCtx & ctx, UINT orgpos, UINT orgnum, UINT newpredstartpos)
{
    ASSERT0(newpreds.get_elem_count() <= 1);
    if (getPredsNum(succ) != orgnum) {
        ASSERT0(newpredstartpos == orgpos);
        if (usePRSSADU()) {
            PRSSAMgr * prssamgr = getRegion()->getPRSSAMgr();
            //Remove phi information before modify CFG.
            prssamgr->removeSuccessorDesignatedPhiOpnd(succ, newpredstartpos);
        }
        if (useMDSSADU()) {
            MDSSAMgr * mdssamgr = getRegion()->getMDSSAMgr();
            //Remove phi information before modify CFG.
            MDSSAUpdateCtx ssactx(const_cast<CfgOptCtx&>(ctx).getOptCtx());
            mdssamgr->removeSuccessorDesignatedPhiOpnd(
                succ, newpredstartpos, ssactx);
        }
    }
    if (ctx.needUpdateDomInfo()) {
        List<UINT>::Iter it;
        UINT bbid = newpreds.get_head(&it);
        if (bbid == BBID_UNDEF) { return newpredstartpos; }
        tryUpdateDomInfoAndMDSSA(getVertex(bbid), succ->getVex(), ctx);
    }
    //No need to duplicate operand if there is only one predecessor.
    return newpredstartpos;
}


//The function replaces original predecessor with a list of
//new predecessors.
//bb: the predecessor will be replaced.
//succ: the target BB.
//newpreds: list of new predecessors.
//Return the new position of 'bb' that is in the predecessor list of 'succ'.
UINT IRCFG::replacePredWith(
    IRBB const* bb, IRBB const* succ, List<UINT> const& newpreds,
    OUT CfgOptCtx & ctx)
{
    bool is_pred;
    UINT orgpos = WhichPred(bb, succ, is_pred);
    ASSERT0(is_pred);
    UINT orgnum = getPredsNum(succ);
    UINT newpredstartpos = OptimizedCFG<IRBB, IR>::replacePredWith(
        bb, succ, newpreds, ctx);
    if (newpreds.get_elem_count() <= 1) {
        return afterReplacePredInCase1(
            bb, succ, newpreds, ctx, orgpos, orgnum, newpredstartpos);
    }
    return afterReplacePredInCase2(
        bb, succ, newpreds, ctx, orgpos, orgnum, newpredstartpos);
}


void IRCFG::setLabAndBBMap()
{
    for (IRBB * bb = m_bb_list->get_tail();
         bb != nullptr; bb = m_bb_list->get_prev()) {
        for (LabelInfo const* li = bb->getLabelList().get_head();
             li != nullptr; li = bb->getLabelList().get_next()) {
            ASSERTN(m_lab2bb.get(li) == nullptr,
                    ("Label has been mapped to BB%d", m_lab2bb.get(li)->id()));
            m_lab2bb.set(li, bb);
            if (LABELINFO_is_catch_start(li)) {
                BB_is_catch_start(bb) = true;
            }
            if (LABELINFO_is_terminate(li)) {
                BB_is_terminate(bb) = true;
            }
        }
    }
}


void IRCFG::initEntryAndExit()
{
    m_lab2bb.clean();

    //This function may be called multiple times.
    //Entry BB will be generated and recorded during the first call of
    //build(). In order to avoid generating entry-BB at each call of
    //initEntryAndExit, we prefer to reuse the first generated entry BB.
    //Thus do NOT clean m_entry before calling initEntryAndExit.
    //If you expect to reconstruct whole CFG, please destroy the PASS_CFG
    //first and then register the CFG pass again.
    m_entry = nullptr;
    m_exit_list.clean();
    if (m_bb_list != nullptr && m_bb_list->get_elem_count() == 0) {
        return;
    }
    //Region's BB related API guarantees the first BB in BBList is already
    //the entry of current Region.
    m_entry = m_bb_list->get_head();
    ASSERT0(BB_is_entry(m_entry));
    addBB(m_entry);
    setLabAndBBMap();
}


//Build CFG according to IRBB list.
void IRCFG::build(OptCtx & oc)
{
    CFG<IRBB, IR>::build(oc);
}


static LabelInfo const* useFirstAvailLabel(
    IR * ir, LabelInfo const* li, IRCFG const* cfg)
{
    if (li->hasSideEffect()) { return li; }
    IRBB * tgt = cfg->findBBbyLabel(li);
    ASSERT0(tgt);
    LabelInfoListIter liit;
    LabelInfoList & lst = tgt->getLabelList();
    for (lst.get_head(&liit); liit != nullptr; liit = lst.get_next(liit)) {
        LabelInfo const* tli = liit->val();
        if (tli->hasSideEffect()) { continue; }
        if (tli != li) {
            ir->setLabel(tli);
        }
        return tli;
    }
    UNREACHABLE();
    return nullptr;
}


void IRCFG::recomputeDomInfo(MOD OptCtx & oc)
{
    oc.setInvalidDom();
    oc.setInvalidPDom();
    m_rg->getPassMgr()->checkValidAndRecompute(&oc, PASS_DOM, PASS_UNDEF);
}


void IRCFG::removeDomInfo(C<IRBB*> * bbct, MOD CfgOptCtx & ctx)
{
    //Note the iteration of predecessor and successor may be costly.
    xcom::DGraph::removeDomInfo(bbct->val()->id(), ctx.needUpdateDomInfo(),
                                CFGOPTCTX_vertex_iter_time(&ctx));
}


void IRCFG::removeLoopInfo(IRBB const* bb, MOD LI<IRBB> * li, MOD OptCtx & oc)
{
    ASSERT0(li);
    li->getBodyBBSet()->diff(bb->id());
    if (li->getLoopHead() == bb) {
        //LoopInfo need to be recompute.
        LI_loop_head(li) = nullptr;
        oc.setInvalidLoopInfo();
    }
}


void IRCFG::removeLoopInfo(IRBB const* bb, CfgOptCtx const& ctx)
{
    if (!const_cast<CfgOptCtx&>(ctx).getOptCtx().is_loopinfo_valid()) {
        return;
    }
    LoopInfoIter it;
    for (LI<IRBB> * li = xoc::iterInitLoopInfo(getLoopInfo(), it);
         li != nullptr; li = xoc::iterNextLoopInfo(it)) {
        removeLoopInfo(bb, li, const_cast<CfgOptCtx&>(ctx).getOptCtx());
    }
}


void IRCFG::removeAllOutEdge(IRBB const* bb, OUT CfgOptCtx & ctx)
{
    AdjVertexIter it;
    Vertex const* vex = bb->getVex();
    Vertex const* next;
    for (Vertex const* out = Graph::get_first_out_vertex(vex, it);
         out != nullptr; out = next) {
        next = Graph::get_next_out_vertex(it);
        removeEdge(vex, out, ctx);
    }
}


void IRCFG::removeAllInEdge(IRBB const* bb, OUT CfgOptCtx & ctx)
{
    AdjVertexIter it;
    Vertex const* vex = bb->getVex();
    Vertex const* next;
    for (Vertex const* in = Graph::get_first_in_vertex(vex, it);
         in != nullptr; in = next) {
        next = Graph::get_next_in_vertex(it);
        removeEdge(in, vex, ctx);
    }
}


//You should clean the relation between Label and BB before removing BB.
//Note Label2BB should has been mainated before come to the function.
void IRCFG::removeBB(C<IRBB*> * bbct, OUT CfgOptCtx & ctx)
{
    ASSERT0(bbct && m_bb_list->in_list(bbct));
    IRBB * bb = bbct->val();

    //Note the function may be costly.
    removeAllMDPhi(bb, ctx);
    removeAllStmt(bb, ctx);
    removeRPO(bb);
    {
    CfgOptCtx tctx(ctx);
    removeLoopInfo(bb, tctx);
    ctx.unionBottomUpInfo(tctx);
    }

    {
    //Invoke removeAllOut and removeAllIn in removeBB rather than
    //removeVertex directly.
    CfgOptCtx tctx(ctx);
    removeAllInEdge(bb, tctx);
    ctx.unionBottomUpInfo(tctx);
    }

    {
    //Invoke removeAllOut and removeAllIn in removeBB rather than
    //removeVertex directly.
    CfgOptCtx tctx(ctx);
    removeAllOutEdge(bb, tctx);
    ctx.unionBottomUpInfo(tctx);
    }

    {
    //CASE:All in/out vertex has been removed, thus the DomInfo of bb is
    //dangled, and just erase it from dominator system.
    //There is no iteration that walking through in/out-edge will happen while
    //no overheads to compilation time.
    CfgOptCtx tctx(ctx);
    removeDomInfo(bbct, tctx);
    ctx.unionBottomUpInfo(tctx);
    }

    removeVertex(bb->id());
    m_bb_list->remove(bbct);
    if (bb->is_exit()) {
        m_exit_list.remove(bb);
    }
    ASSERT0(!bb->is_entry());
    //The mapping between Labels and BB has been maintained by caller's code.
    //removeMapBetweenLabelAndBB(bb);
    m_rg->getBBMgr()->freeBB(bb);
}


//The function remove labels that nobody referrenced.
bool IRCFG::removeRedundantLabel()
{
    START_TIMER(t, "Remove Redundant Label");
    bool change = false;
    BBListIter it;
    TTab<LabelInfo const*> useful;
    for (IRBB * bb = getBBList()->get_head(&it); bb != nullptr;
         bb = getBBList()->get_next(&it)) {
        IR * last_xr = get_last_xr(bb);
        if (last_xr == nullptr) { continue; }

        LabelInfo const* li = last_xr->getLabel();
        if (li != nullptr) {
            li = useFirstAvailLabel(last_xr, li, this);
            useful.append(li);
            continue;
        }

        if (!last_xr->hasCaseList()) { continue; }

        IR * caselst = last_xr->getCaseList();
        ASSERT0(caselst);
        for (IR * cs = caselst; cs != nullptr; cs = cs->get_next()) {
            ASSERT0(cs->is_case());
            LabelInfo const* li = cs->getLabel();
            li = useFirstAvailLabel(cs, li, this);
            useful.append(li);
        }
    }

    for (IRBB * bb = getBBList()->get_head(&it); bb != nullptr;
         bb = getBBList()->get_next(&it)) {
        LabelInfoListIter liit;
        LabelInfoListIter liit_next;
        LabelInfoList & lst = bb->getLabelList();
        for (lst.get_head(&liit); liit != nullptr; liit = liit_next) {
            liit_next = lst.get_next(liit);
            LabelInfo const* li = liit->val();
            if (!useful.find(li) && !li->hasSideEffect()) {
                lst.remove(liit);
                getLabel2BBMap()->remove(li);
                change = true;
            }
        }
    }
    END_TIMER(t, "Remove Redundant Label");
    return change;
}


bool IRCFG::refineCFG(MOD CfgOptCtx & optctx)
{
    bool change = false;
    bool lchange = true;
    UINT count = 0;
    while (lchange && count < 20) {
        lchange = false;
        if (g_do_cfg_remove_unreach_bb && removeUnreachBB(optctx, nullptr)) {
            computeExitList();
            change = true;
            lchange = true;
            ASSERT0L3(verifyIRandBB(getBBList(), m_rg));
        }
        RemoveEmptyBBCtx rmctx(optctx);
        if (g_do_cfg_remove_empty_bb && removeEmptyBB(rmctx)) {
            computeExitList();
            change = true;
            lchange = true;
            ASSERT0L3(verifyIRandBB(getBBList(), m_rg));
        }
        if (g_do_cfg_remove_trampolin_bb && removeTrampolinEdge(optctx)) {
            computeExitList();
            change = true;
            lchange = true;
            ASSERT0L3(verifyIRandBB(getBBList(), m_rg));
            //There might produce unreachable-BB after the pass. However
            //unreachable-BB will crash RPO verification, thus leave the
            //RPO, Dom verification only after unreachable-BB removing.
            goto AGAIN;
        }
        if (g_do_cfg_remove_trampolin_bb && removeTrampolinBB(optctx)) {
            computeExitList();
            change = true;
            lchange = true;
            ASSERT0L3(verifyIRandBB(getBBList(), m_rg));
            //There might produce unreachable-BB after the pass. However
            //unreachable-BB will crash RPO verification, thus leave the
            //RPO, Dom verification only after unreachable-BB removing.
            goto AGAIN;
        }
        if (g_do_cfg_remove_trampolin_branch && removeTrampolinBranch(optctx)) {
            computeExitList();
            change = true;
            lchange = true;
            ASSERT0L3(verifyIRandBB(getBBList(), m_rg));
            //There might produce unreachable-BB after the pass. However
            //unreachable-BB will crash RPO verification, thus leave the
            //RPO, Dom verification only after unreachable-BB removing.
            goto AGAIN;
        }
AGAIN:
        count++;
    }
    ASSERT0(!lchange);
    ASSERT0(verify());
    return change;
}


//Note if CFG rebuild, SSAInfo and MDSSAInfo should be recomputed.
void IRCFG::rebuild(OptCtx & oc, SortPredByBBId const* sortpred)
{
    initEntryAndExit();
    CFG<IRBB, IR>::rebuild(oc);
    buildEHEdge();
    if (usePRSSADU()) {
        ASSERT0(sortpred);
        sortpred->sort();
    }
    //After CFG rebuilding, empty BB should be removed because empty BB disturb
    //the computation of entry and exit.
    CfgOptCtx ctx(oc);
    CFGOPTCTX_need_update_dominfo(&ctx) = false;
    RemoveEmptyBBCtx rmctx(ctx);
    removeEmptyBB(rmctx);

    //Compute exit BB after CFG rebuilt.
    computeExitList();
    refineCFG(ctx);
    set_valid(true);
}


//Note if CFG rebuild, SSAInfo and MDSSAInfo should be recomputed.
void IRCFG::rebuild(OptCtx & oc)
{
    if (usePRSSADU()) {
        SortPredByBBId sortpred(this);
        sortpred.collectPhiOpnd2PredBB();
        rebuild(oc, &sortpred);
        return;
    }
    rebuild(oc, nullptr);
}


bool IRCFG::verify() const
{
    verifyLabel2BB();
    CFG<IRBB, IR>::verify();
    return true;
}


//Do early control flow optimization.
void IRCFG::initCFG(OptCtx & oc)
{
    if (getBBList()->get_elem_count() == 0) {
        //If bb list is empty, set CFG is invalid.
        //oc.is_cfg_valid() = true;
        return;
    }
    //cfg->removeEmptyBB();
    build(oc);
    buildEHEdge();
    if (g_dump_opt.isDumpAfterPass() && g_dump_opt.isDumpCFG()) {
        dump();
    }
    //Rebuild CFG may generate redundant empty BB which disturbs the
    //computation of Entry and Exit.
    CfgOptCtx ctx(oc);
    CFGOPTCTX_need_update_dominfo(&ctx) = false;
    CFGOPTCTX_do_merge_label(&ctx) = oc.do_merge_label();
    RemoveEmptyBBCtx rmctx(ctx);
    removeEmptyBB(rmctx);
    computeExitList();
    bool change = refineCFG(ctx);
    if (change && g_dump_opt.isDumpAfterPass() && g_dump_opt.isDumpCFGOpt()) {
        dump();
    }
}


void IRCFG::findTargetBBOfMulticondBranch(
    IR const* ir, OUT List<IRBB*> & tgt_bbs)
{
    ASSERT0(ir->isMultiConditionalBr());
    tgt_bbs.clean();
    if (m_bb_list == nullptr) { return; }

    IR * casev_list = SWITCH_case_list(ir);
    if (SWITCH_deflab(ir) != nullptr) {
        IRBB * tbb = findBBbyLabel(SWITCH_deflab(ir));
        ASSERT0(tbb);
        tgt_bbs.append_tail(tbb);
    }

    for (IR * casev = casev_list; casev != nullptr; casev = IR_next(casev)) {
        IRBB * tbb = findBBbyLabel(CASE_lab(casev));
        ASSERT0(tbb);
        tgt_bbs.append_tail(tbb);
    }
}


//This function find BBs which belong to exception handler region.
//catch_start: start BB of an exception handler region.
//mainstreambbs: record BBs which can be reached from entry of
//    region. Note that the BB set only records BBs that in main stream
//    control flow.
//ehbbs: record all BB of the exception handler region.
//Note: this function does not clean ehbbs. Caller is responsible for that.
void IRCFG::findEHRegion(
    IRBB const* catch_start, xcom::BitSet const& mainstreambbs,
    OUT xcom::BitSet & ehbbs)
{
    ASSERT0(catch_start && catch_start->isExceptionHandler());
    List<xcom::Vertex const*> list;
    xcom::Vertex const* bbv = catch_start->getVex();
    ASSERT0(bbv);
    list.append_head(bbv);
    for (xcom::Vertex const* v = list.remove_head();
         v != nullptr; v = list.remove_head()) {
        UINT id = v->id();
        if (mainstreambbs.is_contain(id) || ehbbs.is_contain(id)) {
            continue;
        }
        ehbbs.bunion(id);
        AdjVertexIter it;
        for (Vertex const* out = Graph::get_first_out_vertex(v, it);
             out != nullptr; out = Graph::get_next_out_vertex(it)) {
            if (!mainstreambbs.is_contain(out->id()) &&
                !ehbbs.is_contain(out->id())) {
                list.append_tail(out);
            }
        }
    }
}


//This function find all BB of exception try region, and record them in trybbs.
//trybbs: record all BB of the try region.
//Note: this function does not clean trybbs. Caller is responsible for that.
void IRCFG::findAllTryRegions(OUT xcom::BitSet & trybbs)
{
    BBListIter ct;
    xcom::BitSet t;
    for (m_bb_list->get_head(&ct);
         ct != m_bb_list->end(); ct = m_bb_list->get_next(ct)) {
        IRBB const* bb = ct->val();
        if (!bb->isTryStart()) { continue; }
        t.clean();
        findTryRegion(bb, t);
        trybbs.bunion(t);
    }
}


//This function find BBs which belong to exception try region.
//try_start: start BB of an entry of exception try region.
//trybbs: record all BB of the try region.
//Note: this function does not clean trybbs. Caller is responsible for that.
void IRCFG::findTryRegion(IRBB const* try_start, OUT xcom::BitSet & trybbs)
{
    ASSERT0(try_start && try_start->isTryStart());
    List<xcom::Vertex const*> list;
    xcom::Vertex const* bbv = try_start->getVex();
    ASSERT0(bbv);
    list.append_head(bbv);
    for (xcom::Vertex const* v = list.remove_head();
         v != nullptr; v = list.remove_head()) {
        UINT id = v->id();
        if (trybbs.is_contain(id)) { continue; }
        trybbs.bunion(id);

        IRBB * bb = getBB(id);
        ASSERTN(bb, ("vertex on CFG correspond to nothing"));

        if (bb->isTryEnd() && bb != try_start) {
            //BB may have both try_start and try_end label.
            //If it is the case, the try_end is always other region's
            //end label, just ignore that.
            continue;
        }

        for (xcom::EdgeC * el = v->getOutList();
             el != nullptr; el = EC_next(el)) {
            xcom::Edge const* e = EC_edge(el);
            CFGEdgeInfo * ei = (CFGEdgeInfo*)EDGE_info(e);
            if (ei != nullptr && CFGEI_is_eh(ei)) {
                //Do not consider EH edge.
                continue;
            }

            xcom::Vertex const* succ = EDGE_to(e);
            if (!trybbs.is_contain(succ->id())) {
                list.append_tail(succ);
            }

        }
    }
}


//Find a list bb that referred labels which is the target of ir.
void IRCFG::findTargetBBOfIndirectBranch(
    IR const* ir, OUT List<IRBB*> & tgtlst)
{
    ASSERT0(ir->isIndirectBr());
    for (IR * c = IGOTO_case_list(ir); c != nullptr; c = c->get_next()) {
        ASSERT0(c->is_case());
        IRBB * bb = m_lab2bb.get(CASE_lab(c));
        ASSERT0(bb); //no bb is correspond to lab.
        tgtlst.append_tail(bb);

        #ifdef _DEBUG_
        bool find = false;
        for (LabelInfo const* li = bb->getLabelList().get_head();
             li != nullptr; li = bb->getLabelList().get_next()) {
            if (isSameLabel(CASE_lab(c), li)) {
                find = true;
                break;
            }
        }
        ASSERT0(find);
        #endif
    }
}


//Find natural loop and scan loop body to find call and early exit, etc.
void IRCFG::LoopAnalysis(OUT OptCtx & oc)
{
    if (getBBList()->get_elem_count() == 0) {
        //If bb is empty, set LoopInfo to be invalid.
        //oc.setValidPass(PASS_LOOP_INFO);
        return;
    }
    m_rg->getPassMgr()->checkValidAndRecompute(&oc, PASS_DOM, PASS_UNDEF);
    findLoop(oc);
    collectLoopInfo();
    oc.setValidPass(PASS_LOOP_INFO);
}


//Find bb that 'lab' attouchemented.
IRBB * IRCFG::findBBbyLabel(LabelInfo const* lab) const
{
    IRBB * bb = m_lab2bb.get(lab);
    if (bb == nullptr) { return nullptr; }

    #ifdef _DEBUG_
    bool find = false;
    for (LabelInfo const* li = bb->getLabelList().get_head();
         li != nullptr; li = bb->getLabelList().get_next()) {
        if (isSameLabel(lab, li)) {
            find = true;
            break;
        }
    }
    ASSERT0(find);
    #endif

    return bb;
}


IRBB * IRCFG::splitBB(IRBB * bb, IRListIter split_point, OptCtx & oc)
{
    IRBB * newbb = m_rg->allocBB();

    //Move rest IRs from bb to newbb.
    bb->getIRList().extractRestIRIntoList(
        split_point, false, newbb->getIRList());
    insertFallThroughBBAfter(bb, newbb, &oc);
    return newbb;
}


void IRCFG::preprocessBeforeRemoveBB(IRBB * bb, MOD CfgOptCtx & ctx)
{
    //Remove related PHI operand from successor BB before removing 'bbct'
    //if bbct's successor has PHI.
    removeSuccPhiOpnd(bb, ctx);
}


//Some optimizations may append stmt into BB which has already down-boundary
//stmt. That makes BB invalid. Split such invalid BB into two or more BBs.
//The function will try to maintain RPO and DOM info.
bool IRCFG::splitBBIfNeeded(IRBB * bb, OptCtx & oc)
{
    ASSERT0(bb);
    IRListIter it;
    for (bb->getIRList().get_head(&it); it != nullptr;) {
        IRListIter cur = it; bb->getIRList().get_next(&it);
        if (IRBB::isLowerBoundary(cur->val()) && it != nullptr) {
            IRBB * newbb = splitBB(bb, cur, oc);
            return splitBBIfNeeded(newbb, oc);
        }
    }
    return false;
}


bool IRCFG::tryUpdateRPOBeforeCFGChanged(
    IRBB * newbb, IRBB const* marker, bool newbb_prior_marker, MOD OptCtx * oc)
{
    if (!oc->is_rpo_valid()) {
        oc->setInvalidRPO();
        oc->setInvalidCDG();
        return false;
    }
    //CASE:compile/dce16.c
    //In this case, we have to amend PRO before change CFG, that is to say,
    //inserting BB56 between BB43 and BB35. Because tryUpdateRPO() collect
    //and compute new RPO by walking through the predecessors of 'exit_bb'.
    addBB(newbb);
    bool succ = tryUpdateRPO(newbb, marker, newbb_prior_marker);
    if (!succ) {
        oc->setInvalidRPO();
        oc->setInvalidCDG();
    }
    //Do NOT perform verify here because newbb might not have been add to
    //BB list.
    //ASSERT0L3(verifyRPO(*oc));
    return succ;
}


bool IRCFG::tryUpdateRPO(IRBB * newbb, IRBB const* marker,
                         bool newbb_prior_marker)
{
    ASSERT0(newbb != marker);
    ASSERT0(newbb->rpo() == RPO_UNDEF);
    ASSERTN(newbb->getVex(), ("BB should be add to graph firstly"));
    ASSERTN(marker->getVex(), ("BB is not belong to graph"));
    return getRPOMgr().tryUpdateRPO(
        newbb->getVex(), marker->getVex(), getRPOVexList(), newbb_prior_marker);
}


//Insert BB before bb.
//e.g:BB1 BB2 BB3
//      \  |  /
//        BB4
//  after inserting newbb,
//    BB1 BB2 BB3
//      \  |  /
//       newbb
//         |
//        BB4
void IRCFG::insertBBBefore(IN IRBB * bb, IN IRBB * newbb)
{
    addBB(newbb);
    getBBList()->insert_before(newbb, bb);
    xcom::Vertex const* bbv = bb->getVex();
    ASSERT0(bbv);
    xcom::Vertex * newbbv = newbb->getVex();
    ASSERT0(newbbv);
    for (xcom::EdgeC * predlist = bbv->getInList();
         predlist != nullptr; predlist = predlist->get_next()) {
        xcom::Edge * prededge = predlist->getEdge();
        //Make sure the order of new edge is in same order as original edge.
        //e.g:original edge order is: BB1->BB4, BB2->BB4, BB3->BB4. The order
        //of new edge is: BB1->newbb, BB2->newbb, BB3->newbb.
        xcom::Edge * newedge = allocEdge(prededge->from(), newbbv);
        EDGE_info(newedge) = prededge->info();
    }
    xcom::EdgeC * next = nullptr;
    for (xcom::EdgeC * predlist = bbv->getInList();
         predlist != nullptr; predlist = next) {
        next = predlist->get_next();
        DGraph::removeEdge(predlist->getEdge());
    }
    DGraph::addEdge(newbbv->id(), bbv->id());
    moveLabels(bb, newbb);
}


//The function insert a tampolining BB bewteen bb and its next BB.
IRBB * IRCFG::changeFallthroughBBToJumpBB(IRBB * bb, OptCtx * oc)
{
    ASSERT0(bb->is_fallthrough());
    BBList * bblst = getBBList();
    BBListIter it;
    bblst->find(bb, &it);
    ASSERTN(it, ("BB%d is not in BBList", bb->id()));
    it = bblst->get_next(it);
    ASSERT0(it);
    return changeFallthroughBBToJumpBB(bb, it->val(), oc);
}


//The function insert a tampolining BB bewteen prev and bb, that will
//make prev no longer fallthrough to bb.
//prev: the previous of 'next' BB, note prev must fallthrough to 'next'.
//next: the next BB in BBList.
IRBB * IRCFG::changeFallthroughBBToJumpBB(
    IRBB * prev, MOD IRBB * next, OptCtx * oc)
{
    ASSERT0(prev && next);
    ASSERT0(prev->is_fallthrough());

    //Given 'to' has a fallthrough in-edge. Insert a tmp BB
    //e.g:Given following edges,
    //    from->bb1->bb2->to, where all edges are fallthrough edges;
    //    from->to is jump-edge
    //    bb1->to is jump-edge
    //
    //We got it and have to revise the fallthrough edge 'bb2->to',
    //the result is from->bb1->bb2->inserted_tramp_bb, where all
    //edges are fallthrough, inserted_tramp_bb->to becomes jump-edge
    //    from->to is jump-edge
    //    bb1->to is jump-edge
    //    bb2->inserted_tramp_bb, tmp_tramp_bb->to, both are jump-edge.
    //    ir-list of inserted_tramp_bb is:
    //        goto L1:
    //
    //    ir-list of 'to' is:
    //        L1:
    //        ...
    IRBB * tramp_bb = m_rg->allocBB();
    LabelInfo * li = m_rg->genILabel();
    IR * goto_ir = m_rg->getIRMgr()->buildGoto(li);
    tramp_bb->getIRList().append_tail(goto_ir);
    addLabel(next, li);
    addBB(tramp_bb);
    tryUpdateRPOBeforeCFGChanged(tramp_bb, prev, false, oc);
    if (oc->is_dom_valid()) {
        addDomInfoToNewIPDom(prev->id(), tramp_bb->id());
    }
    getBBList()->insert_after(tramp_bb, prev);

    //Insert a trampolining BB between the previous BB of 'to'
    //that contains a jump IR.
    insertVertexBetween(prev->id(), next->id(), tramp_bb->id());

    //Now, fallthrough edge bb2->to has been broken, we can insert
    //'newbb' before 'to' correctly.
    return tramp_bb;
}


//The function insert newbb after 'marker'.
//Return the newbb.
IRBB * IRCFG::insertFallThroughBBAfter(IRBB const* marker, MOD OptCtx * oc)
{
    IRBB * newbb = m_rg->allocBB();
    insertFallThroughBBAfter(marker, newbb, oc);
    return newbb;
}


void IRCFG::insertFallThroughBBAfter(IRBB const* marker, IRBB * newbb,
                                     MOD OptCtx * oc)
{
    ASSERTN(marker->is_fallthrough(), ("can not insert fallthrough BB"));
    addBB(newbb);
    BBList * bblst = getBBList();
    //Insert newbb into BB List.
    BBListIter marker_it;
    bblst->find(marker, &marker_it);
    ASSERT0(marker_it);

    //Add CFG edge if necessary.
    BBListIter next_it = marker_it;
    bblst->get_next(&next_it);
    ASSERTN(next_it, ("miss fallthrough BB, illegal CFG"));
    IRBB const* next = next_it->val();
    ASSERTN(getEdge(marker->id(), next->id()), ("miss fallthrough edge"));
    tryUpdateRPOBeforeCFGChanged(newbb, marker, false, oc);

    //Insert newbb that should follow 'marker'.
    bblst->insert_after(newbb, marker_it);
    insertVertexBetween(marker->id(), next->id(), newbb->id());
    if (oc->is_dom_valid()) {
        addDomInfoToFallThroughBB(marker, newbb, next);
    }
}


void IRCFG::addDomInfoToFallThroughBB(IRBB const* marker, IRBB const* newbb,
                                      IRBB const* oldnext)
{
    addDomInfoToImmediateSucc(marker->getVex(), newbb->getVex(),
                              oldnext->getVex());
}


void IRCFG::replaceBranchLabel(IRBB const* from, IRBB const* to,
                               LabelInfo const* li)
{
    IR * last_xr_of_from = get_last_xr(const_cast<IRBB*>(from));
    if (last_xr_of_from->is_igoto()) {
        bool find_label = false;
        CIGoto * igoto = (CIGoto*)last_xr_of_from;
        for (IR * cs = igoto->getCaseList(); cs != nullptr;
             cs = cs->get_next()) {
            if (findBBbyLabel(CASE_lab(cs)) != to) { continue; }
            cs->setLabel(li);
            find_label = true;
            break;
        }
        ASSERT0(find_label);
    } else {
        ASSERT0(last_xr_of_from->getLabel() != nullptr &&
                findBBbyLabel(last_xr_of_from->getLabel()) == to);
        last_xr_of_from->setLabel(li);
    }
}


//The function insert newbb bewteen 'from' and 'to'. As a result, the
//function may break up fallthrough edge of 'to' if necessary.
//Return trampoline BB if the function inserted it before 'to'.
IRBB *  IRCFG::insertBBBetween(
    IRBB const* from, IN BBListIter from_it, MOD IRBB * to,
    IN BBListIter to_it, IN IRBB * newbb, MOD OptCtx * oc)
{
    ASSERT0(from_it->val() == from && to_it->val() == to);
    //Revise BB list, note that 'from' is either fall-through to 'to',
    //or jumping to 'to'.
    addBB(newbb);

    //First, processing edge if 'from'->'to' is fallthrough.
    BBList * bblst = getBBList();
    BBListIter tmp_it = from_it;
    if (from->is_fallthrough() && bblst->get_next(&tmp_it) == to) {
        //Insert newbb that must be followed 'from'.
        bblst->insert_after(newbb, from_it);
        insertVertexBetween(from->id(), to->id(), newbb->id());
        return nullptr;
    }

    //Try to record the fall-through predecessor of 'to'.
    xcom::AdjVertexIter vit;
    IRBB * fallthrough_pred = nullptr;
    for (Vertex const* in = Graph::get_first_in_vertex(to->getVex(), vit);
         in != nullptr; in = Graph::get_next_in_vertex(vit)) {
        IRBB * pred = getBB(in->id());
        ASSERT0(pred);
        if (pred->is_fallthrough() && bblst->isPrevBB(pred, to_it)) {
            fallthrough_pred = pred;
            break;
        }
    }

    LabelInfo * li = m_rg->genILabel();
    addLabel(newbb, li);

    //Revise the target LABEL of last XR in 'from'.
    replaceBranchLabel(from, to, li);

    //Insert newbb that must be fallthrough BB prior to 'to'.
    bblst->insert_before(newbb, to_it);

    //Insert newbb between 'from' and 'to' in Graph.
    insertVertexBetween(from->id(), to->id(), newbb->id());
    if (fallthrough_pred != nullptr) {
        //Now, we get edge from->newbb->to, which newbb->to is fallthrough
        //edge, that may violate the original fallthrough edge of to.
        //Thus we have to change original-fallthrough->to to be jump-edge to
        //keep consistency between CFG and Graph.
        ASSERT0(bblst->isPrevBB(fallthrough_pred, newbb));

        //NOTE: The function insert a tampolining BB bewteen bb and its next BB.
        return changeFallthroughBBToJumpBB(fallthrough_pred, to, oc);
    }
    return nullptr;
}


//Move all Labels which attached on src BB to tgt BB.
void IRCFG::moveLabels(IRBB * src, IRBB * tgt)
{
    //Remap label2bb.
    LabelInfoListIter it;
    LabelInfoList & lst = src->getLabelList();
    if (lst.get_elem_count() == 0) { return; }

    for (LabelInfo const* li = lst.get_head(&it);
         li != nullptr; li = lst.get_next(&it)) {
        m_lab2bb.setAlways(li, tgt);
    }
    tgt->mergeLabeInfoList(src);

    //Cutoff the relation between src and Labels.
    src->cleanLabelInfoList();
}


void IRCFG::removeSuccDesignatedPhiOpnd(
    IRBB const* succ, UINT pos, CfgOptCtx const& ctx)
{
    //Before removing bb or change bb successor,
    //you need remove the related PHI operand if BB 'succ' has PHI.
    if (usePRSSADU()) {
        PRSSAMgr * prssamgr = getRegion()->getPRSSAMgr();
        prssamgr->removeSuccessorDesignatedPhiOpnd(succ, pos);
    }
    if (useMDSSADU()) {
        MDSSAMgr * mdssamgr = getRegion()->getMDSSAMgr();
        MDSSAUpdateCtx ssactx(const_cast<CfgOptCtx&>(ctx).getOptCtx());
        if (!ctx.needUpdateDomInfo()) {
            //Info MDSSAMgr does not need to maintain DU chain.
            MDSSAUPDATECTX_update_duchain(&ssactx) = false;
        }
        mdssamgr->removeSuccessorDesignatedPhiOpnd(succ, pos, ssactx);
    }
}


void IRCFG::removeSuccPhiOpnd(IRBB const* bb, CfgOptCtx const& ctx)
{
    UINT pos = 0;
    AdjVertexIter it;
    for (Vertex const* t = Graph::get_first_out_vertex(bb->getVex(), it);
         t != nullptr; t = Graph::get_next_out_vertex(it), pos++) {
        IRBB * succ = getBB(t->id());
        bool is_pred;
        UINT npred = WhichPred(bb, succ, is_pred);
        ASSERT0_DUMMYUSE(is_pred);
        removeSuccDesignatedPhiOpnd(succ, npred, ctx);
    }
}


//Cut off the mapping relation between Labels and BB.
void IRCFG::removeMapBetweenLabelAndBB(IRBB * bb)
{
    LabelInfoListIter it;
    for (LabelInfo const* li = bb->getLabelList().get_head(&it);
         li != nullptr; li = bb->getLabelList().get_next(&it)) {
        m_lab2bb.remove(li);
    }
    bb->cleanLabelInfoList();
}


//The function remove all stmt in 'bb'.
//Note caller should guarantee stmt is useless and removable.
void IRCFG::removeAllStmt(IRBB * bb, CfgOptCtx const& ctx)
{
    BBIRListIter it;
    CfgOptCtx & pctx = const_cast<CfgOptCtx&>(ctx);
    for (IR * ir = bb->getIRList().get_head(&it); ir != nullptr;
         ir = bb->getIRList().get_next(&it)) {
        xoc::removeStmt(ir, getRegion(), pctx.getOptCtx());
        getRegion()->freeIRTree(ir);
    }
    bb->getIRList().clean();
}


//The function remove all MDPhis in 'bb'.
//Note caller should guarantee phi is useless and removable.
void IRCFG::removeAllMDPhi(IRBB * bb, CfgOptCtx const& ctx)
{
    if (!useMDSSADU()) { return; }
    MDSSAMgr * mgr = getRegion()->getMDSSAMgr();
    MDSSAUpdateCtx ssactx(const_cast<CfgOptCtx&>(ctx).getOptCtx());
    if (!ctx.needUpdateDomInfo()) {
        //Info MDSSAMgr does not need to maintain DU chain.
        MDSSAUPDATECTX_update_duchain(&ssactx) = false;
    }
    mgr->removePhiList(bb, ssactx);
}


void IRCFG::reviseMDSSA(xcom::VexTab const& vextab, xcom::Vertex const* root,
                        MOD CfgOptCtx & ctx)
{
    if (!useMDSSADU()) { return; }
    START_TIMER_FMT(t, ("IRCFG::Revise MDSSA"));
    ASSERT0(ctx.getOptCtx().is_dom_valid());
    xcom::DomTree domtree;
    genDomTree(domtree);
    ReconstructMDSSAVF vf(vextab, domtree, this, m_rg->getMDSSAMgr(),
                          &ctx.getOptCtx(), ctx.getActMgr());
    ReconstructMDSSA recon(domtree, root, vf);
    recon.reconstruct();
    END_TIMER_FMT(t, ("IRCFG::Revise MDSSA"));
}


void IRCFG::insertBBBetween(IRBB const* from, IRBB * to, IRBB * newbb,
                            OUT CfgOptCtx & ctx)
{
    xcom::Graph::insertVertexBetween(from->id(), to->id(), newbb->id());
    if (!ctx.needUpdateDomInfo()) { return; }
    tryUpdateDomInfoAndMDSSA(from->getVex(), to->getVex(), ctx);
}


void IRCFG::tryUpdateDomInfoAndMDSSA(
    Vertex const* from, Vertex const* to, MOD CfgOptCtx & ctx)
{
    xcom::VexTab modset;
    //NOTE:We prefer to compute RPO at the beginning of pass.
    //RPO is necessary to compute DomInfo and recompute it is not
    //very costly, thus we prefer to recompute RPO.
   ASSERT0(ctx.getOptCtx().is_rpo_valid());
    tryUpdateDomInfoAndMDSSA(modset, from, to, ctx);
}


//NOTE: the function needs RPO to compute DomInfo.
void IRCFG::tryUpdateDomInfoAndMDSSA(
    MOD xcom::VexTab & modset, Vertex const* from, Vertex const* to,
    MOD CfgOptCtx & ctx)
{
    //TBD: the function does NOT need RPO to update DomInfo. It updates
    //DomInfo via current DomInfo incrementally.
    //ASSERTN(ctx.getOptCtx().is_rpo_valid(), ("compute DOM need RPO"));
    ASSERTN(ctx.getOptCtx().is_dom_valid(),
            ("DOM has already been corrupted"));

    //Note if Dom Info is not maintained, SSA update can not be proven
    //to be correct. However, for now we except to keep updating to
    //generate MDSSAInfo (even if version is incorrect) for ID of MDPHI in
    //order to tolerate subsequently processing of CFG.
    xcom::Vertex const* root = nullptr;
    UINT iter_time = 0;
    //NOTE: the function needs RPO to compute DomInfo.
    reviseDomInfoAfterAddOrRemoveEdge(from, to, &modset, root, iter_time);

    //Since PDom info is not important as Dom info, recompute PDom
    //whenever you need it.
    ctx.getOptCtx().setInvalidPDom();
    CFGOPTCTX_vertex_iter_time(&ctx) += iter_time;
    ASSERT0(root);
    reviseMDSSA(modset, root, ctx);
}


void IRCFG::removeEdge(Vertex const* from, Vertex const* to,
                       OUT CfgOptCtx & ctx)
{
    xcom::Edge * e = xcom::Graph::getEdge(from->id(), to->id());
    ASSERT0(e != nullptr);
    xcom::Graph::removeEdge(e);
    if (!ctx.needUpdateDomInfo()) { return; }
    tryUpdateDomInfoAndMDSSA(from, to, ctx);
}


void IRCFG::removeEdge(IRBB * from, IRBB * to, OUT CfgOptCtx & ctx)
{
    bool is_pred;
    UINT npred = WhichPred(from, to, is_pred);
    ASSERTN_DUMMYUSE(is_pred, ("'from' is not predecessor of 'to'"));
    removeSuccDesignatedPhiOpnd(to, npred, ctx);
    removeEdge(from->getVex(), to->getVex(), ctx);
}


xcom::Edge * IRCFG::addEdge(IRBB * from, IRBB * to, OUT CfgOptCtx & ctx)
{
    PRSSAMgr * prssamgr = getRegion()->getPRSSAMgr();
    PRSSAInfoCollect col;
    if (usePRSSADU() && ctx.needUpdateDomInfo()) {
        //Note if dominfo is not maintained, SSA update can not prove to be
        //correct.
        col.init(prssamgr, ctx.getOptCtx());
        col.collect(from, to);
    }
    xcom::Edge * e = xcom::DGraph::addEdge(from->id(), to->id());
    setVertex(from, to, e);

    //After adding BB or changing BB's successor, you need to add the related
    //PHI operand as well if the successor of BB has a PHI stmt.
    if (usePRSSADU()) {
        ASSERT0(prssamgr);
        prssamgr->addSuccessorDesignatedPhiOpnd(from, to, col);
    }
    if (useMDSSADU()) {
        MDSSAStatus st;
        MDSSAMgr * mdssamgr = getRegion()->getMDSSAMgr();
        mdssamgr->addSuccessorDesignatedPhiOpnd(from, to, ctx.getOptCtx(), st);

        //CASE: If DomInfo is not maintained, SSA update can not be proven to
        //be correct.
        if (!ctx.needUpdateDomInfo() && !st.is_succ()) {
            mdssamgr->set_valid(false);
        }
    }
    if (ctx.needUpdateDomInfo()) {
        tryUpdateDomInfoAndMDSSA(from->getVex(), to->getVex(), ctx);
    }
    return e;
}


//CASE: Given pred1->bb, fallthrough edge, and pred2->bb, jumping edge.
//  pred2:
//    goto bb;
//  pred1:
//    a=1;
//  bb:
//    goto next;
//  next:
//    ...
//Remove bb and revise CFG.
//ct: container in m_bb_list of CFG. It will be updated if related BB removed.
static bool removeTrampolinBBCase1(IRCFG * cfg, BBListIter * ct,
                                   OUT CfgOptCtx & ctx)
{
    List<IRBB*> preds;
    IRBB * bb = (*ct)->val();
    ASSERT0(cfg->getSuccsNum(bb) == 1);
    IRBB const* succ = cfg->get_first_succ(bb);
    ASSERT0(succ);
    IR const* uncond_br = cfg->get_first_xr(const_cast<IRBB*>(bb));
    ASSERT0(uncond_br && uncond_br->isUnconditionalBr());
    BBListIter tmp_bb_ct = *ct;
    IRBB * next = cfg->getBBList()->get_next(&tmp_bb_ct);
    if (next == nullptr || //bb can not be the last BB in bb-list.
        next != succ) { //next lexical bb must also be the successor.
        return false;
    }

    Vertex const* vex = next->getVex();
    ASSERT0(vex);
    if (vex->getInDegree() > 1) {
        //successor is not just the branch target of 'bb', thus it
        //can not be removed.
        return false;
    }

    tmp_bb_ct = *ct;
    IRBB * prev = cfg->getBBList()->get_prev(&tmp_bb_ct);
    preds.clean(); //use list because cfg may be modify.
    cfg->get_preds(preds, bb);
    for (IRBB * pred = preds.get_head();
         pred != nullptr; pred = preds.get_next()) {
        cfg->moveLabels(bb, next);
        if (pred->is_fallthrough() && prev == pred) {
            cfg->removeEdge(pred, bb, ctx);

            //Add normal control flow edge.
            xcom::Edge * e = cfg->addEdge(pred, next, ctx);
            CFGEdgeInfo * ei = (CFGEdgeInfo*)EDGE_info(e);
            if (ei != nullptr && CFGEI_is_eh(ei)) {
                //If there is already an edge, check if it is an
                //exception edge. If it is, change the exception edge
                //to be normal control flow edge.
                CFGEI_is_eh(ei) = false;
            }
            return true;
        }

        //CASE:
        // pred:
        //   goto bb;
        //   ...
        // bb:
        //   goto next;
        // next:
        //   ...
        //Remove bb and revise CFG.

        //Revise branch target LabelInfo of xr in 'pred'.
        IR * last_xr_of_pred = cfg->get_last_xr(pred);
        if (last_xr_of_pred != nullptr) {
            ASSERT0(last_xr_of_pred->getLabel());
            ASSERTN(cfg->findBBbyLabel(last_xr_of_pred->getLabel()) == next,
                    ("Labels of bb should have already moved to "
                     "next BB by moveLabels()"));
            last_xr_of_pred->setLabel(uncond_br->getLabel());
        }
        cfg->removeEdge(pred, bb, ctx);

        xcom::Edge * e = cfg->addEdge(pred, next, ctx);
        //TODO: Add operands of PHI if 'next_bb' has PHI.

        CFGEdgeInfo * ei = (CFGEdgeInfo*)EDGE_info(e);
        if (ei != nullptr && CFGEI_is_eh(ei)) {
            //If there is already an edge, check if it is an
            //exception edge. If it is, change the exception edge
            //to be normal control flow edge.
            CFGEI_is_eh(ei) = false;
        }
    }

    //The mapping between Labels and BB has been maintained by above code.
    //removeMapBetweenLabelAndBB(bb);

    cfg->removeBB(*ct, ctx);

    //Update ct to reprocess BB list from beginning.
    cfg->getBBList()->get_head(ct);
    return true;
}


bool IRCFG::removeTrampolinBB(OUT CfgOptCtx & ctx)
{
    bool removed = false;
    BBListIter ct;
    List<IRBB*> succs;
    List<IRBB*> preds;
    for (m_bb_list->get_head(&ct); ct != nullptr;
         ct = m_bb_list->get_next(ct)) {
        IRBB const* bb = ct->val();
        if (bb->isExceptionHandler()) { continue; }

        IR const* uncond_br = get_first_xr(const_cast<IRBB*>(bb));
        if (uncond_br == nullptr ||
            !uncond_br->isUnconditionalBr() ||
            (uncond_br->isIndirectBr() && uncond_br->hasMultiTarget()) ||
            bb->getNumOfIR() != 1) {
            continue;
        }
        removed |= removeTrampolinBBCase1(this, &ct, ctx);
    }
    return removed;
}


//Return true if changed.
//The function will attempt to maintain DomInfo.
static bool removeTrampolinEdgeCase2_1(
    IRCFG * cfg, IR * last_xr, IRBB * bb, IRBB * pred, IRBB * succ,
    OUT CfgOptCtx & ctx)
{
    //CASE: given edge pred->bb--->succ, which pred fall-through to bb.
    //  pred:
    //    S1
    //  bb:
    //    L1:
    //    goto L2
    //=>
    //After: pred   bb--->succ
    //         |__________^
    //  pred:
    //    S1
    //    goto L2
    //  bb:
    //    L1:
    //    goto L2
    BB_irlist(pred).append_tail(cfg->getRegion()->dupIRTree(last_xr));
    CfgOptCtx tctx(ctx);
    cfg->removeEdge(pred, bb, tctx);
    cfg->addEdge(pred, succ, tctx);
    ctx.unionBottomUpInfo(tctx);
    if (bb->getNumOfPred() == 0) {
        //The optimization produces unreachable BB.
        //Unreachable-BB will corrupt RPO, thus user should recompute RPO,
        //DomInfo, LoopInfo etc before perform other CFG optimizations.
        CFGOPTCTX_has_generate_unreach_bb(&ctx) = true;
        ctx.getOptCtx().setInvalidIfCFGChanged();
    }
    return true;
}


//Return true if changed.
//The function does not change DomInfo.
static bool removeTrampolinEdgeCase2_2(
    IRCFG * cfg, IR * last_xr, IR * last_xr_of_pred, IRBB * bb, IRBB * pred,
    IRBB * succ, OUT CfgOptCtx & ctx)
{
    LabelInfo const* tgt_li = last_xr->getLabel();

    //CASE: pred->bb->succ, pred jump to bb.
    //  pred:
    //    goto L1
    //  bb:
    //    L1:
    //    goto L2
    //=>
    //After: pred  bb->succ
    //         |________^
    //  pred:
    //    goto L2
    //  bb:
    //    L1:
    //    goto L2
    ASSERT0(last_xr_of_pred->getLabel() &&
            cfg->findBBbyLabel(last_xr_of_pred->getLabel()) == bb);
    ASSERT0(last_xr_of_pred->getLabel() != nullptr);
    if (succ->id() == bb->id()) {
        //CASE: pred->bb--
        //             ^__|
        //pred jump to bb, and bb's target is itself.
        //  pred:
        //    goto L1;
        //  bb:
        //    L1:
        //    goto L1;
        //Nothing to do.
        return false;
    }
    GOTO_lab(last_xr_of_pred) = tgt_li;

    //Remove pred->bb edge. And there may still other preds jump to bb.
    //Thus do not remove bb->succ edge.
    cfg->removeEdge(pred, bb, ctx);
    cfg->addEdge(pred, succ, ctx);
    if (bb->getNumOfPred() == 0) {
        //The optimization produces unreachable BB.
        //Unreachable-BB will corrupt RPO, thus user should recompute RPO,
        //DomInfo, LoopInfo etc before perform other CFG optimizations.
        CFGOPTCTX_has_generate_unreach_bb(&ctx) = true;
        ctx.getOptCtx().setInvalidIfCFGChanged();
    }
    return true;
}


//Return true if changed.
//The function does not change DomInfo.
static bool removeTrampolinEdgeCase2_3(
    IRCFG * cfg, IR * last_xr, IR * last_xr_of_pred, IRBB * bb, IRBB * pred,
    IRBB * succ, BBListIter bbct, OUT CfgOptCtx & ctx)
{
    LabelInfo const* tgt_li = last_xr->getLabel();
    // CASE: pred->f->bb------>L2_BB
    //         |______^
    //  pred:
    //    truebr/falsebr L1
    //  f:
    //    ...
    //    ...
    //  bb:
    //    L1:
    //    goto L2
    //=>
    //After: pred->f->bb------>L2_BB
    //         |_______________^
    //  pred:
    //    truebr/falsebr L2
    //  f:
    //    ...
    //    ...
    //  bb:
    //    L1:
    //    goto L2
    BBListIter prev_of_bb = bbct;
    if (cfg->getBBList()->get_prev(&prev_of_bb) == pred) {
        //Can not remove jumping-edge if 'bb' is
        //fall-through successor of 'pred'.
        return false;
    }
    ASSERT0(last_xr_of_pred->getLabel() &&
            cfg->findBBbyLabel(last_xr_of_pred->getLabel()) == bb);
    ASSERT0(last_xr_of_pred->getLabel() != nullptr);
    if (bb == succ) { return false; }

    //bb should not be the same one with succ.
    BR_lab(last_xr_of_pred) = tgt_li;

    //Link up bb's pred and succ.
    cfg->removeEdge(pred, bb, ctx);
    cfg->addEdge(pred, succ, ctx);

    //CASE:compile/tramp_edge.c
    //Remove trampoline-edge will change LoopInfo, CDG etc.
    //NOTE: DomInfo will be handled via the status of ctx.needUpdateDomInfo(),
    //thus there is no need to invalid it here.
    //NOTE: The case does NOT change RPO.
    OptCtx::setInvalidIfCFGChangedExcept(
        &ctx.getOptCtx(), PASS_DOM, PASS_RPO, PASS_UNDEF);
    return true;
}


//CASE: Given pred1->bb, fallthrough edge, and pred2->bb, jumping edge.
//  bb:
//    goto L1
//  next of bb:
//    L1:
//    ...
//    ...
//Remove bb and revise CFG.
//ct: container in m_bb_list of CFG.
//    It will be updated if related BB removed.
//Return true if changed.
//The function does not change DomInfo.
static bool removeTrampolinEdgeCase2(
    IRCFG * cfg, BBListIter bbct, OUT CfgOptCtx & ctx)
{
    ASSERT0(bbct);
    IRBB * bb = bbct->val();
    bool removed = false;
    List<IRBB*> preds; //record preds in list because CFG may be modified.
    cfg->get_preds(preds, bb);
    IRBB * succ = cfg->get_first_succ(bb);
    ASSERT0(succ);
    if (succ == bb) {
        //CASE: BB_pred
        //       |
        //       V
        //      BB <-
        //       |   |
        //       |___|
        //BB and succ are the same BB.
        return false;
    }
    IR * last_xr = cfg->get_last_xr(bb);
    ASSERT0(last_xr);
    LabelInfo const* tgt_li = last_xr->getLabel();
    ASSERT0_DUMMYUSE(tgt_li);
    ASSERT0(cfg->findBBbyLabel(tgt_li) == succ);
    for (IRBB * pred = preds.get_head();
         pred != nullptr; pred = preds.get_next()) {
        if (pred == bb) {
            //bb's pred is itself.
            continue;
        }
        if (pred->getNumOfIR() == 0) {
            continue;
        }
        IR * last_xr_of_pred = cfg->get_last_xr(pred);
        if (!IRBB::isLowerBoundary(last_xr_of_pred)) {
            removed |= removeTrampolinEdgeCase2_1(
                cfg, last_xr, bb, pred, succ, ctx);
            continue;
        }
        if (last_xr_of_pred->is_goto()) {
            removed |= removeTrampolinEdgeCase2_2(
                cfg, last_xr, last_xr_of_pred, bb, pred, succ, ctx);
            continue;
        }
        if (last_xr_of_pred->isConditionalBr()) {
            removed |= removeTrampolinEdgeCase2_3(
                cfg, last_xr, last_xr_of_pred, bb, pred, succ, bbct, ctx);
            continue;
        }
    }
    return removed;
}


//CASE: Given pred1->bb, fallthrough edge,
//  and pred2->bb, jumping edge.
//  bb:
//    goto L1
//  next of bb:
//    L1:
//    ...
//    ...
//Remove bb and revise CFG.
//ct: container in m_bb_list of CFG.
//    It will be updated if related BB removed.
//Return true if changed.
//The function does not change DomInfo.
static bool removeTrampolinEdgeCase1(
    IRCFG * cfg, BBListIter bbct, CfgOptCtx const& ctx)
{
    ASSERT0(bbct);
    IRBB * bb = bbct->val();
    BBListIter next_ct = cfg->getBBList()->get_next(bbct);
    if (next_ct == nullptr) { return false; }

    //BB is not the last one in BB list.
    IR * last_xr = cfg->get_last_xr(bb);
    ASSERT0(last_xr && last_xr->is_goto());

    LabelInfo const* tgt_li = last_xr->getLabel();
    ASSERT0(tgt_li != nullptr);

    IRBB * target = cfg->findBBbyLabel(tgt_li);
    if (target == next_ct->val()) {
        Vertex const* vex = target->getVex();
        ASSERT0(vex);
        if (vex->getInDegree() > 1) {
            //'target' is not just the branch target of 'bb', thus
            //it can not be removed.
            return false;
        }

        //CASE1:Remove the redundant GOTO.
        //  e.g: region func main () {
        //    truebr (eq $1, $2), L2;
        //    goto L1; //goto is actually fallthrough to label L1.
        //             //So it can be removed.
        //    label L1;
        //    goto L1;
        //    label L2;
        //  };
        ASSERT0(bb->getNumOfIR() == 1);
        BB_irlist(bb).remove_tail();
        cfg->getRegion()->freeIRTree(last_xr);
        return true;
    }
    return false;
}


//NOTE: the function needs RPO to compute DomInfo.
bool IRCFG::removeTrampolinEdge(OUT CfgOptCtx & ctx)
{
    if (ctx.needUpdateDomInfo()) {
        //Since RPO is necessary to compute DomInfo and recompute it is not
        //very costly, thus we prefer to recompute RPO.
        ctx.getOptCtx().getRegion()->getPassMgr()->checkValidAndRecompute(
            &ctx.getOptCtx(), PASS_RPO, PASS_UNDEF);
    }
    bool removed = false;
    BBListIter ct;
    for (m_bb_list->get_head(&ct);
         ct != m_bb_list->end(); ct = m_bb_list->get_next(ct)) {
        IRBB * bb = ct->val();
        if (bb->getNumOfIR() != 1) {
            //BB is almost not a trampoline BB if there are multiples IRs.
            continue;
        }
        IR * last_xr = get_last_xr(bb);
        ASSERT0(last_xr);
        if (last_xr->hasSideEffect(true) || !last_xr->is_goto() ||
            bb->isAttachDedicatedLabel()) {
            //BB has sideeffect and should not be processed.
            continue;
        }
        if (removeTrampolinEdgeCase1(this, ct, ctx)) {
            removed = true;
            if (ctx.has_generate_unreach_bb()) {
                return removed;
            }
            continue;
        }
        if (removeTrampolinEdgeCase2(this, ct, ctx)) {
            removed = true;
            if (ctx.has_generate_unreach_bb()) {
                return removed;
            }
            continue;
        }
    }
    return removed;
}


//The function will maintain DomInfo.
bool IRCFG::removeRedundantBranchCase2(
    IRBB * bb, IR * last_xr, bool always_true, bool always_false,
    BBListIter const& ct, OUT CfgOptCtx & ctx, MOD List<IRBB*> & succs)
{
    bool removed = false;
    if ((last_xr->is_truebr() && always_false) ||
        (last_xr->is_falsebr() && always_true)) {
        IR * r = BB_irlist(bb).remove_tail();
        xoc::removeStmt(r, m_rg, ctx.getOptCtx());
        m_rg->freeIRTree(r);

        //Remove branch edge, leave fallthrough edge.
        get_succs(succs, bb);
        BBListIter tmp_ct = ct;
        m_bb_list->get_next(&tmp_ct);
        for (IRBB * s = succs.get_head();
             s != nullptr; s = succs.get_next()) {
            if (s != tmp_ct->val()) {
                //Remove branch edge, leave fallthrough edge.
                //DomInfo keeps unchanged when removing these edges.
                removeEdge(bb, s, ctx);
            }
        }
        removed = true;
        return removed;
    }
    return removed;
}


//The function will maintain DomInfo.
bool IRCFG::removeRedundantBranchCase1(
    IRBB * bb, IR * last_xr, bool always_true, bool always_false,
    BBListIter const& ct, OUT CfgOptCtx & ctx, MOD List<IRBB*> & succs)
{
    bool removed = false;
    if ((last_xr->is_truebr() && always_true) ||
        (last_xr->is_falsebr() && always_false)) {
        //Substitute cond_br with 'goto'.
        LabelInfo const* tgt_li = last_xr->getLabel();
        ASSERT0(tgt_li != nullptr);
        BB_irlist(bb).remove_tail();
        xoc::removeStmt(last_xr, m_rg, ctx.getOptCtx());
        m_rg->freeIRTree(last_xr);

        IR * uncond_br = m_rg->getIRMgr()->buildGoto(tgt_li);
        BB_irlist(bb).append_tail(uncond_br);

        //Remove fallthrough edge, leave branch edge.
        get_succs(succs, bb);
        BBListIter tmp_ct = ct;
        m_bb_list->get_next(&tmp_ct);
        for (IRBB * s = succs.get_head();
             s != nullptr; s = succs.get_next()) {
            if (s == tmp_ct->val()) {
                //Remove branch edge, leave fallthrough edge.
                //DomInfo keeps unchanged when removing these edges.
                removeEdge(bb, s, ctx);
            }
        }
        removed = true;
        return removed;
    }
    return removed;
}


bool IRCFG::removeRedundantBranch(OUT CfgOptCtx & ctx)
{
    bool removed = OptimizedCFG<IRBB, IR>::removeRedundantBranch(ctx);
    BBListIter ct;
    List<IRBB*> succs;
    for (IRBB * bb = m_bb_list->get_head(&ct);
         bb != nullptr; bb = m_bb_list->get_next(&ct)) {
        IR * last_xr = get_last_xr(bb);
        if (last_xr == nullptr || !last_xr->isConditionalBr() ||
            last_xr->hasSideEffect(true)) {
            continue;
        }
        IR * det = BR_det(last_xr);
        ASSERT0(det != nullptr);
        bool always_true = (det->is_const() && det->is_int() &&
                            CONST_int_val(det) != 0) ||
                            det->is_str();
        bool always_false = det->is_const() && det->is_int() &&
                            CONST_int_val(det) == 0;
        if (removeRedundantBranchCase1(
                bb, last_xr, always_true, always_false, ct, ctx, succs)) {
            //The function will maintain DomInfo.
            removed = true;
            continue;
        }
        if (removeRedundantBranchCase2(
                bb, last_xr, always_true, always_false, ct, ctx, succs)) {
            //The function will maintain DomInfo.
            removed = true;
            continue;
        }
    }
    return removed;
}


bool IRCFG::verifyDomAndPdom(OptCtx const& oc) const
{
    if (oc.is_dom_valid()) {
        if (!DGraph::verifyDom()) {
            return false;
        }
    }
    if (oc.is_pdom_valid()) {
        if (!DGraph::verifyPdom()) {
            return false;
        }
    }
    return true;
}


bool IRCFG::verifyLoopInfo(OptCtx const& oc) const
{
    ASSERT0(xoc::verifyLoopInfoTree(getLoopInfo(), oc));
    ASSERT0(xoc::verifyLoopInfoTreeByRecomp(this, getLoopInfo(), oc));
    return true;
}


void IRCFG::dumpDomSet() const
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpCFG()) { return; }
    note(m_rg, "\n==-- DUMP DOM&IDOM PDOM&IPDOM IN IRCFG --==");
    m_rg->getLogMgr()->incIndent(2);
    CFG<IRBB, IR>::dumpDomSet(m_rg);
    m_rg->getLogMgr()->decIndent(2);
}


void IRCFG::dumpSubGraph(BBSet const& subset, FILE * h, UINT flag) const
{
    if (!getRegion()->isLogMgrInit() || !g_dump_opt.isDumpCFG() ||
        h == nullptr) {
        return;
    }
    getRegion()->getLogMgr()->push(h, "");

    bool detail = HAVE_FLAG(flag, DUMP_DETAIL);
    if (detail) {
        //Print comment
        note(getRegion(), "\n/*");
        xoc::dumpBBSet(subset, m_rg);
        note(getRegion(), "\n*/\n");
    }

    //Start to dump DOT.
    note(getRegion(), "digraph G {\n");

    //There is no need to define the layout mode of DOT, use the default set.
    //LR refers to layout from Left to Right,
    //TB refers to layout from top to bottom.
    //BT refers to layout from bottom to top.
    //CHAR const* layout = "LR"; //TB, BT
    //fprintf(h, "rankdir=%s;\n", layout);

    //Print Region name.
    note(getRegion(),
         "\nstartnode [fontsize=24,style=filled, "
         "color=gold,shape=none,label=\"RegionName:%s\"];",
         m_rg->getRegionName());

    //Print carriage-return for DOT file, use \l instead of \n to
    //avoid DOT complaint.
    bool org_carr = getRegion()->getLogMgr()->isReplaceNewline();
    getRegion()->getLogMgr()->setReplaceNewline(true);
    dumpDotNodeInSubGraph(subset, h, flag, this);
    dumpDotEdgeInSubGraph(subset, h, flag, this);
    getRegion()->getLogMgr()->pop();
    getRegion()->getLogMgr()->setReplaceNewline(org_carr);
    fprintf(h,"\n}\n");
    fflush(h);
}


void IRCFG::dumpSubGraph(BBSet const& subset, CHAR const* name, UINT flag) const
{
     //Do not dump if LogMr is not initialized.
    if (!getRegion()->isLogMgrInit() || !g_dump_opt.isDumpCFG()) { return; }

    //Note this function does not use LogMgr as output.
    //So it is dispensable to check LogMgr.
    if (name == nullptr) {
        name = "graph_cfg.sub.dot";
    }
    FileObj fo(name, true, false);
    FILE * h = fo.getFileHandler();
    ASSERTN(h, ("%s create failed!!!", name));
    dumpSubGraph(subset, h, flag);
}


void IRCFG::dumpDOT(CHAR const* name, UINT flag) const
{
    //Do not dump if LogMr is not initialized.
    if (!getRegion()->isLogMgrInit() || !g_dump_opt.isDumpCFG()) { return; }

    //Note this function does not use LogMgr as output.
    //So it is dispensable to check LogMgr.
    if (name == nullptr) {
        name = "graph_cfg.dot";
    }
    FileObj fo(name, true, false);
    FILE * h = fo.getFileHandler();
    ASSERTN(h, ("%s create failed!!!", name));
    dumpDOT(h, flag);
}


//Note the function will use \l instead of \n to avoid DOT complaint.
static void dumpDotNode(FILE * h, UINT flag, IRCFG const* cfg)
{
    bool dump_mdssa = HAVE_FLAG(flag, IRCFG::DUMP_MDSSA);
    MDSSAMgr const* mdssamgr = (MDSSAMgr const*)cfg->getRegion()->getMDSSAMgr();
    if (mdssamgr == nullptr || !mdssamgr->is_valid()) {
        dump_mdssa = false;
    }
    bool dump_detail = HAVE_FLAG(flag, IRCFG::DUMP_DETAIL);
    //Print node
    VertexIter c = VERTEX_UNDEF;
    for (xcom::Vertex const* v = cfg->get_first_vertex(c);
         v != nullptr; v = cfg->get_next_vertex(c)) {
        dumpVertex(v, flag, cfg, dump_detail, dump_mdssa, mdssamgr, h);
    }
}


static void dumpDotEdgeByWholeGraph(
    OUT xcom::TTab<xcom::Edge const*> & visited, FILE * h, UINT flag,
    IRCFG const* cfg)
{
    xcom::VertexIter it = VERTEX_UNDEF;
    for (xcom::Vertex const* v = cfg->get_first_vertex(it);
         v != nullptr; v = cfg->get_next_vertex(it)) {
        //Print in-edge list.
        UINT pos = 0;
        for (xcom::EdgeC const* ec = v->getInList();
             ec != nullptr; ec = ec->get_next(), pos++) {
            if (visited.find(ec->getEdge())) { continue; }
            visited.append(ec->getEdge());
            xcom::Edge const* e = ec->getEdge();
            dumpDotEdgeImpl(e, h, flag, &pos);
        }
    }
}


//Print rest of edges.
static void dumpRestDotEdge(
    xcom::TTab<xcom::Edge const*> const& visited, FILE * h, UINT flag,
    IRCFG const* cfg)
{
    EdgeIter ite;
    for (xcom::Edge const* e = cfg->get_first_edge(ite);
         e != nullptr;  e = cfg->get_next_edge(ite)) {
        if (visited.find(e)) { continue; }
        dumpDotEdgeImpl(e, h, flag, nullptr);
    }
}


static void dumpDOTComments(Region const* rg, BBList const* bblst)
{
    note(rg, "\n/*");
    dumpBBList(bblst, rg);
    note(rg, "\n*/\n");
}


static void dumpDOTBegionDesc(Region const* rg)
{
    note(rg, "digraph G {\n");

    //There is no need to define the layout mode of DOT, use the default set.
    //LR refers to layout from Left to Right,
    //TB refers to layout from top to bottom.
    //BT refers to layout from bottom to top.
    //CHAR const* layout = "LR"; //TB, BT
    //note(rg, "rankdir=%s;\n", layout);
}


static void dumpDOTRegionName(Region const* rg)
{
    ASSERT0(rg);
    note(rg,
         "\nstartnode [fontsize=24,style=filled, "
         "color=gold,shape=none,label=\"RegionName:%s\"];",
         rg->getRegionName());
}


static void dumpDOTEndDesc(FILE * h)
{
    fprintf(h,"\n}\n");
    fflush(h);
}


//Note the function will use \l instead of \n to avoid DOT complaint.
static void dumpDotEdge(FILE * h, UINT flag, IRCFG const* cfg)
{
    xcom::TTab<xcom::Edge const*> visited;
    dumpDotEdgeByWholeGraph(visited, h, flag, cfg);
    dumpRestDotEdge(visited, h, flag, cfg);
}


void IRCFG::dumpDOT(FILE * h, UINT flag) const
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpCFG()) {
        return;
    }
    ASSERT0(h);
    m_rg->getLogMgr()->push(h, "");
    bool detail = HAVE_FLAG(flag, DUMP_DETAIL);
    if (detail) {
        dumpDOTComments(m_rg, getBBList());
    }
    //Start to dump DOT.
    dumpDOTBegionDesc(m_rg);
    dumpDOTRegionName(m_rg);

    //Print carriage-return for DOT file, use \l instead of \n to
    //avoid DOT complaint.
    bool org_carr = getRegion()->getLogMgr()->isReplaceNewline();
    getRegion()->getLogMgr()->setReplaceNewline(true);
    dumpDotNode(h, flag, this);
    dumpDotEdge(h, flag, this);
    m_rg->getLogMgr()->pop();
    m_rg->getLogMgr()->setReplaceNewline(org_carr);
    dumpDOTEndDesc(h);
}


static void dumpVCGNodeWithDetail(
    UINT flag, IRBB const* bb, VCGDesc const& vd, IRCFG const* cfg, FILE * h)
{
    ASSERT0(h);
    bool dump_mdssa = HAVE_FLAG(flag, IRCFG::DUMP_MDSSA);
    MDSSAMgr const* mdssamgr = (MDSSAMgr const*)cfg->getRegion()->getMDSSAMgr();
    if (mdssamgr == nullptr || !mdssamgr->is_valid()) {
        dump_mdssa = false;
    }
    fprintf(h,
            "\nnode: {title:\"%d\" vertical_order:%d shape:%s color:%s "
            "fontname:\"%s\" scaling:%d label:\"",
            bb->id(), vd.vertical_order, vd.shape, vd.color, vd.font, vd.scale);
    fprintf(h, "   BB%d ", bb->rpo());
    fprintf(h, " rpo:%d ", bb->rpo());
    dumpBBLabel(const_cast<IRBB*>(bb)->getLabelList(), cfg->getRegion());
    fprintf(h, "\n");

    //Dump MDSSA Phi List.
    if (dump_mdssa) {
        ASSERT0(mdssamgr);
        MDPhiList const* philist = mdssamgr->getPhiList(bb);
        if (philist != nullptr) {
            mdssamgr->dumpPhiList(philist);
        }
    }

    //Dump IR list.
    for (IR * ir = BB_first_ir(const_cast<IRBB*>(bb));
         ir != nullptr; ir = BB_next_ir(const_cast<IRBB*>(bb))) {
        //fprintf(h, "%s\n", dump_ir_buf(ir, buf));

        //TODO: implement dump_ir_buf();
        if (dump_mdssa) {
            ASSERT0(mdssamgr);
            mdssamgr->dumpIRWithMDSSA(ir, DumpFlag::combineIRID(IR_DUMP_KID));
        } else {
            dumpIR(ir, cfg->getRegion(), nullptr,
                   DumpFlag::combineIRID(IR_DUMP_KID));
        }
    }
    fprintf(h, "\"}");
    fflush(h);
}


//Dump node without detail.
static void dumpVCGNodeBrief(
    xcom::Vertex const* v, IRBB const* bb, VCGDesc const& vd, FILE * h)
{
    ASSERT0(h);
    fprintf(h,
            "\nnode: {title:\"%d\" vertical_order:%d shape:%s color:%s "
            "fontname:\"%s\" scaling:%d label:\"%d",
            v->id(), vd.vertical_order, vd.shape, vd.color, vd.font, vd.scale,
            v->id());
    fprintf(h, " rpo:%d", bb->rpo());
    fprintf(h, "\" }");
}


static void dumpVCGNode(UINT flag, IRCFG const* cfg)
{
    bool detail = HAVE_FLAG(flag, IRCFG::DUMP_DETAIL);
    FILE * h = cfg->getRegion()->getLogMgr()->getFileHandler();
    ASSERT0(h);
    UINT total_vertical_order = 1;
    VertexIter c;
    for (xcom::Vertex const* v = cfg->get_first_vertex(c);
        v != nullptr; v = cfg->get_next_vertex(c)) {
        IRBB * bb = cfg->getBB(v->id());
        ASSERTN(bb, ("Not find BB%d", v->id()));
        VCGDesc vd;
        vd.vertical_order = total_vertical_order;
        vd.shape = "box";
        vd.font = "courB";
        vd.color = "gold";
        vd.scale = 1;
        if (BB_is_catch_start(bb)) {
            vd.shape = "uptrapezoid";
        }
        if (BB_is_entry(bb) || BB_is_exit(bb)) {
            vd.font = "Times Bold";
            vd.scale = 2;
            vd.color = "cyan";
        }
        if (detail) {
            dumpVCGNodeWithDetail(flag, bb, vd, cfg, h);
            continue;
        }
        dumpVCGNodeBrief(v, bb, vd, h);
        total_vertical_order++;
    }
}


static void dumpVCGRegionName(FILE * h, Region const* rg)
{
    ASSERT0(h && rg);
    fprintf(h,
            "\nnode: {title:\"\" vertical_order:0 shape:box color:turquoise "
            "borderwidth:0 fontname:\"Courier Bold\" "
            "scaling:2 label:\"RegionName:%s\" }",
            rg->getRegionName());
}


static void dumpVCGEndDesc(FILE * h)
{
    ASSERT0(h);
    fprintf(h, "\n}\n");
}


//Print graph structure description.
static void dumpVCGBeginDesc(FILE * h)
{
    ASSERT0(h);
    fprintf(h,
            "graph: {"
            "title: \"Graph\"\n"
            "shrink: 15\n"
            "stretch: 27\n"
            "layout_downfactor: 1\n"
            "layout_upfactor: 1\n"
            "layout_nearfactor: 1\n"
            "layout_splinefactor: 70\n"
            "spreadlevel: 1\n"
            "treefactor: 0.500000\n"
            "node_alignment: center\n"
            "orientation: top_to_bottom\n"
            "late_edge_labels: no\n"
            "display_edge_labels: yes\n"
            "dirty_edge_labels: no\n"
            "finetuning: no\n"
            "nearedges: no\n"
            "splines: yes\n"
            "ignoresingles: no\n"
            "straight_phase: no\n"
            "priority_phase: no\n"
            "manhatten_edges: no\n"
            "smanhatten_edges: no\n"
            "port_sharing: no\n"
            "crossingphase2: yes\n"
            "crossingoptimization: yes\n"
            "crossingweight: bary\n"
            "arrow_mode: free\n"
            "layoutalgorithm: mindepthslow\n"
            "node.borderwidth: 2\n"
            "node.color: lightcyan\n"
            "node.textcolor: black\n"
            "node.bordercolor: blue\n"
            "edge.color: darkgreen\n");
}


static void dumpVCGEdge(UINT flag, IRCFG const* cfg)
{
    bool dump_eh = HAVE_FLAG(flag, IRCFG::DUMP_EH);
    FILE * h = cfg->getRegion()->getLogMgr()->getFileHandler();
    ASSERT0(h);
    VertexIter it;
    TTab<xcom::Edge const*> visited;
    UINT pos = 0;
    for (xcom::Vertex * v = cfg->get_first_vertex(it);
         v != nullptr; v = cfg->get_next_vertex(it), pos++) {
        //Print in-edge list.
        UINT pos2 = 0;
        for (xcom::EdgeC const* ec = v->getInList();
             ec != nullptr; ec = ec->get_next(), pos2++) {
            if (visited.find(ec->getEdge())) { continue; }
            visited.append(ec->getEdge());
            xcom::Edge const* e = ec->getEdge();

            CFGEdgeInfo * ei = (CFGEdgeInfo*)e->info();
            if (ei == nullptr) {
                fprintf(h,
                        "\nedge: { sourcename:\"%d\" targetname:\"%d\" "
                        " thickness:4 color:darkred label:\"p%d\" }",
                        e->from()->id(), e->to()->id(), pos2);
                continue;
            }

            if (CFGEI_is_eh(ei)) {
                if (dump_eh) {
                    fprintf(h,
                            "\nedge: { sourcename:\"%d\" targetname:\"%d\" "
                            "linestyle:dotted color:lightgrey }",
                            e->from()->id(), e->to()->id());
                }
                continue;
            }

            fprintf(h,
                    "\nedge: { sourcename:\"%d\" targetname:\"%d\" "
                    " thickness:4 color:darkred }",
                    e->from()->id(), e->to()->id());
            ASSERTN(0, ("unsupported CFGEdgeInfo"));
        }
    }

    //Print rest of edges.
    EdgeIter c;
    for (xcom::Edge * e = cfg->get_first_edge(c);
         e != nullptr; e = cfg->get_next_edge(c)) {
        if (visited.find(e)) { continue; }

        CFGEdgeInfo * ei = (CFGEdgeInfo*)EDGE_info(e);
        if (ei == nullptr) {
            fprintf(h,
                    "\nedge: { sourcename:\"%d\" targetname:\"%d\" "
                    " thickness:4 color:darkred }",
                    e->from()->id(), e->to()->id());
            continue;
        }

        if (CFGEI_is_eh(ei)) {
            if (dump_eh) {
                fprintf(h,
                        "\nedge: { sourcename:\"%d\" targetname:\"%d\" "
                        "linestyle:dotted color:lightgrey }",
                        e->from()->id(), e->to()->id());
            }
            continue;
        }

        fprintf(h,
                "\nedge: { sourcename:\"%d\" targetname:\"%d\" "
                " thickness:4 color:darkred }",
                e->from()->id(), e->to()->id());
        ASSERTN(0, ("unsupported CFGEdgeInfo"));
    }
}


void IRCFG::dumpVCG(CHAR const* name, UINT flag) const
{
    if (!getRegion()->isLogMgrInit()) { return; }
    ASSERT0(m_rg);
    if (name == nullptr) { name = "graph_cfg.vcg"; }

    //Note this function does not use LogMgr as output.
    //So it is dispensable to check LogMgr.
    FileObj fo(name, true, false);
    FILE * h = fo.getFileHandler();
    ASSERTN(h, ("%s create failed!!!",name));
    dumpVCGBeginDesc(h);
    dumpVCGRegionName(h, m_rg);
    getRegion()->getLogMgr()->push(h, "");
    dumpVCGNode(flag, this);
    dumpVCGEdge(flag, this);
    getRegion()->getLogMgr()->pop();
    dumpVCGEndDesc(h);
}


void IRCFG::dumpGraph(FILE * h) const
{
    ASSERT0(h);
    dumpAllVertices(h);
    dumpAllEdges(h);
}


void IRCFG::dumpGraph() const
{
    if (!getRegion()->isLogMgrInit()) { return; }
    note(getRegion(), "\n-- DUMP ALL VERTEX AND ALL EDGE --");
    getRegion()->getLogMgr()->incIndent(2);
    DefFixedStrBuf buf;
    dumpAllVertices(buf);
    dumpAllEdges(buf);
    note(getRegion(), "%s", buf.getBuf());
    getRegion()->getLogMgr()->decIndent(2);
}


void IRCFG::dumpForTest(UINT flag) const
{
    if (!getRegion()->isLogMgrInit()) { return; }
    bool detail = HAVE_FLAG(flag, DUMP_DETAIL);
    if (detail) {
        dumpBBList((BBList*)m_bb_list, m_rg);
    }
    dumpGraph();
}


bool IRCFG::dump() const
{
    if (!getRegion()->isLogMgrInit()) { return false; }
    START_TIMER_FMT(t, ("DUMP %s", getPassName()));
    note(getRegion(), "\n==---- DUMP %s '%s' ----==",
         getPassName(), m_rg->getRegionName());
    m_rg->getLogMgr()->incIndent(2);
    //dumpDOT(getRegion()->getLogMgr()->getFileHandler(), DUMP_COMBINE);
    dumpForTest(DUMP_COMBINE);
    m_lab2bb.dump(m_rg);
    if (g_dump_opt.isDumpDOM()) {
        dumpDomSet();
    }
    m_rg->getLogMgr()->decIndent(2);
    END_TIMER_FMT(t, ("DUMP %s", getPassName()));
    return true;
}


//Record the Exit BB here.
void IRCFG::computeExitList()
{
    //Clean the Exit flag.
    BBListIter ct = nullptr;
    for (m_exit_list.get_head(&ct);
         ct != m_exit_list.end();
         ct = m_exit_list.get_next(ct)) {
        IRBB * bb = ct->val();
        ASSERT0(bb);
        BB_is_exit(bb) = false;
    }

    CFG<IRBB, IR>::computeExitList();

    //Record the Exit flag as BB attribute to speed up accessing.
    for (m_exit_list.get_head(&ct);
         ct != m_exit_list.end();
         ct = m_exit_list.get_next(ct)) {
        IRBB * bb = ct->val();
        ASSERT0(bb);
        BB_is_exit(bb) = true;
    }
}


void IRCFG::cloneLab2BB(Lab2BB const& src)
{
    m_lab2bb.clean();
    ASSERT0(&m_lab2bb != &src);
    xcom::TMap<UINT, IRBB*> bbmap;
    BBListIter it;
    for (IRBB * bb = getBBList()->get_head(&it); bb != nullptr;
         bb = getBBList()->get_next(&it)) {
        bbmap.set(bb->id(), bb);
    }
    Lab2BBIter lit;
    IRBB * srcbb;
    for (LabelInfo const* li = src.get_first(lit, &srcbb);
         !lit.end(); li = src.get_next(lit, &srcbb)) {
        IRBB * tgtbb = bbmap.get(srcbb->id());
        ASSERTN(tgtbb, ("src CFG is not identical to current CFG"));
        m_lab2bb.set(li, tgtbb);
    }
}


void IRCFG::clone(IRCFG const& src, bool clone_edge_info, bool clone_vex_info)
{
    CFG<IRBB, IR>::clone(src, clone_edge_info, clone_vex_info);
    m_tm = src.getRegion()->getTypeMgr();
    setBitSetMgr(src.getBitSetMgr());
    cloneLab2BB(src.m_lab2bb);
}


void IRCFG::computeDomAndIdom(MOD OptCtx & oc, xcom::BitSet const* uni)
{
    if (getBBList()->get_elem_count() == 0) { return; }

    DUMMYUSE(uni);
    START_TIMER(t, "Compute Dom, IDom");
    ASSERT0(oc.is_cfg_valid());
    ASSERTN(m_entry, ("ONLY support SESE or SEME"));
    m_rg->getPassMgr()->checkValidAndRecompute(&oc, PASS_RPO, PASS_UNDEF);
    RPOVexList * vlst = getRPOVexList();
    ASSERT0(vlst);
    ASSERT0(vlst->get_elem_count() == getBBList()->get_elem_count());

    //TBD:Try different methods.
    //xcom::DGraph::computeDom(&vlst, uni);
    //xcom::DGraph::computeIdom();
    bool f = xcom::DGraph::computeIdom2(*vlst);
    DUMMYUSE(f);
    ASSERT0(f);

    f = xcom::DGraph::computeDom2(*vlst);
    DUMMYUSE(f);
    ASSERT0(f);

    oc.setValidPass(PASS_DOM);
    END_TIMER(t, "Compute Dom, IDom");
}


void IRCFG::dumpRPOVexList(RPOVexList const& vlst) const
{
    if (!m_rg->isLogMgrInit()) { return; }
    xcom::RPOMgr::dumpRPOVexList(
        m_rg->getLogMgr()->getFileHandler(), vlst,
        m_rg->getLogMgr()->getIndent());
}


void IRCFG::dumpRPO() const
{
    if (!m_rg->isLogMgrInit()) { return; }
    note(m_rg, "\n==---- DUMP RPO ----==");
    ASSERT0(getBBList());
    m_rg->getLogMgr()->incIndent(2);
    BBListIter it;
    note(m_rg, "\n-- DUMP RPO IN BB LEX ORDER, BBNUM(%u) --",
         getBBList()->get_elem_count());
    m_rg->getLogMgr()->incIndent(2);
    for (IRBB const* bb = getBBList()->get_head(&it);
         bb != nullptr; bb = getBBList()->get_next(&it)) {
        note(m_rg, "\nBB%u -- rpo:%d --", bb->id(), bb->rpo());
    }
    m_rg->getLogMgr()->decIndent(2);
    CFG<IRBB, IR>::dumpRPOVexList(m_rg);
    m_rg->getLogMgr()->decIndent(2);
}


void IRCFG::computeRPO(OptCtx & oc)
{
    CFG<IRBB, IR>::computeRPO(oc);
    if (g_dump_opt.isDumpAfterPass() && g_dump_opt.isDumpRPO()) {
        dumpRPO();
    }
}


void IRCFG::computePdomAndIpdom(MOD OptCtx & oc, xcom::BitSet const* uni)
{
    if (getBBList()->get_elem_count() == 0) { return; }
    START_TIMER(t, "Compute PDom,IPDom");
    ASSERT0(oc.is_cfg_valid());

    m_rg->getPassMgr()->checkValidAndRecompute(&oc, PASS_RPO, PASS_UNDEF);
    RPOVexList * vexlst = getRPOVexList();
    ASSERT0(vexlst);
    ASSERT0(vexlst->get_elem_count() == getBBList()->get_elem_count());

    List<xcom::Vertex const*> vlst;
    for (Vertex const* v = vexlst->get_tail(); v != nullptr;
         v = vexlst->get_prev()) {
        ASSERT0(v->id() != VERTEX_UNDEF);
        vlst.append_tail(v);
    }
    bool f = false;
    if (uni != nullptr) {
        f = xcom::DGraph::computePdom(vlst, uni);
    } else {
        f = xcom::DGraph::computePdom(vlst);
    }
    DUMMYUSE(f);
    ASSERT0(f);

    f = xcom::DGraph::computeIpdom();
    ASSERT0(f);
    revisePdomByIpdom();

    oc.setValidPass(PASS_PDOM);
    END_TIMER(t, "Compute PDom,IPDom");
}


void IRCFG::remove_xr(IRBB * bb, IR * ir, CfgOptCtx const& ctx)
{
    ASSERT0(ir->is_stmt());
    xoc::removeStmt(ir, m_rg, const_cast<CfgOptCtx&>(ctx).getOptCtx());
    ir = BB_irlist(bb).remove(ir);
    m_rg->freeIRTree(ir);
}


static void reviseTrampolinBranchEdge(
    IRBB * bb, IRBB * next, IRBB * nextnext, IRBB * next_tgt, IRCFG * cfg,
    OUT CfgOptCtx & ctx)
{
    ASSERT0(bb && next && nextnext && next_tgt && cfg);
    //Change 'bb' target BB.
    IR * br = cfg->get_last_xr(bb);
    ASSERT0(br);
    if (br->is_truebr()) {
        IR_code(br) = IR_FALSEBR;
    } else {
        ASSERT0(br->is_falsebr());
        IR_code(br) = IR_TRUEBR;
    }
    IR * jmp = cfg->get_first_xr(next);
    ASSERT0(jmp && jmp->is_goto());
    br->setLabel(GOTO_lab(jmp));

    //Clean 'next' BB to empty BB.
    cfg->remove_xr(next, jmp, ctx);

    if (next_tgt == nextnext) {
        //CASE:redundant_label.gr
        //  BB1--   //bb
        //   |   |
        //   v   |
        //  BB5  |  //next
        //   |   |
        //   v   |
        //  BB7<-   //nextnext, next_tgt
        return;
    }

    //Maintain DOM info.
    //e.g:exec/20000503-1.c, compile/cfg_trampo.c
    //  --BB1      //bb
    // |  |
    // |  v
    // |  BB4---   //next
    // |        |
    // |        |
    // -> BB6   |  //nextnext
    //    |     |
    //    v     |
    //    BB7 <-   //next_tgt
    //After revision:
    //  --BB1 //invert branch-condition
    // |   |
    // |   v
    // |  BB4 //become empty BB
    // |   |
    // |   v
    // |  BB6
    // |   |
    // |   v
    // -> BB7
    xcom::Edge const* e_of_jmp = cfg->getEdge(next->id(), next_tgt->id());
    ASSERT0(e_of_jmp);
    CFGEdgeInfo * ei = (CFGEdgeInfo*)EDGE_info(e_of_jmp);
    ASSERT0(ei == nullptr || !ei->is_eh());
    xcom::Edge const* e_of_bb = cfg->getEdge(bb->id(), nextnext->id());
    ASSERT0(e_of_bb);
    CFGEdgeInfo * ei2 = (CFGEdgeInfo*)EDGE_info(e_of_bb);
    ASSERT0(ei2 == nullptr || !ei2->is_eh());
    List<UINT> list;
    list.append_head(bb->id());
    cfg->replacePredWith(next, next_tgt, list, ctx);

    //Remove the invalid branch.
    cfg->removeEdge(bb, nextnext, ctx);

    //Add next->nextnext because 'next' is empty.
    //CASE: compile.gr/classic_du_by_prssa.gr
    //  BB9->BB2->BB4
    //       | ^
    //       V |
    //       BB3
    //After previous two steps, the addEdge will turn BB3 into a predecessor
    //of BB4, which will violate the RPO of BB3 and BB4. Thus reompute RPO
    //after the CFG change.
    xcom::Edge * newe = cfg->addEdge(next, nextnext, ctx);
    EDGE_info(newe) = ei;
    OptCtx & oc = ctx.getOptCtx();

    //CASE2:compiler.gr/rpo.gr
    //  BB8---->BB2->BB3   BB4
    //          | ^____|   ^
    //          |__________|
    //After remove edge BB3->BB2, and add edge BB3->BB4, the LoopInfo of
    //  BB8---->BBBB2-->BB3-->BB4
    //          | ^_|         ^
    //          |_____________|
    //LI1:BB2,{2,3} become LI1:BB2,{2}, thus invalid passes that affected by
    //the CFG change. The CFG optimization corrupt RPO.

    //CASE3:compile/tramp_branch.c
    //  BB8---->BB3->BB4   BB5
    //          | ^____|   ^
    //          |__________|
    //After remove edge BB3->BB5, and add edge BB4->BB4, the IDomInfo of
    //  BB8---->BBBB3-->BB4-->BB5
    //            ^_|
    //BB5 changed from BB3 to BB4, the CFG optimization also corrupt DomInfo.
    oc.setInvalidIfCFGChanged();

    //Since RPO is necessary for following CFG modifcation, recompute RPO.
    oc.getRegion()->getPassMgr()->checkValidAndRecompute(
        &oc, PASS_RPO, PASS_UNDEF);

    //Restore edge-info.
    xcom::Edge * e2 = cfg->getEdge(bb->id(), next_tgt->id());
    ASSERT0(e2);
    EDGE_info(e2) = ei2;
}


static bool findRemovableTrampolinBranch(
    BBListIter const& it, OUT IRBB ** bb, OUT IRBB ** next,
    OUT IRBB ** nextnext, OUT IRBB ** next_tgt, IRCFG const* cfg,
    OUT CfgOptCtx & ctx)
{
    *bb = it->val();
    if ((*bb)->isExceptionHandler()) {
        //Do not modify exception/side-effect BB.
        return false;
    }
    IRCFG * pcfg = const_cast<IRCFG*>(cfg);
    IR * br = pcfg->get_last_xr(*bb);
    if (br == nullptr || !br->isConditionalBr() ||
        br->hasSideEffect(true)) {
        return false;
    }

    //Check the fallthrough BB of 'bb'.
    BBListIter nextbbit = it;
    *next = cfg->getBBList()->get_next(&nextbbit);
    IR * jmp = nullptr;
    if ((*next) == nullptr || //bb may be the last BB in bb-list.
        (*next)->isExceptionHandler() || //be marked side-effect by label.
        (jmp = pcfg->get_first_xr(*next)) == nullptr || //bb can not be empty
        !jmp->is_goto()) { //the only single IR must be GOTO
        return false;
    }

    //So far, fallthrough BB is the candidate to be removed.
    //Check if br's target BB is the next BB of fallthrough BB.
    *nextnext = cfg->getBBList()->get_next(&nextbbit);
    if ((*nextnext) == nullptr || !(*nextnext)->hasLabel(BR_lab(br))) {
        //Label is not suited to the case.
        return false;
    }

    *next_tgt = cfg->findBBbyLabel(GOTO_lab(jmp));
    xcom::Edge const* e_of_jmp = cfg->getEdge(
        (*next)->id(), (*next_tgt)->id());
    ASSERT0(e_of_jmp);
    CFGEdgeInfo * ei = (CFGEdgeInfo*)EDGE_info(e_of_jmp);
    if (ei != nullptr && ei->is_eh()) {
        //Do not remove exception/side-effect edge.
        return false;
    }

    xcom::Edge const* e_of_bb = cfg->getEdge(
        (*bb)->id(), (*nextnext)->id());
    ASSERT0(e_of_bb);
    CFGEdgeInfo * ei2 = (CFGEdgeInfo*)EDGE_info(e_of_bb);
    if (ei2 != nullptr && ei2->is_eh()) {
        //Do not remove exception/side-effect edge.
        return false;
    }
    return true;
}


bool IRCFG::removeTrampolinBranchForBB(BBListIter & it, OUT CfgOptCtx & ctx)
{
    IRBB * bb = nullptr;
    IRBB * next = nullptr; //the fallthrough BB of bb.
    IRBB * nextnext = nullptr; //the next BB of fallthrough BB.
    IRBB * next_tgt = nullptr; //the jump target BB of fallthrough BB.
    if (!findRemovableTrampolinBranch(it, &bb, &next, &nextnext,
                                      &next_tgt, this, ctx)) {
        return false;
    }
    //Begin replacement.
    reviseTrampolinBranchEdge(bb, next, nextnext, next_tgt, this, ctx);
    return true;
}


void IRCFG::sortPhiOpnd(IR * phi, Pred2Opnd const& pred2opnd)
{
    ASSERT0(phi->getBB());
    IR * newopnds = nullptr;
    IR * last = nullptr;
    xcom::AdjVertexIter it;
    for (Vertex const* in = Graph::get_first_in_vertex(
            phi->getBB()->getVex(), it);
         in != nullptr; in = Graph::get_next_in_vertex(it)) {
        IR * opnd = const_cast<Pred2Opnd&>(pred2opnd).get(in->id());
        ASSERT0(opnd);
        ASSERT0(xcom::in_list(PHI_opnd_list(phi), opnd));
        xcom::remove(&PHI_opnd_list(phi), opnd);
        xcom::add_next(&newopnds, &last, opnd);
    }
    PHI_opnd_list(phi) = newopnds;
}


bool IRCFG::removeTrampolinBranch(OUT CfgOptCtx & ctx)
{
    if (ctx.needUpdateDomInfo()) {
        //Since RPO is necessary to compute DomInfo and recompute it is not
        //very costly, thus we prefer to recompute RPO.
        ctx.getOptCtx().getRegion()->getPassMgr()->checkValidAndRecompute(
            &ctx.getOptCtx(), PASS_RPO, PASS_UNDEF);
    }
    bool changed = false;
    BBListIter it;
    for (IRBB * bb = m_bb_list->get_head(&it);
         bb != nullptr; bb = m_bb_list->get_next(&it)) {
        changed |= removeTrampolinBranchForBB(it, ctx);
    }
    return changed;
}


void IRCFG::changeToBeFallthroughBB(IRBB * bb, BBListIter const& bbit,
                                    OUT CfgOptCtx & ctx)
{
    ASSERT0(bb && bbit);
    xcom::EdgeC * ec = bb->getVex()->getOutList();
    if (ec == nullptr) { return; }

    ASSERT0(!bb->is_empty());
    BBListIter next_ct = bbit;
    BBList * bbl = getBBList();
    bbl->get_next(&next_ct);
    IRBB * next_bb = nullptr;
    if (next_ct != nullptr) {
        next_bb = next_ct->val();
    }

    bool has_fallthrough = false;
    xcom::EdgeC * next_ec = nullptr;
    for (; ec != nullptr; ec = next_ec) {
        next_ec = ec->get_next();

        xcom::Edge * e = ec->getEdge();
        if (e->info() != nullptr && CFGEI_is_eh((CFGEdgeInfo*)e->info())) {
            continue;
        }

        IRBB * succ = getBB(e->to()->id());
        ASSERT0(succ);
        if (succ == next_bb) {
            //Keep the fall-throught edge unchanged.
            has_fallthrough = true;
            ec = ec->get_next();
            continue;
        }

        //Note removeEdge() will update PHI, whereas in-edge of succ changed,
        //and operands of phi maintained as well.
        removeEdge(bb, succ, ctx);
    }

    //Add an edge between bb and next_bb if fallthrough edge miss.
    //e.g: Add edge BB->BB_next.
    //      _______
    //     |       |
    //     V       |
    //   __BB      |
    //  |  |       |
    //  |  V       |
    //  |  BB_next |
    //  |  |_______|
    //  |
    //   ->BB5
    if (next_bb != nullptr && !has_fallthrough) {
        //Note the function will add operands of PHI if 'next_bb' has PHI.
        addEdge(bb, next_bb, ctx);
    }
}


//Perform miscellaneous control flow optimizations.
//Return true if CFG changed.
//Include removing dead bb which is unreachable, removing empty bb as many
//as possible, simplify and remove the branch like "if (x==x)", removing
//the trampolin branch.
//Note these optimizations performed in the function should NOT use DOM, PDOM,
//LOOPINFO, or RPO information.
bool IRCFG::performMiscOpt(MOD CfgOptCtx & ctx)
{
    START_TIMER(t, "CFG Optimizations");
    bool change = false;
    bool lchange = false;
    UINT count = 0;
    OptCtx & oc = ctx.getOptCtx();
    ASSERT0L3(verifyIRandBB(getBBList(), m_rg));
    ASSERT0L3(verifyRPO(ctx.getOptCtx()));
    ASSERT0L3(verifyLoopInfo(ctx.getOptCtx()));
    ASSERT0L3(verifyDomAndPdom(ctx.getOptCtx()));
    do {
        lchange = false;
        if (g_do_cfg_remove_unreach_bb) {
            bool res = removeUnreachBB(ctx, nullptr);
            lchange |= res;
            if (res) {
                //TODO:maintain them on the fly.
                oc.setInvalidCDG();
                oc.setInvalidSCC();
                ASSERT0L3(verifyIRandBB(getBBList(), m_rg));
                ASSERT0L3(verifyRPO(ctx.getOptCtx()));
                ASSERT0L3(verifyLoopInfo(ctx.getOptCtx()));
                ASSERT0L3(verifyDomAndPdom(ctx.getOptCtx()));
            }
        }
        if (g_do_cfg_remove_redundant_label) {
            bool res = removeRedundantLabel();
            lchange |= res;
            if (res) {
                //The optimization does not change CFG.
                ASSERT0L3(verifyIRandBB(getBBList(), m_rg));
                ASSERT0L3(verifyRPO(ctx.getOptCtx()));
                ASSERT0L3(verifyLoopInfo(ctx.getOptCtx()));
                ASSERT0L3(verifyDomAndPdom(ctx.getOptCtx()));
            }
        }
        if (g_do_cfg_remove_empty_bb) {
            RemoveEmptyBBCtx rmctx(ctx);
            //NOTE:If user expects to see which BB has been removed, set
            //the recording.
            //rmctx.setRecordRemovedBB();
            bool res = removeEmptyBB(rmctx);
            lchange |= res;
            if (res) {
                //removeEmptyBB only maintained these frequently used CFG info.
                OptCtx::setInvalidIfCFGChangedExcept(
                    &oc, PASS_RPO, PASS_DOM, PASS_LOOP_INFO, PASS_UNDEF);
                ASSERT0L3(verifyIRandBB(getBBList(), m_rg));
                ASSERT0L3(verifyRPO(ctx.getOptCtx()));
                ASSERT0L3(verifyLoopInfo(ctx.getOptCtx()));
                ASSERT0L3(verifyDomAndPdom(ctx.getOptCtx()));
            }
        }
        if (g_do_cfg_remove_redundant_branch) {
            bool is_dom_valid = oc.is_dom_valid();
            bool res = removeRedundantBranch(ctx);
            lchange |= res;
            ASSERT0(oc.is_dom_valid() == is_dom_valid);
            if (res) {
                //The function does not change DomInfo.
                ASSERT0L3(verifyIRandBB(getBBList(), m_rg));

                //There might produce unreachable-BB after the pass. However
                //unreachable-BB will crash RPO verification, thus leave the
                //RPO, Dom verification only after unreachable-BB removing.
                goto AGAIN;
            }
        }
        if (g_do_cfg_remove_trampolin_bb) {
            bool res = removeTrampolinEdge(ctx);
            lchange |= res;
            if (res) {
                //The function does not change DomInfo.
                ASSERT0L3(verifyIRandBB(getBBList(), m_rg));

                //There might produce unreachable-BB after the pass. However
                //unreachable-BB will corrupt RPO verification, thus leave the
                //RPO, Dom verification only after unreachable-BB removing.
                goto AGAIN;
            }
        }
        if (g_do_cfg_remove_trampolin_branch) {
            bool res = removeTrampolinBranch(ctx);
            lchange |= res;
            if (res) {
                //The function will maintain DomInfo.
                ASSERT0L3(verifyIRandBB(getBBList(), m_rg));

                //There might produce unreachable-BB after the pass. However
                //unreachable-BB will crash RPO verification, thus leave the
                //RPO, Dom verification only after unreachable-BB removing.
                goto AGAIN;
            }
        }
AGAIN:
        change |= lchange;
        count++;
    } while (lchange && count < 1000);
    ASSERTN(!lchange, ("CFG optimization iterated too many times."));
    if (change) {
        computeExitList();
        //SSAInfo is invalid by adding new-edge to BB.
        //This will confuse phi insertion.
        //CFG optimization should maintain Phi information.
        ASSERT0L3(PRSSAMgr::verifyPRSSAInfo(m_rg, ctx.getOptCtx()));
        ASSERT0L3(MDSSAMgr::verifyMDSSAInfo(m_rg, ctx.getOptCtx()));
        if (g_dump_opt.isDumpAfterPass() && g_dump_opt.isDumpCFGOpt()) {
            dump();
        }
    }
    ASSERT0(verifyIRandBB(getBBList(), m_rg));
    ASSERT0(verifyRPO(ctx.getOptCtx()));
    ASSERT0(verifyLoopInfo(ctx.getOptCtx()));
    ASSERT0(verifyDomAndPdom(ctx.getOptCtx()));
    END_TIMER(t, "CFG Optimizations");
    return change;
}

} //namespace xoc
