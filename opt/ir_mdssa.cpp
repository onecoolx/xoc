/*@
Copyright (c) 2013-2021, Su Zhenyu steven.known@gmail.com

All rights reserved.

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

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

author: Su Zhenyu
@*/
#include "cominc.h"
#include "comopt.h"

namespace xoc {

typedef xcom::TTab<MDPhi const*> PhiTab;

static CHAR const* g_parting_line_char = "----------------";
static CHAR const* g_msg_no_mdssainfo = " NOMDSSAINFO!!";

//
//START MDSSAStatus
//
CHAR const* MDSSAStatus::getStatusName(FlagSetIdx s) const
{
    switch (s) {
    case MDSSA_STATUS_DOM_IS_INVALID: return "dom is invalid";
    default: UNREACHABLE();
    }
    return nullptr;
}
//END MDSSAStatus


//
//START ConstMDDefIter
//
static void iterDefCHelperPhi(
    MDDef const* def, IR const* use, MDSSAMgr const* mgr,
    OUT ConstMDDefIter & it)
{
    //Iter phi's opnd
    ASSERT0(def->is_phi());
    for (IR const* opnd = MDPHI_opnd_list(def);
         opnd != nullptr; opnd = opnd->get_next()) {
        if (opnd->is_const()) {
            //CONST does not have VMD info.
            continue;
        }
        VMD const* vmd = ((MDPhi const*)def)->getOpndVMD(opnd,
            const_cast<MDSSAMgr*>(mgr)->getUseDefMgr());
       if (vmd == nullptr) {
            //Note VOpndSet of 'opnd' may be empty after some optimization.
            //It does not happen when MDSSA just constructed. The USE that
            //without real-DEF will have a virtual-DEF that version is 0.
            //During some increment-maintaining of MDSSA, VOpnd may be removed,
            //and VOpndSet become empty.
            //This means the current USE, 'opnd', does not have real-DEF stmt,
            //the value of 'opnd' always coming from parameter of global value.
            //The ID of PHI should not be removed, because it is be regarded
            //as a place-holder of PHI, and the place-holder indicates the
            //position of related predecessor of current BB of PHI in CFG.
            continue;
        }

        MDDef * vmd_tdef = vmd->getDef();
        if (vmd_tdef == nullptr || vmd_tdef == def ||
            it.is_visited(vmd_tdef)) {
            continue;
        }
        it.set_visited(vmd_tdef);
        it.append_tail(vmd_tdef);
    }
}


static void iterDefCHelper(
    MDDef const* def, IR const* use, MDSSAMgr const* mgr,
    OUT ConstMDDefIter & it)
{
    ASSERT0(def);
    if (def->is_phi()) {
        iterDefCHelperPhi(def, use, mgr, it);
        return;
    }
    ASSERT0(def->getOcc());
    if (use != nullptr && isKillingDef(def->getOcc(), use, nullptr)) {
        //Stop the iteration until encounter the killing DEF real stmt.
        return;
    }
    MDDef const* prev = def->getPrev();
    if (prev == nullptr || it.is_visited(prev)) { return; }
    it.set_visited(prev);
    it.append_tail(prev);
}


MDDef const* ConstMDDefIter::get_first(MDDef const* def)
{
    ASSERT0(def);
    set_visited(def);
    iterDefCHelper(def, nullptr, m_mdssamgr, *this);
    return def;
}


MDDef const* ConstMDDefIter::get_next()
{
    MDDef const* def = remove_head();
    if (def == nullptr) { return nullptr; }
    iterDefCHelper(def, nullptr, m_mdssamgr, *this);
    return def;
}


MDDef const* ConstMDDefIter::get_first_untill_killing_def(
    MDDef const* def, IR const* use)
{
    ASSERT0(def && use && use->is_exp());
    set_visited(def);
    iterDefCHelper(def, use, m_mdssamgr, *this);
    return def;
}


MDDef const* ConstMDDefIter::get_next_untill_killing_def(IR const* use)
{
    ASSERT0(use && use->is_exp());
    MDDef const* def = remove_head();
    if (def == nullptr) { return nullptr; }
    iterDefCHelper(def, use, m_mdssamgr, *this);
    return def;
}
//END ConstMDDefIter


//
//START ConstMDSSAUSEIRIter
//
ConstMDSSAUSEIRIter::ConstMDSSAUSEIRIter(MDSSAMgr const* mdssamgr)
    : vopndset_iter(nullptr), current_pos_in_vopndset(BS_UNDEF),
      current_pos_in_useset(BS_UNDEF), current_useset(nullptr),
      m_mdssamgr(mdssamgr),
      m_udmgr(const_cast<MDSSAMgr*>(mdssamgr)->getUseDefMgr())
{
    m_rg = mdssamgr->getRegion();
}


IR const* ConstMDSSAUSEIRIter::get_first(IR const* def)
{
    MDSSAMgr * pthis = const_cast<MDSSAMgr*>(m_mdssamgr);
    UseDefMgr * udmgr = const_cast<UseDefMgr*>(m_udmgr);
    MDSSAInfo * info = pthis->getMDSSAInfoIfAny(def);
    ASSERT0(info);
    vopndset_iter = nullptr;
    vopndset = info->getVOpndSet();
    //Find the first iter and position in VOpndSet.
    for (current_pos_in_vopndset = vopndset->get_first(
            &vopndset_iter);
         current_pos_in_vopndset != BS_UNDEF;
         current_pos_in_vopndset = vopndset->get_next(
            current_pos_in_vopndset, &vopndset_iter)) {
        VMD * vopnd = (VMD*)udmgr->getVOpnd(current_pos_in_vopndset);
        ASSERT0(vopnd && vopnd->is_md());
        current_useset = vopnd->getUseSet();
        useset_iter.clean();
        //Find the first iter and position in UseSet.
        for (current_pos_in_useset = current_useset->get_first(useset_iter);
             !useset_iter.end();
             current_pos_in_useset = current_useset->get_next(useset_iter)) {
            IR * use = m_rg->getIR(current_pos_in_useset);
            ASSERT0(use && !use->isReadPR());
            return use;
        }
    }
    return nullptr;
}


IR const* ConstMDSSAUSEIRIter::get_next()
{
    UseDefMgr * udmgr = const_cast<UseDefMgr*>(m_udmgr);

    //Update iter and position in UseSet.
    for (; !useset_iter.end(); UNREACHABLE()) {
        //Find next USE.
        current_pos_in_useset = current_useset->get_next(useset_iter);
        if (useset_iter.end()) {
            //Prepare next VOpnd.
            current_pos_in_vopndset = vopndset->get_next(
                current_pos_in_vopndset, &vopndset_iter);
            //Step into next VOpnd.
            break;
        } else {
            IR * use = m_rg->getIR(current_pos_in_useset);
            ASSERT0(use && !use->isReadPR());
            return use;
        }
    }

    //Update iter and position in VOpndSet.
    for (; current_pos_in_vopndset != BS_UNDEF;
         current_pos_in_vopndset = vopndset->get_next(
             current_pos_in_vopndset, &vopndset_iter)) {
        VMD * vopnd = (VMD*)udmgr->getVOpnd(current_pos_in_vopndset);
        ASSERT0(vopnd && vopnd->is_md());
        current_useset = vopnd->getUseSet();
        useset_iter.clean();
        //Find the first iter and position in UseSet.
        for (current_pos_in_useset = current_useset->get_first(
                 useset_iter);
             !useset_iter.end(); UNREACHABLE()) {
            IR * use = m_rg->getIR(current_pos_in_useset);
            ASSERT0(use && !use->isReadPR());
            return use;
        }
    }
    return nullptr;
}
//END ConstMDSSAUSEIRIter


//
//START CollectUse
//
bool CollectCtx::verify() const
{
    if (flag.have(COLLECT_OUTSIDE_LOOP_IMM_USE) ||
        flag.have(COLLECT_INSIDE_LOOP)) {
        ASSERTN(m_li, ("need LoopInfo"));
    }
    ASSERTN(!(flag.have(COLLECT_OUTSIDE_LOOP_IMM_USE) &
              flag.have(COLLECT_INSIDE_LOOP)),
            ("flags are conflict"));
    return true;
}
//END CollectCtx


//
//START CollectUse
//
CollectUse::CollectUse(MDSSAMgr const* mgr, MDSSAInfo const* info,
                       CollectCtx const& ctx, OUT IRSet * set)
    : m_mgr(mgr), m_ctx(ctx)
{
    m_udmgr = const_cast<MDSSAMgr*>(mgr)->getUseDefMgr();
    ASSERT0(set);
    collectForMDSSAInfo(info, set);
}


CollectUse::CollectUse(MDSSAMgr const* mgr, VMD const* vmd,
                       CollectCtx const& ctx, OUT IRSet * set)
    : m_mgr(mgr), m_ctx(ctx)
{
    m_udmgr = const_cast<MDSSAMgr*>(mgr)->getUseDefMgr();
    ASSERT0(set);
    collectForVOpnd(vmd, set);
}


void CollectUse::collectOutsideLoopImmUseForVOpnd(
    VOpnd const* vopnd, MOD MDDefVisitor & vis, OUT IRSet * set) const
{
    VMD::UseSetIter vit;
    ASSERT0(m_ctx.verify());
    LI<IRBB> const* li = m_ctx.getLI();
    ASSERT0(vopnd->is_md());
    VMD * pvopnd = const_cast<VMD *>((VMD const*)vopnd);
    Region * rg = m_udmgr->getRegion();
    for (UINT i = pvopnd->getUseSet()->get_first(vit);
         !vit.end(); i = pvopnd->getUseSet()->get_next(vit)) {
        IR const* ir = rg->getIR(i);
        ASSERT0(ir && !ir->is_undef());
        if (ir->is_id()) {
            MDPhi const* phi = ((CId*)ir)->getMDPhi();
            ASSERT0(phi);
            if (!li->isInsideLoop(phi->getBB()->id())) {
                //The function only collect the immediate USE outside loop.
                //So if the MDPhi is outside loop, just collects the ID as
                //immediate USE.
                set->bunion(i);
                continue;
            }
            if (vis.is_visited(phi->id())) { continue; }
            //Cross the MDPhi that is inside loop.
            vis.set_visited(phi->id());
            collectUseCrossPhi(phi, vis, set);
            continue;
        }
        ASSERT0(ir->is_exp() && ir->getStmt());
        if (li->isInsideLoop(ir->getStmt()->getBB()->id())) {
            continue;
        }
        //ir is the outside loop immediate USE.
        set->bunion(i);
    }
}


void CollectUse::collectUseForVOpnd(VOpnd const* vopnd, MOD MDDefVisitor & vis,
                                    OUT IRSet * set) const
{
    VMD::UseSetIter vit;
    ASSERT0(m_ctx.verify());
    if (m_ctx.flag.have(COLLECT_OUTSIDE_LOOP_IMM_USE)) {
        return collectOutsideLoopImmUseForVOpnd(vopnd, vis, set);
    }
    bool cross_phi = m_ctx.flag.have(COLLECT_CROSS_PHI);
    bool must_inside_loop = m_ctx.flag.have(COLLECT_INSIDE_LOOP);
    LI<IRBB> const* li = m_ctx.getLI();
    ASSERT0(vopnd->is_md());
    VMD * pvopnd = const_cast<VMD *>((VMD const*)vopnd);
    Region * rg = m_udmgr->getRegion();
    for (UINT i = pvopnd->getUseSet()->get_first(vit);
         !vit.end(); i = pvopnd->getUseSet()->get_next(vit)) {
        if (!cross_phi) {
            set->bunion(i);
            continue;
        }
        IR const* ir = rg->getIR(i);
        ASSERT0(ir && !ir->is_undef());
        if (ir->is_id()) {
            MDPhi const* phi = ((CId*)ir)->getMDPhi();
            ASSERT0(phi);
            if (must_inside_loop && !li->isInsideLoop(phi->getBB()->id())) {
                continue;
            }
            if (vis.is_visited(phi->id())) { continue; }
            vis.set_visited(phi->id());
            collectUseCrossPhi(phi, vis, set);
            continue;
        }
        ASSERT0(ir->is_exp() && ir->getStmt());
        if (must_inside_loop &&
            !li->isInsideLoop(ir->getStmt()->getBB()->id())) {
            continue;
        }
        set->bunion(i);
    }
}


void CollectUse::collectUseCrossPhi(MDPhi const* phi, MOD MDDefVisitor & vis,
                                    OUT IRSet * set) const
{
    ASSERT0(phi && phi->is_phi());
    collectUseForVOpnd(phi->getResult(), vis, set);
}


void CollectUse::collectForVOpnd(VOpnd const* vopnd, OUT IRSet * set) const
{
    //DO NOT CLEAN SET
    ASSERT0(set && vopnd && vopnd->is_md());
    MDDefVisitor vis;
    collectUseForVOpnd(vopnd, vis, set);
}


void CollectUse::collectForMDSSAInfo(
    MDSSAInfo const* info, OUT IRSet * set) const
{
    //DO NOT CLEAN SET
    ASSERT0(set && info);
    VOpndSetIter it = nullptr;
    VOpndSet const& vopndset = info->readVOpndSet();
    MDDefVisitor vis;
    for (BSIdx i = vopndset.get_first(&it);
         i != BS_UNDEF; i = vopndset.get_next(i, &it)) {
        VMD const* vopnd = (VMD*)m_udmgr->getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md());
        vis.clean();
        collectUseForVOpnd(vopnd, vis, set);
    }
}
//END CollectUse


//
//START CollectDef
//
CollectDef::CollectDef(MDSSAMgr const* mgr, MDSSAInfo const* info,
                       CollectCtx const& ctx, MD const* ref, OUT IRSet * set)
    : m_mgr(mgr), m_info(info), m_ctx(ctx), m_ref(ref)
{
    m_udmgr = const_cast<MDSSAMgr*>(mgr)->getUseDefMgr();
    ASSERT0(set);
    collect(set);
}


void CollectDef::collect(OUT IRSet * set) const
{
    //DO NOT CLEAN 'set'.
    VOpndSetIter it = nullptr;
    bool cross_phi = m_ctx.flag.have(COLLECT_CROSS_PHI);
    bool must_inside_loop = m_ctx.flag.have(COLLECT_INSIDE_LOOP);
    LI<IRBB> const* li = m_ctx.getLI();
    if (must_inside_loop) { ASSERT0(li); }
    for (BSIdx i = m_info->readVOpndSet().get_first(&it);
         i != BS_UNDEF; i = m_info->readVOpndSet().get_next(i, &it)) {
        VOpnd const* t = m_udmgr->getVOpnd(i);
        ASSERT0(t && t->is_md());
        MDDef * tdef = ((VMD*)t)->getDef();
        if (tdef == nullptr) { continue; }
        if (must_inside_loop && !li->isInsideLoop(tdef->getBB()->id())) {
            continue;
        }
        if (tdef->is_phi() && cross_phi) {
            //TODO: iterate phi operands.
            collectDefThroughDefChain(tdef, set);
            continue;
        }

        IR const* defstmt = tdef->getOcc();
        ASSERT0(defstmt);
        if (defstmt->isCallStmt()) {
            //CASE:call()
            //     ...=USE
            //Call is the only stmt that need to process specially.
            //Because it is not killing-def.
            collectDefThroughDefChain(tdef, set);
            continue;
        }

        ASSERT0(defstmt->isMemRefNonPR());
        MD const* mustdef = defstmt->getRefMD();
        if (m_ref != nullptr && mustdef != nullptr &&
            m_ref->is_exact() && mustdef->is_exact() &&
            (mustdef == m_ref || mustdef->is_exact_cover(m_ref))) {
            //defstmt is killing definition of 'ref'.
            set->bunion(defstmt->id());
            continue;
        }

        if (m_ref != nullptr) {
            //TODO:
            //CASE1:DEF=...
            //      ...=USE
            //CASE2:...=
            //      ...=USE
            //Both cases need to collect all DEFs until
            //encounter the killing-def.
            collectDefThroughDefChain(tdef, set);
            continue;
        }

        //CASE1:...=
        //         =...
        //CASE2:DEF=...
        //         =...
        //Both cases need to collect all DEFs through def-chain.
        collectDefThroughDefChain(tdef, set);
    }
}


void CollectDef::collectDefThroughDefChain(
    MDDef const* def, OUT IRSet * set) const
{
    ASSERT0(def);
    ConstMDDefIter it(m_mgr);
    bool must_inside_loop = m_ctx.flag.have(COLLECT_INSIDE_LOOP);
    LI<IRBB> const* li = m_ctx.getLI();
    if (must_inside_loop) { ASSERT0(li); }
    for (MDDef const* d = it.get_first(def); d != nullptr; d = it.get_next()) {
        if (must_inside_loop && !li->isInsideLoop(d->getBB()->id())) {
            continue;
        }
        if (d->is_phi()) {
            //Nothing to do. The DEF of operand will be iterated at
            //iterDefCHelper().
            continue;
        }
        ASSERT0(d->getOcc());
        set->bunion(d->getOcc()->id());

        //TODO:for now, we have to walk alone with DEF chain to
        //mark almost all DEF to be effect. This may lead to
        //traverse the same DEF many times. Apply DP like algo to reduce
        //the traversal time.
    }
}
//END CollectDef


//
//START BBID2LiveSet
//
void BBID2LiveSet::dump(Region const* rg) const
{
    ASSERT0(rg);
    note(rg, "\n==-- DUMP BBID2LiveSet --==");
    MDSSAMgr * mgr = rg->getMDSSAMgr();
    ASSERT0(mgr);
    LiveSet * liveset;
    xcom::TMapIter<UINT, LiveSet*> it;
    for (UINT bbid = get_first(it, &liveset);
         bbid != BBID_UNDEF; bbid = get_next(it, &liveset)) {
        note(rg, "\nBB%u:", bbid);
        if (liveset == nullptr) {
            prt(rg, "--");
            continue;
        }
        LiveSetIter its;
        bool first = true;
        for (BSIdx i = liveset->get_first(&its);
             i != BS_UNDEF; i = liveset->get_next(i, &its)) {
            VMD * t = (VMD*)mgr->getUseDefMgr()->getVOpnd(i);
            ASSERT0(t && t->is_md());
            if (!first) { note(rg, ","); }
            first = false;
            t->dump(rg);
        }
    }
}
//END BBID2LiveSet


//
//START MDSSAConstructRenameVisit
//
class MDSSAConstructRenameVisitVF : public xcom::VisitTreeFuncBase {
    DefMDSet const& m_effect_mds;
    BB2DefMDSet & m_defed_mds_vec;
    MD2VMDStack & m_md2vmdstk;
    MDSSAMgr * m_mgr;
    IRCFG * m_cfg;
    BB2VMDMap m_bb2vmdmap;
public:
    MDSSAConstructRenameVisitVF(
        DefMDSet const& effect_mds, BB2DefMDSet & defed_mds_vec,
        MD2VMDStack & md2vmdstk, MDSSAMgr * mgr)
            : m_effect_mds(effect_mds), m_defed_mds_vec(defed_mds_vec),
              m_md2vmdstk(md2vmdstk), m_mgr(mgr)
    {
        m_cfg = mgr->getCFG();
        m_bb2vmdmap.setElemNum(m_cfg->getBBList()->get_elem_count());
    }
    void visitWhenAllKidHaveBeenVisited(
        xcom::Vertex const* v, xcom::Stack<Vertex const*> &)
    {
        //Do post-processing while all kids of BB has been processed.
        MD2VMD * mdid2vmd = m_bb2vmdmap.get(v->id());
        ASSERT0(mdid2vmd);
        xcom::DefSBitSet * defed_mds = m_defed_mds_vec.get(v->id());
        ASSERT0(defed_mds);
        DefSBitSetIter cur = nullptr;
        for (BSIdx i = defed_mds->get_first(&cur);
             i != BS_UNDEF; i = defed_mds->get_next(i, &cur)) {
            VMDStack * vs = m_md2vmdstk.get(i);
            ASSERT0(vs && vs->get_bottom());
            ASSERT0(vs->get_top()->mdid() == (MDIdx)i);
            VMD * vmd = mdid2vmd->get(i);
            while (vs->get_top() != vmd) {
                vs->pop();
            }
        }
        //vmdmap is useless from now on.
        m_bb2vmdmap.erase(v->id());
    }
    bool visitWhenFirstMeet(xcom::Vertex const* v, xcom::Stack<Vertex const*> &)
    {
        DefMDSet const* defed_mds = m_defed_mds_vec.get(v->id());
        ASSERT0(defed_mds);
        IRBB * bb = m_cfg->getBB(v->id());
        m_mgr->handleBBRename(bb, m_effect_mds, *defed_mds,
                              m_bb2vmdmap, m_md2vmdstk);
        return true;
    }
};


class MDSSAConstructRenameVisit
    : public xcom::VisitTree<MDSSAConstructRenameVisitVF> {
    COPY_CONSTRUCTOR(MDSSAConstructRenameVisit);
public:
    MDSSAConstructRenameVisit(DomTree const& domtree, IRBB * root,
                              MDSSAConstructRenameVisitVF & vf)
        : VisitTree(domtree, root->id(), vf) {}
};
//END MDSSAConstructRenameVisit


//
//START RenameDefVisit
//
class RenameDefVisitFunc : public xcom::VisitTreeFuncBase {
    RenameDef & m_rndef;
    IRCFG * m_cfg;
    DomTree const& m_dt;
public:
    RenameDefVisitFunc(RenameDef & rndef, IRCFG * cfg, DomTree const& dt)
        : m_rndef(rndef), m_cfg(cfg), m_dt(dt) {}

    void visitWhenAllKidHaveBeenVisited(
        xcom::Vertex const* v, MOD xcom::Stack<Vertex const*> &)
    { m_rndef.getBBID2LiveSet().free(v->id()); }
    bool visitWhenFirstMeet(
        xcom::Vertex const* v, MOD xcom::Stack<Vertex const*> & stk)
    {
        //Init liveset for given vertex.
        LiveSet * tliveset = m_rndef.getBBID2LiveSet().get(v->id());
        if (tliveset == nullptr) {
            Vertex const* parent = m_dt.getParent(v);
            ASSERT0(parent);
            LiveSet const* pset = m_rndef.getBBID2LiveSet().get(parent->id());
            ASSERT0(pset);
            tliveset = m_rndef.getBBID2LiveSet().genAndCopy(v->id(), *pset);
        } else if (tliveset->all_killed()) {
            stk.pop(); //no need to perform rename-def anymore.
            m_rndef.getBBID2LiveSet().free(v->id());
            //All VMDs processed, no need to go to kid vertex.
            return false;
        }
        IRBB * vbb = m_cfg->getBB(v->id());
        ASSERT0(vbb);
        BBIRListIter irlistit;
        vbb->getIRList().get_head(&irlistit);
        m_rndef.renameUseInBBTillNextDef(v, vbb, true, irlistit, *tliveset);
        if (tliveset->all_killed()) {
            stk.pop(); //no need to perform rename-def anymore.
            m_rndef.getBBID2LiveSet().free(v->id());
            //All VMDs processed, no need to go to kid vertex.
            return false;
        }
        return true;
    }
};


class RenameDefVisit : public xcom::VisitTree<RenameDefVisitFunc> {
public:
    RenameDefVisit(VexIdx rootid, DomTree const& domtree,
                   RenameDefVisitFunc & vf) : VisitTree(domtree, rootid, vf)
    {
        //Skip the root vertex on DomTree.
        setVisited(rootid);
    }
};
//END RenameDefVisit



//
//START RenameDef
//
RenameDef::RenameDef(DomTree const& dt, bool build_ddchain,
                     MDSSAMgr * mgr, ActMgr * am)
    : m_domtree(dt), m_am(am)
{
    m_liveset = nullptr;
    m_mgr = mgr;
    m_cfg = m_mgr->getCFG();
    m_is_build_ddchain = build_ddchain;
    m_udmgr = m_mgr->getUseDefMgr();
    m_rg = mgr->getRegion();
}


void RenameDef::clean()
{
    BBID2LiveSet & ls = getBBID2LiveSet();
    LiveSet * liveset = nullptr;
    BBID2LiveSetIter it;
    for (UINT bbid = ls.get_first(it, &liveset);
         bbid != BBID_UNDEF; bbid = ls.get_next(it, &liveset)) {
        if (liveset == nullptr) { continue; }
        liveset->clean();
    }
}


void RenameDef::dumpRenameBB(IRBB const* bb)
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    ActMgr * am = getActMgr();
    if (am == nullptr) { return; }
    am->dump("RenameDef:renaming BB%u", bb->id());
}


void RenameDef::dumpRenameVMD(IR const* ir, VMD const* vmd)
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    ActMgr * am = getActMgr();
    if (am == nullptr) { return; }
    VMDFixedStrBuf buf1;
    VMDFixedStrBuf buf2;
    am->dump("RenameDef:renaming %s with %s",
             xoc::dumpIRName(ir, buf1), vmd->dump(buf2));
}


void RenameDef::dumpInsertDDChain(IR const* ir, VMD const* vmd)
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    ActMgr * am = getActMgr();
    if (am == nullptr) { return; }
    VMDFixedStrBuf buf1;
    VMDFixedStrBuf buf2;
    am->dump("RenameDef:insert %s into DDChain by access MDSSAInfo of %s",
             vmd->dump(buf1), xoc::dumpIRName(ir, buf2));
}


void RenameDef::dumpInsertDDChain(MDPhi const* phi, VMD const* vmd)
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    ActMgr * am = getActMgr();
    if (am == nullptr) { return; }
    VMDFixedStrBuf buf;
    am->dump("RenameDef:insert %s into DDChain by access MDPhi%u",
             vmd->dump(buf), phi->id());
}


void RenameDef::dumpRenamePhi(MDPhi const* phi, UINT opnd_pos)
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    ActMgr * am = getActMgr();
    if (am == nullptr) { return; }
    am->dump("RenameDef:rename MDPhi%u with No.%u operand",
             phi->id(), opnd_pos);
}


void RenameDef::renamePhiOpnd(MDPhi const* phi, UINT opnd_idx, MOD VMD * vmd)
{
    ASSERT0(phi->is_phi());
    UseDefMgr * udmgr = m_mgr->getUseDefMgr();
    IR * opnd = phi->getOpnd(opnd_idx);
    ASSERT0(opnd);
    MDSSAInfo * info = m_mgr->getMDSSAInfoIfAny(opnd);
    if (info == nullptr || info->isEmptyVOpndSet()) {
        info = m_mgr->genMDSSAInfoAndVOpnd(opnd, MDSSA_INIT_VERSION);
    }
    info->renameSpecificUse(opnd, vmd, udmgr);
    ASSERT0(info->readVOpndSet().get_elem_count() == 1);
}


//stmtbb: the BB of inserted stmt
//newinfo: MDSSAInfo that intent to be swap-in.
bool RenameDef::renameVMDForDesignatedPhiOpnd(
    MDPhi * phi, UINT opnd_pos, MOD LiveSet & liveset)
{
    dumpRenamePhi(phi, opnd_pos);
    UseDefMgr * udmgr = m_mgr->getUseDefMgr();
    MDIdx phimdid = phi->getResult()->mdid();
    LiveSetIter it = nullptr;
    for (BSIdx i = liveset.get_first(&it); i != BS_UNDEF;
         i = liveset.get_next(i, &it)) {
        VMD * t = (VMD*)udmgr->getVOpnd(i);
        ASSERT0(t && t->is_md());
        if (t->mdid() == phimdid) {
            renamePhiOpnd(phi, opnd_pos, t);
            return true;
        }
    }
    return false;
}


//vmd: intent to be swap-in.
//irtree: may be stmt or exp.
//irit: for local used.
void RenameDef::renameVMDForIRTree(
    IR * irtree, VMD * vmd, MOD IRIter & irit, bool & no_exp_has_ssainfo)
{
    irit.clean();
    for (IR * e = iterExpInit(irtree, irit);
         e != nullptr; e = iterExpNext(irit, true)) {
        if (!MDSSAMgr::hasMDSSAInfo(e)) { continue; }
        no_exp_has_ssainfo = false;
        MDSSAInfo * einfo = m_mgr->getMDSSAInfoIfAny(e);
        if (einfo == nullptr || einfo->isEmptyVOpndSet()) {
            einfo = m_mgr->genMDSSAInfoAndVOpnd(e, MDSSA_INIT_VERSION);
        }
        einfo->renameSpecificUse(e, vmd, m_mgr->getUseDefMgr());
    }
}


