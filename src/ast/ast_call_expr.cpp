/*****************************************************
 *  Implementation of "AddExpr".
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

CallExpr::CallExpr(std::string f, ExprList *p, Location *l) {

    setBasicInfo(CALL_EXPR, l);

    funct = f;
    params = p;
}

void CallExpr::accept(Visitor *v) { v->visit(this); }

void CallExpr::dumpTo(std::ostream &os) {
    ASTNode::dumpTo(os);
    newLine(os);
    os << funct;
    newLine(os);
    os << params << ")";
    decIndent(os);
}
