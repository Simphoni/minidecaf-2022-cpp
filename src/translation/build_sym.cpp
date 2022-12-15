/*****************************************************
 *  Implementation of the first semantic analysis pass.
 *
 *  In the first pass, we will:
 *    1. create appropriate type::Type instances for the types;
 *    2. create and manage scope::Scope instances;
 *    3. create symb::Symbol instances;
 *    4. manage the symbol tables.
 *  After this pass, the ATTR(sym) or ATTR(type) attributs of the visited nodes
 *  should have been set.
 *
 *  Keltin Leung
 */

#include "3rdparty/list.hpp"
#include "ast/ast.hpp"
#include "ast/visitor.hpp"
#include "compiler.hpp"
#include "config.hpp"
#include "scope/scope.hpp"
#include "scope/scope_stack.hpp"
#include "symb/symbol.hpp"
#include "type/type.hpp"

using namespace mind;
using namespace mind::scope;
using namespace mind::symb;
using namespace mind::type;
using namespace mind::err;

/* Pass 1 of the semantic analysis.
 */
class SemPass1 : public ast::Visitor {
  public:
    // visiting declarations
    virtual void visit(ast::FuncDefn *);
    virtual void visit(ast::Program *);
    virtual void visit(ast::IfStmt *);
    virtual void visit(ast::WhileStmt *);
    virtual void visit(ast::DoWhileStmt *);
    virtual void visit(ast::ForStmt *);
    virtual void visit(ast::CompStmt *);
    virtual void visit(ast::VarDecl *);
    // visiting types
    virtual void visit(ast::IntType *);
};

/* Visiting an ast::Program node.
 *
 * PARAMETERS:
 *   prog  - the ast::Progarm node to visit
 */
void SemPass1::visit(ast::Program *prog) {
    prog->ATTR(gscope) = new GlobalScope();
    scopes->open(prog->ATTR(gscope));

    // visit global variables and each function
    for (auto it = prog->func_and_globals->begin();
         it != prog->func_and_globals->end(); ++it) {
        (*it)->accept(this);
        if ((*it)->getKind() == mind::ast::ASTNode::FUNC_DEFN &&
            std::string("main") ==
                dynamic_cast<mind::ast::FuncDefn *>(*it)->name)
            prog->ATTR(main) =
                dynamic_cast<mind::ast::FuncDefn *>(*it)->ATTR(sym);
    }

    scopes->close(); // close the global scope
}

/* Visiting an ast::FunDefn node.
 *
 * NOTE:
 *   tasks include:
 *   1. build up the Function symbol
 *   2. build up symbols of the parameters
 *   3. build up symbols of the local variables
 *
 *   we will check Declaration Conflict Errors for symbols declared in the SAME
 *   class scope, but we don't check such errors for symbols declared in
 *   different scopes here (we leave this task to checkOverride()).
 * PARAMETERS:
 *   fdef  - the ast::FunDefn node to visit
 */
void SemPass1::visit(ast::FuncDefn *fdef) {
    fdef->ret_type->accept(this);
    Type *t = fdef->ret_type->ATTR(type);

    Function *f = new Function(fdef->name, t, fdef->getLocation());

    // checks the Declaration Conflict Error of Case 1 (but don't check Case
    // 2,3). if DeclConflictError occurs, we don't put the symbol into the
    // symbol table
    bool hasFwdDecl = false;
    Symbol *sym = scopes->lookup(fdef->name, fdef->getLocation(), false);
    if (NULL != sym) {
        if (!sym->isFunction())
            issue(fdef->getLocation(), new DeclConflictError(fdef->name, sym));
        hasFwdDecl = true;
        Function *ofun = static_cast<Function *>(sym);
        util::List<Type *> *p = ofun->getType()->getArgList();
        util::List<Type *> *q = f->getType()->getArgList();
        if (p->length() != q->length())
            issue(fdef->getLocation(), new DeclConflictError(fdef->name, sym));
        for (auto pit = p->begin(), qit = q->begin(); pit != p->end();
             ++pit, ++qit)
            if (!(*pit)->equal(*qit))
                issue(fdef->getLocation(),
                      new DeclConflictError(fdef->name, sym));
        if (ofun->readDeclareState() &&
            !fdef->forward_decl) // declare only once
            issue(fdef->getLocation(), new DeclConflictError(fdef->name, sym));
        if (fdef->forward_decl) // no need to declare twice
            return;
    } else {
        scopes->declare(f);
        fdef->first_decl = true;
    }
    if (NULL != sym)
        f = static_cast<Function *>(sym);
    fdef->ATTR(sym) = f;
    // opens function scope
    scopes->open(f->getAssociatedScope());

    // adds the parameters
    if (!hasFwdDecl) { // prepare arguments
        for (ast::VarList::iterator it = fdef->formals->begin();
             it != fdef->formals->end(); ++it) {
            (*it)->accept(this);
            f->appendParameter((*it)->ATTR(sym));
        }
    }

    // adds the local variables
    for (auto it = fdef->stmts->begin(); it != fdef->stmts->end(); ++it)
        (*it)->accept(this);

    // closes function scope
    scopes->close();
}

