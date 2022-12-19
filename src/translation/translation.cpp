/*****************************************************
 *  Implementation of the third translation pass.
 *
 *  In the third pass, we will:
 *    translate all the statements and expressions
 *
 *  Keltin Leung
 */

#include "translation.hpp"
#include "asm/offset_counter.hpp"
#include "ast/ast.hpp"
#include "compiler.hpp"
#include "config.hpp"
#include "scope/scope.hpp"
#include "symb/symbol.hpp"
#include "tac/tac.hpp"
#include "tac/trans_helper.hpp"
#include "type/type.hpp"

using namespace mind;
using namespace mind::symb;
using namespace mind::tac;
using namespace mind::type;
using namespace mind::assembly;
using namespace mind::err;

/* Constructor.
 *
 * PARAMETERS:
 *   helper - the translation helper
 */
Translation::Translation(tac::TransHelper *helper) {
    mind_assert(NULL != helper);

    tr = helper;
}

/* Translating an ast::Program node.
 */
void Translation::visit(ast::Program *p) {
    loop_level = 0;
    for (auto it = p->func_and_globals->begin();
         it != p->func_and_globals->end(); ++it)
        (*it)->accept(this);
}

// three sugars for parameter offset management
#define RESET_OFFSET() tr->getOffsetCounter()->reset(OffsetCounter::PARAMETER)
#define NEXT_OFFSET(x) tr->getOffsetCounter()->next(OffsetCounter::PARAMETER, x)

/* Translating an ast::FuncDefn node.
 *
 * NOTE:
 *   call tr->startFunc() before translating the statements and
 *   call tr->endFunc() after all the statements have been translated
 */
void Translation::visit(ast::FuncDefn *f) {
    Function *fun = f->ATTR(sym);

    // attaching function entry label
    if (f->first_decl)
        fun->attachEntryLabel(tr->getNewEntryLabel(fun));
    if (f->forward_decl)
        return;

    // arguments
    int order = 0;
    for (auto it = f->formals->begin(); it != f->formals->end(); ++it) {
        auto v = (*it)->ATTR(sym);
        v->setOrder(order++);
        v->attachTemp(tr->getNewTempI4());
    }

    fun->offset = fun->getOrder() * POINTER_SIZE;

    RESET_OFFSET();

    tr->startFunc(fun);

    // You may process params here, i.e use reg or stack to pass parameters
    int numparams = f->formals->length();
    auto fit = f->formals->begin();
    for (int i = 0; i < 8 && i < numparams; ++i, ++fit)
        tr->genBindRegToTemp((*fit)->ATTR(sym)->getTemp(), i);

    // translates statement by statement
    for (auto it = f->stmts->begin(); it != f->stmts->end(); ++it)
        (*it)->accept(this);

    tr->genReturn(tr->genLoadImm4(0)); // Return 0 by default

    tr->endFunc();
}

/* Translating an ast::AssignStmt node.
 *
 * NOTE:
 *   different kinds of Lvalue require different translation
 */
void Translation::visit(ast::AssignExpr *s) {
    // TODO
    s->left->accept(this);
    s->e->accept(this);
    ast::Lvalue *left = s->left;
    s->ATTR(val) = s->e->ATTR(val);
    if (left->ATTR(lv_kind) == ast::Lvalue::SIMPLE_VAR) {
        ast::VarRef *var = static_cast<ast::VarRef *>(left);
        if (var->ATTR(sym)->isGlobalVar()) {
            tr->genStore(s->e->ATTR(val), tr->genLoadSymbol(var->var), 0);
        } else {
            tr->genAssign(var->ATTR(sym)->getTemp(), s->e->ATTR(val));
        }
    } else { // array element
        ast::ArrayRef *var = static_cast<ast::ArrayRef *>(left);
        if (var->ATTR(sym)->isGlobalVar()) {
            Temp baseptr = tr->genLoadSymbol(var->var);
            Temp ptr = tr->genAdd(baseptr, var->index->ATTR(offset));
            tr->genStore(s->ATTR(val), ptr, 0);
        } else {
            Temp ptr =
                tr->genAdd(var->ATTR(sym)->getTemp(), var->index->ATTR(offset));
            tr->genStore(s->ATTR(val), ptr, 0);
        }
    }
}