//ir: may be stmt or exp
//irit: for local used.
void RenameDef::renameLivedVMDForIRTree(
    IR * ir, MOD IRIter & irit, LiveSet const& liveset)
{
    VOpndSetIter it = nullptr;
    UseDefMgr * udmgr = m_mgr->getUseDefMgr();
    bool no_exp_has_ssainfo = true;
    for (BSIdx i = liveset.get_first(&it); i != BS_UNDEF;
         i = liveset.get_next(i, &it)) {
        VMD * t = (VMD*)udmgr->getVOpnd(i);
        ASSERT0(t && t->is_md());
        dumpRenameVMD(ir, t);
        renameVMDForIRTree(ir, t, irit, no_exp_has_ssainfo);
        if (no_exp_has_ssainfo) {
            //Early quit the loop.
            return;
        }
    }
}


//Insert vmd after phi.
bool RenameDef::tryInsertDDChainForDesigatedVMD(
    MDPhi * phi, VMD * vmd, MOD LiveSet & liveset)
{
    ASSERT0(phi->is_phi());
    VMD const* phires = phi->getResult();
    ASSERT0(phires && phires->is_md());
    if (phires->mdid() != vmd->mdid()) { return false; }
    liveset.set_killed(vmd->id());
    dumpInsertDDChain(phi, vmd);
    m_mgr->insertDefBefore(phires, vmd);
    return true;
}


bool RenameDef::tryInsertDDChainForDesigatedVMD(
    IR * ir, VMD * vmd, bool before, MOD LiveSet & liveset)
{
    ASSERT0(ir->is_stmt());
    dumpInsertDDChain(ir, vmd);
    MDSSAInfo * irinfo = m_mgr->getMDSSAInfoIfAny(ir);
    if (irinfo == nullptr || irinfo->isEmptyVOpndSet()) {
        //ir may be new generated stmt. There is not MDSSAInfo allocated yet.
        irinfo = m_mgr->genMDSSAInfoAndNewVesionVMD(ir);
    }
    VOpndSetIter vit = nullptr;
    UseDefMgr * udmgr = m_mgr->getUseDefMgr();
    MDIdx vmdid = vmd->mdid();
    VOpndSet const& irdefset = irinfo->readVOpndSet();
    BSIdx nexti = BS_UNDEF;
    for (BSIdx i = irdefset.get_first(&vit); i != BS_UNDEF; i = nexti) {
        nexti = irdefset.get_next(i, &vit);
        VMD * irdef = (VMD*)udmgr->getVOpnd(i);
        ASSERT0(irdef && irdef->is_md());
        if (irdef->mdid() != vmdid) { continue; }

        //ir's MDDef killed vmd.
        ASSERTN(irdef->version() != vmd->version(),
                ("same version DEF appeared at multiple places"));
        liveset.set_killed(vmd->id());
        if (before) {
            m_mgr->insertDefBefore(vmd, irdef);
        } else {
            m_mgr->insertDefBefore(irdef, vmd);
        }
        return true;
    }
    return false;
}


bool RenameDef::tryInsertDDChainForPhi(MDPhi * phi, MOD LiveSet & liveset)
{
    ASSERT0(phi->is_phi());
    VOpndSetIter it = nullptr;
    UseDefMgr * udmgr = m_mgr->getUseDefMgr();
    bool inserted = false;
    BSIdx nexti;
    for (BSIdx i = liveset.get_first(&it); i != BS_UNDEF; i = nexti) {
        nexti = liveset.get_next(i, &it); //i may be removed.
        VMD * t = (VMD*)udmgr->getVOpnd(i);
        ASSERT0(t && t->is_md());
        if (!liveset.is_live(i)) { continue; }
        inserted |= tryInsertDDChainForDesigatedVMD(phi, t, liveset);
    }
    return inserted;
}


bool RenameDef::tryInsertDDChainForStmt(
    IR * ir, bool before, MOD LiveSet & liveset)
{
    ASSERT0(ir->is_stmt());
    VOpndSetIter it = nullptr;
    UseDefMgr * udmgr = m_mgr->getUseDefMgr();
    bool inserted = false;
    BSIdx nexti;
    for (BSIdx i = liveset.get_first(&it); i != BS_UNDEF; i = nexti) {
        nexti = liveset.get_next(i, &it); //i may be removed.
        VMD * t = (VMD*)udmgr->getVOpnd(i);
        ASSERT0(t && t->is_md());
        if (!liveset.is_live(i)) { continue; }
        inserted |= tryInsertDDChainForDesigatedVMD(ir, t, before, liveset);
    }
    return inserted;
}


void RenameDef::killLivedVMD(MDPhi const* phi, MOD LiveSet & liveset)
{
    UseDefMgr * udmgr = m_mgr->getUseDefMgr();
    MDIdx phimdid = phi->getResult()->mdid();
    VOpndSetIter it = nullptr;
    BSIdx nexti = BS_UNDEF;
    for (BSIdx i = liveset.get_first(&it); i != BS_UNDEF; i = nexti) {
        nexti = liveset.get_next(i, &it);
        VMD * t = (VMD*)udmgr->getVOpnd(i);
        ASSERT0(t && t->is_md());
        if (t->mdid() == phimdid) {
            liveset.set_killed(t->id());
        }
    }
}


//defvex: domtree vertex.
void RenameDef::iterSuccBBPhiListToRename(
    Vertex const* defvex, IRBB const* succ, UINT opnd_idx,
    MOD LiveSet & liveset)
{
    ASSERT0(succ);
    dumpRenameBB(succ);
    MDPhiList * philist = m_mgr->getPhiList(succ->id());
    if (philist == nullptr) { return; }
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi);
        renameVMDForDesignatedPhiOpnd(phi, opnd_idx, liveset);
        if (liveset.all_killed()) {
            return;
        }
    }
}


//defvex: domtree vertex.
void RenameDef::iterSuccBB(Vertex const* defvex, MOD LiveSet & liveset)
{
    Vertex const* cfgv = m_cfg->getVertex(defvex->id());
    ASSERT0(cfgv);
    AdjVertexIter it;
    for (Vertex const* succv = Graph::get_first_out_vertex(cfgv, it);
         succv != nullptr; succv = Graph::get_next_out_vertex(it)) {
        UINT opnd_idx = 0; //the index of corresponding predecessor.
        AdjVertexIter it2;
        bool find = false;
        //Note the function will count the number of predecessors of
        //each BB as the number of operand of PHI, even if some of them are
        //unreachable from region-entry, and will be removed by followed CFG
        //optimizations.
        for (Vertex const* predv = Graph::get_first_in_vertex(succv, it2);
             predv != nullptr;
             predv = Graph::get_next_in_vertex(it2), opnd_idx++) {
            if (predv->id() == cfgv->id()) {
                find = true;
                break;
            }
        }
        ASSERTN_DUMMYUSE(find, ("not found related pred"));
        //Replace opnd of PHI of 'succ' with lived SSA version.
        iterSuccBBPhiListToRename(defvex, m_cfg->getBB(succv->id()),
                                  opnd_idx, liveset);
    }
}


void RenameDef::iterBBPhiListToKillLivedVMD(IRBB const* bb, LiveSet & liveset)
{
    ASSERT0(bb);
    MDPhiList * philist = m_mgr->getPhiList(bb->id());
    if (philist == nullptr) { return; }
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi);
        killLivedVMD(phi, liveset);
        if (liveset.all_killed()) {
            return;
        }
    }
}


void RenameDef::connectPhiTillPrevDef(
    IRBB const* bb, BBIRListIter & irlistit, MOD LiveSet & liveset)
{
    ASSERT0(bb);
    MDPhiList * philist = m_mgr->getPhiList(bb->id());
    if (philist == nullptr) { return; }
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi);
        tryInsertDDChainForPhi(phi, liveset);
        if (liveset.all_killed()) {
            return;
        }
    }
}


void RenameDef::connectIRTillPrevDef(
    IRBB const* bb, BBIRListIter & irlistit, MOD LiveSet & liveset)
{
    IRIter irit;
    BBIRList & irlist = const_cast<IRBB*>(bb)->getIRList();
    for (; irlistit != nullptr; irlistit = irlist.get_prev(irlistit)) {
        IR * stmt = irlistit->val();
        if (!MDSSAMgr::hasMDSSAInfo(stmt)) { continue; }
        tryInsertDDChainForStmt(stmt, false, liveset);
        if (liveset.all_killed()) {
            return;
        }
    }
}


void RenameDef::renameIRTillNextDef(
    IRBB const* bb, BBIRListIter & irlistit, MOD LiveSet & liveset)
{
    dumpRenameBB(bb);
    IRIter irit;
    BBIRList & irlist = const_cast<IRBB*>(bb)->getIRList();
    for (; irlistit != nullptr; irlistit = irlist.get_next(irlistit)) {
        IR * stmt = irlistit->val();
        renameLivedVMDForIRTree(stmt, irit, liveset);
        if (!MDSSAMgr::hasMDSSAInfo(stmt)) { continue; }
        tryInsertDDChainForStmt(stmt, true, liveset);
        if (liveset.all_killed()) {
            return;
        }
    }
}


//stmtbbid: indicates the BB of inserted stmt
//v: the vertex on DomTree.
//bb: the BB that to be renamed
//dompred: indicates the predecessor of 'bb' in DomTree
//Note stmtbbid have to dominate 'bb'.
void RenameDef::connectDefInBBTillPrevDef(
    IRBB const* bb, BBIRListIter & irlistit, MOD LiveSet & liveset)
{
    connectIRTillPrevDef(bb, irlistit, liveset);
    if (liveset.all_killed()) { return; }
    connectPhiTillPrevDef(bb, irlistit, liveset);
}


//stmtbbid: indicates the BB of inserted stmt
//v: the vertex on DomTree.
//bb: the BB that to be renamed
//dompred: indicates the predecessor of 'bb' in DomTree
//Note stmtbbid have to dominate 'bb'.
void RenameDef::renameUseInBBTillNextDef(
    Vertex const* defvex, IRBB const* bb, bool include_philist,
    BBIRListIter & irlistit, MOD LiveSet & liveset)
{
    if (include_philist) {
        iterBBPhiListToKillLivedVMD(bb, liveset);
        if (liveset.all_killed()) { return; }
    }
    renameIRTillNextDef(bb, irlistit, liveset);
    if (liveset.all_killed()) { return; }
    iterSuccBB(defvex, liveset);
}


//defvex: the vertex on DomTree.
//start_ir: if it is nullptr, the renaming will start at the first IR in bb.
//          otherwise the renaming will start at the NEXT IR of start_ir.
void RenameDef::renameFollowUseIntraBBTillNextDef(
    Vertex const* defvex, MOD LiveSet & stmtliveset,
    IRBB const* start_bb, IR const* start_ir)
{
    ASSERT0(start_bb);
    BBIRListIter irlistit = nullptr;
    BBIRList & irlist = const_cast<IRBB*>(start_bb)->getIRList();
    if (start_ir == nullptr) {
        irlist.get_head(&irlistit);
    } else {
        irlist.find(const_cast<IR*>(start_ir), &irlistit);
        ASSERT0(irlistit);
        irlistit = irlist.get_next(irlistit);
    }
    renameUseInBBTillNextDef(defvex, start_bb, false, irlistit, stmtliveset);
}


void RenameDef::connectDefInterBBTillPrevDef(
    Vertex const* defvex, MOD LiveSet & stmtliveset, IRBB const* start_bb)
{
    for (Vertex const* p = m_domtree.getParent(defvex);
         p != nullptr; p = m_domtree.getParent(p)) {
        IRBB * bb = m_cfg->getBB(p->id());
        ASSERT0(bb);
        BBIRListIter irlistit;
        bb->getIRList().get_tail(&irlistit);
        connectDefInBBTillPrevDef(bb, irlistit, stmtliveset);
        if (stmtliveset.all_killed()) {
            return;
        }
    }
}


void RenameDef::renameFollowUseInterBBTillNextDef(
    Vertex const* defvex, MOD LiveSet & stmtliveset, IRBB const* start_bb)
{
    RenameDefVisitFunc vf(*this, m_cfg, m_domtree);
    RenameDefVisit rn(defvex->id(), m_domtree, vf);
    rn.visit();
}


//defvex: the vertex on DomTree.
void RenameDef::rename(Vertex const* defvex, LiveSet * defliveset,
                       IRBB const* start_bb, IR const* start_ir)
{
    renameFollowUseIntraBBTillNextDef(defvex, *defliveset, start_bb, start_ir);
    if (defliveset->all_killed()) {
        m_bbid2liveset.free(start_bb->id());
        return;
    }
    renameFollowUseInterBBTillNextDef(defvex, *defliveset, start_bb);
}


void RenameDef::connect(Vertex const* defvex, LiveSet * defliveset,
                        IRBB const* start_bb, IR const* start_ir)
{
    ASSERT0(start_bb && start_ir);
    BBIRListIter irlistit = nullptr;
    BBIRList & irlist = const_cast<IRBB*>(start_bb)->getIRList();
    irlist.find(const_cast<IR*>(start_ir), &irlistit);
    ASSERT0(irlistit);
    irlistit = irlist.get_prev(irlistit);
    connectDefInBBTillPrevDef(start_bb, irlistit, *defliveset);
    if (defliveset->all_killed()) { return; }
    connectDefInterBBTillPrevDef(defvex, *defliveset, start_bb);
}


void RenameDef::processPhi(MDPhi const* newphi)
{
    ASSERT0(newphi);
    IRBB const* bb = newphi->getBB();
    Vertex const* defvex = m_domtree.getVertex(bb->id());
    ASSERT0(m_bbid2liveset.get(bb->id()) == nullptr);
    LiveSet * defliveset = m_bbid2liveset.genAndCopy(
        bb->id(), newphi->getResult());
    rename(defvex, defliveset, bb, nullptr);
    //Phi does not have previous-def.
}


void RenameDef::processStmt(IR * newstmt)
{
    ASSERT0(newstmt && MDSSAMgr::hasMDSSAInfo(newstmt));
    IRBB const* bb = newstmt->getBB();
    MDSSAInfo const* info = m_mgr->getMDSSAInfoIfAny(newstmt);
    if (info == nullptr || info->isEmptyVOpndSet()) {
        info = m_mgr->genMDSSAInfoAndNewVesionVMD(newstmt);
    }
    if (info->readVOpndSet().get_elem_count() == 0) {
        //MDSSAInfo may be empty if CALL does not have MustRef and MayRef.
        return;
    }
    ASSERT0(m_bbid2liveset.get(bb->id()) == nullptr ||
            m_bbid2liveset.get(bb->id())->is_empty());
    LiveSet * defliveset = m_bbid2liveset.genAndCopy(
        bb->id(), info->readVOpndSet());
    Vertex const* defvex = m_domtree.getVertex(bb->id());
    ASSERTN(defvex, ("miss vertex on domtree"));
    rename(defvex, defliveset, bb, newstmt);
    if (!m_is_build_ddchain) { return; }

    //Previous defliveset may has been freed.
    LiveSet * newdefliveset = m_bbid2liveset.genAndCopy(
        bb->id(), info->readVOpndSet());
    connect(defvex, newdefliveset, bb, newstmt);
}


void RenameDef::rename(IR * newstmt)
{
    processStmt(newstmt);
}


void RenameDef::rename(MDPhi const* newphi)
{
    processPhi(newphi);
}
//END RenameDef


//
//START RecomputeDefDefAndDefUseChain
//
RecomputeDefDefAndDefUseChain::RecomputeDefDefAndDefUseChain(
    xcom::DomTree const& domtree, MDSSAMgr * mgr,
    OptCtx const& oc, ActMgr * am)
    : m_domtree(domtree), m_mgr(mgr), m_oc(oc), m_am(am)
{
    m_rg = m_mgr->getRegion();
    m_cfg = m_rg->getCFG();
}


void RecomputeDefDefAndDefUseChain::recompute(MOD IR * stmt)
{
    ASSERT0(stmt && stmt->is_stmt() && MDSSAMgr::hasMDSSAInfo(stmt));
    MDSSAUpdateCtx ssactx(m_oc);
    if (MDSSAMgr::getMDSSAInfoIfAny(stmt) != nullptr) {
        //There is no MDSSAInfo if 'stmt' is just generated.
        //Remove old MDSSAInfo, cutoff DefDef chain and DefUse chain before
        //recomputation.
        m_mgr->removeStmtMDSSAInfo(stmt, ssactx);
    }
    //Generate new MDSSAInfo according to stmt's memory reference.
    MDSSAInfo const* info = m_mgr->genMDSSAInfoAndNewVesionVMD(stmt);
    ASSERT0_DUMMYUSE(info);

    //MDSSAInfo may be empty if CALL does not have any MustRef and MayRef.
    //ASSERT0(info->readVOpndSet().get_elem_count() > 0);
    //Perform renaming according to the new versioned VMD.
    //Note the original USE of origin VMD may not be reached by this renaming
    //because 'stmt' may have been moved a BB that does not dominate origin
    //USE any more. And the origin USE's DefUse chain will be revised during
    //InsertPreheaderMgr::reviseSSADU().
    RenameDef rn(getDomTree(), true, m_mgr, getActMgr());
    rn.rename(stmt);
}


void RecomputeDefDefAndDefUseChain::recompute(xcom::List<IR*> const& irlist)
{
    xcom::List<IR*>::Iter it;
    for (IR * stmt = irlist.get_head(&it);
         stmt != nullptr; stmt = irlist.get_next(&it)) {
        ASSERT0(MDSSAMgr::getMDSSAInfoIfAny(stmt));
        recompute(stmt);
    }
}


void RecomputeDefDefAndDefUseChain::recompute(MDPhi const* phi)
{
    ASSERT0(phi);
    RenameDef rn(getDomTree(), true, m_mgr, getActMgr());
    rn.rename(phi);
}


//In C++, local declared class should NOT be used in template parameters of a
//template class. Because the template class may be instanced outside the
//function and the local type in function is invisible.
class VFToRecomp {
    IR const* m_prev_stmt;
    IRBB const* m_start_bb;
    MDSSAMgr * m_mgr;
    OptCtx const& m_oc;
    MDSSAStatus & m_st;
public:
    VFToRecomp(IR const* prev, IRBB const* s, MDSSAMgr * m,
               OptCtx const& oc, MOD MDSSAStatus & st)
        : m_prev_stmt(prev), m_start_bb(s), m_mgr(m), m_oc(oc), m_st(st) {}
    bool visitIR(MOD IR * ir, OUT bool & is_term)
    {
        if (!ir->is_exp() || !MDSSAMgr::hasMDSSAInfo(ir)) { return true; }
        m_mgr->findAndSetLiveInDef(ir, m_prev_stmt, m_start_bb, m_oc, m_st);
        return true;
    }
};


//irit: for local used.
void RecomputeDefDefAndDefUseChain::recomputeDefForRHS(MOD IR * stmt)
{
    class IterTree : public VisitIRTree<VFToRecomp> {
    public:
        IterTree(VFToRecomp & vf) : VisitIRTree(vf) {}
    };
    ASSERT0(stmt && stmt->is_stmt() && stmt->getBB());
    IRBB * start_bb = stmt->getBB();
    IR * prev_stmt = start_bb->getPrevIR(stmt);
    MDSSAStatus st;
    VFToRecomp vf(prev_stmt, start_bb, m_mgr, m_oc, st);
    IterTree it(vf);
    it.visit(stmt);
}


void RecomputeDefDefAndDefUseChain::recomputeDefForPhiOpnd(MDPhi const* phi)
{
    UINT idx = 0;
    ASSERT0(phi->getBB());
    Vertex const* vex = phi->getBB()->getVex();
    ASSERT0(vex);
    AdjVertexIter itv;
    ASSERT0(m_oc.is_dom_valid());
    MDSSAStatus st;
    for (xcom::Vertex const* in = Graph::get_first_in_vertex(vex, itv);
         in != nullptr; in = Graph::get_next_in_vertex(itv), idx++) {
        IR * opnd = phi->getOpnd(idx);
        ASSERT0(opnd->is_leaf());
        m_mgr->findAndSetLiveInDef(opnd, m_cfg->getBB(in->id()), m_oc, st);
    }
}


void RecomputeDefDefAndDefUseChain::recomputeDefForPhiOpnd(
    MDPhiList const* philist)
{
    ASSERT0(philist);
    ASSERT0(m_oc.is_dom_valid());
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi && phi->is_phi());
        recomputeDefForPhiOpnd(phi);
    }
}


void RecomputeDefDefAndDefUseChain::recompute(MDPhiList const* philist)
{
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi && phi->is_phi());
        recompute(phi);
   }
}
//END RecomputeDefDefAndDefUseChain


//
//SATRT RenameExp
//
RenameExp::RenameExp(MDSSAMgr * mgr, OptCtx * oc, ActMgr * am) :
    m_mgr(mgr), m_am(am), m_oc(oc)
{
    m_rg = mgr->getRegion();
}


//In C++, local declared class should NOT be used in template parameters of a
//template class. Because the template class may be instanced outside the
//function and the local type in function is invisible.
class VFToRename {
public:
    bool visitIR(IR * ir, OUT bool & is_term)
    {
        if (!ir->is_exp() || !ir->isMemRefNonPR()) { return true; }
        m_mdssamgr->findAndSetLiveInDef(
            ir, m_startir, m_startbb, *m_oc, *m_st);
        return true;
    }
public:
    //startir: the start position in 'startbb', it can be NULL.
    //         If it is NULL, the function first finding the Phi list of
    //         'startbb', then keep finding its predecessors until meet the
    //         CFG entry.
    //startbb: the BB that begin to do searching. It can NOT be NULL.
    IR const* m_startir;
    IRBB const* m_startbb;
    MDSSAMgr * m_mdssamgr;
    OptCtx * m_oc;
    MDSSAStatus * m_st;
public:
    //startir: may be NULL.
    VFToRename(IR const* startir, IRBB const* startbb, MDSSAMgr * mdssamgr,
               OptCtx * oc, MDSSAStatus * st)
    {
        ASSERT0(startbb && mdssamgr && oc && st);
        m_startir = startir;
        m_startbb = startbb;
        m_mdssamgr = mdssamgr;
        m_oc = oc;
        m_st = st;
    }
};


void RenameExp::rename(MOD IR * root, IR const* startir, IRBB const* startbb)
{
    class IterTree : public VisitIRTree<VFToRename> {
    public:
        IterTree(VFToRename & vf) : VisitIRTree(vf) {}
    };
    ASSERT0(root && (root->is_stmt() || root->is_exp()));
    ASSERT0(startir == nullptr ||
            (startir->is_stmt() && startir->getBB() == startbb));
    MDSSAStatus st;
    VFToRename vf(startir, startbb, m_mgr, m_oc, &st);
    IterTree it(vf);
    it.visit(root);
}
//END RenameExp


//
//START ReconstructMDSSA
//
ReconstructMDSSAVF::ReconstructMDSSAVF(
    xcom::VexTab const& vextab, DomTree const& dt, xcom::Graph const* cfg,
    MDSSAMgr * mgr, OptCtx * oc, ActMgr * am)
        : m_vextab(vextab), m_cfg(cfg), m_mdssamgr(mgr), m_oc(oc), m_am(am),
          m_dt(dt)
{
    ASSERT0(m_mdssamgr);
    m_rg = m_mdssamgr->getRegion();
}


void ReconstructMDSSAVF::renameBBPhiList(IRBB const* bb) const
{
    MDPhiList const* philist = m_mdssamgr->getPhiList(bb);
    if (philist == nullptr) { return; }
    RenameDef rn(m_dt, false, m_mdssamgr, getActMgr());
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi const* phi = it->val();
        ASSERT0(phi && phi->is_phi());
        rn.clean();
        rn.rename(phi);
    }
}


void ReconstructMDSSAVF::renameBBIRList(IRBB const* bb) const
{
    BBIRList & irlst = const_cast<IRBB*>(bb)->getIRList();
    BBIRListIter irit;
    IR * prev = nullptr;
    RenameExp rne(m_mdssamgr, m_oc, getActMgr());

    //CASE:DefDef chain is necessary after new stmt generated.
    //e.g:compile/vect18_5_2.c, compile.gr/vect18_5.low.gr
    //The new generated stmt should connect DefDef chain with the previous
    //stmt when it has been inserted into a BB.
    bool need_build_dd_chain = true;
    RenameDef rnd(m_dt, need_build_dd_chain, m_mdssamgr, getActMgr());
    for (IR * ir = irlst.get_head(&irit);
         ir != nullptr; prev = ir, ir = irlst.get_next(&irit)) {
        ASSERT0(ir->is_stmt());
        if (MDSSAMgr::hasMDSSAInfo(ir)) {
            rnd.clean();
            rnd.rename(ir);
        }
        rne.rename(ir, prev, bb);
    }
}
//END ReconstructMDSSA


//
//START LiveSet
//
void LiveSet::dump(MDSSAMgr const* mgr) const
{
    note(mgr->getRegion(), "\nLiveSet:");
    bool first = true;
    VOpndSetIter it;
    for (BSIdx i = get_first(&it);
         i != BS_UNDEF; i = get_next(i, &it)) {
        VMD * t = (VMD*)const_cast<MDSSAMgr*>(mgr)->getUseDefMgr()->getVOpnd(i);
        ASSERT0(t && t->is_md());
        if (!first) { prt(mgr->getRegion(), ","); }
        first = false;
        t->dump(mgr->getRegion());
    }
    if (first) {
        //Set is empty.
        prt(mgr->getRegion(), "--");
    }
}
//END LiveSet