/* Visits an ast::IfStmt node.
 *
 * PARAMETERS:
 *   e     - the ast::IfStmt node
 */
void SemPass1::visit(ast::IfStmt *s) {
    s->condition->accept(this);
    s->true_brch->accept(this);
    s->false_brch->accept(this);
}

/* Visits an ast::WhileStmt node.
 *
 * PARAMETERS:
 *   e     - the ast::WhileStmt node
 */
void SemPass1::visit(ast::WhileStmt *s) {
    s->condition->accept(this);
    s->loop_body->accept(this);
}

void SemPass1::visit(ast::DoWhileStmt *s) {
    s->loop_body->accept(this);
    s->condition->accept(this);
}

void SemPass1::visit(ast::ForStmt *s) {
    Scope *scope = new LocalScope();
    s->ATTR(scope) = scope;
    scopes->open(scope);
    if (s->init != NULL)
        s->init->accept(this);
    if (s->condition != NULL)
        s->condition->accept(this);
    if (s->rear != NULL)
        s->rear->accept(this);
    s->loop_body->accept(this);

    scopes->close();
}

/* Visiting an ast::CompStmt node.
 */
void SemPass1::visit(ast::CompStmt *c) {
    // opens function scope
    Scope *scope = new LocalScope();
    c->ATTR(scope) = scope;
    scopes->open(scope);

    // adds the local variables
    for (auto it = c->stmts->begin(); it != c->stmts->end(); ++it)
        (*it)->accept(this);

    // closes function scope
    scopes->close();
}
/* Visiting an ast::VarDecl node.
 *
 * NOTE:
 *   we will check Declaration Conflict Errors for symbols declared in the SAME
 *   function scope, but we don't check such errors for symbols declared in
 *   different scopes here (we leave this task to checkOverride()).
 * PARAMETERS:
 *   vdecl - the ast::VarDecl node to visit
 */
void SemPass1::visit(ast::VarDecl *vdecl) {
    Type *t = NULL;

    vdecl->type->accept(this);
    t = vdecl->type->ATTR(type);

    // TODO: Add a new symbol to a scope
    // 1. Create a new `Variable` symbol
    // 2. Check for conflict in `scopes`, which is a global variable refering to
    // a scope stack
    // 3. Declare the symbol in `scopes`
    // 4. Special processing for global variables
    // 5. Tag the symbol to `vdecl->ATTR(sym)`
    Variable *v = new Variable(vdecl->name, t, vdecl->getLocation());
    Symbol *sym = scopes->lookup(vdecl->name, vdecl->getLocation(), false);
    if (sym != NULL)
        issue(vdecl->getLocation(), new DeclConflictError(vdecl->name, sym));
    else {
        scopes->declare(v);
        vdecl->ATTR(sym) = v;
        if (vdecl->init != NULL) {
            vdecl->init->accept(this);
            if (vdecl->ATTR(sym)->isGlobalVar()) {
                // only support initiating with constant
                if (vdecl->init->getKind() == ast::ASTNode::INT_CONST)
                    vdecl->ATTR(sym)->setGlobalInit(
                        dynamic_cast<ast::IntConst *>(vdecl->init)->value);
                else {
                    fprintf(stderr,
                            "[%s:%d] error: globals must be initialized with "
                            "integer constant.\n",
                            __FILE__, __LINE__);
                    throw;
                }
            }
        } else if (vdecl->ATTR(sym)->isGlobalVar()) {
            vdecl->ATTR(sym)->setGlobalInit(0); // default to zero
        }
    }
}

/* Visiting an ast::IntType node.
 *
 * PARAMETERS:
 *   itype - the ast::IntType node to visit
 */
void SemPass1::visit(ast::IntType *itype) { itype->ATTR(type) = BaseType::Int; }

/* Builds the symbol tables for the Mind compiler.
 *
 * PARAMETERS:
 *   tree  - the AST of the program
 */
void MindCompiler::buildSymbols(ast::Program *tree) {
    tree->accept(new SemPass1());
}