/* Translating an ast::ExprStmt node.
 */
void Translation::visit(ast::ExprStmt *s) { s->e->accept(this); }

/* Translating an ast::IfStmt node.
 *
 * NOTE:
 *   you don't need to test whether the false_brch is empty
 */
void Translation::visit(ast::IfStmt *s) {
    Label L1 = tr->getNewLabel(); // entry of the false branch
    Label L2 = tr->getNewLabel(); // exit
    s->condition->accept(this);
    tr->genJumpOnZero(L1, s->condition->ATTR(val));

    s->true_brch->accept(this);
    tr->genJump(L2); // done

    tr->genMarkLabel(L1);
    s->false_brch->accept(this);

    tr->genMarkLabel(L2);
}
/* Translating an ast::WhileStmt node.
 */
void Translation::visit(ast::WhileStmt *s) {
    // L1..cond..body..L2
    Label L1 = tr->getNewLabel();
    Label L2 = tr->getNewLabel();

    Label old_break = current_break_label;
    current_break_label = L2;
    Label old_continue = current_continue_label;
    current_continue_label = L1;
    loop_level++;

    tr->genMarkLabel(L1);
    s->condition->accept(this);
    tr->genJumpOnZero(L2, s->condition->ATTR(val));

    s->loop_body->accept(this);
    tr->genJump(L1);

    tr->genMarkLabel(L2);

    current_break_label = old_break;
    current_continue_label = old_continue;
    loop_level--;
}

void Translation::visit(ast::DoWhileStmt *s) {
    // L1..body..L3..cond..L2
    Label L1 = tr->getNewLabel();
    Label L2 = tr->getNewLabel();
    Label L3 = tr->getNewLabel();

    Label old_break = current_break_label;
    current_break_label = L2;
    Label old_continue = current_continue_label;
    current_continue_label = L3;
    loop_level++;
    // all break statements inside the loop body goes to L2
    // all continue statements inside the loop body goes to L3
    tr->genMarkLabel(L1);
    s->loop_body->accept(this);
    tr->genMarkLabel(L3);
    s->condition->accept(this);
    tr->genJumpOnZero(L2, s->condition->ATTR(val));
    tr->genJump(L1);
    tr->genMarkLabel(L2);

    current_break_label = old_break;
    current_continue_label = old_continue;
    loop_level--;
}

void Translation::visit(ast::ForStmt *s) {
    // init...L1..cond..body..L3..rear..L2
    Label L1 = tr->getNewLabel();
    Label L2 = tr->getNewLabel();
    Label L3 = tr->getNewLabel();

    Label old_break = current_break_label;
    current_break_label = L2;
    Label old_continue = current_continue_label;
    current_continue_label = L3;
    loop_level++;

    if (s->init != NULL)
        s->init->accept(this);
    tr->genMarkLabel(L1);
    if (s->condition != NULL) {
        s->condition->accept(this);
        tr->genJumpOnZero(L2, s->condition->ATTR(val));
    }
    s->loop_body->accept(this);
    tr->genMarkLabel(L3);
    if (s->rear != NULL)
        s->rear->accept(this);
    tr->genJump(L1);
    tr->genMarkLabel(L2);

    current_break_label = old_break;
    current_continue_label = old_continue;
    loop_level--;
}
/* Translating an ast::BreakStmt node.
 */
void Translation::visit(ast::BreakStmt *s) {
    tr->genJump(current_break_label);
    if (loop_level == 0) {
        issue(s->getLocation(), new SyntaxError("break"));
    }
}
void Translation::visit(ast::ContStmt *s) {
    tr->genJump(current_continue_label);
    if (loop_level == 0) {
        issue(s->getLocation(), new SyntaxError("continue"));
    }
}

/* Translating an ast::CompStmt node.
 */