//
//START VMD::UseSet
//
void VMD::UseSet::dump(Region const* rg) const
{
    if (!rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    VMD::UseSetIter it;
    note(rg, "\nVMD::UseSet:");
    xcom::StrBuf tmp(8);
    for (UINT i = get_first(it); !it.end(); i = get_next(it)) {
        IR const* ir = rg->getIR(i);
        ASSERT0(ir);
        prt(rg, "<%s> ", dumpIRName(ir, tmp));
    }
}
//END VMD::UseSet


//
//START BB2DefMDSet
//
void BB2DefMDSet::dump(Region const* rg) const
{
    if (!rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    note(rg, "\n==-- DUMP BB2DefMDSet --==");
    BBList * bbl = rg->getBBList();
    for (IRBB const* bb = bbl->get_head();
         bb != nullptr; bb = bbl->get_next()) {
        DefMDSet * defmds = get(bb->id());
        note(rg, "\nBB%d DefinedMDSet:", bb->id());
        if (defmds == nullptr) { continue; }
        defmds->dump(rg->getLogMgr()->getFileHandler());
    }
}
//END


//
//START MD2VMDStack
//
void MD2VMDStack::dump(Region const* rg) const
{
    if (!rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    note(rg, "\n==-- DUMP MD2VMDStack --==");
    for (VecIdx i = MD_UNDEF + 1; i <= get_last_idx(); i++) {
        VMDStack * s = get(i);
        note(rg, "\nMD%u:", i);
        if (s == nullptr) {
            continue;
        }
        for (VMD * vmd = s->get_bottom(); vmd != nullptr; vmd = s->get_next()) {
            ASSERT0(vmd->mdid() == (UINT)i);
            prt(rg, "v%u|", vmd->version());
        }
    }
}


void MD2VMDStack::destroy()
{
    for (VecIdx i = 0; i <= get_last_idx(); i++) {
        VMDStack * s = get(i);
        if (s != nullptr) { delete s; }
    }
    Vector<VMDStack*>::destroy();
}


VMDStack * MD2VMDStack::gen(UINT mdid)
{
    VMDStack * stk = get(mdid);
    if (stk == nullptr) {
        stk = new VMDStack();
        set(mdid, stk);
    }
    return stk;
}


VMD * MD2VMDStack::get_top(UINT mdid) const
{
    VMDStack * stk = get(mdid);
    if (stk == nullptr) { return nullptr; }
    return stk->get_top();
}


void MD2VMDStack::push(UINT mdid, VMD * vmd)
{
    ASSERT0(mdid != MD_UNDEF && vmd && vmd->mdid() == mdid);
    VMDStack * stk = gen(mdid);
    ASSERT0(stk);
    stk->push(vmd);
}
//END MD2VMDStack


//
//START MDSSAMgr
//
size_t MDSSAMgr::count_mem() const
{
    size_t count = m_map_md2stack.count_mem();
    count += m_max_version.count_mem();
    count += m_usedef_mgr.count_mem();
    count += sizeof(*this);
    return count;
}


MDSSAMgr::MDSSAMgr(Region * rg) :
    Pass(rg),
    m_sbs_mgr(rg->getMiscBitSetMgr()),
    m_seg_mgr(rg->getMiscBitSetMgr()->getSegMgr()),
    m_usedef_mgr(rg, this)
{
    cleanInConstructor();
    ASSERT0(rg);
    m_tm = rg->getTypeMgr();
    m_irmgr = rg->getIRMgr();
    ASSERT0(m_tm);
    ASSERT0(rg->getMiscBitSetMgr());
    ASSERT0(m_seg_mgr);
    m_cfg = rg->getCFG();
    ASSERTN(m_cfg, ("cfg is not available."));
    m_md_sys = rg->getMDSystem();
    m_am = new ActMgr(m_rg);
}


void MDSSAMgr::destroy()
{
    if (m_usedef_mgr.m_mdssainfo_pool == nullptr) { return; }

    //CAUTION: If you do not finish out-of-SSA prior to destory(),
    //the reference to IR's MDSSA info will lead to undefined behaviors.
    //ASSERTN(!m_is_valid,
    //        ("Still in ssa mode, you should do out of "
    //         "SSA before destroy"));

    freePhiList();
    delete m_am;
    m_am = nullptr;
}


void MDSSAMgr::freeBBPhiList(IRBB * bb)
{
    MDPhiList * philist = getPhiList(bb->id());
    if (philist == nullptr) { return; }
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi && phi->is_phi());
        m_rg->freeIRTreeList(phi->getOpndList());
        MDPHI_opnd_list(phi) = nullptr;
    }
}


void MDSSAMgr::freePhiList()
{
    for (IRBB * bb = m_rg->getBBList()->get_head();
         bb != nullptr; bb = m_rg->getBBList()->get_next()) {
        freeBBPhiList(bb);
    }
    m_usedef_mgr.m_philist_vec.destroy();
    m_usedef_mgr.m_philist_vec.init();
}


//The function destroy data structures that allocated during SSA
//construction, and these data structures are only useful in construction.
void MDSSAMgr::cleanLocalUsedData()
{
    //Do NOT clean max_version of each MD, because some passes will generate
    //DEF stmt for individual MD, which need new version of the MD.
    //MD's max verison is often used to update MDSSAInfo incrementally.
    //m_max_version.destroy();
    //m_max_version.init();
}


//This function dumps VMD structure and SSA DU info.
void MDSSAMgr::dumpAllVMD() const
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    note(getRegion(), "\n\n==---- DUMP MDSSAMgr: ALL VMD '%s'----==",
         m_rg->getRegionName());
    VOpndVec * vec = const_cast<MDSSAMgr*>(this)->getUseDefMgr()->
        getVOpndVec();
    xcom::StrBuf tmp(8);
    for (VecIdx i = 1; i <= vec->get_last_idx(); i++) {
        VMD * v = (VMD*)vec->get(i);
        if (v == nullptr) {
            //Some pass, e.g:ir_refinement, will remove ir and related VMD.
            continue;
        }
        note(getRegion(), "\nVMD%d:MD%dV%d: ",
             v->id(), v->mdid(), v->version());
        MDDef * mddef = v->getDef();
        //Print DEF.
        if (v->version() != MDSSA_INIT_VERSION) {
            //After renaming, MD must have defstmt if its version is nonzero.
            ASSERT0(mddef);
        }
        if (mddef != nullptr) {
            if (mddef->is_phi()) {
                ASSERT0(mddef->getBB());
                prt(getRegion(), "DEF:(phi,BB%d)", mddef->getBB()->id());
            } else {
                IR const* stmt = mddef->getOcc();
                ASSERT0(stmt);
                //The stmt may have been removed, and the VMD is obsoleted.
                //If the stmt removed, its UseSet should be empty.
                //ASSERT0(stmt->is_stmt() && !stmt->isWritePR());
                prt(getRegion(), "DEF:(%s)", dumpIRName(stmt, tmp));
            }
        } else {
            prt(getRegion(), "DEF:---");
        }

        //Print USEs.
        prt(getRegion(), "\tUSE:");
        VMD::UseSetIter it;
        BSIdx nexti = 0;
        for (BSIdx j = v->getUseSet()->get_first(it); !it.end(); j = nexti) {
            nexti = v->getUseSet()->get_next(it);
            IR * use = m_rg->getIR(j);
            ASSERT0(use && !use->isReadPR());
            prt(getRegion(), "(%s)", dumpIRName(use, tmp));
            if (nexti != BS_UNDEF) {
                prt(getRegion(), ",");
            }
        }
    }
    note(getRegion(), "\n");
}


//Before removing BB or change BB successor,
//you need remove the related PHI operand if BB successor has PHI.
void MDSSAMgr::removeSuccessorDesignatedPhiOpnd(
    IRBB const* succ, UINT pos, MDSSAUpdateCtx const& ctx)
{
    MDPhiList * philist = getPhiList(succ->id());
    if (philist == nullptr) { return; }
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi && phi->is_phi());
        //CASE:CFG optimization may have already remove the predecessor of
        //'succ' before call the function.
        //ASSERT0(phi->getOpndNum() == succ->getNumOfPred());
        if (phi->getOpndList() == nullptr) {
            //MDPHI does not contain any operand.
            continue;
        }
        IR * opnd = phi->getOpnd(pos);
        removeMDSSAOccForTree(opnd, ctx);
        phi->removeOpnd(opnd);
        m_rg->freeIRTree(opnd);
    }
}


IR * MDSSAMgr::findUniqueDefInLoopForMustRef(IR const* exp, LI<IRBB> const* li,
                                             Region const* rg,
                                             OUT IRSet * set) const
{
    ASSERT0(exp && exp->isMemRefNonPR());
    MD const* mustuse = exp->getMustRef();
    if (mustuse == nullptr) { return nullptr; }
    MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(exp);
    ASSERT0(mdssainfo);
    xcom::DefMiscBitSetMgr sbsmgr;
    IRSet tmpset(sbsmgr.getSegMgr());
    if (set == nullptr) { set = &tmpset; }
    CollectCtx ctx(COLLECT_CROSS_PHI|COLLECT_INSIDE_LOOP);
    ctx.setLI(li);
    CollectDef cd(this, mdssainfo, ctx, mustuse, set);
    if (set->get_elem_count() == 1) {
        IRSetIter it;
        return m_rg->getIR(set->get_first(&it));
    }
    return nullptr;
}


//The function try to find the unique MDDef for given def that is outside
//of the loop.
//Return the MDDef if found, otherwise nullptr.
MDDef const* MDSSAMgr::findUniqueOutsideLoopDef(MDDef const* phi,
                                                LI<IRBB> const* li) const
{
    ASSERT0(phi->is_phi());
    UINT num_outside_def = 0;
    MDDef const* ret = nullptr;
    for (IR const* opnd = MDPHI_opnd_list(phi);
         opnd != nullptr; opnd = opnd->get_next()) {
        VMD * opndvmd = ((MDPhi*)phi)->getOpndVMD(opnd,
            const_cast<MDSSAMgr*>(this)->getUseDefMgr());
        MDDef const* def = opndvmd->getDef();
        if (def == nullptr) { continue; }
        ASSERT0(def->getBB());
        if (li->isInsideLoop(def->getBB()->id())) { continue; }
        ret = def;
        num_outside_def++;
        if (num_outside_def > 1) { return nullptr; }
    }
    return ret;
}


//Find VMD from ir list and phi list.
VMD * MDSSAMgr::findLastMayDef(IRBB const* bb, MDIdx mdid) const
{
    return findLastMayDefFrom(bb, BB_last_ir(const_cast<IRBB*>(bb)), mdid);
}


VMD * MDSSAMgr::findVMDFromPhiList(IRBB const* bb, MDIdx mdid) const
{
    MDPhiList * philist = getPhiList(bb->id());
    if (philist == nullptr) { return nullptr; }
    for (MDPhiListIter pit = philist->get_head();
         pit != philist->end(); pit = philist->get_next(pit)) {
        MDPhi * phi = pit->val();
        ASSERT0(phi && phi->is_phi());
        VMD * res = phi->getResult();
        ASSERT0(res);
        if (res->mdid() == mdid) { return res; }
    }
    return nullptr;
}


//Find VMD from ir list and phi list.
//start: the start position, if it is NULL, the function will scan the phi list.
VMD * MDSSAMgr::findLastMayDefFrom(IRBB const* bb, IR const* start,
                                   MDIdx mdid) const
{
    if (start != nullptr) {
        BBIRListIter it = nullptr;
        BBIRList const& irlist = const_cast<IRBB*>(bb)->getIRList();
        irlist.find(const_cast<IR*>(start), &it);
        ASSERTN(it, ("IR%d is not belong to BB%d", start->id(), bb->id()));
        for (; it != nullptr; it = irlist.get_prev(it)) {
            IR const* ir = it->val();
            VMD * vmd;
            if (MDSSAMgr::hasMDSSAInfo(ir) &&
                (vmd = findMayRef(ir, mdid)) != nullptr) {
                return vmd;
            }
        }
    }
    return findVMDFromPhiList(bb, mdid);
}


//The function does searching that begin at the IDom BB of marker.
//Note DOM info must be available.
VMD * MDSSAMgr::findDomLiveInDefFromIDomOf(
    IRBB const* marker, MDIdx mdid, OptCtx const& oc,
    OUT MDSSAStatus & st) const
{
    UINT idom = ((DGraph*)m_cfg)->get_idom(marker->id());
    if (idom == VERTEX_UNDEF) { return nullptr; }
    ASSERT0(m_cfg->isVertex(idom));
    IRBB * bb = m_cfg->getBB(idom);
    ASSERT0(bb);
    return findDomLiveInDefFrom(mdid, bb->getLastIR(), bb, oc, st);
}


VMD * MDSSAMgr::findLiveInDefFrom(
    MDIdx mdid, IRBB const* bb, IR const* startir, IRBB const* startbb,
    VMDVec const* vmdvec) const
{
    if (bb == startbb) {
        if (startir != nullptr) {
            return findLastMayDefFrom(bb, startir, mdid);
        }
        return findVMDFromPhiList(bb, mdid);
    }
    if (vmdvec != nullptr && vmdvec->hasDefInBB(bb->id())) {
        VMD * livein = findLastMayDef(bb, mdid);
        ASSERT0(livein);
        return livein;
    }
    return nullptr;
}


VMD * MDSSAMgr::findDomLiveInDefFrom(
    MDIdx mdid, IR const* startir, IRBB const* startbb, OptCtx const& oc,
    OUT MDSSAStatus & st) const
{
    ASSERT0(startbb);
    //NOTE startir may be have already removed from startbb. For this case,
    //we have to trust that caller passed in right parameters.
    //ASSERT0(startir == nullptr ||
    //        const_cast<IRBB*>(startbb)->getIRList()->find(
    //        const_cast<IR*>(startir)));
    IRBB const* meetup = m_cfg->getEntry();
    ASSERT0(meetup);
    VMDVec const* vmdvec = m_usedef_mgr.getVMDVec(mdid);
    IRBB * idom = nullptr;
    for (IRBB const* t = startbb; t != nullptr; t = idom) {
        UINT idomidx = ((DGraph*)m_cfg)->get_idom(t->id());
        if (idomidx == VERTEX_UNDEF) {
            idom = nullptr;
        } else {
            ASSERTN(m_cfg->isVertex(idomidx), ("miss DomInfo"));
            idom = m_cfg->getBB(idomidx);
            ASSERT0(idom);
        }
        VMD * livein = findLiveInDefFrom(mdid, t, startir, startbb, vmdvec);
        if (livein != nullptr) { return livein; }
        if (t == meetup) { continue; }
        if (!oc.is_dom_valid()) {
            //In the middle stage of optimization, e.g DCE, pass may transform
            //the CFG into a legal but insane CFG. In the case, the CFG
            //may be divided into serveral isolated parts. Thus there is no
            //livein path from entry to current BB.
            //Note if DOM info is not maintained, SSA update can not prove
            //to be correct. However, for now we keep doing the update to
            //maintain PHI's operand in order to tolerate subsequently
            //processing of CFG.
            st.set(MDSSA_STATUS_DOM_IS_INVALID);
            return nullptr;
        }
    }
    //DEF is Region-Livein-VMD of 'mdid'.
    return nullptr;
}


void MDSSAMgr::addSuccessorDesignatedPhiOpnd(
    IRBB * bb, IRBB * succ, OptCtx const& oc, OUT MDSSAStatus & st)
{
    MDPhiList * philist = getPhiList(succ->id());
    if (philist == nullptr) { return; }
    bool is_pred;
    UINT const pos = m_cfg->WhichPred(bb, succ, is_pred);
    ASSERT0(is_pred);
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi && phi->is_phi());
        MDSSAStatus lst;
        phi->insertOpndAt(this, pos, bb, oc, lst);
        st.bunion(lst);
        ASSERT0(phi->getOpndNum() == succ->getNumOfPred());
    }
}


void MDSSAMgr::dumpPhiList(MDPhiList const* philist) const
{
    if (philist == nullptr) { return; }
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi const* phi = it->val();
        ASSERT0(phi && phi->is_phi());
        note(getRegion(), "\n");
        phi->dump(m_rg, &m_usedef_mgr);
    }
}


void MDSSAMgr::dumpIRWithMDSSAForStmt(IR const* ir, bool & parting_line) const
{
    if (!ir->is_stmt() || (!ir->isMemRefNonPR() && !ir->isCallStmt())) {
        return;
    }
    if (!parting_line) {
        note(getRegion(), "\n----");
        parting_line = true;
    }
    dumpIR(ir, m_rg, nullptr, IR_DUMP_DEF);

    MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(ir);
    if (mdssainfo == nullptr || mdssainfo->isEmptyVOpndSet()) {
        prt(getRegion(), "%s", g_msg_no_mdssainfo);
        return;
    }
    VOpndSetIter iter = nullptr;
    for (BSIdx i = mdssainfo->getVOpndSet()->get_first(&iter);
        i != BS_UNDEF; i = mdssainfo->getVOpndSet()->get_next(i, &iter)) {
        note(getRegion(), "\n--DEF:");
        VMD * vopnd = (VMD*)m_usedef_mgr.getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md());
        if (vopnd->getDef() != nullptr) {
            ASSERT0(vopnd->getDef()->getOcc() == ir);
        }
        vopnd->dump(m_rg, &m_usedef_mgr);
    }
}


void MDSSAMgr::dumpIRWithMDSSAForExp(IR const* ir, bool & parting_line) const
{
    List<IR const*> lst;
    List<IR const*> opnd_lst;
    for (IR const* opnd = iterExpInitC(ir, lst);
         opnd != nullptr; opnd = iterExpNextC(lst)) {
        if (!opnd->isMemRefNonPR() || opnd->is_stmt()) {
            continue;
        }
        VOpndSetIter iter = nullptr;
        if (!parting_line) {
            note(getRegion(), "\n----");
            parting_line = true;
        }

        dumpIR(opnd, m_rg, nullptr, IR_DUMP_DEF);

        note(getRegion(), "\n--USE:");
        bool first = true;
        MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(opnd);
        if (mdssainfo == nullptr || mdssainfo->isEmptyVOpndSet()) {
            prt(getRegion(), "%s", g_msg_no_mdssainfo);
            continue;
        }

        for (BSIdx i = mdssainfo->getVOpndSet()->get_first(&iter);
             i != BS_UNDEF; i = mdssainfo->getVOpndSet()->get_next(i, &iter)) {
            VMD * vopnd = (VMD*)m_usedef_mgr.getVOpnd(i);
            ASSERT0(vopnd && vopnd->is_md());
            if (first) {
                first = false;
            } else {
                prt(getRegion(), ",");
            }
            prt(getRegion(), "MD%dV%d", vopnd->mdid(), vopnd->version());
        }
    }
}


//Dump IR tree and MDSSAInfo if any.
//ir: can be stmt or expression.
//flag: the flag to dump IR.
void MDSSAMgr::dumpIRWithMDSSA(IR const* ir, DumpFlag flag) const
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    ASSERT0(ir);
    dumpIR(ir, m_rg, nullptr, flag);

    bool parting_line = false;
    m_rg->getLogMgr()->incIndent(2);
    //ir->dumpRef(m_rg, 0); //Dump REF may make dumpinfo in a mess.
    dumpIRWithMDSSAForStmt(ir, parting_line);
    dumpIRWithMDSSAForExp(ir, parting_line);
    m_rg->getLogMgr()->decIndent(2);
}


void MDSSAMgr::dumpBBList() const
{
    dumpVOpndRef();
}


void MDSSAMgr::dumpVOpndRef() const
{
    Region * rg = getRegion();
    if (!rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    note(rg, "\n==-- DUMP MDSSAMgr VOpndRef '%s' --==\n", rg->getRegionName());
    BBList * bbl = m_rg->getBBList();
    for (IRBB * bb = bbl->get_head(); bb != nullptr; bb = bbl->get_next()) {
        note(rg, "\n--- BB%d ---", bb->id());
        dumpPhiList(getPhiList(bb->id()));
        for (IR * ir = BB_first_ir(bb); ir != nullptr; ir = BB_next_ir(bb)) {
            note(rg, "\n");
            dumpIRWithMDSSA(ir);
        }
    }
}


bool MDSSAMgr::dump() const
{
    if (!m_rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return false; }
    START_TIMER(t, "MDSSA: Dump After Pass");
    note(getRegion(), "\n==---- DUMP %s '%s' ----==",
         getPassName(), m_rg->getRegionName());
    getRegion()->getLogMgr()->incIndent(2);
    m_md_sys->dump(m_rg->getVarMgr(), true);
    dumpVOpndRef();
    dumpDUChain();
    getRegion()->getLogMgr()->decIndent(2);
    END_TIMER(t, "MDSSA: Dump After Pass");
    return true;
}


//Find the VOpnd if 'ir' must OR may referenced 'md'.
//Return the VMD if found.
VMD * MDSSAMgr::findMayRef(IR const* ir, MDIdx mdid) const
{
    ASSERT0(ir->isMemRef());
    ASSERT0(ir->is_stmt() || ir->is_exp());
    MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(ir);
    ASSERTN(mdssainfo, ("miss MDSSAInfo"));
    VOpndSetIter iter = nullptr;
    VOpndSet const& vopndset = mdssainfo->readVOpndSet();
    for (BSIdx i = vopndset.get_first(&iter);
         i != BS_UNDEF; i = vopndset.get_next(i, &iter)) {
        VMD * t = (VMD*)m_usedef_mgr.getVOpnd(i);
        ASSERT0(t && t->is_md());
        if (t->mdid() == mdid) { return t; }
    }
    return nullptr;
}


//Find the MustDef of 'ir'.
MDDef * MDSSAMgr::findMustMDDef(IR const* ir) const
{
    ASSERT0(ir && (ir->is_exp() || ir->is_stmt()) && ir->isMemRefNonPR());
    MD const* mustref = ir->getMustRef();
    if (mustref == nullptr || (!mustref->is_exact() && !mustref->is_range())) {
        return nullptr;
    }
    MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(ir);
    ASSERTN(mdssainfo, ("miss MDSSAInfo"));
    VOpndSetIter iter = nullptr;
    for (BSIdx i = mdssainfo->readVOpndSet().get_first(&iter);
         i != BS_UNDEF; i = mdssainfo->readVOpndSet().get_next(i, &iter)) {
        VMD * t = (VMD*)m_usedef_mgr.getVOpnd(i);
        ASSERT0(t && t->is_md());
        MDDef * tdef = t->getDef();
        if (tdef != nullptr && tdef->getResultMD(m_md_sys) == mustref) {
            return tdef;
        }
    }
    return nullptr;
}


bool MDSSAMgr::hasDef(IR const* ir) const
{
    ASSERT0(ir->is_exp());
    MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(ir);
    ASSERTN(mdssainfo, ("miss MDSSAInfo"));
    VOpndSetIter iter = nullptr;
    for (BSIdx i = mdssainfo->readVOpndSet().get_first(&iter);
         i != BS_UNDEF; i = mdssainfo->readVOpndSet().get_next(i, &iter)) {
        VMD * t = (VMD*)m_usedef_mgr.getVOpnd(i);
        ASSERT0(t && t->is_md());
        MDDef * tdef = t->getDef();
        if (tdef != nullptr) { return true; }
    }
    return false;
}


static bool isRegionLiveInByMDDef(
    IR const* ir, MDDef const* def, xcom::TTab<MDDef const*> & visited,
    MDSSAMgr const* mgr)
{
    ASSERT0(ir && ir->is_exp() && ir->isMemOpnd());
    ASSERT0(def && !def->is_phi());
    if (visited.find(def)) { return true; }
    visited.append(def);
    IR const* defocc = def->getOcc();
    ASSERT0(defocc);
    MD const* defocc_mustref = defocc->getMustRef();
    if (defocc_mustref == nullptr) {
        //CASE: the defocc May Def the MD reference of 'ir'.
        //e.g: *p = 10; //The stmt may define MD7.
        //     ... = g; //MustRef is MD7.
        return false;
    }
    if (xoc::isDependent(ir, defocc, true, mgr->getRegion())) {
        return false;
    }
    return true;
}


static bool isRegionLiveInByMDPhi(
    IR const* ir, MDPhi const* phi, xcom::TTab<MDDef const*> & visited,
    MDSSAMgr const* mgr)
{
    ASSERT0(ir && ir->is_exp() && ir->isMemOpnd());
    ASSERT0(phi && phi->is_phi());
    if (visited.find(phi)) { return true; }
    visited.append(phi);
    for (IR const* opnd = MDPHI_opnd_list(phi);
         opnd != nullptr; opnd = opnd->get_next()) {
        VMD * opndvmd = ((MDPhi*)phi)->getOpndVMD(opnd,
            const_cast<MDSSAMgr*>(mgr)->getUseDefMgr());
        MDDef const* def = opndvmd->getDef();
        if (def == nullptr) { continue; }
        if (def->is_phi()) {
            if (!isRegionLiveInByMDPhi(ir, (MDPhi const*)def, visited, mgr)) {
                return false;
            }
            continue;
        }
        if (!isRegionLiveInByMDDef(ir, def, visited, mgr)) {
            return false;
        }
        continue;
    }
    return true;
}


bool MDSSAMgr::isRegionLiveIn(IR const* ir) const
{
    ASSERT0(ir && ir->isMemOpnd() && ir->is_exp());
    MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(ir);
    ASSERTN(mdssainfo, ("invalid MDSSA"));
    MD const* mustref = ir->getMustRef();
    if (mustref == nullptr) { return mdssainfo->isLiveInVOpndSet(this); }

    //Check the MustRef of 'ir' whether it is defined in all the paths that
    //begins at the region entry to current ir.
    VOpndSetIter it = nullptr;
    xcom::TTab<MDDef const*> visited;
    for (BSIdx i = mdssainfo->readVOpndSet().get_first(&it);
        i != BS_UNDEF; i = mdssainfo->readVOpndSet().get_next(i, &it)) {
        VMD const* vopnd = (VMD*)getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md());
        if (vopnd->isLiveIn()) { continue; }
        MDDef const* mddef = vopnd->getDef();
        ASSERT0(mddef);
        if (mddef->is_phi()) {
            //CASE: Determine whether exist MayDef by crossing MDPhi.
            //e.g: t1 is region live-in.
            //  BB1:
            //  st d = ld t2; //d's MD ref is {MD11V1:MD2V1}
            //  falsebr L1 ld i, 0;
            //   |   |
            //   |   V
            //   |  BB2:
            //   |  st e = #20; //e's MD ref is {MD12V1:MD2V2}
            //   |  |
            //   V  V
            //  BB3:
            //  L1:
            //  MDPhi: MD2V3 <- (MD2V1 BB1), (MD2V2 BB2)
            //  return ld t1; //t1's MD ref is {MD7V0:MD2V3}
            if (!isRegionLiveInByMDPhi(
                    ir, (MDPhi const*)mddef, visited, this)) {
                return false;
            }
            continue;
        }
        if (!isRegionLiveInByMDDef(ir, mddef, visited, this)) {
            return false;
        }
        continue;
    }
    return true;
}


//Find nearest virtual DEF in VOpndSet of 'ir'.
MDDef * MDSSAMgr::findNearestDef(IR const* ir) const
{
    ASSERT0(ir && ir->is_exp() && ir->isMemOpnd());
    MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(ir);
    ASSERTN(mdssainfo, ("miss MDSSAInfo"));
    VOpndSetIter iter = nullptr;
    INT lastrpo = RPO_UNDEF;
    MDDef * last = nullptr;
    for (BSIdx i = mdssainfo->readVOpndSet().get_first(&iter);
         i != BS_UNDEF; i = mdssainfo->readVOpndSet().get_next(i, &iter)) {
        VMD * t = (VMD*)m_usedef_mgr.getVOpnd(i);
        ASSERT0(t && t->is_md());
        MDDef * tdef = t->getDef();
        if (last == nullptr) {
            if (tdef != nullptr) {
                last = tdef;
                ASSERT0(tdef->getBB());
                lastrpo = last->getBB()->rpo();
                ASSERT0(lastrpo != RPO_UNDEF);
            } else {
                ASSERT0(t->isLiveIn());
                //Regard the virtual def at the entry of region,
                //it is the farmost def.
            }
            continue;
        }

        if (tdef == nullptr) { continue; }

        ASSERT0(tdef->getResult() && tdef->getResult()->is_md());

        IRBB * tbb = tdef->getBB();
        ASSERT0(tbb);
        ASSERT0(tbb->rpo() != RPO_UNDEF);
        if (tbb->rpo() > lastrpo) {
            //tdef is near more than 'last', then update 'last'.
            last = tdef;
            //Update nearest BB's rpo.
            lastrpo = tbb->rpo();
            continue;
        }
        if (tbb != last->getBB()) {
            //last is near more than tdef, so nothing to do.
            continue;
        }

        //tdef' and 'last' are placed in same BB.
        if (tdef->is_phi()) {
            ; //last is near more than tdef, so nothing to do.
            if (tdef->getResult()->mdid() == last->getResult()->mdid()) {
                ASSERT0(tdef == last || !last->is_phi());
            }
            continue;
        }
        if (last->is_phi()) {
            if (tdef->getResult()->mdid() == last->getResult()->mdid()) {
                ASSERT0(tdef == last || !tdef->is_phi());
            }

            //tdef is near more than 'last', then update 'last'.
            last = tdef;
            ASSERTN(lastrpo == tbb->rpo(), ("lastrpo should be updated"));
            continue;
        }
        if (tbb->is_dom(last->getOcc(), tdef->getOcc(), true)) {
            //tdef is near more than 'last', then update 'last'.
            last = tdef;
        }
    }
    return last;
}


//Find killing must-def IR stmt for expression ir.
//Return the IR stmt if found.
//e.g: g is global variable, it is exact.
//x is a pointer that we do not know where it pointed to.
//    1. *x += 1; # *x may overlapped with g
//    2. g = 0; # exactly defined g
//    3. call foo(); # foo may overlapped with g
//    4. return g;
//In the case, the last reference of g in stmt 4 may be defined by
//stmt 1, 2, 3, there is no nearest killing def.
IR * MDSSAMgr::findKillingDefStmt(IR const* ir) const
{
    MDDef * mddef = findKillingMDDef(ir);
    if (mddef != nullptr && !mddef->is_phi()) {
        ASSERT0(mddef->getOcc());
        return mddef->getOcc();
    }
    return nullptr;
}


