/*****************************************************
 *  Implementation of "IfStmt".
 *
 *  Please refer to ast/ast.hpp for the definition.
 *
 *  Keltin Leung
 */

#include "ast/ast.hpp"
#include "ast/visitor.hpp"
#include "config.hpp"

using namespace mind;
using namespace mind::ast;

ForStmt::ForStmt(Statement *i, Expr *c, Expr *r, Statement *body, Location *l) {

    setBasicInfo(FOR_STMT, l);

    init = i;
    condition = c;
    rear = r;
    loop_body = body;
}

void ForStmt::accept(Visitor *v) { v->visit(this); }

void ForStmt::dumpTo(std::ostream &os) {
    ASTNode::dumpTo(os);
    newLine(os);
    os << init;
    newLine(os);
    os << condition;

    newLine(os);
    os << loop_body;
    newLine(os);
    os << rear << ")";
    decIndent(os);
}