void Translation::visit(ast::CompStmt *c) {
    // translates statement by statement
    for (auto it = c->stmts->begin(); it != c->stmts->end(); ++it)
        (*it)->accept(this);
}
/* Translating an ast::ReturnStmt node.
 */
void Translation::visit(ast::ReturnStmt *s) {
    s->e->accept(this);
    tr->genReturn(s->e->ATTR(val));
}

void Translation::visit(ast::CallExpr *e) {
    util::List<ast::Expr *> *arguments = e->params;
    std::vector<Temp> param_temp_list;
    for (auto ait = arguments->begin(); ait != arguments->end(); ait++) {
        (*ait)->accept(this);
        param_temp_list.push_back((*ait)->ATTR(val));
    }
    for (int i = param_temp_list.size() - 1; i >= 8; i--)
        tr->genPush(param_temp_list[i]);
    int order = 0;
    for (auto ait = arguments->begin(); ait != arguments->end() && order < 8;
         ait++)
        tr->genParam((*ait)->ATTR(val), order++);
    e->ATTR(val) = tr->genCall(e->ATTR(sym)->getEntryLabel());
}

/* Translating an ast::AddExpr node.
 */
void Translation::visit(ast::AddExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genAdd(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::SubExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genSub(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::MulExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genMul(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::DivExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genDiv(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::ModExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genMod(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::AndExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genLAnd(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::OrExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genLOr(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::EquExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genEqu(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::NeqExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genNeq(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::LeqExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genLeq(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::LesExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genLes(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::GeqExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genGeq(e->e1->ATTR(val), e->e2->ATTR(val));
}
void Translation::visit(ast::GrtExpr *e) {
    e->e1->accept(this);
    e->e2->accept(this);
    e->ATTR(val) = tr->genGtr(e->e1->ATTR(val), e->e2->ATTR(val));
}
/* Translating an ast::IntConst node.
 */
void Translation::visit(ast::IntConst *e) {
    e->ATTR(val) = tr->genLoadImm4(e->value);
}

/* Translating an ast::NegExpr node.
 */
void Translation::visit(ast::NegExpr *e) {
    e->e->accept(this);

    e->ATTR(val) = tr->genNeg(e->e->ATTR(val));
}

void Translation::visit(ast::NotExpr *e) {
    e->e->accept(this);
    e->ATTR(val) = tr->genLNot(e->e->ATTR(val));
}

void Translation::visit(ast::BitNotExpr *e) {
    e->e->accept(this);
    e->ATTR(val) = tr->genBNot(e->e->ATTR(val));
}
/* Translating an ast::LvalueExpr node.
 *
 * NOTE:
 *   different Lvalue kinds need different translation
 */
void Translation::visit(ast::LvalueExpr *e) {
    // TODO
    e->lvalue->accept(this);
    ast::Lvalue *lv = e->lvalue;
    if (lv->ATTR(lv_kind) == ast::Lvalue::SIMPLE_VAR) {
        // a variable or ptr of an array
        ast::VarRef *var = dynamic_cast<ast::VarRef *>(lv);
        if (var->ATTR(sym)->isGlobalVar()) {
            if (var->ATTR(type)->isBaseType()) {
                e->ATTR(val) = tr->getNewTempI4();
                tr->genLoad(e->ATTR(val), tr->genLoadSymbol(var->var), 0);
            } else {
                e->ATTR(val) = tr->genLoadSymbol(var->var);
            }
        } else {
            e->ATTR(val) = var->ATTR(sym)->getTemp();
        }
    } else {
        // an array element
        ast::ArrayRef *var = static_cast<ast::ArrayRef *>(lv);
        e->ATTR(val) = tr->getNewTempI4();
        if (var->ATTR(sym)->isGlobalVar()) {
            Temp baseptr = tr->genLoadSymbol(var->var);
            Temp ptr = tr->genAdd(baseptr, var->index->ATTR(offset));
            tr->genLoad(e->ATTR(val), ptr, 0);
        } else {
            Temp ptr =
                tr->genAdd(var->ATTR(sym)->getTemp(), var->index->ATTR(offset));
            tr->genLoad(e->ATTR(val), ptr, 0);
        }
    }
}

/* Translating an ast::VarRef node.
 *
 * NOTE:
 *   there are two kinds of variable reference: member variables or simple
 * variables
 */
void Translation::visit(ast::VarRef *ref) {
    switch (ref->ATTR(lv_kind)) {
    case ast::Lvalue::SIMPLE_VAR:
        // nothing to do
        break;

    default:
        mind_assert(false); // impossible
    }
    // actually it is so simple :-)
}

void Translation::visit(ast::ArrayIndex *idx) {
    if (idx->lower != NULL)
        idx->lower->accept(this);
    idx->offset->accept(this);
    ArrayType *at = static_cast<ArrayType *>(idx->ATTR(dim));
    Temp newdim = tr->genLoadImm4(at->getElementType()->getSize());
    if (idx->lower != NULL)
        idx->ATTR(offset) =
            tr->genAdd(tr->genMul(idx->offset->ATTR(val), newdim),
                       idx->lower->ATTR(offset));
    else
        idx->ATTR(offset) = tr->genMul(idx->offset->ATTR(val), newdim);
}

void Translation::visit(ast::ArrayRef *ref) {
    ref->index->accept(this);
    switch (ref->ATTR(lv_kind)) {
    case ast::Lvalue::ARRAY_ELE:
        break;
    default:
        mind_assert(false);
    }
}

/* Translating an ast::VarDecl node.
 */
void Translation::visit(ast::VarDecl *decl) {
    // TODO
    if (decl->ATTR(sym)->isGlobalVar())
        return;
    if (decl->type->ATTR(type)->isBaseType()) {
        decl->ATTR(sym)->attachTemp(tr->getNewTempI4());
        // the `init` Expr is allowed to use the newly-declared symbol
        if (decl->init != NULL) {
            decl->init->accept(this);
            tr->genAssign(decl->ATTR(sym)->getTemp(), decl->init->ATTR(val));
        }
    } else { // malloc array
        ArrayType *t = (ArrayType *)decl->type->ATTR(type);
        // the array is represented by a pointer
        Temp baseptr = tr->genAlloc(t->getSize());
        decl->ATTR(sym)->attachTemp(baseptr);
        if (decl->arrayinit != NULL) {
            tr->genParam(baseptr, 0);
            tr->genParam(tr->genLoadImm4(0), 1);
            tr->genParam(tr->genLoadImm4(t->getSize() / 4), 2);
            Label dst = tr->getNewLabel();
            dst->str_form = std::string("fill_n");
            tr->genCall(dst);
            int offset = 0;
            for (auto it = decl->arrayinit->begin();
                 it != decl->arrayinit->end(); it++) {
                tr->genStore(tr->genLoadImm4(*it), baseptr, offset);
                offset += 4;
            }
        }
    }
}

void Translation::visit(ast::IfExpr *e) {
    Label L1 = tr->getNewLabel(); // entry of the false branch
    Label L2 = tr->getNewLabel(); // exit
    e->ATTR(val) = tr->getNewTempI4();

    e->condition->accept(this);
    tr->genJumpOnZero(L1, e->condition->ATTR(val));

    e->true_brch->accept(this);
    tr->genAssign(e->ATTR(val), e->true_brch->ATTR(val));
    tr->genJump(L2); // done

    tr->genMarkLabel(L1);
    e->false_brch->accept(this);
    tr->genAssign(e->ATTR(val), e->false_brch->ATTR(val));

    tr->genMarkLabel(L2);
}

/* Translates an entire AST into a Piece list.
 *
 * PARAMETERS:
 *   tree  - the AST
 * RETURNS:
 *   the result Piece list (represented by the first node)
 */
Piece *MindCompiler::translate(ast::Program *tree) {
    TransHelper *helper = new TransHelper(md);

    tree->accept(new Translation(helper));

    return helper->getPiece();
}