//Find killing must-def Virtual-DEF for expression ir.
//Return the MDDef if found.
//e.g: g is global variable, it is exact.
//x is a pointer that we do not know where it pointed to.
//    1. *x += 1; # *x may overlapped with g
//    2. g = 0; # exactly defined g
//    3. call foo(); # foo may overlapped with g
//    4. return g;
//In the case, the last reference of g in stmt 4 may be defined by
//stmt 1, 2, 3, there is no nearest killing def.
MDDef * MDSSAMgr::findKillingMDDef(IR const* ir) const
{
    ASSERT0(ir && ir->is_exp() && ir->isMemOpnd());
    MD const* opndmd = ir->getMustRef();
    if (opndmd == nullptr || (!opndmd->is_exact() && !opndmd->is_range())) {
        //TBD: For those exp who do not have MustRef, must they not
        //have killing-def?
        return nullptr;
    }
    MDDef * def = findNearestDef(ir);
    if (def == nullptr || def->is_phi()) { return nullptr; }
    ASSERT0(def->getOcc());
    return xoc::isKillingDef(def->getOcc(), ir, nullptr) ? def : nullptr;
}


static void dumpDef(MDDef const* def, MD const* vopndmd, UseDefMgr const* mgr,
                    Region * rg, xcom::BitSet & visited_def,
                    MOD List<MDDef const*> & wl,
                    MOD IRSet & visited, MOD bool & has_dump_something)
{
    if (def->is_phi()) {
        if (has_dump_something) {
            prt(rg, " ");
        }
        prt(rg, "(mdphi%d)", def->id());
        has_dump_something = true;

        //Collect opnd of PHI to go forward to
        //retrieve corresponding DEFs.
        for (IR const* opnd = MDPHI_opnd_list(def);
             opnd != nullptr; opnd = opnd->get_next()) {
            if (opnd->is_const()) {
                //CONST does not have VMD info.
                continue;
            }

            VMD * opndvmd = ((MDPhi*)def)->getOpndVMD(opnd, mgr);

            //CASE:Do NOT assert VOpnd here.
            //  sometime VOpnd of ID will be NULL before reconstruction.
            //  st x_1 = ...     st x_2 = ...
            //      \           /
            //      x_3 = phi(x_1, x_2)
            //  If some pass removed 'st x_1', the PHI will be
            //       x_3 = phi(--, x_2)
            //  where the first operand is missed, and the illegal PHI will
            //  be recomputed until MDSSA is reconstructed.
            //  The situation will be checked during verifyPhi().
            //  Thus just omit the operand of PHI if it is NULL.
            //ASSERT0(opndvmd);

            if (opndvmd != nullptr && opndvmd->getDef() != nullptr &&
                !visited_def.is_contain(opndvmd->getDef()->id())) {
                //Keep walking previous DEF.
                wl.append_tail(opndvmd->getDef());
            }
        }
        return;
    }

    //def is normal IR stmt.
    ASSERT0(def->getOcc());
    if (!visited.find(def->getOcc())) {
        visited.append(def->getOcc());
        if (has_dump_something) {
            prt(rg, " ");
        }
        prt(rg, "(%s)", DumpIRName().dump(def->getOcc()));
        has_dump_something = true;
    }

    MD const* defmd = def->getOcc()->getMustRef();
    if (defmd != nullptr && defmd->is_exact() && vopndmd->is_exact() &&
        (defmd == vopndmd || defmd->is_exact_cover(vopndmd))) {
        //Stop iteration. def is killing may-def.
        return;
    }
    if (def->getPrev() != nullptr &&
        !visited_def.is_contain(def->getPrev()->id())) {
        //Keep walking previous DEF.
        wl.append_tail(def->getPrev());
    }
}


//The function dump all possible DEF of 'vopnd' by walking through the
//Def Chain.
void MDSSAMgr::dumpDefByWalkDefChain(List<MDDef const*> & wl, IRSet & visited,
                                     VMD const* vopnd) const
{
    if (vopnd->getDef() == nullptr) { return; }
    MD const* vopndmd = m_rg->getMDSystem()->getMD(vopnd->mdid());
    ASSERT0(vopndmd);
    wl.clean();
    wl.append_tail(vopnd->getDef());
    xcom::BitSet visited_def;
    bool has_dump_something = false;
    for (MDDef const* def = wl.remove_head();
         def != nullptr; def = wl.remove_head()) {
        visited_def.bunion(def->id());
        ASSERT0(def->getResult()->mdid() == vopnd->mdid());
        dumpDef(def, vopndmd, const_cast<MDSSAMgr*>(this)->getUseDefMgr(),
                m_rg, visited_def, wl, visited, has_dump_something);
    }
}


bool MDSSAMgr::hasPhiWithAllSameOperand(IRBB const* bb) const
{
    MDPhiList * philist = getPhiList(bb->id());
    if (philist == nullptr) { return true; }
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi);
        IR const* first_opnd = phi->getOpndList();
        for (IR const* opnd = first_opnd->get_next();
             opnd != nullptr; opnd = opnd->get_next()) {
            if (first_opnd->isIREqual(opnd, getIRMgr(), false)) { continue; }
            return false;
        }
    }
    return true;
}


//Return true if exist USE to 'ir'.
//This is a helper function to provid simple query, an example to
//show how to retrieval VOpnd and USE occurences as well.
//ir: stmt
bool MDSSAMgr::hasUse(IR const* ir) const
{
    ASSERT0(ir && ir->is_stmt());
    MDSSAInfo const* info = const_cast<MDSSAMgr*>(this)->getMDSSAInfoIfAny(ir);
    if (info == nullptr || info->isEmptyVOpndSet()) { return false; }
    VOpndSetIter iter = nullptr;
    for (BSIdx i = info->readVOpndSet().get_first(&iter);
         i != BS_UNDEF; i = info->readVOpndSet().get_next(i, &iter)) {
        VOpnd const* vopnd = getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md());
        if (((VMD*)vopnd)->hasUse()) {
            return true;
        }
    }
    return false;
}


bool MDSSAMgr::constructDesignatedRegion(MOD SSARegion & ssarg)
{
    START_TIMER(t, "MDSSA: Construct Designated Region");
    ASSERT0(ssarg.verify());
    xcom::Vertex const* rootv = m_cfg->getVertex(ssarg.getRootBB()->id());
    ASSERT0(rootv);
    xcom::VexTab vextab;
    vextab.add(rootv);
    BBSet const& bbset = ssarg.getBBSet();
    BBSetIter it;
    for (BSIdx i = bbset.get_first(&it);
         i != BS_UNDEF; i = bbset.get_next(i, &it)) {
        vextab.append(i);
    }
    ReconstructMDSSAVF vf(vextab, ssarg.getDomTree(), m_cfg,
                          this, ssarg.getOptCtx(), ssarg.getActMgr());
    ReconstructMDSSA recon(ssarg.getDomTree(), rootv, vf);
    recon.reconstruct();
    ASSERT0(MDSSAMgr::verifyMDSSAInfo(m_rg, *ssarg.getOptCtx()));
    END_TIMER(t, "MDSSA: Construct Designated Region");
    return true;
}


//lst: for local used.
void MDSSAMgr::dumpExpDUChainIter(
    IR const* ir, MOD ConstIRIter & it, OUT bool * parting_line) const
{
    IRSet visited(getSBSMgr()->getSegMgr());
    xcom::List<MDDef const*> wl;
    it.clean();
    xcom::StrBuf tmp(8);
    for (IR const* opnd = xoc::iterInitC(const_cast<IR*>(ir), it);
         opnd != nullptr; opnd = xoc::iterNextC(it)) {
        if (!opnd->isMemRefNonPR() || opnd->is_stmt()) { continue; }
        VOpndSetIter iter = nullptr;
        if (!(*parting_line)) {
            note(getRegion(), "\n%s", g_parting_line_char);
            (*parting_line) = true;
        }
        note(getRegion(), "\n");
        prt(getRegion(), "%s", dumpIRName(opnd, tmp));

        MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(opnd);
        if (mdssainfo == nullptr || mdssainfo->isEmptyVOpndSet()) {
            prt(getRegion(), g_msg_no_mdssainfo);
            continue;
        }
        MDDef * kdef = findKillingMDDef(opnd);
        if (kdef != nullptr) {
            prt(getRegion(), " KDEF:%s", dumpIRName(kdef->getOcc(), tmp));
        }

        //Not found killing def, thus dump total define-chain.
        //Define-chain represents the may-def list.
        m_rg->getLogMgr()->incIndent(2);
        note(getRegion(), "\nDEFSET:");
        visited.clean();
        m_rg->getLogMgr()->incIndent(2);
        for (BSIdx i = mdssainfo->getVOpndSet()->get_first(&iter);
             i != BS_UNDEF; i = mdssainfo->getVOpndSet()->get_next(i, &iter)) {
            VMD * vopnd = (VMD*)m_usedef_mgr.getVOpnd(i);
            ASSERT0(vopnd && vopnd->is_md());
            note(getRegion(), "\nMD%uV%u:", vopnd->mdid(), vopnd->version());
            dumpDefByWalkDefChain(wl, visited, vopnd);
        }
        m_rg->getLogMgr()->decIndent(2);
        m_rg->getLogMgr()->decIndent(2);
    }
}


//Dump all USE.
static void dumpUseSet(VMD const* vmd, Region * rg)
{
    ASSERT0(vmd);
    VMD::UseSetIter vit;
    xcom::StrBuf tmp(8);
    for (BSIdx i = const_cast<VMD*>(vmd)->getUseSet()->get_first(vit);
         !vit.end(); i = const_cast<VMD*>(vmd)->getUseSet()->get_next(vit)) {
        IR const* use = rg->getIR(i);
        ASSERT0(use && (use->isMemRef() || use->is_id()));
        prt(rg, "(%s) ", dumpIRName(use, tmp));
    }
}


void MDSSAMgr::dumpDUChainForStmt(IR const* ir, bool & parting_line) const
{
    ASSERT0(ir->isMemRefNonPR() || ir->isCallStmt());
    Region * rg = getRegion();
    if (!parting_line) {
        note(rg, "\n%s", g_parting_line_char);
        parting_line = true;
    }
    note(rg, "\n");
    prt(rg, "%s", DumpIRName().dump(ir));
    MDSSAMgr * pmgr = const_cast<MDSSAMgr*>(this);
    MDSSAInfo * mdssainfo = pmgr->getMDSSAInfoIfAny(ir);
    if (mdssainfo == nullptr || mdssainfo->isEmptyVOpndSet()) {
        prt(rg, g_msg_no_mdssainfo);
        return;
    }

    rg->getLogMgr()->incIndent(2);
    note(rg, "\nUSE:");

    //Dump VOpnd and the USE List for each VOpnd.
    rg->getLogMgr()->incIndent(2);
    VOpndSetIter iter = nullptr;
    for (BSIdx i = mdssainfo->getVOpndSet()->get_first(&iter);
         i != BS_UNDEF; i = mdssainfo->getVOpndSet()->get_next(i, &iter)) {
        VMD * vopnd = (VMD*)pmgr->getUseDefMgr()->getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md());
        if (vopnd->getDef() != nullptr) {
            ASSERT0(vopnd->getDef()->getOcc() == ir);
        }
        note(rg, "\nMD%uV%u:", vopnd->mdid(), vopnd->version());

        //Dump all USE.
        dumpUseSet(vopnd, rg);
    }
    rg->getLogMgr()->decIndent(2);
    rg->getLogMgr()->decIndent(2);
}


void MDSSAMgr::dumpDUChainForStmt(IR const* ir, MOD ConstIRIter & it) const
{
    ASSERT0(ir->is_stmt());
    dumpIR(ir, getRegion());
    getRegion()->getLogMgr()->incIndent(2);
    bool parting_line = false;
    //Handle stmt.
    if (ir->isMemRefNonPR() || ir->isCallStmt()) {
        dumpDUChainForStmt(ir, parting_line);
    }
    //Handle expression.
    dumpExpDUChainIter(ir, it, &parting_line);
    if (parting_line) {
        note(getRegion(), "\n%s", g_parting_line_char);
        note(getRegion(), "\n");
    }
    getRegion()->getLogMgr()->decIndent(2);
}


void MDSSAMgr::dumpDUChain() const
{
    Region * rg = getRegion();
    if (!rg->isLogMgrInit() || !g_dump_opt.isDumpMDSSAMgr()) { return; }
    note(rg, "\n==-- DUMP MDSSAMgr DU CHAIN '%s' --==\n", rg->getRegionName());
    BBList * bbl = rg->getBBList();
    ConstIRIter it;
    for (IRBB * bb = bbl->get_head(); bb != nullptr; bb = bbl->get_next()) {
        bb->dumpDigest(rg);
        dumpPhiList(getPhiList(bb->id()));
        for (IR * ir = BB_first_ir(bb); ir != nullptr; ir = BB_next_ir(bb)) {
            dumpDUChainForStmt(ir, it);
        }
    }
}


MDSSAInfo * MDSSAMgr::genMDSSAInfoAndNewVesionVMD(IR * ir)
{
    ASSERT0(ir && ir->is_stmt());
    //Note ir already has VOpndSet, keep it unchanged, then generate new one.
    MDSSAInfo * mdssainfo = genMDSSAInfo(ir);
    mdssainfo->cleanVOpndSet(getUseDefMgr());

    //Generate new VMD according to MD reference.
    MD const* ref = ir->getMustRef();
    MDIdx mustmd = MD_UNDEF;
    if (ref != nullptr &&
        !ref->is_pr()) { //ir may be CallStmt, thus its result is PR.
        mustmd = MD_id(ref);
        VMD * vmd = genNewVersionVMD(MD_id(ref));
        ASSERT0(getSBSMgr());
        ASSERT0(vmd->getDef() == nullptr);
        VMD_def(vmd) = genMDDefStmt(ir, vmd);
        mdssainfo->addVOpnd(vmd, getUseDefMgr());
    }
    MDSet const* refset = ir->getMayRef();
    if (refset != nullptr) {
        MDSetIter iter;
        for (BSIdx i = refset->get_first(&iter);
             i != BS_UNDEF; i = refset->get_next((UINT)i, &iter)) {
            if ((MDIdx)i == mustmd) { continue; }
            MD * md = m_md_sys->getMD(i);
            ASSERTN(md && !md->is_pr(), ("PR should not in MaySet"));
            VMD * vmd2 = genNewVersionVMD(MD_id(md));
            ASSERT0(getSBSMgr());
            VMD_def(vmd2) = genMDDefStmt(ir, vmd2);
            mdssainfo->addVOpnd(vmd2, getUseDefMgr());
        }
    }
    return mdssainfo;
}


//Generate MDSSAInfo and generate VOpnd for referrenced MD that both include
//MustRef MD and MayRef MDs.
MDSSAInfo * MDSSAMgr::genMDSSAInfoAndVOpnd(IR * ir, UINT version)
{
    ASSERT0(ir);
    MDSSAInfo * mdssainfo = genMDSSAInfo(ir);
    MD const* ref = ir->getMustRef();
    if (ref != nullptr &&
        !ref->is_pr()) { //ir may be Call stmt, its result is PR.
        VMD const* vmd = genVMD(MD_id(ref), version);
        ASSERT0(getSBSMgr());
        mdssainfo->addVOpnd(vmd, getUseDefMgr());
    }
    MDSet const* refset = ir->getMayRef();
    if (refset != nullptr) {
        MDSetIter iter;
        for (BSIdx i = refset->get_first(&iter);
             i != BS_UNDEF; i = refset->get_next((UINT)i, &iter)) {
            MD * md = m_md_sys->getMD(i);
            ASSERTN(md && !md->is_pr(), ("PR should not in MaySet"));
            VMD const* vmd2 = genVMD(MD_id(md), version);
            ASSERT0(getSBSMgr());
            mdssainfo->addVOpnd(vmd2, getUseDefMgr());
        }
    }
    return mdssainfo;
}


//Return true if all vopnds of 'def' can reach 'exp'.
bool MDSSAMgr::isMustDef(IR const* def, IR const* exp) const
{
    MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(def);
    return mdssainfo != nullptr && mdssainfo->isMustDef(
        const_cast<MDSSAMgr*>(this)->getUseDefMgr(), exp);
}


//Return true if def1 dominates def2.
bool MDSSAMgr::isDom(MDDef const* def1, MDDef const* def2) const
{
    ASSERT0(def1 != def2);
    IRBB const* bb1 = def1->getBB();
    IRBB const* bb2 = def2->getBB();
    if (bb1 != bb2) { return m_cfg->is_dom(bb1->id(), bb2->id()); }
    if (def1->is_phi()) {
        if (def2->is_phi()) {
            //PHIs that are in same BB do not dominate each other.
            return false;
        }
        return true;
    }
    if (def2->is_phi()) { return false; }
    ASSERT0(def1->getOcc() && def2->getOcc());
    return bb1->is_dom(def1->getOcc(), def2->getOcc(), true);
}


//Generate VMD for stmt and its kid expressions that reference memory.
void MDSSAMgr::initVMD(IN IR * ir, OUT DefMDSet & maydef)
{
    ASSERT0(ir->is_stmt());
    if (ir->isMemRefNonPR() ||
        (ir->isCallStmt() && !ir->isReadOnly())) {
        MD const* ref = ir->getMustRef();
        if (ref != nullptr &&
           !ref->is_pr()) { //MustRef of CallStmt may be PR.
            maydef.bunion(MD_id(ref));
        }
        MDSet const* refset = ir->getMayRef();
        if (refset != nullptr) {
            maydef.bunion((DefSBitSet&)*refset);
        }
        genMDSSAInfoAndVOpnd(ir, MDSSA_INIT_VERSION);
    }
    m_iter.clean();
    for (IR * t = xoc::iterExpInit(ir, m_iter);
         t != nullptr; t = xoc::iterExpNext(m_iter)) {
        ASSERT0(t->is_exp());
        if (t->isMemRefNonPR()) {
            genMDSSAInfoAndVOpnd(t, MDSSA_INIT_VERSION);
        }
    }
}


void MDSSAMgr::collectUseSet(
    IR const* def, LI<IRBB> const* li, CollectFlag f, OUT IRSet * useset)
{
    MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(def);
    ASSERT0(mdssainfo);
    CollectCtx ctx(f);
    ctx.setLI(li);
    CollectUse cu(this, mdssainfo, ctx, useset);
}


void MDSSAMgr::collectUseSet(IR const* def, CollectFlag f, OUT IRSet * useset)
{
    MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(def);
    ASSERT0(mdssainfo);
    CollectCtx ctx(f);
    CollectUse cu(this, mdssainfo, ctx, useset);
}


void MDSSAMgr::collectUseMD(IR const* ir, OUT LiveInMDTab & livein_md)
{
    ASSERT0(ir);
    MD const* ref = ir->getMustRef();
    if (ref != nullptr &&
        !ref->is_pr()) { //ir may be Call stmt, its result is PR.
        livein_md.append(ref->id());
    }

    MDSet const* refset = ir->getMayRef();
    if (refset == nullptr) { return; }

    MDSetIter iter;
    for (BSIdx i = refset->get_first(&iter);
         i != BS_UNDEF; i = refset->get_next((UINT)i, &iter)) {
        MD * md = m_md_sys->getMD(i);
        ASSERTN(md && !md->is_pr(), ("PR should not in MayBeSet"));
        livein_md.append(md->id());
    }
}


void MDSSAMgr::computeLiveInMD(IRBB const* bb, OUT LiveInMDTab & livein_md)
{
    livein_md.clean();
    ConstIRIter irit;
    IRBB * pbb = const_cast<IRBB*>(bb);
    for (IR const* ir = BB_last_ir(pbb); ir != nullptr; ir = BB_prev_ir(pbb)) {
        //Handle Def.
        MD const* exact_def = nullptr;
        if (ir->isMemRefNonPR() &&
            (exact_def = ir->getExactRef()) != nullptr) {
            ASSERT0(!exact_def->is_pr());
            LiveInMDTabIter it;
            for (UINT mdid = livein_md.get_first(it);
                 mdid != MD_UNDEF; mdid = livein_md.get_next(it)) {
                MD const* md = m_md_sys->getMD(mdid);
                ASSERT0(md);
                if (exact_def->is_exact_cover(md)) {
                    //ir kills the value of md.
                    livein_md.remove(exact_def->id());
                }
            }
        }
        //Handle Use.
        irit.clean();
        for (IR const* t = iterExpInitC(ir, irit);
             t != nullptr; t = iterExpNextC(irit)) {
            ASSERT0(t->is_exp());
            if (t->isMemRefNonPR()) {
                collectUseMD(t, livein_md);
            }
        }
    }
}


//maydef_md: record MDs that defined in 'bb'.
void MDSSAMgr::collectDefinedMDAndInitVMD(
    IN IRBB * bb, OUT DefMDSet & maydef)
{
    BBIRListIter it;
    for (IR * ir = bb->getIRList().get_head(&it);
         ir != nullptr; ir = bb->getIRList().get_next(&it)) {
        initVMD(ir, maydef);
    }
}


//Insert a new PHI into bb according to given MDIdx.
//Note the operand of PHI will be initialized in initial-version.
MDPhi * MDSSAMgr::insertPhiWithNewVersion(
    UINT mdid, IN IRBB * bb, UINT num_opnd)
{
    MDPhi * phi = insertPhi(mdid, bb, num_opnd);
    VMD * newres = genNewVersionVMD(mdid);
    MDDEF_result(phi) = newres;
    VMD_def(newres) = phi;
    return phi;
}


//Insert a new PHI into bb according to given MDIdx.
//Note the operand of PHI will be initialized in initial-version.
MDPhi * MDSSAMgr::insertPhi(UINT mdid, IN IRBB * bb, UINT num_opnd)
{
    //Here each operand and result of phi set to same type.
    //They will be revised to correct type during renaming.
    MDPhi * phi = genMDPhi(mdid, num_opnd, bb, genInitVersionVMD(mdid));
    m_usedef_mgr.genBBPhiList(bb->id())->append_head(phi);
    return phi;
}


//Insert phi for VMD.
//defbbs: record BBs which defined the VMD identified by 'mdid'.
//visited: record visited BB id
void MDSSAMgr::placePhiForMD(UINT mdid, List<IRBB*> const* defbbs,
                             DfMgr const& dfm, xcom::BitSet & visited,
                             List<IRBB*> & wl, BB2DefMDSet & defmds_vec)
{
    ASSERT0(defbbs && mdid != MD_UNDEF);
    visited.clean();
    wl.clean();
    C<IRBB*> * bbit;
    for (IRBB * defbb = defbbs->get_head(&bbit);
         defbb != nullptr; defbb = defbbs->get_next(&bbit)) {
        wl.append_tail(defbb);
        //visited.bunion(defbb->id());
    }

    while (wl.get_elem_count() != 0) {
        IRBB * bb = wl.remove_head();

        //Each basic block in dfcs is in dominance frontier of 'bb'.
        xcom::BitSet const* dfcs = dfm.getDFControlSet(bb->id());
        if (dfcs == nullptr) { continue; }

        for (BSIdx i = dfcs->get_first(); i != BS_UNDEF;
             i = dfcs->get_next(i)) {
            if (visited.is_contain(i)) {
                //Already insert phi for 'mdid' into BB i.
                //TODO:ensure the phi for same PR does NOT be
                //inserted multiple times.
                continue;
            }

            visited.bunion(i);

            IRBB * ibb = m_cfg->getBB(i);
            ASSERT0(ibb);

            //Redundant phi will be removed during refinePhi().
            insertPhi(mdid, ibb);

            ASSERT0(defmds_vec.get(i));
            defmds_vec.get(i)->bunion(mdid);

            wl.append_tail(ibb);
        }
    }
}


//Return true if phi is redundant, otherwise return false.
//CASE1: if all opnds have same defintion or defined by current phi,
//then phi is redundant.
//common_def: record the common_def if the definition of all opnd is the same.
//TODO: p=phi(m,p), if the only use of p is phi, then phi is redundant.
bool MDSSAMgr::doOpndHaveSameDef(MDPhi const* phi, OUT VMD ** common_def) const
{
    MDDef * def = nullptr;
    bool same_def = true; //indicate all DEF of operands are the same stmt.
    MDDef * liveindef = (MDDef*)(-1);
    for (IR const* opnd = phi->getOpndList();
         opnd != nullptr; opnd = opnd->get_next()) {
        VMD * v = phi->getOpndVMD(opnd, &m_usedef_mgr);
        if (v == nullptr) {
            //VOpnd may have been removed from MDSSAMgr, thus the VOpnd that
            //corresponding to the ID is NULL.
            continue;
        }
        ASSERT0(v->is_md());

        MDDef * vdef = nullptr;
        if (v->getDef() != nullptr) {
            vdef = v->getDef();
        } else {
            //DEF of v is the region live-in MD.
            vdef = liveindef;
        }

        if (def == nullptr) {
            def = vdef;
        } else if (def != vdef && phi != vdef) {
            same_def = false;
            break;
        }
    }
    ASSERT0(common_def);
    *common_def = def == nullptr || def == liveindef ?
                  nullptr : def->getResult();
    return same_def;
}


//Return true if one of phi's operand have valid DEF, otherwise return false.
//CASE1: if all opnds's DEF are invalid, then phi is redundant.
//If opnd of phi do not have any valid DEF, then phi is redundant,
//otherwise phi can NOT be removed even if there are only one
//opnd have valid DEF.
//e.g1:Phi: MD14V2 <- MD14V1, MD14V3
//     MD14V1 is valid, MD14V3 is invalid, then Phi can not be removed.
//e.g2:dce6.c, after DCE, there are two BBs. Both PHI prepended at BB
//     are redundant becase their opnd do not have DEF, and it is invalid.
//  --- BB7 ---
//  --- BB5 ---
//  Phi: MD14V2 <- MD14V0(id:60)(BB7)|UsedBy:
//  Phi: MD13V2 <- MD13V1(id:58)(BB7)|UsedBy:
//  Phi: MD10V4 <- MD10V3(id:56)(BB7)|UsedBy:
//  Phi: MD9V4 <- MD9V3(id:54)(BB7)|UsedBy:
//  Phi: MD7V4 <- MD7V3(id:52)(BB7)|UsedBy:
//  starray (i8, ety:i8) id:32 attachinfo:Dbx,MDSSA
//  return id:47 attachinfo:Dbx
bool MDSSAMgr::doOpndHaveValidDef(MDPhi const* phi) const
{
    if (phi->hasNoOpnd()) { return false; }
    //Indicate if there exist a valid DEF for operands
    bool has_valid_def = false;
    for (IR const* opnd = phi->getOpndList();
         opnd != nullptr; opnd = opnd->get_next()) {
        VMD * v = phi->getOpndVMD(opnd, &m_usedef_mgr);
        if (v == nullptr) { continue; }
        ASSERT0(v->is_md());
        if (v->getDef() != nullptr) {
            has_valid_def = true;
            break;
        }
    }
    return has_valid_def;
}


void MDSSAMgr::recordEffectMD(IRBB const* bb, OUT DefMDSet & effect_md)
{
    LiveInMDTab livein_md;
    computeLiveInMD(bb, livein_md);
    LiveInMDTabIter iter;
    for (UINT mdid = livein_md.get_first(iter);
         mdid != MD_UNDEF; mdid = livein_md.get_next(iter)) {
        effect_md.bunion(mdid);
    }
}


