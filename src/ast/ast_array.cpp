/*****************************************************
 *  Implementation of "VarRef".
 *
 *  Please refer to ast/ast.hpp for the definition.
 *
 *  Keltin Leung
 *
 *  removed owner
 */

#include "ast/ast.hpp"
#include "ast/visitor.hpp"
#include "config.hpp"
#include "type/type.hpp"

using namespace mind;
using namespace mind::ast;

// ArrayType member Defs

ArrayType::ArrayType(type::Type *t, ast::Type *b, Location *l) {
    setBasicInfo(ARRAY_TYPE, l);
    base = b;
    ATTR(type) = t;
}

void ArrayType::accept(Visitor *v) { v->visit(this); }

void ArrayType::dumpTo(std::ostream &os) {
    ASTNode::dumpTo(os);
    os << " of " << ATTR(type);
    os << ")";
    decIndent(os);
}

// ArrayRef member Defs

ArrayRef::ArrayRef(std::string n, ArrayIndex *idx, Location *l) {
    setBasicInfo(ARRAY_REF, l);
    var = n;
    index = idx;
    ATTR(sym) = NULL;
}

void ArrayRef::accept(Visitor *v) { v->visit(this); }

void ArrayRef::dumpTo(std::ostream &os) {
    ASTNode::dumpTo(os);
    os << " " << '"' << var << '"';
    newLine(os);
    os << index;
    decIndent(os);
}

// ArrayIndex member Defs

ArrayIndex::ArrayIndex(ArrayIndex *lo, Expr *e, Location *l) {
    setBasicInfo(ARRAY_INDEX, l);
    lower = lo;
    offset = e;
}

void ArrayIndex::accept(Visitor *v) { v->visit(this); }

void ArrayIndex::dumpTo(std::ostream &os) {
    ASTNode::dumpTo(os);
    os << " "
       << "[" << offset << "]";
    newLine(os);
    if (lower != NULL)
        os << lower;
    decIndent(os);
}