void MDSSAMgr::placePhi(DfMgr const& dfm, OUT DefMDSet & effect_md,
                        DefMiscBitSetMgr & bs_mgr,
                        BB2DefMDSet & defined_md_vec,
                        List<IRBB*> & wl)
{
    START_TIMER(t, "MDSSA: Place phi");

    //Record BBs which modified each MD.
    BBList * bblst = m_rg->getBBList();

    //All objects allocated and recorded in md2defbb are used for local purpose,
    //and will be destoied before leaving this function.
    Vector<List<IRBB*>*> md2defbb(bblst->get_elem_count());
    for (IRBB * bb = bblst->get_head(); bb != nullptr; bb = bblst->get_next()) {
        DefSBitSet * bs = bs_mgr.allocSBitSet();
        defined_md_vec.set(bb->id(), bs);
        collectDefinedMDAndInitVMD(bb, *bs);
        if (m_is_semi_pruned) {
            recordEffectMD(bb, effect_md);
        } else {
            //Record all modified MDs which will be versioned later.
            effect_md.bunion(*bs);
        }

        //Record which BB defined these effect mds.
        DefSBitSetIter cur = nullptr;
        for (BSIdx i = bs->get_first(&cur); i != BS_UNDEF;
             i = bs->get_next(i, &cur)) {
            List<IRBB*> * bbs = md2defbb.get(i);
            if (bbs == nullptr) {
                bbs = new List<IRBB*>();
                md2defbb.set(i, bbs);
            }
            bbs->append_tail(bb);
        }
    }

    //Place phi for lived MD.
    xcom::BitSet visited((bblst->get_elem_count() / BITS_PER_BYTE) + 1);
    DefMDSetIter cur = nullptr;
    for (BSIdx i = effect_md.get_first(&cur);
         i != BS_UNDEF; i = effect_md.get_next(i, &cur)) {
        //effect_md includes MDs that have not been defined. These MDs's
        //defbbs is empty.
        List<IRBB*> const* defbbs = md2defbb.get((MDIdx)i);
        if (defbbs != nullptr) {
            placePhiForMD((MDIdx)i, defbbs, dfm, visited, wl, defined_md_vec);
        }
    }
    END_TIMER(t, "MDSSA: Place phi");

    //Free local used objects.
    for (VecIdx i = 0; i <= md2defbb.get_last_idx(); i++) {
        List<IRBB*> * bbs = md2defbb.get(i);
        if (bbs == nullptr) { continue; }
        delete bbs;
    }
}


//Note call stmt is a specical case in renaming because it regards MayDef
//as MayUse.
void MDSSAMgr::renameUse(IR * ir, MD2VMDStack & md2vmdstk)
{
    ASSERT0(ir);
    ASSERT0(ir->is_exp());
    MDSSAInfo * mdssainfo = genMDSSAInfo(ir);
    ASSERT0(mdssainfo);
    VOpndSetIter iter;
    VOpndSet * set = mdssainfo->getVOpndSet();
    VOpndSet removed;
    VOpndSet added;
    BSIdx next;
    for (BSIdx i = set->get_first(&iter); i != BS_UNDEF; i = next) {
        next = set->get_next(i, &iter);
        VMD * vopnd = (VMD*)m_usedef_mgr.getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md() && vopnd->id() == (UINT)i);

        //Get the top-version on stack.
        VMD * topv = md2vmdstk.get_top(vopnd->mdid());
        if (topv == nullptr) {
            //MD does not have top-version, it has no def,
            //and may be parameter.
            continue;
        }

        //e.g: MD1 = MD2(VMD1)
        //    VMD1 will be renamed to VMD2, so VMD1 will not
        //    be there in current IR any more.

        //Set latest version of VMD be the USE of current opnd.
        if (topv->version() == MDSSA_INIT_VERSION) {
            //Do nothing.
            ASSERT0(vopnd == topv);
        } else if (vopnd != topv) {
            //vopnd may be ver0.
            //Current ir does not refer the old version VMD any more.
            ASSERT0(vopnd->version() == MDSSA_INIT_VERSION ||
                    vopnd->findUse(ir));
            ASSERT0(vopnd->version() == MDSSA_INIT_VERSION || vopnd->getDef());
            ASSERT0(!topv->findUse(ir));

            set->remove(vopnd, *getSBSMgr());
            added.append(topv, *getSBSMgr());
        }

        topv->addUse(ir);
    }

    set->bunion(added, *getSBSMgr());
    added.clean(*getSBSMgr());
}


MDPhi * MDSSAMgr::genMDPhi(MDIdx mdid, IR * opnd_list, IRBB * bb, VMD * result)
{
    MDPhi * phi = m_usedef_mgr.allocMDPhi(mdid);

    //Allocate MDSSAInfo for given operands.
    for (IR * opnd = opnd_list; opnd != nullptr; opnd = opnd->get_next()) {
        ASSERT0(opnd->getMustRef() && opnd->getMustRef()->id() == mdid);
        //Generate MDSSAInfo to ID.
        MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(opnd);
        ASSERT0_DUMMYUSE(mdssainfo && !mdssainfo->readVOpndSet().is_empty());
        ASSERT0(mdssainfo->containSpecificMDOnly(mdid, getUseDefMgr()));
        ID_phi(opnd) = phi; //Record ID's host PHI.
    }
    MDPHI_opnd_list(phi) = opnd_list;
    MDPHI_bb(phi) = bb;
    MDDEF_result(phi) = result;

    //Do NOT set DEF of result here because result's version may be zero.
    //VMD_def(result) = phi;
    return phi;
}


MDPhi * MDSSAMgr::genMDPhi(MDIdx mdid, UINT num_opnd, IRBB * bb, VMD * result)
{
    MDPhi * phi = m_usedef_mgr.allocMDPhi(mdid);
    m_usedef_mgr.buildMDPhiOpnd(phi, mdid, num_opnd);
    MDPHI_bb(phi) = bb;
    MDDEF_result(phi) = result;
    //Do NOT set DEF of result here because result's version may be zero.
    //VMD_def(result) = phi;
    return phi;
}


MDDef * MDSSAMgr::genMDDefStmt(IR * ir, VMD * result)
{
    ASSERT0(ir && ir->is_stmt());
    MDDef * mddef = m_usedef_mgr.allocMDDefStmt();
    MDDEF_result(mddef) = result;
    MDDEF_is_phi(mddef) = false;
    MDDEFSTMT_occ(mddef) = ir;
    return mddef;
}


void MDSSAMgr::renameDef(IR * ir, IRBB * bb, MD2VMDStack & md2vmdstk)
{
    ASSERT0(ir && ir->is_stmt());
    MDSSAInfo * mdssainfo = genMDSSAInfo(ir);
    ASSERT0(mdssainfo);
    VOpndSetIter iter;
    VOpndSet * set = mdssainfo->getVOpndSet();
    VOpndSet added;
    BSIdx next;
    for (BSIdx i = set->get_first(&iter); i != BS_UNDEF; i = next) {
        next = set->get_next(i, &iter);
        VMD * vopnd = (VMD*)m_usedef_mgr.getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md() && vopnd->id() == (UINT)i);
        ASSERTN(vopnd->version() == MDSSA_INIT_VERSION,
                ("should be first meet"));

        //Update versioned MD.
        VMD * newv = genNewVersionVMD(vopnd->mdid());
        VMD * nearestv = md2vmdstk.get_top(vopnd->mdid());
        md2vmdstk.push(vopnd->mdid(), newv);

        MDDef * mddef = genMDDefStmt(ir, newv);
        if (nearestv != nullptr && nearestv->getDef() != nullptr) {
            addDefChain(nearestv->getDef(), mddef);
        }
        VMD_def(newv) = mddef;
        set->remove(vopnd, *getSBSMgr());
        added.append(newv, *getSBSMgr());
    }

    set->bunion(added, *getSBSMgr());
    added.clean(*getSBSMgr());
}


//Cut off the DU chain between 'def' and its predecessors.
void MDSSAMgr::cutoffDefChain(MDDef * def)
{
    MDDef * prev = def->getPrev();
    if (prev != nullptr) {
        ASSERT0(prev->getNextSet() && prev->getNextSet()->find(def));
        prev->getNextSet()->remove(def, *getSBSMgr());
    }
    MDDEF_prev(def) = nullptr;
}


//Return true if VMDs of stmt cross version when moving stmt outside of loop.
bool MDSSAMgr::isCrossLoopHeadPhi(IR const* stmt, LI<IRBB> const* li,
                                  OUT bool & cross_nonphi_def) const
{
    bool cross_loophead_phi = false;
    ASSERT0(stmt->is_stmt());
    MDSSAInfo * info = getMDSSAInfoIfAny(stmt);
    if (info == nullptr || info->isEmptyVOpndSet()) { return false; }

    //Check if one of VMD cross MDPhi in loophead.
    IRBB const* loophead = li->getLoopHead();
    ASSERT0(loophead);
    VOpndSet const& vmdset = info->readVOpndSet();
    UseDefMgr const* udmgr = const_cast<MDSSAMgr*>(this)->getUseDefMgr();
    //stmt will be appended at the tail of tgtbb.
    VOpndSetIter vit = nullptr;
    for (BSIdx i = vmdset.get_first(&vit);
         i != BS_UNDEF; i = vmdset.get_next(i, &vit)) {
        VMD const* t = (VMD*)udmgr->getVOpnd(i);
        ASSERT0(t && t->is_md());
        if (t->getDef() == nullptr) { continue; }

        //Only consider prev-def of stmt.
        MDDef const* prev = t->getDef()->getPrev();
        if (prev == nullptr) { continue; }
        if (!li->isInsideLoop(prev->getBB()->id())) {
            //prev-def is not inside loop.
            continue;
        }
        if (!prev->is_phi()) {
            if (!isOverConservativeDUChain(stmt, prev->getOcc(), m_rg)) {
                //stmt may cross non-phi MDDef.
                cross_nonphi_def = true;
            }
            continue;
        }
        if (prev->getBB() == loophead) {
            cross_loophead_phi = true;
            continue;
        }
    }
    return cross_loophead_phi;
}


static bool verifyDDChainBeforeMerge(MDDef * def1, MOD MDDef * def2)
{
    //CASE1: def4 may be have been inserted between other DefDef chain.
    //      BB11:def1
    //       |
    //       v
    //    __BB14:def4__
    //   |             |
    //   v             v
    //  BB16:def2     BB20:def3
    //When inserting def4 at BB14, need to insert MDDef between def1->def2 and
    //def1->def3.
    if (def1->getPrev() == nullptr || def2->getPrev() == nullptr) {
        return true;
    }

    //CASE2:gcse.c.
    //The new inserted stmt may processed in previous updating,
    //and the related VMD has been inserted into some DD chain. In this
    //situation, caller expect to rebuild DD chain and DU chain for whole
    //DomTree. If it is that, just overwrite the DD chain according current
    //DD relation, while no need to check the relation between def1 and def2.
    MDDef * def2prev = def2->getPrev();
    for (MDDef * p = def1->getPrev(); p != nullptr; p = p->getPrev()) {
        if (p == def2prev) { return true; }
    }
    MDDef * def1prev = def1->getPrev();
    for (MDDef * p = def2->getPrev(); p != nullptr; p = p->getPrev()) {
        if (p == def1prev) { return true; }
    }

    //def1 and def2 are in different DomTree path. Merge the path is risky.
    return false;
}


void MDSSAMgr::insertDefBefore(MDDef * def1, MOD MDDef * def2)
{
    ASSERT0(def1 && def2);
    ASSERT0(isDom(def1, def2));
    MDDef * def2prev = def2->getPrev();
    if (def2prev == def1) {
        //def1 and def2 already be DefDef Chain.
        return;
    }
    ASSERT0(def2prev == nullptr ||
            (def2prev->getNextSet() && def2prev->getNextSet()->find(def2)));
    ASSERT0_DUMMYUSE(verifyDDChainBeforeMerge(def1, def2));
    //def1 and def2 are in same DomTree path.
    if (def2prev != nullptr) {
        def2prev->getNextSet()->remove(def2, *getSBSMgr());
        if (def1->getPrev() == nullptr) {
            def2prev->getNextSet()->append(def1, *getSBSMgr());
            MDDEF_prev(def1) = def2prev;
        }
        if (MDDEF_nextset(def1) == nullptr) {
            MDDEF_nextset(def1) = m_usedef_mgr.allocMDDefSet();
        } else {
            ASSERT0(!MDDEF_nextset(def1)->hasAtLeastOneElemDom(def2, this));
        }
        def1->getNextSet()->append(def2, *getSBSMgr());
        MDDEF_prev(def2) = def1;
        return;
    }
    if (MDDEF_nextset(def1) == nullptr) {
        MDDEF_nextset(def1) = m_usedef_mgr.allocMDDefSet();
    } else {
        ASSERT0(!MDDEF_nextset(def1)->hasAtLeastOneElemDom(def2, this));
    }
    def1->getNextSet()->append(def2, *getSBSMgr());
    MDDEF_prev(def2) = def1;
}


//Add relation to def1->def2 where def1 dominated def2.
void MDSSAMgr::addDefChain(MDDef * def1, MDDef * def2)
{
    ASSERT0(def1 && def2);
    ASSERTN(def2->getPrev() == nullptr,
            ("should cutoff outdated def-relation"));
    if (def1->getNextSet() == nullptr) {
        MDDEF_nextset(def1) = m_usedef_mgr.allocMDDefSet();
    }
    def1->getNextSet()->append(def2, *getSBSMgr());
    MDDEF_prev(def2) = def1;
}


//Rename VMD from current version to the top-version on stack if it exist.
void MDSSAMgr::renamePhiResult(IN IRBB * bb, MD2VMDStack & md2vmdstk)
{
    ASSERT0(bb);
    MDPhiList * philist = getPhiList(bb->id());
    if (philist == nullptr) { return; }

    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi && phi->is_phi() && phi->getBB() == bb);

        //Rename phi result.
        VMD * vopnd = phi->getResult();
        ASSERT0(vopnd && vopnd->is_md());

        //Update versioned MD.
        VMD * newv = genNewVersionVMD(vopnd->mdid());
        md2vmdstk.push(vopnd->mdid(), newv);

        MDDEF_result(phi) = newv;
        cutoffDefChain(phi);

        VMD_def(newv) = phi;
    }
}


//Rename VMD from current version to the top-version on stack if it exist.
void MDSSAMgr::renameBB(IN IRBB * bb, MD2VMDStack & md2vmdstk)
{
    renamePhiResult(bb, md2vmdstk);
    BBIRListIter it;
    for (IR * ir = bb->getIRList().get_head(&it);
         ir != nullptr; ir = bb->getIRList().get_next(&it)) {
        //Rename opnd, not include phi.
        //Walk through rhs expression IR tree to rename memory's VMD.
        m_iter.clean();
        for (IR * opnd = xoc::iterInit(ir, m_iter);
             opnd != nullptr; opnd = xoc::iterNext(m_iter)) {
            if (!opnd->isMemOpnd() || opnd->isReadPR()) {
                continue;
            }

            //In memory SSA, rename the MD even if it is ineffect to
            //keep sound DU chain, e.g:
            //  int bar(int * p, int * q, int * m, int * n)
            //  {
            //    *p = *q + 20; *p define MD2V1
            //    *m = *n - 64; *n use MD2V1
            //    return 0;
            //  }
            renameUse(opnd, md2vmdstk);
        }

        if (!ir->isMemRef() || ir->isWritePR()) { continue; }

        //Rename result.
        renameDef(ir, bb, md2vmdstk);
    }
}


void MDSSAMgr::renamePhiOpndInSuccBB(IRBB * bb, MD2VMDStack & md2vmdstk)
{
    ASSERT0(bb->getVex());
    for (EdgeC const* bbel = bb->getVex()->getOutList();
         bbel != nullptr; bbel = bbel->get_next()) {
        UINT opnd_idx = 0; //the index of corresponding predecessor.
        Vertex const* succv = bbel->getTo();
        EdgeC const* sel;
        for (sel = succv->getInList();
             sel != nullptr; sel = sel->get_next(), opnd_idx++) {
            if (sel->getFromId() == bb->id()) {
                break;
            }
        }
        ASSERTN(sel, ("not found related pred"));
        //Replace opnd of PHI of 'succ' with top SSA version.
        handlePhiInSuccBB(m_cfg->getBB(succv->id()), opnd_idx, md2vmdstk);
    }
}


//Replace opnd of PHI of 'succ' with top SSA version.
void MDSSAMgr::handlePhiInSuccBB(IRBB * succ, UINT opnd_idx,
                                 MD2VMDStack & md2vmdstk)
{
    MDPhiList * philist = getPhiList(succ);
    if (philist == nullptr) { return; }

    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi && phi->is_phi());
        IR * opnd = phi->getOpnd(opnd_idx);
        ASSERT0(opnd && opnd->is_id());
        ASSERT0(opnd->getMustRef());

        VMD * topv = md2vmdstk.get_top(opnd->getMustRef()->id());
        ASSERTN(topv, ("miss def-stmt to operand of phi"));

        MDSSAInfo * opnd_ssainfo = getMDSSAInfoIfAny(opnd);
        ASSERT0(opnd_ssainfo);
        opnd_ssainfo->cleanVOpndSet(getUseDefMgr());
        opnd_ssainfo->addVOpnd(topv, getUseDefMgr());
        topv->addUse(opnd);
    }
}


void MDSSAMgr::handleBBRename(IRBB * bb, DefMDSet const& effect_mds,
                              DefMDSet const& defed_mds,
                              MOD BB2VMDMap & bb2vmdmap,
                              MD2VMDStack & md2vmdstk)
{
    ASSERT0(bb2vmdmap.get(bb->id()) == nullptr);
    MD2VMD * mdid2vmd = bb2vmdmap.gen(bb->id());
    DefMDSetIter it = nullptr;
    for (BSIdx mdid = defed_mds.get_first(&it);
         mdid != BS_UNDEF; mdid = defed_mds.get_next(mdid, &it)) {
        VMD * vmd = md2vmdstk.get_top(mdid);
        ASSERT0(vmd || !effect_mds.is_contain(mdid));
        if (vmd != nullptr) {
            mdid2vmd->set(vmd->mdid(), vmd);
        }
    }
    renameBB(bb, md2vmdstk);
    renamePhiOpndInSuccBB(bb, md2vmdstk);
}


void MDSSAMgr::initVMDStack(BB2DefMDSet const& bb2defmds,
                            OUT MD2VMDStack & md2verstk)
{
    DefMiscBitSetMgr bs_mgr;
    DefMDSet effect_mds(bs_mgr.getSegMgr());
    for (VecIdx i = 0; i < (VecIdx)bb2defmds.get_elem_count(); i++) {
        xcom::DefSBitSet const* defmds = bb2defmds.get(i);
        if (defmds == nullptr) { continue; }
        effect_mds.bunion(*defmds);
    }
    initVMDStack(effect_mds, md2verstk);
}


void MDSSAMgr::initVMDStack(DefMDSet const& defmds,
                            OUT MD2VMDStack & md2verstk)
{
    DefMDSetIter it = nullptr;
    for (BSIdx i = defmds.get_first(&it);
         i != BS_UNDEF; i = defmds.get_next(i, &it)) {
        md2verstk.push(i, genInitVersionVMD((MDIdx)i));
    }
}


//Rename variables.
void MDSSAMgr::rename(DefMDSet const& effect_mds, BB2DefMDSet & bb2defmds,
                      DomTree const& domtree, MD2VMDStack & md2vmdstk)
{
    START_TIMER(t, "MDSSA: Rename");
    BBList * bblst = m_rg->getBBList();
    if (bblst->get_elem_count() == 0) { return; }
    initVMDStack(effect_mds, md2vmdstk);
    ASSERT0(m_cfg->getEntry());
    MDSSAConstructRenameVisitVF vf(effect_mds, bb2defmds, md2vmdstk, this);
    MDSSAConstructRenameVisit rn(domtree, m_cfg->getEntry(), vf);
    rn.visit();
    END_TIMER(t, "MDSSA: Rename");
}


void MDSSAMgr::cleanIRSSAInfo(IRBB * bb)
{
    IRListIter lit;
    IRIter it;
    for (IR * ir = BB_irlist(bb).get_head(&lit);
         ir != nullptr; ir = BB_irlist(bb).get_next(&lit)) {
        it.clean();
        for (IR * k = xoc::iterInit(ir, it);
             k != nullptr; k = xoc::iterNext(it)) {
            if (hasMDSSAInfo(k)) {
                cleanMDSSAInfo(k);
            }
        }
    }
}


void MDSSAMgr::destructBBSSAInfo(IRBB * bb)
{
    cleanIRSSAInfo(bb);
    freeBBPhiList(bb);
}


void MDSSAMgr::destructionInDomTreeOrder(IRBB * root, DomTree & domtree)
{
    xcom::Stack<IRBB*> stk;
    UINT n = m_rg->getBBList()->get_elem_count();
    xcom::BitSet visited(n / BIT_PER_BYTE);
    BB2VMDMap bb2vmdmap(n);
    IRBB * v = nullptr;
    stk.push(root);
    while ((v = stk.get_top()) != nullptr) {
        if (!visited.is_contain(v->id())) {
            visited.bunion(v->id());
            destructBBSSAInfo(v);
        }

        xcom::Vertex const* bbv = domtree.getVertex(v->id());
        ASSERTN(bbv, ("dom tree is invalid."));
        xcom::EdgeC const* c = bbv->getOutList();
        bool all_visited = true;
        while (c != nullptr) {
            xcom::Vertex const* dom_succ = c->getTo();
            if (dom_succ == bbv) { continue; }
            if (!visited.is_contain(dom_succ->id())) {
                ASSERT0(m_cfg->getBB(dom_succ->id()));
                all_visited = false;
                stk.push(m_cfg->getBB(dom_succ->id()));
                break;
            }
            c = c->get_next();
        }
        if (all_visited) {
            stk.pop();
            //Do post-processing while all kids of BB has been processed.
        }
    }
}


//Destruction of MDSSA.
//The function perform SSA destruction via scanning BB in preorder
//traverse dominator tree.
//Return true if inserting copy at the head of fallthrough BB
//of current BB's predessor.
void MDSSAMgr::destruction(DomTree & domtree)
{
    START_TIMER(t, "MDSSA: destruction in dom tree order");
    if (!is_valid()) { return; }
    BBList * bblst = m_rg->getBBList();
    if (bblst->get_elem_count() == 0) { return; }
    ASSERT0(m_cfg->getEntry());
    destructionInDomTreeOrder(m_cfg->getEntry(), domtree);
    set_valid(false);
    END_TIMER(t, "MDSSA: destruction in dom tree order");
}


bool MDSSAMgr::verifyDDChain() const
{
    START_TIMER(tverify, "MDSSA: Verify DefDef Chain");
    MDSSAMgr * pthis = const_cast<MDSSAMgr*>(this);
    MDDefVec const* mddefvec = pthis->getUseDefMgr()->getMDDefVec();
    for (VecIdx i = 0; i <= mddefvec->get_last_idx(); i++) {
        MDDef const* mddef = mddefvec->get(i);
        if (mddef == nullptr) { continue; }

        MDDef const* prev = mddef->getPrev();
        if (prev != nullptr) {
            ASSERT0(prev->getNextSet());
            ASSERT0(prev->getNextSet()->find(mddef));
        }
        if (mddef->is_phi() && ((MDPhi*)mddef)->hasNumOfOpndAtLeast(2)) {
            //Note MDPhi does not have previous DEF, because usually Phi has
            //multiple previous DEFs rather than single DEF.
            //CASE:compile/mdssa_phi_prevdef.c
            //Sometime optimization may form CFG that cause PHI1
            //dominiates PHI2, then PHI1 will be PHI2's previous-DEF.
            //ASSERT0(prev == nullptr);
        }
        //CASE: Be careful that 'prev' should not belong to the NextSet of
        //mddef', otherwise the union operation of prev and mddef's succ DEF
        //will construct a cycle in DefDef chain, which is illegal.
        //e.g: for (i = 0; i < 10; i++) {;}, where i's MD is MD5.
        //  MD5V2 <-- PHI(MD5V--, MD5V3)
        //  MD5V3 <-- MD5V2 + 1
        // If we regard MD5V3 as the common-def, PHI is 'mddef', a cycle
        // will appeared.
        if (prev != nullptr && mddef->getNextSet() != nullptr) {
            ASSERTN(!mddef->getNextSet()->find(prev),
                    ("prev should NOT be the NEXT of mddef"));
        }
    }
    END_TIMER(tverify, "MDSSA: Verify DefDef Chain");
    return true;
}


bool MDSSAMgr::verifyPhiOpndList(MDPhi const* phi, UINT prednum) const
{
    MDSSAMgr * pthis = const_cast<MDSSAMgr*>(this);
    VMD * res = phi->getResult();
    ASSERT0_DUMMYUSE(res->is_md());
    UINT opndnum = 0;
    for (IR const* opnd = phi->getOpndList();
         opnd != nullptr; opnd = opnd->get_next()) {
        opndnum++;
        if (!opnd->is_id()) {
            ASSERT0(opnd->is_const() || opnd->is_lda());
            continue;
        }
        ASSERTN(ID_phi(opnd) == phi, ("opnd is not an operand of phi"));

        //CASE1:Opnd may be ID, CONST or LDA.
        MD const* opnd_md = opnd->getMustRef();
        ASSERT0_DUMMYUSE(opnd_md);
        ASSERTN(MD_id(opnd_md) == res->mdid(), ("mdid of VMD is unmatched"));

        //CASE2:An individual ID can NOT represent multiple versioned MD, thus
        //the VOpnd of ID must be unique.
        MDSSAInfo const* opnd_mdssainfo = getMDSSAInfoIfAny(opnd);
        ASSERT0(opnd_mdssainfo);
        UINT vopndnum = opnd_mdssainfo->readVOpndSet().get_elem_count();
        ASSERT0_DUMMYUSE(vopndnum == 1);

        //CASE3:some pass, e.g:DCE, will remove MDPhi step by step, thus
        //do NOT invoke the function during the removing.
        VMD * opndvmd = ((MDPhi*)phi)->getOpndVMD(opnd, pthis->getUseDefMgr());
        ASSERTN_DUMMYUSE(opndvmd, ("miss VOpnd"));

        //CASE4:Version 0 does not have MDDef.
        //ASSERT0(VMD_version(opndvmd) > 0);
    }
    //CASE5:check the number of phi opnds.
    ASSERTN(opndnum == prednum,
            ("The number of phi operand must same with "
             "the number of BB predecessors."));
    return true;
}


//The function verify the operand and VMD info for MDPhi.
//NOTE: some pass, e.g:DCE, will remove MDPhi step by step, thus
//do NOT invoke the function during the removing.
bool MDSSAMgr::verifyPhi() const
{
    BBList * bblst = m_rg->getBBList();
    List<IRBB*> preds;
    for (IRBB * bb = bblst->get_head(); bb != nullptr;
         bb = bblst->get_next()) {
        m_cfg->get_preds(preds, bb);
        MDPhiList * philist = getPhiList(bb);
        if (philist == nullptr) { continue; }

        UINT prednum = bb->getNumOfPred();
        for (MDPhiListIter it = philist->get_head();
             it != philist->end(); it = philist->get_next(it)) {
            MDPhi * phi = it->val();
            ASSERT0(phi);
            verifyPhiOpndList(phi, prednum);
        }
    }
    return true;
}


void MDSSAMgr::collectDefinedMD(IRBB const* bb, OUT DefMDSet & maydef) const
{
    MDPhiList const* philist = getPhiList(bb);
    if (philist != nullptr) {
        for (MDPhiListIter it = philist->get_head();
             it != philist->end(); it = philist->get_next(it)) {
            ASSERT0(it->val());
            maydef.bunion(it->val()->getResult()->mdid());
        }
    }
    BBIRListIter it;
    for (IR const* ir = const_cast<IRBB*>(bb)->getIRList().get_head(&it);
         ir != nullptr; ir = const_cast<IRBB*>(bb)->getIRList().get_next(&it)) {
        if (ir->isCallReadOnly() || !MDSSAMgr::hasMDSSAInfo(ir)) {
            continue;
        }
        MDSSAInfo const* info = getMDSSAInfoIfAny(ir);
        if (info == nullptr || info->isEmptyVOpndSet()) { continue; }
        VOpndSetIter it;
        VOpndSet const& set = info->readVOpndSet();
        for (BSIdx i = set.get_first(&it); i != BS_UNDEF;
             i = set.get_next(i, &it)) {
            VMD const* vmd = (VMD*)getVOpnd(i);
            ASSERT0(vmd && vmd->is_md());
            ASSERT0(vmd->version() != MDSSA_INIT_VERSION);
            ASSERT0(vmd->getDef());
            maydef.bunion(vmd->mdid());
        }
    }
}


void MDSSAMgr::collectDefinedMDForBBList(MOD DefMiscBitSetMgr & bs_mgr,
                                         OUT BB2DefMDSet & bb2defmds) const
{
    BBList const* bblst = m_rg->getBBList();
    BBListIter it;
    for (IRBB const* bb = bblst->get_head(&it);
         bb != nullptr; bb = bblst->get_next(&it)) {
        DefMDSet * bs = bs_mgr.allocSBitSet();
        bb2defmds.set(bb->id(), bs);
        collectDefinedMD(bb, *bs);
    }
}


static bool verifyVerPhiInSuccBB(IRBB const* succ, UINT opnd_idx,
                                 MD2VMDStack const& md2verstk,
                                 MDSSAMgr const* mgr)
{
    MDPhiList * philist = mgr->getPhiList(succ);
    if (philist == nullptr) { return true; }
    MDSSAMgr * pmgr = const_cast<MDSSAMgr*>(mgr);
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi const* phi = it->val();
        ASSERT0(phi && phi->is_phi());
        IR * opnd = phi->getOpnd(opnd_idx);
        ASSERTN(opnd && opnd->is_id(), ("illegal phi operand"));
        VMD const* curvmd = ((MDPhi*)phi)->getOpndVMD(
            opnd, pmgr->getUseDefMgr());
        ASSERTN(curvmd, ("miss VOpnd"));
        VMD const* topvmd = md2verstk.get_top(curvmd);
        if (curvmd == topvmd) { continue; }
        if (topvmd == nullptr && curvmd->version() == MDSSA_INIT_VERSION) {
            continue;
        }
        ASSERTN(0, ("use invalid VMD version"));
    }
    return true;
}


static bool verifyVerPhiResult(IRBB const* bb, MOD MD2VMDStack & md2verstk,
                               MDSSAMgr const* mgr)
{
    ASSERT0(bb);
    MDPhiList const* philist = mgr->getPhiList(bb);
    if (philist == nullptr) { return true; }
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi && phi->is_phi() && phi->getBB() == bb);
        VMD * resvmd = phi->getResult();
        ASSERT0(resvmd && resvmd->is_md());
        md2verstk.push(resvmd);
    }
    return true;
}


static bool verifyVerUse(IR const* ir, MD2VMDStack const& md2verstk,
                         MDSSAMgr const* mgr)
{
    ASSERT0(ir->is_exp());
    MDSSAInfo const* info = mgr->getMDSSAInfoIfAny(ir);
    ASSERT0(info);
    VOpndSetIter it;
    VOpndSet const& set = info->readVOpndSet();
    for (BSIdx i = set.get_first(&it); i != BS_UNDEF;
         i = set.get_next(i, &it)) {
        VMD const* vmd = (VMD*)mgr->getVOpnd(i);
        ASSERT0(vmd && vmd->is_md() && vmd->id() == (UINT)i);
        if (vmd->version() == MDSSA_INIT_VERSION) { continue; }
        VMD const* topvmd = md2verstk.get_top(vmd);
        ASSERTN_DUMMYUSE(vmd == topvmd, ("use invalid version"));
    }
    return true;
}


static bool verifyVerDef(IR const* ir, MOD MD2VMDStack & md2verstk,
                         MDSSAMgr const* mgr)
{
    ASSERT0(ir->is_stmt());
    MDSSAInfo const* info = mgr->getMDSSAInfoIfAny(ir);
    ASSERT0(info);
    VOpndSetIter it;
    VOpndSet const& set = info->readVOpndSet();
    for (BSIdx i = set.get_first(&it); i != BS_UNDEF;
         i = set.get_next(i, &it)) {
        VMD * vmd = (VMD*)mgr->getVOpnd(i);
        ASSERT0(vmd && vmd->is_md() && vmd->id() == (UINT)i);
        ASSERT0(vmd->version() != MDSSA_INIT_VERSION);

        //Verify DefDef chain.
        VMD const* domprev_vmd = md2verstk.get_top(vmd->mdid());
        if (domprev_vmd != nullptr) {
            MDDef const* exp_prev = domprev_vmd->getDef();
            MDDef const* cur_def = vmd->getDef();
            ASSERT0_DUMMYUSE(exp_prev && cur_def);
            ASSERTN(cur_def->getPrev() == exp_prev, ("illegal prev-def"));
            ASSERTN(exp_prev->isNext(cur_def), ("illegal next-def"));
        }
        md2verstk.push(vmd);
    }
    return true;
}


static bool verifyPhiOpndInSuccBB(IRBB const* bb, MD2VMDStack & md2verstk,
                                  MDSSAMgr const* mgr)
{
    IRCFG * cfg = mgr->getRegion()->getCFG();
    ASSERT0(bb->getVex());
    for (EdgeC const* bbel = bb->getVex()->getOutList();
         bbel != nullptr; bbel = bbel->get_next()) {
        UINT opnd_idx = 0; //the index of corresponding predecessor.
        Vertex const* succv = bbel->getTo();
        EdgeC const* sel;
        for (sel = succv->getInList();
             sel != nullptr; sel = sel->get_next(), opnd_idx++) {
            if (sel->getFromId() == bb->id()) {
                break;
            }
        }
        ASSERTN(sel, ("not found related pred"));
        verifyVerPhiInSuccBB(cfg->getBB(succv->id()), opnd_idx, md2verstk, mgr);
    }
    return true;
}


static bool verifyVerBB(IRBB const* bb, MD2VMDStack & md2verstk,
                        MDSSAMgr const* mgr)
{
    verifyVerPhiResult(bb, md2verstk, mgr);
    ConstIRIter irit;
    for (IR const* ir = BB_first_ir(const_cast<IRBB*>(bb));
         ir != nullptr; ir = BB_next_ir(const_cast<IRBB*>(bb))) {
        //Rename opnd, not include phi.
        //Walk through rhs expression IR tree to rename memory's VMD.
        irit.clean();
        for (IR const* opnd = xoc::iterInitC(ir, irit);
             opnd != nullptr; opnd = xoc::iterNextC(irit)) {
            if (!opnd->isMemOpnd() || opnd->isReadPR()) {
                continue;
            }
            verifyVerUse(opnd, md2verstk, mgr);
        }
        if (ir->isCallStmt() || ir->isMemRefNonPR()) {
            //Rename result.
            verifyVerDef(ir, md2verstk, mgr);
        }
    }
    verifyPhiOpndInSuccBB(bb, md2verstk, mgr);
    return true;
}


//Record the top version before enter into BB.
static void recordTopVer(IRBB const* bb, DefMDSet const* defed_mds,
                         MD2VMDStack const& md2verstk,
                         MOD BB2VMDMap & bb2vmd)
{
    ASSERT0(bb2vmd.get(bb->id()) == nullptr);
    MD2VMD * mdid2vmd = bb2vmd.gen(bb->id());
    DefMDSetIter it = nullptr;
    for (BSIdx mdid = defed_mds->get_first(&it);
         mdid != BS_UNDEF; mdid = defed_mds->get_next(mdid, &it)) {
        VMD * vmd = md2verstk.get_top((MDIdx)mdid);
        if (vmd != nullptr) {
            mdid2vmd->set(vmd->mdid(), vmd);
        }
    }
}


static bool verifyVersionImpl(DomTree const& domtree, MDSSAMgr const* mgr)
{
    DefMiscBitSetMgr bs_mgr;
    BB2DefMDSet bb2defmds;
    mgr->collectDefinedMDForBBList(bs_mgr, bb2defmds);
    IRCFG const* cfg = mgr->getRegion()->getCFG();
    IRBB const* root = cfg->getEntry();
    ASSERT0(root);
    xcom::Stack<IRBB const*> stk;
    UINT n = mgr->getRegion()->getBBList()->get_elem_count();
    xcom::BitSet visited(n / BIT_PER_BYTE);
    BB2VMDMap bb2vmd(n);
    IRBB const* bb = nullptr;
    stk.push(root);
    MD2VMDStack md2verstk;

    //The initial-version of each MDs has been created already.
    //The call-site here can guarantee that no new initial-version of MD
    //generated.
    //CASE:To speedup verify, there is no need to push init-version of each MD
    //into stack. The init-version of IR is corresponding to a empty slot in
    //stack.
    //const_cast<MDSSAMgr*>(mgr)->initVMDStack(bb2defmds, md2verstk);
    while ((bb = stk.get_top()) != nullptr) {
        if (!visited.is_contain(bb->id())) {
            visited.bunion(bb->id());
            DefMDSet const* mds = bb2defmds.get(bb->id());
            ASSERT0(mds);
            recordTopVer(bb, mds, md2verstk, bb2vmd);
            verifyVerBB(bb, md2verstk, mgr);
        }
        xcom::Vertex const* bbv = domtree.getVertex(bb->id());
        bool all_visited = true;
        for (xcom::EdgeC const* c = bbv->getOutList();
             c != nullptr; c = c->get_next()) {
            xcom::Vertex const* dom_succ = c->getTo();
            if (dom_succ == bbv) { continue; }
            if (!visited.is_contain(dom_succ->id())) {
                ASSERT0(cfg->getBB(dom_succ->id()));
                all_visited = false;
                stk.push(cfg->getBB(dom_succ->id()));
                break;
            }
        }
        if (all_visited) {
            stk.pop();

            //Do post-processing while all kids of BB has been processed.
            MD2VMD * mdid2vmd = bb2vmd.get(bb->id());
            ASSERT0(mdid2vmd);
            xcom::DefSBitSet const* defmds = bb2defmds.get(bb->id());
            ASSERT0(defmds);
            DefSBitSetIter it = nullptr;
            for (BSIdx i = defmds->get_first(&it);
                 i != BS_UNDEF; i = defmds->get_next(i, &it)) {
                VMDStack * verstk = md2verstk.get(i);
                ASSERT0(verstk && verstk->get_bottom());
                VMD const* vmd = mdid2vmd->get(i);
                while (verstk->get_top() != vmd) {
                    verstk->pop();
                }
            }

            //vmdmap is useless from now on.
            bb2vmd.erase(bb->id());
        }
    }
    return true;
}


//Note the verification is relatively slow.
bool MDSSAMgr::verifyVersion(OptCtx const& oc) const
{
    //Extract dominate tree of CFG.
    START_TIMER(t, "MDSSA: verifyVersion");
    ASSERT0(oc.is_dom_valid());
    DomTree domtree;
    m_cfg->genDomTree(domtree);
    if (!verifyVersionImpl(domtree, this)) {
        return false;
    }
    END_TIMER(t, "MDSSA: verifyVersion");
    return true;
}


bool MDSSAMgr::verifyVMD(VMD const* vmd, BitSet * defset) const
{
    ASSERT0(vmd);
    if (!vmd->is_md()) { return true; }
    MDDef * def = vmd->getDef();
    if (def == nullptr) {
        //ver0 used to indicate the Region live-in MD.
        //It may be parameter or outer region MD.
        ASSERTN(vmd->version() == MDSSA_INIT_VERSION,
                ("Nondef vp's version must be MDSSA_INIT_VERSION"));
    } else {
        ASSERTN(def->getResult() == vmd, ("def is not the DEF of v"));
        ASSERTN(vmd->version() != MDSSA_INIT_VERSION,
                ("version can not be MDSSA_INIT_VERSION"));
        if (defset != nullptr) {
            //Only the first verify after construction need to this costly
            //check.
            ASSERTN(!defset->is_contain(def->id()),
                    ("DEF for each md+version must be unique."));
            defset->bunion(def->id());
        }
    }

    VMD * res = nullptr;
    if (def != nullptr) {
        res = def->getResult();
        ASSERT0(res);
        verifyDef(def, vmd);
    }

    if (res != nullptr) {
        verifyUseSet(res);
    }
    return true;
}


static void verifyIRVMD(IR const* ir, MDSSAMgr const* mgr,
                        TTab<VMD const*> & visited)
{
    ConstIRIter irit;
    for (IR const* t = iterInitC(ir, irit);
         t != nullptr; t = iterNextC(irit)) {
        if (!t->isMemRefNonPR()) { continue; }
        MDSSAInfo * ssainfo = mgr->getMDSSAInfoIfAny(t);
        ASSERT0(ssainfo);
        VOpndSet const& vopndset = ssainfo->readVOpndSet();
        VOpndSetIter it = nullptr;
        for (BSIdx i = vopndset.get_first(&it);
             i != BS_UNDEF; i = vopndset.get_next(i, &it)) {
            VMD const* t = (VMD const*)mgr->getVOpnd(i);
            ASSERT0(t && t->is_md());
            if (visited.find(t)) { continue; }
            mgr->verifyVMD(t);
            visited.append(t);
        }
    }
}


static void verifyPhiListVMD(MDPhiList const* philist, MDSSAMgr const* mgr,
                             TTab<VMD const*> & visited)
{
    if (philist == nullptr) { return; }
    //Record the result MD idx of PHI to avoid multidefining same MD by PHI.
    TTab<MDIdx> phi_res;
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi const* phi = it->val();
        ASSERT0(phi && phi->is_phi());
        VMD const* vmd = phi->getResult();

        //Check if there is multidefinition of same MD by PHI.
        ASSERTN(!phi_res.find(vmd->mdid()), ("multiple define same MD"));
        phi_res.append(vmd->mdid());

        if (!visited.find(vmd)) {
            visited.append(vmd);
            mgr->verifyVMD(vmd, nullptr);
        }
        for (IR * opnd = phi->getOpndList();
             opnd != nullptr; opnd = opnd->get_next()) {
            verifyIRVMD(opnd, mgr, visited);
        }
    }
}


bool MDSSAMgr::verifyRefedVMD() const
{
    TTab<VMD const*> visited;
    BBList * bbl = m_rg->getBBList();
    for (IRBB * bb = bbl->get_head(); bb != nullptr; bb = bbl->get_next()) {
        verifyPhiListVMD(getPhiList(bb), this, visited);
        for (IR * ir = BB_first_ir(bb); ir != nullptr; ir = BB_next_ir(bb)) {
            verifyIRVMD(ir, this, visited);
        }
    }
    return true;
}


bool MDSSAMgr::verifyAllVMD() const
{
    START_TIMER(tverify, "MDSSA: Verify VMD After Pass");

    MDSSAMgr * pthis = const_cast<MDSSAMgr*>(this);
    //Check version for each VMD.
    xcom::BitSet defset;
    VOpndVec * vec = pthis->getUseDefMgr()->getVOpndVec();
    for (VecIdx i = 1; i <= vec->get_last_idx(); i++) {
        VMD * v = (VMD*)vec->get(i);
        if (v == nullptr) {
            //VMD may have been removed.
            continue;
        }
        verifyVMD(v, &defset);
    }
    END_TIMER(tverify, "MDSSA: Verify VMD After Pass");
    return true;
}


void MDSSAMgr::verifyDef(MDDef const* def, VMD const* vopnd) const
{
    ASSERT0(def && def->getResult());
    if (def->is_phi()) {
        //TODO: verify PHI.
        return;
    }
    IR const* stmt = def->getOcc();
    ASSERT0(stmt);
    if (stmt->is_undef()) {
        //The stmt may have been removed, and the VMD is obsoleted.
        //If the stmt removed, its UseSet should be empty, otherwise there is
        //an illegal missing-DEF error.
        ASSERT0(!vopnd->hasUse());
    } else {
        MDSSAInfo const* ssainfo = getMDSSAInfoIfAny(stmt);
        ASSERT0_DUMMYUSE(ssainfo);
        VMD const* res = def->getResult();
        ASSERT0_DUMMYUSE(res);
        ASSERT0(ssainfo->readVOpndSet().is_contain(res->id()));
    }

    bool findref = false;
    if (stmt->getMustRef() != nullptr &&
        stmt->getMustRef()->id() == vopnd->mdid()) {
        findref = true;
    }
    if (stmt->getMayRef() != nullptr &&
        stmt->getMayRef()->is_contain_pure(vopnd->mdid())) {
        findref = true;
    }

    //The number of VMD may be larger than the number of MD.
    //Do NOT ASSERT if not found reference.
    //Some transformation, such as IR Refinement, may change
    //the MDSet contents. This might lead to the inaccurate and
    //redundant memory dependence. But the correctness of
    //dependence is garanteed.
    //e.g:
    //ist:*<4> id:18 //:MD11, MD12, MD14, MD15
    //    lda: *<4> 'r'
    //    ild: i32  //MMD13: MD16
    //        ld: *<4> 'q' //MMD18
    //=> after IR combination: ist(lda) => st
    //st:*<4> 'r' //MMD12
    //    ild : i32 //MMD13 : MD16
    //        ld : *<4> 'q'    //MMD18
    //ist changed to st. The reference MDSet changed to single MD as well.
    //ASSERT0_DUMMYUSE(findref);
    DUMMYUSE(findref);
}


static bool verify_dominance(IRBB const* defbb, IR const* use, IRCFG * cfg)
{
    if (use->is_id()) {
        //If stmt's USE is ID, the USE is placed in PHI, which may NOT be
        //dominated by stmt.
        return true;
    }
    //DEF should dominate all USEs.
    ASSERT0(defbb == MDSSAMgr::getExpBB(use) ||
            cfg->is_dom(defbb->id(), MDSSAMgr::getExpBB(use)->id()));
    return true;
}


//Check SSA uses.
void MDSSAMgr::verifyUseSet(VMD const* vopnd) const
{
    //Check if USE of vopnd references the correct MD/MDSet.
    VMD::UseSetIter iter2;
    IRBB const* defbb = vopnd->getDef() != nullptr ?
        vopnd->getDef()->getBB() : nullptr;
    for (UINT j = const_cast<VMD*>(vopnd)->getUseSet()->get_first(iter2);
         !iter2.end();
         j = const_cast<VMD*>(vopnd)->getUseSet()->get_next(iter2)) {
        IR const* use = (IR*)m_rg->getIR(j);
        ASSERT0(use);
        ASSERT0(use->isMemRef() || use->is_id());
        bool findref = false;
        if (use->getMustRef() != nullptr &&
            use->getMustRef()->id() == vopnd->mdid()) {
            findref = true;
        }
        if (use->getMayRef() != nullptr &&
            use->getMayRef()->is_contain_pure(vopnd->mdid())) {
            findref = true;
        }

        //The number of VMD may be larger than the number of MD.
        //Do NOT ASSERT if not found reference MD at USE point which
        //should correspond to vopnd->md().
        //Some transformation, such as IR Refinement, may change
        //the USE's MDSet. This might lead to the inaccurate and
        //redundant MDSSA DU Chain. So the MDSSA DU Chain is conservative,
        //but the correctness of MDSSA dependence is garanteed.
        //e.g:
        //  ist:*<4> id:18 //:MD11, MD12, MD14, MD15
        //    lda: *<4> 'r'
        //    ild: i32 //MMD13: MD16
        //      ld: *<4> 'q' //MMD18
        //=> After IR combination: ist(lda) transformed to st
        //  st:*<4> 'r' //MMD12
        //    ild : i32 //MMD13 : MD16
        //      ld : *<4> 'q'    //MMD18
        //ist transformed to st. This reduce referenced MDSet to a single MD
        //as well.
        //ASSERT0_DUMMYUSE(findref);
        DUMMYUSE(findref);

        //VOpndSet of each USE should contain vopnd.
        MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(use);
        ASSERT0_DUMMYUSE(mdssainfo);
        ASSERT0(mdssainfo->getVOpndSet()->find(vopnd));
        ASSERT0_DUMMYUSE(verify_dominance(defbb, use, m_cfg));
    }
}


void MDSSAMgr::verifyMDSSAInfoForIR(IR const* ir) const
{
    ASSERT0(ir);
    MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(ir);
    ASSERT0(mdssainfo);
    VOpndSetIter iter = nullptr;
    VOpndSet * set = mdssainfo->getVOpndSet();
    for (BSIdx i = set->get_first(&iter); i != BS_UNDEF;
         i = set->get_next(i, &iter)) {
        VMD * vopnd = (VMD*)m_usedef_mgr.getVOpnd(i);
        ASSERTN(vopnd, ("vopnd may have been removed, "
                        "VOpndSet have not been updated in time"));
        ASSERT0(vopnd && vopnd->is_md());
        MDDef * def = vopnd->getDef();
        if (ir->is_stmt()) {
            ASSERTN(vopnd->version() != MDSSA_INIT_VERSION,
                    ("not yet perform renaming"));
            ASSERTN(def && def->getOcc() == ir, ("IR stmt should have MDDef"));
            ASSERT0(def->is_valid());
            continue;
        }

        //ir is expression.
        if (def != nullptr) {
            ASSERT0(vopnd->findUse(ir));
        } else {
            //The DEF of vopnd is NULL, it should be initial version of MD.
            ASSERT0(vopnd->version() == MDSSA_INIT_VERSION);
        }
        //VMD's Def and UseSet verification will processed in verifyVMD().
    }
}


bool MDSSAMgr::verifyMDSSAInfoUniqueness() const
{
    xcom::TMap<MDSSAInfo const*, IR const*> ir2mdssainfo;
    for (VecIdx i = 0; i <= m_rg->getIRVec().get_last_idx(); i++) {
        IR const* ir = m_rg->getIR(i);
        if (ir == nullptr) { continue; }
        MDSSAInfo const* mdssainfo = getMDSSAInfoIfAny(ir);
        if (mdssainfo != nullptr) {
            ir2mdssainfo.set(mdssainfo, ir);
        }
    }
    return true;
}


bool MDSSAMgr::verifyDUChainAndOccForPhi(MDPhi const* phi) const
{
    for (IR const* opnd = phi->getOpndList();
         opnd != nullptr; opnd = opnd->get_next()) {
        if (!opnd->is_id()) {
            ASSERT0(opnd->is_const() || opnd->is_lda());
            continue;
        }
        MD const* opnd_md = opnd->getMustRef();
        ASSERT0_DUMMYUSE(opnd_md);
        ASSERTN(opnd_md->id() == phi->getResult()->mdid(), ("MD not matched"));
        verifyMDSSAInfoForIR(opnd);
    }
    return true;
}


bool MDSSAMgr::verifyDUChainAndOcc() const
{
    MDSSAMgr * pthis = const_cast<MDSSAMgr*>(this);
    //Check version for each VMD.
    BBList * bbl = m_rg->getBBList();
    BBListIter ct = nullptr;
    for (bbl->get_head(&ct); ct != bbl->end(); ct = bbl->get_next(ct)) {
        IRBB * bb = ct->val();
        //Verify PHI list.
        MDPhiList * philist = pthis->getPhiList(bb);
        if (philist != nullptr) {
            for (MDPhiListIter it = philist->get_head();
                 it != philist->end(); it = philist->get_next(it)) {
                MDPhi const* phi = it->val();
                ASSERT0(phi && phi->is_phi() && phi->getResult());
                verifyDUChainAndOccForPhi(phi);
            }
        }

        IRListIter ctir = nullptr;
        for (BB_irlist(bb).get_head(&ctir);
             ctir != BB_irlist(bb).end();
             ctir = BB_irlist(bb).get_next(ctir)) {
            IR * ir = ctir->val();
            pthis->m_iter.clean();
            for (IR const* x = xoc::iterInit(ir, pthis->m_iter);
                 x != nullptr; x = xoc::iterNext(pthis->m_iter)) {
                if (!x->isMemRefNonPR()) { continue; }
                verifyMDSSAInfoForIR(x);
            }
        }
    }
    return true;
}


//The verification check the DU info in SSA form.
//Current IR must be in SSA form.
bool MDSSAMgr::verify() const
{
    START_TIMER(tverify, "MDSSA: Verify After Pass");
    ASSERT0(verifyDDChain());

    //No need to verify all VMDs because after optimizations, there are some
    //expired VMDs that no one use them, and not maintained. Just verify VMD
    //that referenced by IR.
    //ASSERT0(verifyAllVMD());
    ASSERT0(verifyRefedVMD());
    ASSERT0(verifyDUChainAndOcc());
    ASSERT0(verifyMDSSAInfoUniqueness());
    END_TIMER(tverify, "MDSSA: Verify After Pass");
    return true;
}


void MDSSAMgr::changeVMD(VMD * oldvmd, VMD * newvmd)
{
    ASSERT0(oldvmd && newvmd && oldvmd->is_md() && newvmd->is_md());
    ASSERT0(oldvmd != newvmd);
    VMD::UseSet * oldus = oldvmd->getUseSet();
    VMD::UseSet * newus = newvmd->getUseSet();
    VMD::UseSetIter it;
    for (UINT j = oldus->get_first(it); !it.end(); j = oldus->get_next(it)) {
        IR * use = m_rg->getIR(j);
        MDSSAInfo * usessainfo = getMDSSAInfoIfAny(use);
        ASSERT0(usessainfo);
        usessainfo->removeVOpnd(oldvmd, getUseDefMgr());
        usessainfo->addVOpnd(newvmd, getUseDefMgr());
        newus->append(j);
    }
    oldvmd->cleanUseSet();
}


//DU chain operation.
//Change Def stmt from orginal MDDef to 'newdef'.
//oldvmd: original VMD.
//newdef: target MDDef.
//e.g: olddef->{USE1,USE2} change to newdef->{USE1,USE2}.
void MDSSAMgr::changeDef(VMD * oldvmd, MDDef * newdef)
{
    ASSERT0(oldvmd && newdef);
    ASSERT0(oldvmd->getDef() != newdef);
    VMD_def(oldvmd) = newdef;
}


//DU chain operation.
//Change Def stmt from 'olddef' to 'newdef'.
//olddef: original stmt.
//newdef: target stmt.
//e.g: olddef->{USE1,USE2} change to newdef->{USE1,USE2}.
void MDSSAMgr::changeDef(IR * olddef, IR * newdef)
{
    ASSERT0(olddef && newdef && olddef->is_stmt() && newdef->is_stmt());
    ASSERT0(olddef != newdef);
    ASSERT0(olddef->isMemRefNonPR() && newdef->isMemRefNonPR());
    MDSSAInfo * oldmdssainfo = getMDSSAInfoIfAny(olddef);
    ASSERT0(oldmdssainfo);
    VOpndSetIter it = nullptr;
    VOpndSet const& vopndset = oldmdssainfo->readVOpndSet();
    for (BSIdx i = vopndset.get_first(&it);
         i != BS_UNDEF; i = vopndset.get_next(i, &it)) {
        VMD * t = (VMD*)getVOpnd(i);
        ASSERT0(t && t->is_md());
        MDDef * def = t->getDef();
        ASSERT0(def);
        ASSERT0(t->version() != MDSSA_INIT_VERSION);
        ASSERT0(def->getOcc() == olddef);
        MDDEFSTMT_occ(def) = newdef;
    }

    MDSSAInfo * newmdssainfo = getMDSSAInfoIfAny(newdef);
    if (newmdssainfo == nullptr || newmdssainfo->isEmptyVOpndSet()) {
        copyMDSSAInfo(newdef, olddef);
        return;
    }
    //newdef may have different VOpnd than olddef. Thus appending VOpnd of old
    //to newdef.
    newmdssainfo->addUseSet(oldmdssainfo, getUseDefMgr());
}


//Generate MDSSAInfo and generate VMD for referrenced MD that both include
//The function will generate MDSSAInfo for 'exp' according to the refinfo.
//that defined inside li. The new info for 'exp' will be VMD that defined
//outside of li or the initial version of VMD.
void MDSSAMgr::genMDSSAInfoToOutsideLoopDef(IR * exp,
                                            MDSSAInfo const* refinfo,
                                            LI<IRBB> const* li)
{
    MDSSAInfo * info = genMDSSAInfo(exp);
    ASSERT0(info);
    VOpndSetIter vit = nullptr;
    List<VMD*> newvmds;
    for (BSIdx i = refinfo->readVOpndSet().get_first(&vit);
         i != BS_UNDEF; i = refinfo->readVOpndSet().get_next(i, &vit)) {
        VMD * vmd = (VMD*)getVOpnd(i);
        ASSERT0(vmd->is_md());
        vmd->removeUse(exp);
        MDDef const* newdef = nullptr;
        if (vmd->getDef() == nullptr) {
            ASSERT0(vmd->version() == MDSSA_INIT_VERSION);
        } else if (vmd->getDef()->is_phi()) {
            newdef = findUniqueOutsideLoopDef(vmd->getDef(), li);
        } else {
            for (newdef = vmd->getDef(); newdef != nullptr &&
                 li->isInsideLoop(newdef->getBB()->id());
                 newdef = newdef->getPrev()) {
            }
        }
        if (newdef != nullptr) {
            newvmds.append_tail(newdef->getResult());
        } else {
            //Need add init-version VMD to represent the existence of MD.
            newvmds.append_tail(genInitVersionVMD(vmd->mdid()));
        }
    }
    info->cleanVOpndSet(getUseDefMgr());
    for (VMD * v = newvmds.get_head();
         v != nullptr; v = newvmds.get_next()) {
        if (v->version() != MDSSA_INIT_VERSION) {
            v->addUse(exp);
        }
        info->addVOpnd(v, getUseDefMgr());
    }
}


void MDSSAMgr::changeDefToOutsideLoopDefForTree(IR * exp, LI<IRBB> const* li)
{
    IRIter it;
    for (IR * x = iterInit(exp, it); x != nullptr; x = iterNext(it)) {
        if (x->isMemRefNonPR()) {
            changeDefToOutsideLoopDef(x, li);
        }
    }
}


void MDSSAMgr::changeDefToOutsideLoopDef(IR * exp, LI<IRBB> const* li)
{
    ASSERT0(exp->is_exp() && exp->isMemRefNonPR());
    MDSSAInfo * info = getMDSSAInfoIfAny(exp);
    ASSERT0(info && !info->readVOpndSet().is_empty());
    genMDSSAInfoToOutsideLoopDef(exp, info, li);
}


void MDSSAMgr::findAndSetLiveInDefForTree(
    IR * exp, IR const* startir, IRBB const* startbb, OptCtx const& oc,
    OUT MDSSAStatus & st)
{
    IRIter it;
    for (IR * x = iterInit(exp, it,
            false); //Do NOT iter sibling of root IR of 'exp'
         x != nullptr; x = iterNext(it, true)) {
        if (!x->isMemRefNonPR()) { continue; }
        findAndSetLiveInDef(x, startir, startbb, oc, st);
    }
}


//Note DOM info must be available.
void MDSSAMgr::findAndSetLiveInDef(
    MOD IR * exp, IR const* startir, IRBB const* startbb, OptCtx const& oc,
    OUT MDSSAStatus & st)
{
    ASSERT0(startir == nullptr ||
            (startir->is_stmt() && startir->getBB() == startbb));
    ASSERT0(exp && exp->is_exp() && exp->isMemRefNonPR());
    MDSSAInfo * info = genMDSSAInfo(exp);
    ASSERT0(info);
    List<VMD*> newvmds;
    MD const* must = exp->getMustRef();
    if (must != nullptr) {
        VMD * newvmd = findDomLiveInDefFrom(
            must->id(), startir, startbb, oc, st);
        if (newvmd != nullptr) {
            newvmds.append_tail(newvmd);
        } else {
            //Need append init-version VMD to represent the existence of MD.
            newvmds.append_tail(genInitVersionVMD(must->id()));
        }
    }
    MDSet const* may = exp->getMayRef();
    if (may != nullptr) {
        MDSetIter it;
        for (BSIdx i = may->get_first(&it); i != BS_UNDEF;
             i = may->get_next(i, &it)) {
            VMD * newvmd = findDomLiveInDefFrom(i, startir, startbb, oc, st);
            if (newvmd != nullptr) {
                newvmds.append_tail(newvmd);
                continue;
            }
            //Need append init-version VMD to represent the existence of MD.
            newvmds.append_tail(genInitVersionVMD(i));
        }
    }
    removeExpFromAllVOpnd(exp);
    info->cleanVOpndSet(getUseDefMgr());
    for (VMD * v = newvmds.get_head();
         v != nullptr; v = newvmds.get_next()) {
        if (v->version() != MDSSA_INIT_VERSION) {
            v->addUse(exp);
        }
        info->addVOpnd(v, getUseDefMgr());
    }
}


//DU chain operation.
//Change Use expression from 'olduse' to 'newuse'.
//olduse: single source expression.
//newuse: single target expression.
//e.g: Change MDSSA DU chain DEF->olduse to DEF->newuse.
void MDSSAMgr::changeUse(IR * olduse, IR * newuse)
{
    ASSERT0(olduse && newuse && olduse->is_exp() && newuse->is_exp());
    ASSERTN(olduse != newuse, ("redundant operation"));
    ASSERT0(olduse->isMemRefNonPR() && newuse->isMemRefNonPR());
    MDSSAInfo * oldinfo = getMDSSAInfoIfAny(olduse);
    ASSERT0(oldinfo);
    MDSSAInfo * newinfo = copyAndAddMDSSAOcc(newuse, oldinfo);
    ASSERT0_DUMMYUSE(newinfo);
    removeExpFromAllVOpnd(olduse);
}


//DU chain operation.
//Change Use expression from 'olduse' to 'newuse'.
//olduse: source expression as tree root.
//newuse: target expression as tree root.
//e.g: Change MDSSA DU chain DEF->olduse to DEF->newuse.
void MDSSAMgr::changeUseForTree(
    IR * olduse, IR * newuse, MDSSAUpdateCtx const& ctx)
{
    ASSERT0(olduse && newuse && olduse->is_exp() && newuse->is_exp());
    ASSERTN(olduse != newuse, ("redundant operation"));
    ASSERT0(olduse->isMemRefNonPR() && newuse->isMemRefNonPR());
    addMDSSAOccForTree(newuse, olduse);
    removeMDSSAOccForTree(olduse, ctx);
}


void MDSSAMgr::coalesceDUChain(IR const* src, IR const* tgt)
{
    ASSERT0(src && tgt);
    ASSERT0(src->is_stmt() && tgt->is_exp() && tgt->getStmt() == src);
    MDSSAInfo * src_mdssainfo = getMDSSAInfoIfAny(src);
    MDSSAInfo * tgt_mdssainfo = getMDSSAInfoIfAny(tgt);
    ASSERT0(src_mdssainfo && tgt_mdssainfo);
    VOpndSetIter iter1 = nullptr;
    for (BSIdx i = src_mdssainfo->getVOpndSet()->get_first(&iter1);
         i != BS_UNDEF; i = src_mdssainfo->getVOpndSet()->get_next(i, &iter1)) {
        VMD * src_vopnd = (VMD*)getUseDefMgr()->getVOpnd(i);
        ASSERT0(src_vopnd && src_vopnd->is_md());

        //Find the MD in tgt's vopnd-set which has same mdid with src's
        //except the distinct version.
        //e.g: src has MD6Vx, find MD6Vy in tgt vopnd set.
        VOpndSetIter iter2 = nullptr;
        VMD * tgt_vopnd = nullptr;
        for (BSIdx j = tgt_mdssainfo->getVOpndSet()->get_first(&iter2);
             j != BS_UNDEF;
             j = tgt_mdssainfo->getVOpndSet()->get_next(j, &iter2)) {
            VMD * t = (VMD*)getUseDefMgr()->getVOpnd(j);
            ASSERT0(t && t->is_md());
            if (t->mdid() == src_vopnd->mdid()) {
                ASSERT0(t != src_vopnd);
                tgt_vopnd = t;
                break;
            }
        }

        if (tgt_vopnd == nullptr) {
            //Not find related tgt VMD that has same MD to src VMD.
            //Just skip it because there is no version MD to coalesce.
            continue;
        }

        ASSERTN(tgt_vopnd->version() != src_vopnd->version(),
                ("DEF and USE reference same version MD"));
        //Replace the USE of src to tgt.
        replaceVOpndForAllUse(tgt_vopnd, src_vopnd);
    }
    //Do NOT clean VOpndSet of src here because the subsequent removeStmt()
    //need VOpndSet information.
    //src_mdssainfo->cleanVOpndSet(getUseDefMgr());
}


void MDSSAMgr::addUseToMDSSAInfo(IR const* use, MDSSAInfo * mdssainfo)
{
    ASSERT0(mdssainfo);
    mdssainfo->addUse(use, getUseDefMgr());
}


void MDSSAMgr::addUseSetToMDSSAInfo(IRSet const& set, MDSSAInfo * mdssainfo)
{
    ASSERT0(mdssainfo);
    mdssainfo->addUseSet(set, getUseDefMgr());
}


void MDSSAMgr::addUseSetToVMD(IRSet const& set, MOD VMD * vmd)
{
    ASSERT0(vmd && vmd->is_md());
    vmd->addUseSet(set, m_rg);
}


MDSSAInfo * MDSSAMgr::copyAndAddMDSSAOcc(IR * ir, MDSSAInfo const* src)
{
    //User may retain IRTree in transformation, however the MDSSAInfo has been
    //removed. Thus for the sake of convenient, permit src to be empty here.
    //ASSERT0(!src->readVOpndSet().is_empty());
    MDSSAInfo * irmdssainfo = genMDSSAInfo(ir);
    if (irmdssainfo != src) {
        if (ir->is_id()) {
            //IR_ID represents an individual versioned MD, and each IR_ID only
            //can have one VOpnd. Thus clean the old VOpnd, add new VOpnd.
            MD const* md = ir->getMustRef();
            ASSERT0(md);
            irmdssainfo->copyBySpecificMD(*src, md, getUseDefMgr());
        } else {
            //Set ir references all VOpnds as src has.
            //And ir will be USE of all VOpnds.
            irmdssainfo->copy(*src, getUseDefMgr());
        }
    }
    //Set VOpnd references ir as occurrence.
    addUseToMDSSAInfo(ir, irmdssainfo);
    return irmdssainfo;
}


void MDSSAMgr::copyMDSSAInfo(IR * tgt, IR const* src)
{
    ASSERT0(MDSSAMgr::hasMDSSAInfo(tgt));
    MDSSAInfo * tgtinfo = genMDSSAInfo(tgt);
    MDSSAInfo const* srcinfo = MDSSAMgr::getMDSSAInfoIfAny(src);
    ASSERT0(srcinfo);
    tgtinfo->copy(*srcinfo, getUseDefMgr());
}


//The function copy MDSSAInfo from tree 'src' to tree tgt.
//Note src and tgt must be isomorphic.
void MDSSAMgr::copyMDSSAInfoForTree(IR * tgt, IR const* src)
{
    IRIter it;
    ConstIRIter it2;
    IR * x;
    IR const* y;
    ASSERT0(tgt->isIsomoTo(src, getIRMgr(), true));
    for (x = iterInit(tgt, it), y = iterInitC(src, it2);
         x != nullptr; x = iterNext(it), y = iterNextC(it2)) {
        if (x->isMemRefNonPR()) {
            copyMDSSAInfo(x, y);
        }
    }
}


void MDSSAMgr::addStmtToMDSSAMgr(IR * ir, IR const* ref)
{
    UNREACHABLE();//TODO:
}


void MDSSAMgr::buildDUChain(MDDef const* def, IRSet const& set)
{
    IRSetIter it = nullptr;
    for (BSIdx i = set.get_first(&it);
         i != BS_UNDEF; i = set.get_next(i, &it)) {
        IR * exp = m_rg->getIR(i);
        ASSERT0(exp);
        if (exp->is_undef()) {
            //CASE:During some passes, the optimizer will collect UseSet at
            //first, then performing the optimization, such as Phi-Elimination,
            //lead to some irs in UseSet freed after the elimination. However,
            //the UseSet is not updated at the same time. This caused IR_UNDEF
            //to be in UseSet.
            //e.g: given 'def' is Phi MD13V2,
            //  MDPhi: MD13V2 <-(id:29 MD13V1 BB10), (id:30 MD13V2 BB3)
            //  the UseSet includes id:30. After removePhiFromMDSSAMgr(def),
            //  id:30 which is in UseSet will be UNDEF.
            //For the sake of speed-up of compilation, we just skip IR_UNDEF
            //rather than asking caller remove them before invoking the
            //function.
            continue;
        }
        buildDUChain(def, exp);
    }
}


void MDSSAMgr::buildDUChain(MDDef const* def, MOD IR * exp)
{
    ASSERT0(exp->is_exp());
    ASSERTN(def->getResult(), ("does not have occurrence"));
    if (def->isUse(exp)) { return; }
    def->getResult()->addUse(exp);
    MDSSAInfo * info = genMDSSAInfo(exp);
    ASSERT0(info);
    ASSERTN(!exp->is_id() || info->readVOpndSet().get_elem_count() == 0,
            ("IR_ID can not have more than one DEF"));
    //Note the function does NOT check whether the remainder VOpnd in info
    //is conflict with 'def'. Users have to guarantee it by themself.
    info->addVOpnd(def->getResult(), getUseDefMgr());
}


//Add occurence to each VOpnd in mdssainfo.
//ir: occurence to be added.
//ref: the reference that is isomorphic to 'ir'.
//     It is used to retrieve MDSSAInfo.
void MDSSAMgr::addMDSSAOccForTree(IR * ir, IR const* ref)
{
    ASSERT0(ir->isIREqual(ref, getIRMgr(), false));
    if (ir->is_stmt()) {
        addStmtToMDSSAMgr(ir, ref);
    } else {
        MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(ref);
        if (mdssainfo != nullptr) {
            copyAndAddMDSSAOcc(ir, mdssainfo);
        } else {
            ASSERT0(!hasMDSSAInfo(ref));
        }
    }
    for (UINT i = 0; i < IR_MAX_KID_NUM(ir); i++) {
        IR const* refkid = ref->getKid(i);
        IR * x = ir->getKid(i);
        for (; x != nullptr; x = x->get_next(), refkid = refkid->get_next()) {
            ASSERTN(refkid, ("ir is not isomorphic to ref"));
            addMDSSAOccForTree(x, refkid);
        }
    }
}


void MDSSAMgr::removeMDSSAOccForTree(IR const* ir, MDSSAUpdateCtx const& ctx)
{
    ASSERT0(ir);
    if (hasMDSSAInfo(ir)) {
        if (ir->is_stmt()) {
            removeStmtMDSSAInfo(ir, ctx);
        } else {
            removeExpFromAllVOpnd(ir);
        }
    }
    for (UINT i = 0; i < IR_MAX_KID_NUM(ir); i++) {
        for (IR * x = ir->getKid(i); x != nullptr; x = x->get_next()) {
            removeMDSSAOccForTree(x, ctx);
        }
    }
}


void MDSSAMgr::removeDUChain(MDDef const* def, IR * exp)
{
    ASSERT0(exp->is_exp());
    def->getResult()->removeUse(exp);
    MDSSAInfo * info = genMDSSAInfo(exp);
    ASSERT0(info);
    //Note the function does NOT check whether the remainder VOpnd in info
    //is conflict with 'def'. Users have to guarantee it by themself.
    info->removeVOpnd(def->getResult(), getUseDefMgr());
}


void MDSSAMgr::removeDUChain(IR const* stmt, IR const* exp)
{
    ASSERT0(stmt && exp && stmt->is_stmt() && exp->is_exp());
    MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(exp);
    if (mdssainfo == nullptr) { return; }
    VOpndSetIter iter = nullptr;
    BSIdx next_i = BS_UNDEF;
    for (BSIdx i = mdssainfo->getVOpndSet()->get_first(&iter);
         i != BS_UNDEF; i = next_i) {
        next_i = mdssainfo->getVOpndSet()->get_next(i, &iter);
        VMD * vopnd = (VMD*)getUseDefMgr()->getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md());
        if (vopnd->getDef() == nullptr) {
            ASSERTN(vopnd->version() == MDSSA_INIT_VERSION,
                    ("Only zero version MD has no DEF"));
            continue;
        }
        if (vopnd->getDef()->getOcc() != stmt) { continue; }
        vopnd->removeUse(exp);
        mdssainfo->removeVOpnd(vopnd, getUseDefMgr());
    }
}


void MDSSAMgr::removeAllUse(IR const* stmt, MDSSAUpdateCtx const& ctx)
{
    ASSERT0(stmt && stmt->is_stmt());
    MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(stmt);
    if (mdssainfo == nullptr || mdssainfo->isEmptyVOpndSet()) { return; }

    VOpndSetIter iter = nullptr;
    BSIdx next_i = BS_UNDEF;
    for (BSIdx i = mdssainfo->getVOpndSet()->get_first(&iter);
         i != BS_UNDEF; i = next_i) {
        next_i = mdssainfo->getVOpndSet()->get_next(i, &iter);
        VMD * vopnd = (VMD*)getUseDefMgr()->getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md());
        if (vopnd->getDef() == nullptr) {
            ASSERTN(vopnd->version() == MDSSA_INIT_VERSION,
                    ("Only zero version MD has no DEF"));
            continue;
        }
        if (vopnd->getDef()->getOcc() != stmt) { continue; }

        //Iterate all USEs.
        removeVOpndForAllUse(vopnd, ctx);
        mdssainfo->removeVOpnd(vopnd, getUseDefMgr());
    }
}


void MDSSAMgr::replaceVOpndForAllUse(MOD VMD * to, MOD VMD * from)
{
    ASSERT0(to->is_md() && from->is_md());
    //Replace the USE of src to tgt.
    VMD::UseSetIter it;
    for (UINT k = from->getUseSet()->get_first(it);
         !it.end(); k = from->getUseSet()->get_next(it)) {
        IR const* use = (IR*)m_rg->getIR(k);
        MDSSAInfo * use_mdssainfo = getMDSSAInfoIfAny(use);
        ASSERTN(use_mdssainfo, ("use miss MDSSAInfo"));
        use_mdssainfo->removeVOpnd(from, getUseDefMgr());
        use_mdssainfo->addVOpnd(to, getUseDefMgr());
        to->addUse(use);
    }
    from->cleanUseSet();
}


void MDSSAMgr::findNewDefForID(IR * id, MDSSAInfo * ssainfo, MDDef * olddef,
                               OptCtx const& oc, OUT MDSSAStatus & st)
{
    ASSERT0(id->is_id());
    if (ID_phi(id) == nullptr) { return; }

    ASSERT0(getMDSSAInfoIfAny(id) == ssainfo);
    //CASE: To avoid assertions that raised by verify() which is used
    //to guanrantee operand of MDPhi is not NULL, replace the removed
    //vopnd of operand with initial-version vopnd.
    ASSERT0(olddef);
    VMD * oldres = olddef->getResult();
    MDIdx defmdid = oldres->mdid();
    ASSERT0(defmdid != MD_UNDEF);
    IR const* start = nullptr;
    VMD * livein = nullptr;
    ASSERTN(oc.is_dom_valid(), ("DOM info must be available"));
    if (olddef->is_phi()) {
        livein = findDomLiveInDefFromIDomOf(olddef->getBB(), defmdid, oc, st);
    } else {
        start = olddef->getBB()->getPrevIR(olddef->getOcc());
        livein = findDomLiveInDefFrom(defmdid, start, olddef->getBB(), oc, st);
    }
    if (livein == nullptr || livein == oldres) {
        livein = genInitVersionVMD(defmdid);
        ssainfo->addVOpnd(livein, getUseDefMgr());
        return;
    }
    buildDUChain(livein->getDef(), id);
}


void MDSSAMgr::removeVOpndForAllUse(MOD VMD * vopnd, MDSSAUpdateCtx const& ctx)
{
    ASSERT0(vopnd && vopnd->is_md());
    VMD::UseSet * useset = vopnd->getUseSet();
    VMD::UseSetIter it;
    MDDef * vopnddef = vopnd->getDef();
    MDSSAStatus st;
    for (BSIdx i = useset->get_first(it); !it.end(); i = useset->get_next(it)) {
        IR * use = m_rg->getIR(i);
        ASSERT0(use && (use->isMemRef() || use->is_id()));
        MDSSAInfo * mdssainfo = getMDSSAInfoIfAny(use);
        ASSERT0(mdssainfo);
        if (use->is_id()) {
            //An individual ID can NOT represent multiple versioned MD, thus
            //the VOpnd of ID must be unique.
            mdssainfo->cleanVOpndSet(getUseDefMgr());
        } else {
            mdssainfo->removeVOpnd(vopnd, getUseDefMgr());
        }
        if (!use->is_id()) { continue; }
        if (ctx.need_update_duchain()) {
            //Note VOpndSet of 'vopnd' may be empty after the removing.
            //It does not happen when MDSSA just constructed. The USE that
            //without real-DEF will have a virtual-DEF that version is
            //INIT_VERSION.
            //During some increment maintaining of MDSSA, 'vopnd' may be
            //removed, just like what current function does.
            //This means the current USE, 'use', does not have real-DEF stmt,
            //the value of 'use' always coming from parameter of global value.
            findNewDefForID(use, mdssainfo, vopnddef, ctx.getOptCtx(), st);
        }
    }
    vopnd->cleanUseSet();
}


void MDSSAMgr::removeDefFromUseSet(MDPhi const* phi, MDSSAUpdateCtx const& ctx)
{
    removeVOpndForAllUse(phi->getResult(), ctx);
}


void MDSSAMgr::removePhiFromMDSSAMgr(
    MDPhi * phi, MDDef * prev, MDSSAUpdateCtx const& ctx)
{
    ASSERT0(phi && phi->is_phi());
    for (IR * opnd = phi->getOpndList(); opnd != nullptr;
         opnd = opnd->get_next()) {
        //Update the MDSSAInfo for each phi opnd.
        removeMDSSAOccForTree(opnd, ctx);
    }
    VMD * phires = phi->getResult();
    //Note phires-vopnd should be removed first of all because it use DD-Chain.
    removeVOpndForAllUse(phires, ctx);
    removePhiFromDDChain(phi, prev);
    m_rg->freeIRTreeList(phi->getOpndList());
    MDPHI_opnd_list(phi) = nullptr;
    removeVMD(phires);
}


void MDSSAMgr::removePhiList(IRBB * bb, MDSSAUpdateCtx const& ctx)
{
    MDPhiList * philist = getPhiList(bb);
    if (philist == nullptr) { return; }
    MDPhiListIter next;
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = next) {
        next = philist->get_next(it);
        MDPhi * phi = it->val();
        ASSERT0(phi && phi->is_phi());
        removePhiFromMDSSAMgr(phi, nullptr, ctx);
        philist->remove_head();
    }
    //Do NOT free PhiList here even if it is empty because philist is
    //allocated from memory pool.
}


//Remove given IR expression from UseSet of each vopnd in MDSSAInfo.
//Note current MDSSAInfo is the SSA info of 'exp', the VOpndSet will be
//emtpy when exp is removed from all VOpnd's useset.
//exp: IR expression to be removed.
//NOTE: the function only process exp itself.
void MDSSAMgr::removeExpFromAllVOpnd(IR const* exp)
{
    ASSERT0(exp && exp->is_exp() && hasMDSSAInfo(exp));
    MDSSAInfo * expssainfo = UseDefMgr::getMDSSAInfo(exp);
    ASSERT0(expssainfo);
    VOpndSetIter it = nullptr;
    VOpndSet * vopndset = expssainfo->getVOpndSet();
    UseDefMgr * mgr = getUseDefMgr();
    for (BSIdx i = vopndset->get_first(&it);
         i != BS_UNDEF; i = vopndset->get_next(i, &it)) {
        VMD * vopnd = (VMD*)mgr->getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md());
        vopnd->removeUse(exp);
    }
    expssainfo->cleanVOpndSet(mgr);
}


//Remove Use-Def chain.
//exp: the expression to be removed.
//e.g: ir = ...
//    = ir //S1
//If S1 will be deleted, ir should be removed from its useset in MDSSAInfo.
//NOTE: the function only process exp itself.
void MDSSAMgr::removeUse(IR const* exp)
{
    if (hasMDSSAInfo(exp)) {
        removeExpFromAllVOpnd(exp);
    }
}


//Remove all VMD in set from MDSSAMgr. The function will clean all information
//about these VMDs.
void MDSSAMgr::removeVMDInSet(VOpndSet const& set)
{
    VOpndSetIter it = nullptr;
    for (BSIdx i = set.get_first(&it); i != BS_UNDEF;
         i = set.get_next(i, &it)) {
        VMD * vmd = (VMD*)getUseDefMgr()->getVOpnd(i);
        ASSERT0(vmd && vmd->is_md());
        //The function only remove VMD out of MDSSAMgr.
        removeVMD(vmd);
    }
}


void MDSSAMgr::removeStmtMDSSAInfo(IR const* stmt, MDSSAUpdateCtx const& ctx)
{
    ASSERT0(stmt && stmt->is_stmt());
    MDSSAInfo * stmtmdssainfo = getMDSSAInfoIfAny(stmt);
    ASSERT0(stmtmdssainfo);
    VOpndSetIter iter = nullptr;
    BSIdx next_i = BS_UNDEF;
    for (BSIdx i = stmtmdssainfo->getVOpndSet()->get_first(&iter);
         i != BS_UNDEF; i = next_i) {
        next_i = stmtmdssainfo->getVOpndSet()->get_next(i, &iter);
        VMD * vopnd = (VMD*)getUseDefMgr()->getVOpnd(i);
        ASSERT0(vopnd && vopnd->is_md());
        if (vopnd->getDef() == nullptr) {
            ASSERTN(vopnd->version() == MDSSA_INIT_VERSION,
                    ("Only zero version MD has no DEF"));
            continue;
        }
        ASSERT0(vopnd->getDef()->getOcc() == stmt);

        //Iterate all USEs and remove 'vopnd' from its MDSSAInfo.
        removeVOpndForAllUse(vopnd, ctx);

        //Iterate DefDef chain.
        removeDefFromDDChain(vopnd->getDef());

        //Remove 'vopnd' from current stmt.
        stmtmdssainfo->removeVOpnd(vopnd, getUseDefMgr());

        //Clear DEF info of 'vopnd'
        VMD_def(vopnd) = nullptr;

        //Because all info has been eliminated, the vopnd is out
        //of date and can be removed from MDSSAMgr.
        removeVMD(vopnd);
    }
}


//Union successors in NextSet from 'from' to 'to'.
void MDSSAMgr::unionSuccessors(MDDef const* from, MDDef const* to)
{
    if (from->is_phi()) {
        if (to->getNextSet() == nullptr) {
            if (from->getNextSet() != nullptr) {
                //Note if MDDef indicates PHI, it does not have Previous DEF,
                //because PHI has multiple Previous DEFs rather than single DEF.
                MDDEF_nextset(to) = m_usedef_mgr.allocMDDefSet();
                to->getNextSet()->bunion(*from->getNextSet(), *getSBSMgr());
            }
            return;
        }
        if (from->getNextSet() != nullptr) {
            //Note if MDDef indicates PHI, it does not have Previous DEF,
            //because PHI has multiple Previous DEFs rather than single DEF.
            to->getNextSet()->bunion(*from->getNextSet(), *getSBSMgr());
        }
        return;
    }
    if (to->getNextSet() == nullptr || from->getNextSet() == nullptr) {
        return;
    }
    to->getNextSet()->bunion(*from->getNextSet(), *getSBSMgr());
}


//Remove MDDef from DefDef chain.
//Note the function does not deal with MDSSAInfo of IR occurrence, and just
//process DefDef chain that built on MDDef.
//mddef: will be removed from DefDef chain, and be modified as well.
//prev: previous Def to mddef, and will be modified.
//e.g:D1->D2
//     |->D3
//     |  |->D5
//     |  |->D6
//     |->D4
//  where D1 is predecessor of D2, D3 and D4; D3 is predecssor of D5, D6.
//  After remove D3:
//e.g:D1->D2
//     |->D5
//     |->D6
//     |->D4
//  where D1 is predecessor of D2, D5, D6, D4.
void MDSSAMgr::removeDefFromDDChainHelper(MDDef * mddef, MDDef * prev)
{
    ASSERT0(mddef);
    if (mddef->getNextSet() == nullptr) {
        if (prev != nullptr) {
            if (prev->getNextSet() != nullptr) {
                //Cutoff def-def chain between 'mddef' to its predecessor.
                prev->getNextSet()->remove(mddef, *getSBSMgr());
            } else {
                //Note if mddef indicates PHI, it does not have Previous DEF,
                //because PHI has multiple Previous DEFs rather than single
                //DEF.
                ASSERT0(mddef->is_phi());
            }
        }
        MDDEF_prev(mddef) = nullptr;
        getUseDefMgr()->removeMDDef(mddef);
        return;
    }

    if (prev != nullptr) {
        //CASE: Be careful that 'prev' should not belong to the NextSet of
        //mddef', otherwise the union operation of prev and mddef's succ DEF
        //will construct a cycle in DefDef chain, which is illegal.
        //e.g: for (i = 0; i < 10; i++) {;}, where i's MD is MD5.
        //  MD5V2 <-- PHI(MD5V--, MD5V3)
        //  MD5V3 <-- MD5V2 + 1
        // If we regard MD5V3 as the common-def, PHI is 'mddef', a cycle
        // will appeared.
        ASSERTN(!mddef->getNextSet()->find(prev),
                ("prev is actually the NEXT of mddef"));

        //Union successors of 'mddef' to its predecessor's next-set.
        unionSuccessors(mddef, prev);
        if (prev->getNextSet() != nullptr) {
            prev->getNextSet()->remove(mddef, *getSBSMgr());
        }
    }

    //Update successor's predecesor.
    MDDefSetIter nit = nullptr;
    for (BSIdx w = mddef->getNextSet()->get_first(&nit);
         w != BS_UNDEF; w = mddef->getNextSet()->get_next(w, &nit)) {
        MDDef const* use = getUseDefMgr()->getMDDef(w);
        ASSERTN(use->getPrev() == mddef, ("insanity DD chain"));
        MDDEF_prev(use) = prev;
    }
    MDDEF_prev(mddef) = nullptr;
    mddef->cleanNextSet(getUseDefMgr());
    getUseDefMgr()->removeMDDef(mddef);
}


//Remove MDDef from DefDef chain.
//mddef: will be removed from DefDef chain, and be modified as well.
//e.g:D1->D2
//     |->D3
//     |  |->D5
//     |  |->D6
//     |->D4
//  where D1 is predecessor of D2, D3 and D4; D3 is predecssor of D5, D6.
//  After remove D3:
//e.g:D1->D2
//     |->D5
//     |->D6
//     |->D4
//  where D1 is predecessor of D2, D5, D6, D4.
void MDSSAMgr::removeDefFromDDChain(MDDef * mddef)
{
    ASSERT0(mddef);
    MDDef * prev = mddef->getPrev();
    removeDefFromDDChainHelper(mddef, prev);
}


bool MDSSAMgr::isPhiKillingDef(MDPhi const* phi) const
{
    if (phi->getNextSet() == nullptr) { return true; }
    MD const* phi_result_md = phi->getResultMD(m_md_sys);
    ASSERT0(phi_result_md);
    MDDefSetIter nit = nullptr;
    MDSSAMgr * pthis = const_cast<MDSSAMgr*>(this);
    for (BSIdx i = phi->getNextSet()->get_first(&nit);
         i != BS_UNDEF; i = phi->getNextSet()->get_next(i, &nit)) {
        MDDef const* next_def = pthis->getUseDefMgr()->getMDDef(i);
        ASSERTN(next_def && next_def->getPrev() == phi,
                ("insanity DD chain"));
        MD const* next_result_md = next_def->getResultMD(m_md_sys);
        if (!next_result_md->is_exact_cover(phi_result_md)) {
            //There are DEFs represented by phi and its operand may pass
            //through 'def'. These DEFs belong to the MayDef set of
            //followed USE.
            return false;
        }
    }
    return true;
}


//Remove PHI that without any USE.
//Return true if any PHI was removed, otherwise return false.
bool MDSSAMgr::removePhiNoUse(MDPhi * phi, OptCtx const& oc)
{
    ASSERT0(phi && phi->is_phi() && phi->getBB());
    VMD * vopnd = phi->getResult();
    ASSERT0(vopnd && vopnd->is_md());
    ASSERT0(phi == vopnd->getDef());
    if (vopnd->hasUse()) { return false; }

    MD const* phi_result_md = vopnd->getMD(m_md_sys);
    ASSERT0(phi_result_md);
    if (!phi_result_md->is_exact()) {
        //Inexact MD indicates a non-killing-def, and the phi which is
        //non-killing-def always used to pass through DEF in def-chain.
        //Thus the phi is the connection point to other DEF, and can not be
        //removed.
        return false;
    }
    if (!isPhiKillingDef(phi)) { return false; }

    //Note MDPhi does not have previous DEF, because usually Phi has
    //multiple previous DEFs rather than single DEF.
    //CASE:compile/mdssa_phi_prevdef.c
    //Sometime optimization may form CFG that cause PHI1
    //dominiates PHI2, then PHI1 will be PHI2's previous-DEF.
    //ASSERT0(phi->getPrev() == nullptr);

    MDDef * phiprev = nullptr;
    if (phi->isDefRealStmt()) {
        MDSSAStatus st;
        VMD * prev = findDomLiveInDefFromIDomOf(
            phi->getBB(), phi->getResult()->mdid(), oc, st);
        if (prev != nullptr) {
            phiprev = prev->getDef();
        }
    }
    MDSSAUpdateCtx ctx(oc);
    removePhiFromMDSSAMgr(phi, phiprev, ctx);
    return true;
}


//Check each USE|DEF of ir, remove the expired one which is not reference
//the memory any more that ir defined.
//Return true if DU changed.
bool MDSSAMgr::removeExpiredDU(IR const* ir)
{
    //TODO: Do NOT attempt to remove if not found reference MD at USE point
    //which should correspond to vopnd->md().
    //Some transformation, such as IR Refinement, may change
    //the USE's MDSet. This might lead to the inaccurate and
    //redundant MDSSA DU Chain. So the MDSSA DU Chain is conservative,
    //but the correctness of MDSSA dependence is garanteed.
    //e.g:
    //  ist:*<4> id:18 //:MD11, MD12, MD14, MD15
    //    lda: *<4> 'r'
    //    ild: i32 //MMD13: MD16
    //      ld: *<4> 'q' //MMD18
    //=> After IR combination: ist(lda) transformed to st
    //  st:*<4> 'r' //MMD12
    //    ild : i32 //MMD13 : MD16
    //      ld : *<4> 'q'    //MMD18
    //ist transformed to st. This reduce referenced MDSet to a single MD
    //as well.
    return false;
}


//Remove MDDef from Def-Def chain.
//phi: will be removed from Def-Def chain, and be modified as well.
//prev: designated previous DEF.
//e.g:D1<->D2
//     |<->D3
//     |   |<->D5
//     |   |<->D6
//     |->D4
//  where predecessor of D3 is D1, successors of D3 are D5, D6
//  After remove D3:
//    D1<->D2
//     |<->D5
//     |<->D6
//     |<->D4
//    D3<->nullptr
//  where predecessor of D5, D6 is D1, successor of D1 includes D5, D6.
void MDSSAMgr::removePhiFromDDChain(MDPhi * phi, MDDef * prev)
{
    ASSERT0(phi && phi->is_phi());
    removeDefFromDDChainHelper(phi, prev);
}


//This function perform SSA destruction via scanning BB in sequential order.
void MDSSAMgr::destruction(MOD OptCtx & oc)
{
    BBList * bblst = m_rg->getBBList();
    if (bblst->get_elem_count() == 0) { return; }
    UINT bbnum = bblst->get_elem_count();
    BBListIter bbct;
    for (bblst->get_head(&bbct);
         bbct != bblst->end(); bbct = bblst->get_next(bbct)) {
        ASSERT0(bbct->val());
        destructBBSSAInfo(bbct->val());
    }
    if (bbnum != bblst->get_elem_count()) {
        oc.setInvalidIfCFGChanged();
    }
    set_valid(false);
}


//wl: is an optional parameter to record BB which expected to deal with.
//    It is a work-list that is used to drive iterative collection and
//    elimination of redundant PHI elmination.
//Return true if phi removed.
bool MDSSAMgr::removePhiHasCommonDef(List<IRBB*> * wl, MDPhi * phi,
                                     OptCtx const& oc)
{
    ASSERT0(phi);
    VMD * common_def = nullptr;
    if (!doOpndHaveSameDef(phi, &common_def)) {
        return false;
    }

    //commond_def may be NULL.
    //e.g:Phi: MD10V3 <- MD10V0
    //  The only operand of PHI is livein MD, thus the
    //  commond_def is NULL.
    for (IR * opnd = phi->getOpndList();
         opnd != nullptr; opnd = opnd->get_next()) {
        VMD * vopnd = phi->getOpndVMD(opnd, &m_usedef_mgr);
        ASSERTN(vopnd,
            ("at least init-version VOpnd should be attached in the 'opnd'"
             "if original non-init-version VOpnd removed"));
        ASSERT0(vopnd->is_md());
        if (wl != nullptr && vopnd->getDef() != nullptr) {
            ASSERT0(vopnd->getDef()->getBB());
            wl->append_tail(vopnd->getDef()->getBB());
        }
    }
    if (common_def != phi->getResult() && common_def != nullptr) {
        //Change DEF from PHI to common_def to elements in UseList.
        ASSERT0(common_def->is_md());

        //CASE: Be careful that 'prev' should not belong to the NextSet of
        //mddef', otherwise the union operation of prev and mddef's succ DEF
        //will construct a cycle in Def-Def chain, which is illegal.
        //e.g: for (i = 0; i < 10; i++) {;}, where i's MD is MD5.
        //  MD5V2 <-- PHI(MD5V--, MD5V3)
        //  MD5V3 <-- MD5V2 + 1 #S1
        //Assume above code told us that MD5V3 is the common_def, whereas we
        //are going to remove PHI because its operands have a common_def. If
        //we set #S1 (the common_def) to be previous DEF in def-def chain of
        //MD5, a def-def cycle will appeared, of course it is invalid.
        MDDef * prev = common_def->getDef();
        ASSERT0(prev);
        if (phi->isInNextSet(prev, getUseDefMgr())) {
            //Avoid making a def-def cycle.
            prev = nullptr;
        }

        //Do NOT do collection crossing PHI.
        CollectCtx clctx(COLLECT_IMM_USE);
        IRSet useset(getSBSMgr()->getSegMgr());
        if (prev != nullptr) {
            CollectUse cu(this, phi->getResult(), clctx, &useset);
        }
        MDSSAUpdateCtx ctx(oc);
        removePhiFromMDSSAMgr(phi, prev, ctx);
        if (prev != nullptr) {
            //Set UseSet of PHI to be the USE of previous DEF.
            buildDUChain(prev, useset);
        }
        return true;
    }
    MDSSAUpdateCtx ctx(oc);
    removePhiFromMDSSAMgr(phi, nullptr, ctx);
    return true;
}


//wl: is an optional parameter to record BB which expected to deal with.
//    It is a work-list that is used to drive iterative collection and
//    elimination of redundant PHI elmination.
//Return true if phi removed.
bool MDSSAMgr::removePhiHasNoValidDef(List<IRBB*> * wl, MDPhi * phi,
                                      OptCtx const& oc)
{
    ASSERT0(phi);
    if (doOpndHaveValidDef(phi)) {
        return false;
    }
    for (IR * opnd = phi->getOpndList();
         opnd != nullptr; opnd = opnd->get_next()) {
        VMD * vopnd = phi->getOpndVMD(opnd, &m_usedef_mgr);
        //if (vopnd == nullptr) {
        //    //VOpnd may be have been removed from MDSSAMgr, thus the VOpnd
        //    //that corresponding to current ID is NULL.
        //    continue;
        //}
        ASSERTN(vopnd, ("init-version should be placed if vopnd removed"));

        ASSERT0(vopnd->is_md());
        if (wl != nullptr && vopnd->getDef() != nullptr) {
            ASSERT0(vopnd->getDef()->getBB());
            wl->append_tail(vopnd->getDef()->getBB());
        }
    }

    //Do NOT do collection crossing PHI.
    CollectCtx clctx(COLLECT_IMM_USE);
    IRSet useset(getSBSMgr()->getSegMgr());
    if (phi->getPrev() != nullptr) {
        CollectUse cu(this, phi->getResult(), clctx, &useset);
    }
    MDSSAUpdateCtx ctx(oc);
    removePhiFromMDSSAMgr(phi, phi->getPrev(), ctx);
    if (phi->getPrev() != nullptr) {
        //Set UseSet of PHI to be the USE of previous DEF.
        buildDUChain(phi->getPrev(), useset);
    }
    return true;
}


//wl: work list for temporary used.
//Return true if any PHI was removed.
bool MDSSAMgr::prunePhiForBB(IRBB const* bb, List<IRBB*> * wl, OptCtx const& oc)
{
    ASSERT0(bb);
    MDPhiList * philist = getPhiList(bb);
    if (philist == nullptr) { return false; }
    bool remove = false;
    MDPhiListIter prev = nullptr;
    MDPhiListIter next = nullptr;
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = next) {
        next = philist->get_next(it);
        MDPhi * phi = it->val();
        ASSERT0(phi);
        if (removePhiHasCommonDef(wl, phi, oc)) {
            remove = true;
            philist->remove(prev, it);
            continue;
        }
        if (removePhiHasNoValidDef(wl, phi, oc)) {
            remove = true;
            philist->remove(prev, it);
            continue;
        }

        //Remove PHI that without any USE.
        //TBD: PHI that without USE could not removed in some case:
        //e.g:for (...) { S1<---will insert PHI of x
        //      x[i]=0;   S2
        //    }
        //    x[j]=...;   S3
        //  where x[j] is NOT killing-def of x[i].
        //  ----
        //  MDSSAMgr inserted PHI of x, it is the previous DEF of S3.
        //  If we remove PHI at S1, S2 will lost USE at opnd of PHI, thus
        //  will be removed finally. It is incorrect.
        if (removePhiNoUse(phi, oc)) {
            remove = true;
            philist->remove(prev, it);
            continue;
        }
        prev = it;
    }
    return remove;
}


//Remove redundant phi.
//Return true if any PHI was removed.
bool MDSSAMgr::removeRedundantPhi(OptCtx const& oc)
{
    List<IRBB*> wl;
    return prunePhi(wl, oc);
}


//Remove redundant phi.
//wl: work list for temporary used.
//Return true if any PHI was removed.
bool MDSSAMgr::prunePhi(List<IRBB*> & wl, OptCtx const& oc)
{
    START_TIMER(t, "MDSSA: Prune phi");
    BBList * bblst = m_rg->getBBList();
    BBListIter ct;

    wl.clean();
    for (bblst->get_head(&ct); ct != bblst->end(); ct = bblst->get_next(ct)) {
        IRBB * bb = ct->val();
        ASSERT0(bb);
        wl.append_tail(bb);
    }

    bool remove = false;
    IRBB * bb = nullptr;
    while ((bb = wl.remove_head()) != nullptr) {
        remove |= prunePhiForBB(bb, &wl, oc);
    }
    END_TIMER(t, "MDSSA: Prune phi");
    return remove;
}


//Clean MDSSAInfo AI of all IR.
void MDSSAMgr::cleanMDSSAInfoAI()
{
    for (VecIdx i = 0; i <= m_rg->getIRVec().get_last_idx(); i++) {
        IR * ir = m_rg->getIR(i);
        if (ir != nullptr && ir->getAI() != nullptr &&
            ir->getAI()->is_init()) {
            IR_ai(ir)->clean(AI_MD_SSA);
        }
    }
}


//Reinitialize MD SSA manager.
void MDSSAMgr::reinit()
{
    destroy();
    cleanMDSSAInfoAI();
    m_max_version.destroy();
    m_max_version.init();
    m_usedef_mgr.reinit();
    init();
}


//The function will attempt to remove the USE that located in outside loop BB.
//Note the function will NOT cross MDPhi.
bool MDSSAMgr::tryRemoveOutsideLoopUse(MDDef * def, LI<IRBB> const* li)
{
    VMD * res = def->getResult();
    VMD::UseSetIter it;
    UINT next_i;
    bool removed = false;
    VMD::UseSet * uset = res->getUseSet();
    for (UINT i = uset->get_first(it); !it.end(); i = next_i) {
        next_i = uset->get_next(it);
        IR * u = m_rg->getIR(i);
        ASSERT0(u);
        if (u->is_id()) {
            MDPhi const* phi = ID_phi(u);
            if (li->isInsideLoop(phi->getBB()->id())) { continue; }
        } else {
            IRBB const* bb = u->getStmt()->getBB();
            ASSERT0(bb);
            if (li->isInsideLoop(bb->id())) { continue; }
        }
        removeDUChain(def, u);
        removed = true;
    }
    return removed;
}


bool MDSSAMgr::verifyMDSSAInfo(Region const* rg, OptCtx const& oc)
{
    MDSSAMgr * ssamgr = (MDSSAMgr*)(rg->getPassMgr()->
        queryPass(PASS_MDSSA_MGR));
    if (ssamgr == nullptr || !ssamgr->is_valid()) { return true; }
    ASSERT0(ssamgr->verify());
    ASSERT0(ssamgr->verifyPhi());
    if (oc.is_dom_valid()) {
        ASSERT0(ssamgr->verifyVersion(oc));
    }
    return true;
}


//Duplicate Phi operand that is at the given position, and insert after
//given position sequently.
//pos: given position
//num: the number of duplication.
//Note caller should guarrentee the number of operand is equal to the
//number predecessors of BB of Phi.
void MDSSAMgr::dupAndInsertPhiOpnd(IRBB const* bb, UINT pos, UINT num)
{
    ASSERT0(bb && num >= 1);
    MDPhiList * philist = getPhiList(bb);
    if (philist == nullptr || philist->get_elem_count() == 0) {
        return;
    }
    ASSERTN(0, ("TO BE CHECK"));
    for (MDPhiListIter it = philist->get_head();
         it != philist->end(); it = philist->get_next(it)) {
        MDPhi * phi = it->val();
        ASSERT0(phi->getOpndNum() == bb->getNumOfPred());
        IR * opnd = phi->getOpnd(pos);
        ASSERTN(opnd, ("MDPHI does not contain such many operands."));
        MDSSAInfo * opndinfo = getMDSSAInfoIfAny(opnd);
        ASSERT0(opndinfo || !opnd->is_id());
        for (UINT i = 0; i < num; i++) {
            IR * newopnd = m_rg->dupIR(opnd);
            phi->insertOpndAfter(opnd, newopnd);
            if (opndinfo != nullptr) {
                addUseToMDSSAInfo(newopnd, opndinfo);
            }
        }
    }
}


//Return true if stmt dominates use's stmt, otherwise return false.
bool MDSSAMgr::isStmtDomUseInsideLoop(IR const* stmt, IR const* use,
                                      LI<IRBB> const* li) const
{
    IRBB const* usestmtbb = nullptr;
    if (use->is_id()) {
        MDPhi const* phi = ID_phi(use);
        ASSERT0(phi && phi->is_phi());
        usestmtbb = phi->getBB();
    } else {
        ASSERT0(use->getStmt());
        usestmtbb = use->getStmt()->getBB();
    }
    ASSERT0(usestmtbb);

    if (!li->isInsideLoop(usestmtbb->id())) {
        //Only check dominiation info inside loop.
        return true;
    }

    IRBB const* defstmtbb = stmt->getBB();
    ASSERT0(defstmtbb);
    if (defstmtbb != usestmtbb &&
        m_cfg->is_dom(defstmtbb->id(), usestmtbb->id())) {
        return true;
    }
    if (defstmtbb == usestmtbb) {
        if (use->is_id()) { //use's stmt is MDPhi
            //stmt can not dominate PHI because PHI is always
            //in the front of BB.
            return false;
        }
        IR const* ustmt = use->getStmt();
        ASSERT0(ustmt);
        return defstmtbb->is_dom(stmt, ustmt, true);
    }

    return false;
}


//Return true if ir dominates all its USE expressions which inside loop.
//In ssa mode, stmt's USE may be placed in operand list of PHI.
bool MDSSAMgr::isStmtDomAllUseInsideLoop(IR const* ir, LI<IRBB> const* li) const
{
    ASSERT0(ir && ir->getBB());
    ConstMDSSAUSEIRIter it(this);
    for (IR const* use = it.get_first(ir);
        use != nullptr; use = it.get_next()) {
        if (!isStmtDomUseInsideLoop(ir, use, li)) {
            return false;
        }
    }
    return true;
}


//Move MDPhi from 'from' to 'to'.
//This function often used in updating PHI when adding new dominater
//BB to 'to'.
void MDSSAMgr::movePhi(IRBB * from, IRBB * to)
{
    ASSERT0(from && to && from != to);
    MDPhiList * from_philist = getPhiList(from);
    if (from_philist == nullptr || from_philist->get_elem_count() == 0) {
        return;
    }

    MDPhiList * to_philist = m_usedef_mgr.genBBPhiList(to->id());
    MDPhiListIter to_it = to_philist->get_head();
    for (MDPhiListIter from_it = from_philist->get_head();
         from_it != from_philist->end();
         from_it = from_philist->get_next(from_it)) {

        //Move MDPhi from 'from' to 'to'.
        MDPhi * phi = from_it->val();
        MDPHI_bb(phi) = to;
        if (to_it == nullptr) {
            //'to' BB does not have PHI list.
            to_it = to_philist->append_head(phi);
        } else {
            //Make sure phi's order in 'to' is same with 'from'.
            to_it = to_philist->insert_after(phi, to_it);
        }
    }
    from_philist->clean();
}


//Return true if the value of ir1 and ir2 are definitely same, otherwise
//return false to indicate unknown.
bool MDSSAMgr::hasSameValue(IR const* ir1, IR const* ir2)
{
    ASSERT0(ir1->isMemRefNonPR() && ir2->isMemRefNonPR());
    MDSSAInfo const* info1 = getMDSSAInfoIfAny(ir1);
    MDSSAInfo const* info2 = getMDSSAInfoIfAny(ir2);
    ASSERT0(info1 && info2);
    return info1->isEqual(*info2);
}





//Return true if there is at least one USE 'vmd' has been placed in given
//IRBB 'bbid'.
//vmd: the function will iterate all its USE occurrences.
//it: for tmp used.
bool MDSSAMgr::isUseWithinBB(VMD const* vmd, MOD VMD::UseSetIter & it,
                             UINT bbid) const
{
    ASSERT0(vmd && vmd->is_md());
    it.clean();
    VMD::UseSet const* uset = const_cast<VMD*>(vmd)->getUseSet();
    for (UINT i = uset->get_first(it); !it.end(); i = uset->get_next(it)) {
        IR * u = m_rg->getIR(i);
        ASSERT0(u);
        if (u->is_id()) {
            MDPhi const* phi = ID_phi(u);
            if (phi->getBB()->id() == bbid) { return true; }
            continue;
        }
        IRBB const* bb = u->getStmt()->getBB();
        ASSERT0(bb);
        if (bb->id() == bbid) { return true; }
    }
    return false;
}


bool MDSSAMgr::isOverConservativeDUChain(IR const* ir1, IR const* ir2,
                                         Region const* rg)
{
    ASSERT0(ir1 && ir2 && ir1 != ir2);
    if (ir1->isNotOverlap(ir2, rg)) { return true; }
    MD const* must1 = ir1->getMustRef();
    MD const* must2 = ir2->getMustRef();
    if (must1 == nullptr || must2 == nullptr || must2 == must1) {
        //If MustDef is empty, for conservative purpose, 'defir' is
        //the MayDef of 'exp', thus we have to consider the
        //inexact DU relation.
        return false;
    }
    return !must1->is_overlap(must2);
}


//Return true if 'def' is NOT the real-def of mustref, and can be ignored.
//phi: one of DEF of exp.
static bool canIgnorePhi(IR const* exp, MDPhi const* phi, MDSSAMgr const* mgr,
                         MOD PhiTab & visit)
{
    ASSERT0(exp && exp->is_exp() && phi->is_phi());
    visit.append(phi);
    for (IR const* opnd = phi->getOpndList();
         opnd != nullptr; opnd = opnd->get_next()) {
        MDSSAInfo const* info = mgr->getMDSSAInfoIfAny(opnd);
        ASSERT0(info);
        VMD * vmd = (VMD*)info->readVOpndSet().get_unique(mgr);
        ASSERT0(vmd && vmd->is_md());
        MDDef const* def = vmd->getDef();
        if (def == nullptr) { continue; }
        if (def->is_phi()) {
            if (visit.get((MDPhi const*)def) != nullptr ||
                canIgnorePhi(exp, (MDPhi const*)def, mgr, visit)) {
                continue;
            }
            return false;
        }
        IR const* defir = def->getOcc();
        if (!MDSSAMgr::isOverConservativeDUChain(defir, exp,
                                                 mgr->getRegion())) {
            return false;
        }
    }
    return true;
}


//Return true if the DU chain between 'def' and 'use' can be ignored during
//DU chain manipulation.
//def: one of DEF of exp.
//exp: expression.
bool MDSSAMgr::isOverConservativeDUChain(MDDef const* def, IR const* exp) const
{
    ASSERT0(exp->is_exp() && exp->isMemRefNonPR());
    MD const* mustref = exp->getMustRef();
    if (mustref == nullptr) {
        //If MustRef is empty, for conservative purpose, 'def' is the MayDef
        //of 'exp', thus we have to consider the inexact DU relation.
        return false;
    }
    if (def->is_phi()) {
        PhiTab phitab;
        return canIgnorePhi(exp, (MDPhi const*)def, this, phitab);
    }
    return isOverConservativeDUChain(def->getOcc(), exp, getRegion());
}


void MDSSAMgr::construction(OptCtx & oc)
{
    START_TIMER(t0, "MDSSA: Construction");
    m_rg->getPassMgr()->checkValidAndRecompute(&oc, PASS_DOM, PASS_UNDEF);
    ASSERT0(oc.is_ref_valid());
    ASSERT0(oc.is_dom_valid());
    reinit();
    //Extract dominate tree of CFG.
    START_TIMER(t1, "MDSSA: Extract Dom Tree");
    DomTree domtree;
    m_cfg->genDomTree(domtree);
    END_TIMER(t1, "MDSSA: Extract Dom Tree");
    if (!construction(domtree, oc)) {
        return;
    }
    m_is_valid = true;
    END_TIMER(t0, "MDSSA: Construction");
}


bool MDSSAMgr::construction(DomTree & domtree, OptCtx & oc)
{
    ASSERT0(m_rg);
    START_TIMER(t1, "MDSSA: Build dominance frontier");
    DfMgr dfm;
    dfm.build((xcom::DGraph&)*m_cfg); //Build dominance frontier.
    END_TIMER(t1, "MDSSA: Build dominance frontier");
    if (dfm.hasHighDFDensityVertex((xcom::DGraph&)*m_cfg)) {
        return false;
    }

    List<IRBB*> wl;
    DefMiscBitSetMgr bs_mgr;
    DefMDSet effect_mds(bs_mgr.getSegMgr());
    BB2DefMDSet defed_mds;
    placePhi(dfm, effect_mds, bs_mgr, defed_mds, wl);

    //Perform renaming.
    MD2VMDStack md2vmdstk;
    rename(effect_mds, defed_mds, domtree, md2vmdstk);

    //Note you can clean version stack after renaming.
    ASSERT0(verifyPhi());
    prunePhi(wl, oc);
    cleanLocalUsedData();
    if (g_dump_opt.isDumpAfterPass() && g_dump_opt.isDumpMDSSAMgr()) {
        dump();
    }
    m_is_valid = true;
    ASSERT0(verify());
    ASSERT0(verifyIRandBB(m_rg->getBBList(), m_rg));
    ASSERT0(verifyPhi() && verifyAllVMD() && verifyVersion(oc));
    return true;
}
//END MDSSAMgr

} //namespace xoc
